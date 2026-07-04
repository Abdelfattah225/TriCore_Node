# Changelog — TC397 Safety Node Networking

All changes made to the original Infineon `Ethernet_1_KIT_TC397_TFT` example.

---

## Phase 1: Static IP bring-up (HNC-SAF-01.1)

### Problem
The Infineon example uses DHCP to get an IP address. But our setup is a **direct Ethernet cable** between the laptop and the TC397 board — no router, no switch, no DHCP server. The board never gets an IP, so nothing works.

### Changes

**`Configurations/lwipopts.h`**
- Changed `#define LWIP_DHCP 1` → `#define LWIP_DHCP 0`
- Why: Disables the DHCP client. No DHCP server exists on a direct cable.

**`Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c`** — `Ifx_Lwip_init()` function
- Added static IP configuration before `netif_add()`:
  ```c
  IP4_ADDR(&ipaddr,  192, 168, 10, 30);
  IP4_ADDR(&netmask, 255, 255, 255, 0);
  IP4_ADDR(&gw,      0, 0, 0, 0);
  ```
- Why: The board needs a fixed IP on the 192.168.10.0/24 subnet.

### Result
`ping 192.168.10.30` from the laptop succeeds. HNC-SAF-01.1 PASS.

---

## Phase 3-4: Architecture + frame codec

### Problem
The Infineon example is a simple TCP echo server. We need a real command/response protocol with multiple command types, telemetry, and a clean API for teammates.

### New files created

**`Networking/HAL/net_if.c` + `net_if.h`**
- Wraps `Ifx_Lwip_init()` and `Ifx_Lwip_pollTimerFlags()` / `Ifx_Lwip_pollReceiveFlags()`
- Calls `transport_init()` and `transport_poll()`
- Why: Teammates call `net_if_init()` and `net_if_poll()` without knowing lwIP internals.

**`Networking/COMM/frame_codec.c` + `frame_codec.h`**
- `frame_codec_pack()`: builds `[CMD_TYPE][SEQ][LEN][PAYLOAD][CRC16]`
- `frame_codec_unpack()`: parses + verifies CRC-16
- Why: Shared message format between TC397 and NXP.

**`Networking/COMM/transport.c` + `transport.h`**
- UDP socket for telemetry TX (port 6000)
- TCP server for command RX (port 6001)
- TCP event TX (replies)
- Why: Handles all networking so teammates don't touch lwIP.

**`Networking/APP/dispatcher.c` + `dispatcher.h`**
- `dispatcher_register(cmd_type, handler)`: register a handler for a command type
- `dispatcher_dispatch(cmd_type, seq, payload, len)`: call the registered handler
- Why: Decouples command routing from transport. Teammates just register handlers.

### Result
Clean layered architecture. HNC-SAF-01.4 (decode CMD_TYPE + payload) PASS.

---

## Phase 5-6: UDP telemetry + TCP server

### Problem
Need UDP telemetry (TC→NXP) and TCP command server (NXP→TC).

### Implementation in `transport.c`
- `transport_send_telemetry()`: sends UDP packet to 192.168.10.10:6000
- TCP server: listens on port 6001, accepts one client, receives frames, dispatches
- `transport_send_event()`: sends TCP reply back to connected client

### Result
UDP telemetry + TCP command server working. HNC-SAF-01.2 PASS.

---

## Bug fix #1: Debug prints not working (Ifx_Console_print)

### Problem
All `Ifx_Console_print()` calls were silently doing nothing. No debug output appeared on UART, making it impossible to diagnose TCP issues.

### Root cause
`Ifx_Console_print()` requires `Ifx_Console_init()` to be called first, which sets up the `Ifx_g_console.standardIo` pointer. This function is **never called** anywhere in the project. The pointer stays NULL, and all print calls silently fail.

The lwIP debug messages (e.g. "netif: new ip address assigned") use a **different** UART path: `sendUARTMessage()` from `UART_Logging.h`, which IS initialized by `initUART()` inside `Ifx_Lwip_init()`.

### Fix
Replaced all `Ifx_Console_print()` calls with a custom `dbg_print()` function that uses `vsnprintf()` + `sendUARTMessage()`:

```c
static void dbg_print(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        sendUARTMessage(buf, (Ifx_SizeT)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    }
}
```

Added to both `transport.c` and `Cpu0_Main.c`.

### Lesson
Always verify your debug output mechanism works **before** using it to debug. The fact that lwIP messages appeared but our `>>>` messages didn't was the key clue.

---

## Bug fix #2: TCP reply not reaching the laptop

### Problem
Board receives `setHeater` command, handler runs, `transport_send_event()` returns success, but the laptop never receives the TCP reply.

### Root causes (multiple)

1. **`transport_poll()` cleared the reply flag unconditionally**
   - Original code: `s_pending_reply_flag = 0;` ran even when `tcp_write()` failed
   - The reply was lost forever with no retry

2. **No `tcp_poll` callback registered**
   - The working Echo.c registers `tcp_poll()` as a safety net
   - Our code didn't, so a failed `tcp_write` was never retried

3. **No Nagle disable**
   - Small 3-byte replies could be delayed by Nagle's algorithm

4. **No immediate send attempt**
   - `transport_send_event()` only queued data
   - Echo.c calls `tcp_write()` directly in the recv callback

### Fix
- Added `transport_try_send_pending()` helper — only clears flag on `ERR_OK`
- Called it from 3 places: recv callback (immediate), tcp_poll (periodic retry), transport_poll (main loop retry)
- Registered `tcp_poll(newpcb, transport_tcp_poll, 1)`
- Called `tcp_nagle_disable(newpcb)`

### Result
TCP replies work. `tcp_write=0` (ERR_OK), `tcp_output=0` (ERR_OK).

---

## Bug fix #3: Connection reuse — board rejects new connections

### Problem
After the first Python client disconnects, the board refuses all new TCP connections. Only a board reset fixes it.

### Root causes

1. **`tcp_close()` without removing callbacks**
   - Stale callbacks (recv, sent, err, poll) can fire on a closing PCB
   - This causes crashes or wrong state

2. **`tcp_err()` unconditionally cleared `s_tcp_client_pcb`**
   - If an old connection's error fired after a new connection was accepted, it would clear the NEW connection's pointer
   - Race condition

3. **`tcp_close()` can return `ERR_MEM`**
   - If the PCB pool is exhausted, `tcp_close()` fails silently
   - The PCB leaks, eventually the board can't accept new connections

### Fix
Added `transport_tcp_close()` helper (based on Echo.c's `echoClose()`):
```c
static void transport_tcp_close(struct tcp_pcb *tpcb)
{
    tcp_arg(tpcb, NULL);       // Remove all callbacks FIRST
    tcp_recv(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    err_t err = tcp_close(tpcb);
    if (err != ERR_OK) {
        tcp_abort(tpcb);       // Fallback if tcp_close fails
    }
}
```

Fixed `tcp_err()` to check `arg == s_tcp_client_pcb` before clearing:
```c
static void transport_tcp_err(void *arg, err_t err)
{
    struct tcp_pcb *closed_pcb = (struct tcp_pcb *)arg;
    if (closed_pcb != NULL && closed_pcb == s_tcp_client_pcb) {
        s_tcp_client_pcb = NULL;  // Only clear if this is the CURRENT client
    }
}
```

Used `tcp_arg(newpcb, newpcb)` so `tcp_err` can identify which PCB had the error.

### Result
Multiple sequential TCP connections work. `CLOSE: tcp_close OK` → `ACCEPT: client connected`.

---

## Phase 8-9: CRC-16 + latency measurement

### CRC-16 (HNC-SAF-01.3)

Changed frame format from:
```
[CMD_TYPE][SEQ][LEN][PAYLOAD]
```
to:
```
[CMD_TYPE][SEQ][LEN][PAYLOAD][CRC16_LO][CRC16_HI]
```

CRC-16/CCITT (poly 0x1021, init 0xFFFF) calculated over CMD_TYPE + PAYLOAD.

- `frame_codec_pack()` appends CRC
- `frame_codec_unpack()` verifies CRC, returns `FRAME_UNPACK_OK` / `INCOMPLETE` / `CRC_ERROR`
- `transport.c` handles all 3 return codes: OK→dispatch, INCOMPLETE→wait, CRC_ERROR→skip+log

### Latency measurement (HNC-SAF-01.5)

Added STM timestamps in `transport_tcp_recv()`:
- `t_rx`: timestamp when pbuf data enters our layer
- `t_dispatch`: timestamp just before `dispatcher_dispatch()`
- Prints: `>>> DISPATCH: cmd=0x01 seq=0 latency=1 us`

STM runs at 100 MHz (100 ticks/us), so resolution is ~10 us.

### Result
- CRC-16 verification: PASS (no CRC errors in normal operation)
- Latency: 0-1 us (requirement was < 5000 us = 5 ms) — **5000x faster** than required

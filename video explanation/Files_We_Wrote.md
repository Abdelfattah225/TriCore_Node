# Files We Wrote — What Each One Does

## One diagram

```
  Files we CREATED (8 new files):
  ┌─────────────────────────────────────────────────────────┐
  │                                                         │
  │  Networking/HAL/net_if.c + .h     ← lwIP wrapper        │
  │  Networking/COMM/frame_codec.c + .h ← pack/unpack + CRC │
  │  Networking/COMM/transport.c + .h   ← UDP + TCP sockets  │
  │  Networking/APP/dispatcher.c + .h   ← handler registry   │
  │                                                         │
  │  nxp_simulator.py                  ← Python test tool   │
  │                                                         │
  └─────────────────────────────────────────────────────────┘

  Files we MODIFIED (3 existing files):
  ┌─────────────────────────────────────────────────────────┐
  │                                                         │
  │  Cpu0_Main.c                       ← handlers + main loop│
  │  Configurations/lwipopts.h         ← DHCP off           │
  │  Libraries/.../Ifx_Lwip.c          ← static IP          │
  │                                                         │
  └─────────────────────────────────────────────────────────┘
```

## File-by-file explanation

### 1. `Networking/HAL/net_if.c` + `net_if.h` — The lwIP Wrapper

```
  Role: Bridge between application code and lwIP stack.

  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐
  │  Cpu0_Main.c │─────►│   net_if.c   │─────►│  Ifx_Lwip.c  │
  │              │      │              │      │  (lwIP)      │
  │ net_if_init()│      │ wraps:       │      │              │
  │ net_if_poll()│      │ Ifx_Lwip_init│      │ GETH hardware│
  │              │      │ timer polling│      │              │
  └──────────────┘      │ + transport  │      └──────────────┘
                        └──────────────┘

  net_if_init(mac)
  ├── calls Ifx_Lwip_init(mac)  → starts lwIP, sets static IP
  └── calls transport_init()    → opens UDP + TCP sockets

  net_if_poll()
  ├── calls Ifx_Lwip_pollTimerFlags()   → drive TCP/ARP timers
  ├── calls Ifx_Lwip_pollReceiveFlags() → check for incoming packets
  └── calls transport_poll()            → retry pending TCP replies

  WHY IT EXISTS:
  Teammates call net_if_init() and net_if_poll() — two functions.
  They never see Ifx_Lwip, GETH, or lwIP internals.
```

---

### 2. `Networking/COMM/frame_codec.c` + `frame_codec.h` — The Message Packer

```
  Role: Convert between raw bytes and structured messages.

  Frame on wire:  [CMD][SEQ][LEN][PAYLOAD...][CRC_LO][CRC_HI]

  ┌──────────┐  frame_codec_pack()   ┌─────────────────────┐
  │ CMD=0x01 │  ──────────────────►  │ 01 05 01 19 26 AD    │
  │ SEQ=0x05 │  builds the frame     │ ──┘──┘──┘──┘────┘    │
  │ payload  │  + appends CRC        │ CMD SEQ LEN PAY CRC   │
  │ =0x19    │                       └─────────────────────┘
  └──────────┘

  ┌─────────────────────┐  frame_codec_unpack()  ┌──────────┐
  │ 01 05 01 19 26 AD   │  ──────────────────►   │ CMD=0x01 │
  │ ──┘──┘──┘──┘────┘    │  parses + verifies CRC │ SEQ=0x05 │
  │ CMD SEQ LEN PAY CRC  │                        │ payload  │
  └─────────────────────┘                        │ =0x19    │
                                                 └──────────┘

  3 return codes:
    FRAME_UNPACK_OK         → valid frame, dispatch it
    FRAME_UNPACK_INCOMPLETE → not enough bytes yet, wait
    FRAME_UNPACK_CRC_ERROR  → corrupted, drop it

  ALSO exports:
    frame_codec_crc16()     → raw CRC-16/CCITT function
    FRAME_OVERHEAD          → 5 (header + CRC, no payload)
    FRAME_HEADER_SIZE       → 3 (CMD + SEQ + LEN)
    FRAME_CRC_SIZE          → 2 (CRC16 little-endian)
    CMD_SET_HEATER, etc.    → command type constants

  WHY IT EXISTS:
  Teammates use frame_codec_pack() to build replies.
  They don't need to know the byte layout or CRC math.
```

---

### 3. `Networking/COMM/transport.c` + `transport.h` — The Socket Layer

```
  Role: All TCP and UDP communication. The biggest file.

  ┌──────────────────────────────────────────────────────────┐
  │                                                          │
  │  UDP side (telemetry):                                  │
  │    transport_send_telemetry(buf, len)                    │
  │    → sends UDP packet to 192.168.10.10:6000             │
  │    → fire-and-forget, no connection needed              │
  │                                                          │
  │  TCP side (commands + events):                          │
  │    Listens on port 6001                                 │
  │    Accepts one client at a time                         │
  │                                                          │
  │    Incoming: TCP packet → frame_codec_unpack →          │
  │              dispatcher_dispatch → YOUR handler          │
  │                                                          │
  │    Outgoing: transport_send_event(buf, len) →            │
  │              tcp_write + tcp_output → back to client    │
  │                                                          │
  └──────────────────────────────────────────────────────────┘

  Internal functions (teammates never call these):
    transport_tcp_accept()   → callback: new client connected
    transport_tcp_recv()     → callback: data received from client
    transport_tcp_close()    → clean up connection (remove callbacks)
    transport_tcp_err()      → callback: connection error
    transport_tcp_poll()     → callback: periodic retry (500ms)
    transport_try_send_pending() → attempt TCP send, retry on fail
    transport_poll()         → main loop retry for pending replies

  Public API (teammates use these 3):
    transport_init()              → setup sockets (called by net_if_init)
    transport_send_telemetry()    → send UDP
    transport_send_event()        → send TCP reply

  WHY IT EXISTS:
  Handles ALL the tricky lwIP TCP stuff (callbacks, PCB management,
  Nagle, retries, connection cleanup). Teammates never touch lwIP.
```

---

### 4. `Networking/APP/dispatcher.c` + `dispatcher.h` — The Router

```
  Role: Route incoming commands to the right handler function.

  ┌──────────────────────────────────────────────────────┐
  │  Handler Registry (array of 16 slots)                │
  │                                                      │
  │  [0] CMD_SET_HEATER      → test_heater_handler()     │
  │  [1] CMD_SET_SEAT        → test_seat_handler()       │
  │  [2] CMD_SET_TRUNK       → test_trunk_handler()      │
  │  [3] CMD_SET_AMBIENT_LED → test_led_handler()        │
  │  [4] CMD_REQUEST_SENSORS → test_sensors_handler()    │
  │  [5] (empty — add yours here)                        │
  │  ...                                                 │
  │  [15] (empty)                                        │
  └──────────────────────────────────────────────────────┘

  dispatcher_register(CMD_XXX, your_handler)
  → adds (CMD_XXX, handler) to the next empty slot

  dispatcher_dispatch(CMD_XXX, seq, payload, len)
  → searches array for CMD_XXX
  → calls the matching handler with (seq, payload, len)
  → if no match found: silently ignores (could add default handler later)

  WHY IT EXISTS:
  Decouples "which command arrived" from "what to do about it".
  Teammates register their handlers at startup, the dispatcher
  calls them automatically when their command arrives.
  No if/else chains, no switch statements, no hardcoded routing.
```

---

### 5. `Cpu0_Main.c` — The Application (MODIFIED)

```
  Role: Entry point + application logic + super-loop.

  What's in it:
  ┌──────────────────────────────────────────────────────┐
  │  1. dbg_print() helper          → UART debug output   │
  │  2. send_rejected() helper      → quick CMD_REJECTED  │
  │  3. test_heater_handler()       → setHeater handler   │
  │  4. test_seat_handler()         → setSeat handler     │
  │  5. test_trunk_handler()        → setTrunk handler    │
  │  6. test_led_handler()          → setAmbientLED       │
  │  7. test_sensors_handler()      → requestSensors      │
  │  8. core0_main()                → startup + main loop│
  └──────────────────────────────────────────────────────┘

  core0_main() does:
    ├── Enable interrupts, disable watchdog
    ├── Configure STM timer (1ms tick for lwIP)
    ├── Enable GETH (Ethernet hardware)
    ├── Set MAC address: DE:AD:BE:EF:FE:ED
    ├── net_if_init(mac)           → start lwIP + transport
    ├── dispatcher_init()          → clear handler table
    ├── dispatcher_register(...)   → register all handlers
    └── while(1) {
            net_if_poll()          → drive lwIP + transport
            if (1 second passed) {
                send UDP telemetry  → sensor data to laptop
            }
        }

  WHY IT EXISTS:
  This is where teammates add their handlers.
  It's the ONLY file they need to modify (plus frame_codec.h for new CMDs).
```

---

### 6. `Configurations/lwipopts.h` — lwIP Configuration (MODIFIED)

```
  Role: Compile-time configuration for the lwIP TCP/IP stack.

  What we changed:
    LWIP_DHCP  1 → 0    Disable DHCP (no router on direct cable)

  What's already there (don't touch):
    NO_SYS       = 1     No OS, bare-metal (raw/callback API)
    LWIP_NETCONN = 0     Disable netconn API
    LWIP_SOCKET  = 0     Disable socket API
    MEM_SIZE     = 25KB  lwIP heap size
    MEM_ALIGNMENT = 4    32-bit alignment

  WHY IT EXISTS:
  lwIP is configurable via #defines. This file tells lwIP
  what features to compile in/out. We only changed DHCP.
```

---

### 7. `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c` — lwIP Port (MODIFIED)

```
  Role: Infineon's lwIP port for TC397. Connects lwIP to GETH hardware.

  What we changed:
    Ifx_Lwip_init() function:
    ├── Added IP4_ADDR(&ipaddr, 192, 168, 10, 30)   ← static IP
    ├── Added IP4_ADDR(&netmask, 255, 255, 255, 0)  ← /24
    └── Added IP4_ADDR(&gw, 0, 0, 0, 0)             ← no gateway

  What already exists (don't touch):
    Ifx_Lwip_onTimerTick()      → called every 1ms by STM ISR
    Ifx_Lwip_pollTimerFlags()   → processes TCP/ARP timers
    Ifx_Lwip_pollReceiveFlags() → reads incoming packets from GETH
    ISR_Geth_Tx / ISR_Geth_Rx  → Ethernet DMA interrupts

  WHY IT EXISTS:
  This is Infineon's bridge between lwIP (software) and GETH (hardware).
  We only touched the IP address setup. Everything else is Infineon's code.
```

---

### 8. `nxp_simulator.py` — Python Test Tool (NEW)

```
  Role: Simulates the NXP gateway on your laptop.

  Runs on: Laptop (Windows/Linux), Python 3

  ┌──────────────────────────────────────────────────────┐
  │  What it does:                                       │
  │                                                      │
  │  1. Starts UDP listener on :6000 (telemetry)        │
  │  2. Connects TCP to board :6001 (commands)           │
  │  3. Sends all 5 command types:                      │
  │     - setHeater                                      │
  │     - setSeat                                        │
  │     - setTrunk                                       │
  │     - setAmbientLED                                  │
  │     - requestSensors → gets sensorData back         │
  │  4. Closes connection, reconnects (tests reuse)     │
  │  5. Prints decoded results + CRC verification       │
  │                                                      │
  │  Usage:                                              │
  │    python nxp_simulator.py                           │
  │                                                      │
  │  Prerequisites:                                      │
  │    - Laptop IP set to 192.168.10.10/24              │
  │    - Board powered + flashed + connected via cable   │
  └──────────────────────────────────────────────────────┘

  WHY IT EXISTS:
  Permanent regression test. Run it after any code change
  to verify the networking layer still works.
  Teammates can add their own commands to test their handlers.
```

---

## Dependency map (who calls who)

```
  Cpu0_Main.c
    │
    ├──► net_if.c ────► Ifx_Lwip.c ────► GETH hardware
    │       │
    │       └──► transport.c ────► lwIP TCP/UDP
    │                 │
    │                 └──► frame_codec.c (unpack incoming)
    │                         │
    │                         └──► dispatcher.c
    │                                 │
    │                                 └──► YOUR handler (in Cpu0_Main.c)
    │                                         │
    │                                         └──► frame_codec.c (pack reply)
    │                                                 │
    │                                                 └──► transport.c (send)
    │
    └──► frame_codec.c (pack telemetry)
            │
            └──► transport.c (UDP send)
```

## Summary table

| File | Lines | Role | Who touches it |
|------|-------|------|-----------------|
| `net_if.c/.h` | ~15 | lwIP wrapper | Nobody (done) |
| `frame_codec.c` | ~50 | Pack/unpack + CRC | Nobody (done) |
| `frame_codec.h` | ~47 | Constants + API | Teammates (add CMDs) |
| `transport.c` | ~340 | TCP/UDP sockets | Nobody (done) |
| `transport.h` | ~34 | API | Nobody (read only) |
| `dispatcher.c` | ~50 | Handler routing | Nobody (done) |
| `dispatcher.h` | ~20 | API | Nobody (read only) |
| `Cpu0_Main.c` | ~234 | App + handlers | **Teammates** (add handlers) |
| `lwipopts.h` | ~70 | lwIP config | Nobody (done) |
| `Ifx_Lwip.c` | ~500 | lwIP port | Nobody (done, IP set) |
| `nxp_simulator.py` | ~244 | Test tool | Teammates (add tests) |

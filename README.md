# TC397 Safety Node — Networking Layer

Safety controller networking for the **Hyper Nova Cockpit** graduation project.  
Runs on the **Infineon AURIX TC397 TFT** board with **lwIP** (raw/callback API, NO_SYS mode).

---

## What this project does

The TC397 board talks to an NXP gateway (or a laptop simulator) over Ethernet:

| Direction | Protocol | Port | Purpose |
|-----------|----------|------|---------|
| TC397 → NXP | UDP | 6000 | Telemetry (sensor data, fire-and-forget) |
| NXP → TC397 | TCP | 6001 | Commands (setHeater, setSeat, etc.) |
| TC397 → NXP | TCP | 6001 | Event replies (sensorData, commandRejected) |

**No DHCP, no router** — direct Ethernet cable, static IPs:
- TC397: `192.168.10.30/24`
- Laptop/NXP: `192.168.10.10/24`

---

## Hardware setup

1. Connect an Ethernet cable between your PC and the TC397 board (port 1)
2. Set your PC's Ethernet adapter to static IP: `192.168.10.10` / mask `255.255.255.0` / no gateway
3. Connect the board to your PC via USB (for flashing + UART debug)
4. Power the board

---

## How to build & flash (for beginners)

### Prerequisites
- [AURIX Development Studio](https://www.infineon.com/aurixdevelopmentstudio) (v1.10.36 or later)
- USB cable (mini-USB for the miniWiggler debug interface)

### Steps
1. Open AURIX Development Studio
2. `File → Import... → Existing Projects into Workspace`
3. Select this folder (the one containing `.project`)
4. Click **Finish**
5. Click the **Build** button (hammer icon) — should show `0 errors`
6. Click the **Flash** button (chip icon) to flash the board
7. Open the serial console (terminal icon) to see UART debug output:
   - Baud rate: `115200`
   - You should see: `netif: new ip address assigned: 192.168.10.30`

---

## How to verify

### 1. Ping test
```bash
ping 192.168.10.30
```
Should reply. This proves the Ethernet link and static IP work.

### 2. Python NXP simulator
```bash
python nxp_simulator.py
```
This connects to the board, sends all command types, and verifies replies + UDP telemetry. Expected output: all tests PASS.

---

## Project structure

```
Ethernet_1_KIT_TC397_TFT/
├── Cpu0_Main.c              ← Main application (handlers + super-loop)
├── nxp_simulator.py          ← Python test tool (run from your laptop)
├── .gitignore
│
├── Networking/               ← OUR CODE — the networking layer
│   ├── HAL/
│   │   ├── net_if.c          ← Wraps Ifx_Lwip_init + polling
│   │   └── net_if.h
│   ├── COMM/
│   │   ├── frame_codec.c     ← Pack/unpack frames + CRC-16
│   │   ├── frame_codec.h
│   │   ├── transport.c       ← UDP TX, TCP server RX, TCP event TX
│   │   └── transport.h
│   └── APP/
│       ├── dispatcher.c      ← Command handler registry
│       └── dispatcher.h
│
├── Configurations/
│   └── lwipopts.h            ← lwIP config (DHCP disabled, NO_SYS=1)
│
├── Libraries/                ← Infineon iLLD + lwIP (do not modify)
│   └── Ethernet/lwip/port/src/
│       └── Ifx_Lwip.c        ← Modified: static IP instead of DHCP
│
├── Echo.c / Echo.h           ← Original Infineon echo example (unused)
└── Images/                   ← Infineon documentation images
```

### What we modified from the original Infineon example

| File | Change | Why |
|------|--------|-----|
| `Configurations/lwipopts.h` | `LWIP_DHCP 0` | No DHCP server on direct cable |
| `Libraries/.../Ifx_Lwip.c` | Static IP `192.168.10.30` | Direct cable, no router |
| `Cpu0_Main.c` | Replaced echo with dispatcher + handlers | Our application logic |
| `Networking/` (new) | 8 new files | The networking layer |
| `nxp_simulator.py` (new) | Python test tool | Regression testing |

---

## Message protocol

### Frame format (with CRC-16)
```
[CMD_TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD NB][CRC16_LO 1B][CRC16_HI 1B]
```

- **CMD_TYPE**: command/event type (see table below)
- **SEQ**: rolling sequence number (for dedup/loss detection)
- **LEN**: payload length (0-250)
- **PAYLOAD**: command-specific data
- **CRC16**: CRC-16/CCITT (poly 0x1021, init 0xFFFF) over CMD_TYPE + PAYLOAD

### Command types

| CMD_TYPE | Name | Direction | Payload | Reply |
|----------|------|-----------|---------|-------|
| `0x01` | setHeater | NXP→TC | `[temperature 1B]` | commandRejected |
| `0x02` | setSeat | NXP→TC | `[occupied 1B]` | commandRejected |
| `0x03` | setTrunk | NXP→TC | `[open 1B]` | commandRejected |
| `0x04` | setAmbientLED | NXP→TC | `[brightness 1B]` | commandRejected |
| `0x10` | requestSensors | NXP→TC | (none) | sensorData |
| `0x80` | sensorData | TC→NXP | `[temp][humidity][fuel][seat]` | (none) |
| `0x81` | faultEvent | TC→NXP | (varies) | (none) |
| `0x82` | commandRejected | TC→NXP | (none) | (none) |

---

## How to add a new command handler (for teammates)

This is the **only** thing you need to know. You do NOT touch lwIP, TCP, UDP, or transport code.

### Step 1: Define your command type
In `Networking/COMM/frame_codec.h`:
```c
#define CMD_MY_NEW_COMMAND  0x05
```

### Step 2: Write your handler function
In `Cpu0_Main.c` (or your own file):
```c
static void my_handler(uint8_t cmd_type, uint8_t seq,
                       const uint8_t *payload, uint8_t len)
{
    // Your logic here — read payload, control hardware, etc.

    // Optionally send a reply back to the NXP:
    uint8_t reply[FRAME_OVERHEAD];  // 5 bytes if no payload
    uint8_t reply_len = frame_codec_pack(EVT_CMD_REJECTED, seq, NULL, 0, reply);
    transport_send_event(reply, reply_len);
}
```

### Step 3: Register it
In `core0_main()`, after `dispatcher_init()`:
```c
dispatcher_register(CMD_MY_NEW_COMMAND, my_handler);
```

### That's it. The networking layer handles everything else:
- TCP connection management
- Frame parsing + CRC verification
- Dispatching to your handler
- Sending your reply back over TCP

---

## Debug output (UART)

Connect a serial terminal at `115200` baud. You'll see messages like:

```
netif: new ip address assigned: 192.168.10.30       ← lwIP init
>>> ACCEPT: client connected, state=4                ← TCP client connected
>>> HANDLER: setHeater, seq=0                        ← Your handler was called
>>> SEND: tcp_write=0                                ← Reply queued OK
>>> SEND: tcp_output=0  OK reply flushed             ← Reply sent
>>> DISPATCH: cmd=0x01 seq=0 latency=1 us            ← RX→dispatch latency
>>> RECV: client closed (p=NULL)                     ← Client disconnected
>>> CLOSE: tcp_close OK                              ← Connection cleaned up
```

If you see `>>> CRC ERROR`, a corrupted frame was received and dropped.

---

## Acceptance criteria

| ID | Description | Status |
|----|-------------|--------|
| HNC-SAF-01.1 | lwIP netif on GETH, link up, ping succeeds | PASS |
| HNC-SAF-01.2 | UDP telemetry socket + TCP command server up | PASS |
| HNC-SAF-01.3 | CRC-16 verify on CMD_TYPE+payload | PASS |
| HNC-SAF-01.4 | Decode CMD_TYPE + payload into typed struct | PASS |
| HNC-SAF-01.5 | RX→dispatch latency < 5 ms | PASS (0-1 us) |

---

## Architecture diagram

```
┌─────────────────────────────────────────────┐
│                  Cpu0_Main.c                  │
│  (super-loop: net_if_poll + telemetry TX)     │
│  (registers handlers via dispatcher)          │
└──────────────┬──────────────────────────────┘
               │ dispatcher_register() / dispatcher_dispatch()
               ▼
┌─────────────────────────────────────────────┐
│              APP / dispatcher.c               │
│  (handler registry: cmd_type → handler fn)    │
└──────────────┬──────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────┐
│        COMM / frame_codec.c + transport.c     │
│  frame_codec: pack/unpack [CMD][SEQ][LEN]    │
│               [PAYLOAD][CRC16]                │
│  transport: UDP TX (telemetry)                │
│             TCP server RX (commands)          │
│             TCP event TX (replies)            │
└──────────────┬──────────────────────────────┘
               │ Ifx_Lwip_init() / Ifx_Lwip_pollTimerFlags()
               ▼
┌─────────────────────────────────────────────┐
│        HAL / net_if.c + Ifx_Lwip.c            │
│  (lwIP + GETH hardware driver)                │
└─────────────────────────────────────────────┘
```

Teammates only interact with the **top two layers**. They never touch lwIP.

---

## Teammates: what NOT to touch

| File/Folder | Why |
|-------------|-----|
| `Networking/COMM/transport.c` | TCP/UDP internals — working, don't break it |
| `Networking/HAL/net_if.c` | lwIP wrapper — working |
| `Libraries/` | Infineon iLLD + lwIP source — never modify |
| `Configurations/lwipopts.h` | lwIP config — already tuned |

You only touch: `Cpu0_Main.c` (or your own driver files) + `frame_codec.h` (to add new CMD_TYPEs).

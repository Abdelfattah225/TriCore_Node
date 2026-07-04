# Step 9 — Teammate Hand-off Guide

## One diagram

```
  You (Mostafa / Ayman)          The networking layer
  ┌──────────────────────┐       ┌──────────────────────────┐
  │                      │       │  transport.c              │
  │  Write handler()     │       │  frame_codec.c            │
  │  Register it        │──────►│  dispatcher.c             │
  │                      │       │  net_if.c                 │
  │  That's it.          │       │  Ifx_Lwip.c               │
  │                      │       │                          │
  │  Don't touch lwIP.   │       │  All of this = BLACK BOX  │
  │  Don't touch TCP.   │       │  You don't need to read it│
  │  Don't touch UDP.   │       │                          │
  └──────────────────────┘       └──────────────────────────┘
```

## What you DO touch

```
  ┌─────────────────────────────────────────────────────┐
  │  1. Cpu0_Main.c     → write your handler function   │
  │  2. frame_codec.h   → add #define CMD_XXX 0xNN      │
  │                                                      │
  │  That's 2 files. Nothing else.                      │
  └─────────────────────────────────────────────────────┘
```

## Step-by-step: Add a new command

### Example: Add "setFanSpeed" command (0x06)

**Step 1: `frame_codec.h` — add your command type**
```c
#define CMD_SET_FAN_SPEED  0x06
```

**Step 2: `Cpu0_Main.c` — write the handler**
```c
static void fan_speed_handler(uint8_t cmd_type, uint8_t seq,
                              const uint8_t *payload, uint8_t len)
{
    // payload[0] = fan speed (0-100)
    uint8_t speed = payload[0];

    // TODO: call your HAL driver here
    // IfxPort_setPinHigh(...) or whatever

    // Optionally send a reply back to NXP:
    uint8_t reply[FRAME_OVERHEAD];              // 5 bytes (no payload)
    uint8_t n = frame_codec_pack(EVT_CMD_REJECTED, seq, NULL, 0, reply);
    transport_send_event(reply, n);
}
```

**Step 3: `Cpu0_Main.c` — register it in `core0_main()`**
```c
dispatcher_init();
dispatcher_register(CMD_SET_HEATER,      test_heater_handler);
dispatcher_register(CMD_SET_SEAT,        test_seat_handler);
dispatcher_register(CMD_SET_TRUNK,       test_trunk_handler);
dispatcher_register(CMD_SET_AMBIENT_LED, test_led_handler);
dispatcher_register(CMD_REQUEST_SENSORS, test_sensors_handler);
dispatcher_register(CMD_SET_FAN_SPEED,   fan_speed_handler);  // ← YOURS
```

**Done.** Build, flash, test with Python simulator.

## What the networking layer does for you

```
  When NXP sends a command:

  1. Ethernet packet arrives at GETH hardware
  2. lwIP processes TCP/IP headers
  3. transport_tcp_recv() callback fires
  4. frame_codec_unpack() parses [CMD][SEQ][LEN][PAYLOAD][CRC]
  5. CRC verified → if bad, dropped (you never see it)
  6. dispatcher_dispatch() finds your handler by CMD_TYPE
  7. YOUR HANDLER IS CALLED with (cmd_type, seq, payload, len)

  You just read payload[] and do your thing.
```

## The 3 APIs you use

```
  ┌──────────────────────────────────────────────────────────┐
  │ 1. dispatcher_register(CMD_XXX, your_handler)            │
  │    → tells the system "I handle CMD_XXX"                 │
  │                                                          │
  │ 2. transport_send_event(buf, len)                       │
  │    → sends a TCP reply back to NXP                       │
  │    → buf must be packed by frame_codec_pack()            │
  │                                                          │
  │ 3. transport_send_telemetry(buf, len)                    │
  │    → sends a UDP packet to NXP (fire-and-forget)         │
  │    → buf must be packed by frame_codec_pack()            │
  └──────────────────────────────────────────────────────────┘
```

## Reply patterns

```
  Pattern A: Reply with commandRejected (no payload)
  ─────────────────────────────────────────────────
  uint8_t reply[FRAME_OVERHEAD];           // 5 bytes
  uint8_t n = frame_codec_pack(EVT_CMD_REJECTED, seq, NULL, 0, reply);
  transport_send_event(reply, n);

  Pattern B: Reply with data (e.g. sensorData)
  ───────────────────────────────────────────
  uint8_t payload[4] = {temp, humidity, fuel, seat};
  uint8_t reply[FRAME_OVERHEAD + 4];        // 9 bytes
  uint8_t n = frame_codec_pack(EVT_SENSOR_DATA, seq, payload, 4, reply);
  transport_send_event(reply, n);

  Pattern C: Send telemetry (UDP, no reply expected)
  ───────────────────────────────────────────────────
  uint8_t payload[4] = {temp, humidity, fuel, seat};
  uint8_t buf[FRAME_OVERHEAD + 4];
  uint8_t n = frame_codec_pack(EVT_SENSOR_DATA, 0, payload, 4, buf);
  transport_send_telemetry(buf, n);
```

## What NOT to touch

```
  ┌──────────────────────────────────────────────────┐
  │  NEVER MODIFY:                                    │
  │                                                    │
  │  Networking/COMM/transport.c    ← TCP/UDP engine  │
  │  Networking/COMM/transport.h                      │
  │  Networking/COMM/frame_codec.c   ← CRC + pack     │
  │  Networking/HAL/net_if.c         ← lwIP wrapper   │
  │  Networking/HAL/net_if.h                          │
  │  Networking/APP/dispatcher.c     ← router         │
  │  Networking/APP/dispatcher.h                      │
  │  Libraries/*                     ← Infineon code  │
  │  Configurations/lwipopts.h       ← lwIP config    │
  │  Libraries/.../Ifx_Lwip.c        ← static IP      │
  │                                                    │
  │  If you break these, networking stops working.    │
  └──────────────────────────────────────────────────┘
```

## How to test your handler

```
  1. Build + flash in AURIX Studio
  2. Run:  python nxp_simulator.py
  3. Add your command to the simulator:

     # In nxp_simulator.py, add:
     send_and_recv(rx, CMD_SET_FAN_SPEED, seq,
                   bytes([50]), "Test: setFanSpeed(50)")
     seq += 1

  4. Watch UART for:
     >>> HANDLER: setFanSpeed, seq=X

  5. If you see it → your handler is registered correctly
```

## Debug output cheat sheet

```
  UART message                    Meaning
  ──────────────────────────────  ─────────────────────────
  >>> ACCEPT: client connected    NXP connected over TCP
  >>> HANDLER: setHeater, seq=0   Your handler was called
  >>> SEND: tcp_write=0           Reply queued OK
  >>> SEND: tcp_output=0          Reply sent to NXP
  >>> DISPATCH: latency=1 us      Time from RX to your handler
  >>> CRC ERROR: dropping frame   Corrupted frame (cable issue?)
  >>> RECV: client closed         NXP disconnected
  >>> CLOSE: tcp_close OK         Connection cleaned up properly
  >>> TCP_ERR: err=X              Connection error (cable pulled?)
```

## Final acceptance criteria

| ID | Description | Status |
|----|-------------|--------|
| HNC-SAF-01.1 | lwIP netif on GETH, ping succeeds | ✅ |
| HNC-SAF-01.2 | UDP telemetry + TCP command server | ✅ |
| HNC-SAF-01.3 | CRC-16 verify on CMD_TYPE+payload | ✅ |
| HNC-SAF-01.4 | Decode CMD_TYPE + payload into typed struct | ✅ |
| HNC-SAF-01.5 | RX→dispatch latency < 5 ms | ✅ (0-1 us) |

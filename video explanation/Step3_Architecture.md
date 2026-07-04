# Step 3 — Architecture: The Networking Layer

## One diagram

```
                         Cpu0_Main.c
                    (your handlers + super-loop)
                    ┌──────────────────────┐
                    │ heater_handler()     │ ← You write these
                    │ seat_handler()       │
                    │ sensors_handler()    │
                    └──────────┬───────────┘
                               │ dispatcher_register(CMD, handler)
                               │ dispatcher_dispatch(CMD, seq, payload, len)
                               ▼
                    ┌──────────────────────┐
           APP      │   dispatcher.c       │ ← Routes commands to handlers
                    │   "if cmd==X → call │    You never touch this
                    │    handler X()"      │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
           COMM     │  frame_codec.c       │ ← Pack/unpack [CMD][SEQ][LEN][PAYLOAD][CRC]
                    │  transport.c         │ ← UDP TX, TCP RX, TCP TX
                    │                      │    You never touch this
                    └──────────┬───────────┘
                               │ net_if_poll()
                               ▼
                    ┌──────────────────────┐
           HAL      │  net_if.c            │ ← Wraps lwIP + GETH hardware
                    │  Ifx_Lwip.c          │    You never touch this
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
           HARDWARE │  GETH (Ethernet MAC) │ ← Infineon hardware
                    │  PHY (RTL8211F)      │
                    └──────────────────────┘
```

## The rule for teammates

```
  ┌─────────────────────────────────────────────────────┐
  │  YOU TOUCH:  Cpu0_Main.c  (write handlers)         │
  │              frame_codec.h (add CMD_TYPE if needed) │
  │                                                     │
  │  YOU DON'T TOUCH:  Everything else                  │
  └─────────────────────────────────────────────────────┘
```

## How to add a handler (3 steps)

```c
// 1. frame_codec.h — add your command type
#define CMD_MY_COMMAND  0x05

// 2. Cpu0_Main.c — write the handler
static void my_handler(uint8_t cmd_type, uint8_t seq,
                      const uint8_t *payload, uint8_t len)
{
    // read payload, control hardware, etc.

    // optionally reply:
    uint8_t reply[FRAME_OVERHEAD];
    uint8_t n = frame_codec_pack(EVT_CMD_REJECTED, seq, NULL, 0, reply);
    transport_send_event(reply, n);
}

// 3. Cpu0_Main.c — register it in core0_main()
dispatcher_register(CMD_MY_COMMAND, my_handler);
```

## File map

```
Networking/
├── HAL/   net_if.c/.h        → lwIP + GETH wrapper (DON'T TOUCH)
├── COMM/  frame_codec.c/.h   → pack/unpack + CRC (touch .h only for new CMDs)
│         transport.c/.h      → UDP/TCP sockets (DON'T TOUCH)
└── APP/   dispatcher.c/.h    → handler registry (DON'T TOUCH)
```

That's it. The networking layer is a black box. You register handlers, it calls them when commands arrive.

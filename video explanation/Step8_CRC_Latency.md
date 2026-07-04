# Step 8 — CRC-16 & Latency Measurement

## One diagram

```
  Frame format BEFORE:          Frame format AFTER:
  ┌───┬───┬───┬─────────┐       ┌───┬───┬───┬─────────┬───────┐
  │CMD│SEQ│LEN│ PAYLOAD │       │CMD│SEQ│LEN│ PAYLOAD │CRC16  │
  └───┴───┴───┴─────────┘       └───┴───┴───┴─────────┴───────┘
   3 bytes header                 3 bytes + payload + 2 bytes CRC
   No integrity check             CRC-16/CCITT over CMD+PAYLOAD

  Latency measurement:
  ┌─────────────┐                    ┌─────────────┐
  │ pbuf arrives │── STM tick ──────►│ dispatch()  │
  │ t_rx = read  │   measure delta   │ t_dispatch   │
  └─────────────┘                    └─────────────┘
                     │
                     ▼
              latency = 0-1 us
              requirement: < 5000 us (5 ms)
              result: 5000x faster than needed
```

## CRC-16: What and why

```
  Without CRC:
  ┌──────────────────────────────────────────┐
  │ Cable noise flips one bit in payload     │
  │ Board reads wrong temperature value      │
  │ No one knows it's wrong                   │
  └──────────────────────────────────────────┘

  With CRC:
  ┌──────────────────────────────────────────┐
  │ Cable noise flips one bit in payload     │
  │ Board calculates CRC → doesn't match     │
  │ >>> CRC ERROR: dropping frame            │
  │ Corrupted frame rejected, waits for next │
  └──────────────────────────────────────────┘
```

## How CRC-16/CCITT works

```
  Polynomial: 0x1021
  Initial value: 0xFFFF
  No reflection, no final XOR

  Input:  CMD_TYPE byte + PAYLOAD bytes
  Output: 2-byte CRC (little-endian in frame)

  Example: setHeater command
  ┌─────────────────────────────────────────────────────┐
  │ CMD=0x01  SEQ=0x05  LEN=0x01  PAYLOAD=0x19          │
  │                                                      │
  │ CRC input: [0x01, 0x19]  (CMD + PAYLOAD only)       │
  │ CRC output: 0xAD26                                   │
  │                                                      │
  │ Frame on wire: 01 05 01 19 26 AD                    │
  │                ├──┤│──┤│──┤│──┤│────┤               │
  │                CMD  SEQ LEN PAY CRC16(LE)           │
  └─────────────────────────────────────────────────────┘
```

## Where CRC happens in code

```
  SENDING (frame_codec_pack):
  ┌──────────────────────────────────────┐
  │ 1. Build header [CMD][SEQ][LEN]      │
  │ 2. Copy payload                      │
  │ 3. CRC = crc16(CMD + PAYLOAD)        │
  │ 4. Append CRC [LO][HI]               │
  │ 5. Return total frame                │
  └──────────────────────────────────────┘

  RECEIVING (frame_codec_unpack):
  ┌──────────────────────────────────────┐
  │ 1. Read header [CMD][SEQ][LEN]        │
  │ 2. Read payload                      │
  │ 3. Read received CRC                 │
  │ 4. CRC = crc16(CMD + PAYLOAD)        │
  │ 5. If CRC != received → CRC_ERROR    │
  │ 6. If CRC == received → OK, dispatch │
  └──────────────────────────────────────┘

  3 return codes:
    FRAME_UNPACK_OK         = 1  → dispatch to handler
    FRAME_UNPACK_INCOMPLETE = 0  → wait for more data
    FRAME_UNPACK_CRC_ERROR  = 2  → drop frame, log error
```

## Latency: How we measure

```
  STM (System Timer Module) runs at 100 MHz
  = 100,000 ticks per millisecond
  = 100 ticks per microsecond

  In transport_tcp_recv():
  ┌──────────────────────────────────────────────────┐
  │ uint32_t t_rx = IfxStm_get(&MODULE_STM0);        │ ← pbuf arrives
  │                                                   │
  │   ... parse frame, check CRC ...                  │
  │                                                   │
  │ uint32_t t_dispatch = IfxStm_get(&MODULE_STM0);  │ ← before dispatch
  │                                                   │
  │ uint32_t delta_us = (t_dispatch - t_rx) / 100;    │ ← convert to us
  │                                                   │
  │ dbg_print("latency=%d us", delta_us);             │
  └──────────────────────────────────────────────────┘

  UART output:
  >>> DISPATCH: cmd=0x01 seq=0 latency=1 us
  >>> DISPATCH: cmd=0x10 seq=4 latency=0 us
```

## Acceptance criteria

| ID | Description | Result |
|----|-------------|--------|
| HNC-SAF-01.3 | CRC-16 verify on CMD_TYPE+payload | ✅ PASS |
| HNC-SAF-01.5 | RX→dispatch latency < 5 ms | ✅ PASS (0-1 us) |

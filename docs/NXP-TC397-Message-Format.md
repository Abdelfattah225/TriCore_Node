---
tags: [interface, l2, contract]
status: draft
---
# NXP ↔ TC397 Message Format

> Shared contract between NXP and TC397. Lives in `hnc-docs`.
>
> **Transport updated (2026-06-16):** carried over **lwIP** now — **TCP** for commands/events, **UDP** for telemetry (TC397-Networking). The raw-L2 framing below is superseded; the **CMD_TYPE table still applies** as the application payload.

## Message Layout (over UDP/TCP)
```
[CMD_TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD NB]
```

- **CMD_TYPE:** message type byte (table below)
- **SEQ:** rolling sequence (dedupe / loss visibility)
- **LEN:** payload length
- TCP/UDP already checksum the datagram, so the app CRC is optional.

### Legacy raw-L2 framing (superseded)
`[DST_MAC 6B][SRC_MAC 6B][EtherType 0x88B5][CMD_TYPE 1B][PAYLOAD NB][CRC 2B]`

## CMD_TYPE (draft)
| Value | Direction | Meaning |
|-------|-----------|---------|
| 0x01 | NXP→TC | setHeater |
| 0x02 | NXP→TC | setSeat |
| 0x03 | NXP→TC | setTrunk |
| 0x04 | NXP→TC | setAmbientLED (TC LEDs) |
| 0x10 | NXP→TC | requestSensors |
| 0x80 | TC→NXP | sensorData |
| 0x81 | TC→NXP | faultEvent |
| 0x82 | TC→NXP | commandRejected |

## Open
- [ ] Finalize CMD_TYPE table
- [ ] Define payload byte layout per type
- [ ] Choose CRC algorithm
- [ ] Document in `hnc-docs`

## Related
Decision-Raw-L2 · TC397-Networking · NXP-Networking

---
tags: [interface, safety, contract]
status: draft
---
# Safety Matrix

> Enforced in hardware by TC397. The authoritative rules for what the AI may not do.

| Action | Block Condition | Reason |
|--------|----------------|--------|
| Seat movement | Speed > 10 km/h | Occupant safety during motion |
| Trunk unlock | Speed > 0 km/h | Security + safety |
| Heater ON | Engine overheat fault active | Thermal protection |
| All commands | Critical fault state | Fail-safe |

## On Block
TC397 sends `commandRejected` → NXP → RPi5 → agent explains to driver in natural language. LED shows amber.

## Open
- [ ] Confirm exact speed thresholds
- [ ] Define "critical fault state" set
- [ ] Document in `hnc-docs`

## Related
TC397-Safety-Matrix · Decision-NXP-Master

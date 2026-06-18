---
tags: [tc397, component, safety]
status: planned
---
# TC397 · Safety Matrix Enforcement

## Goal
Every incoming command is checked against the Safety-Matrix before any actuator moves. The TC397 does not trust upstream nodes.

## Tasks
- [ ] Implement speed-gate check (seat > 10 km/h, trunk > 0 km/h)
- [ ] Implement fault-gate check (heater blocked on overheat)
- [ ] Critical fault → safe mode (block all)
- [ ] Send rejection event back to NXP → RPi5 explains to driver
- [ ] LED indication on block

## Related
Safety-Matrix · Safety-TC397-Index

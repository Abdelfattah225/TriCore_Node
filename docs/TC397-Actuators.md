---
tags: [tc397, component, actuator]
status: planned
---
# TC397 · Actuators

| Actuator | Hardware | Driver | Use Case |
|----------|----------|--------|----------|
| Air conditioner | **2× JGA25-370 DC 12 V** (fans/blower) | **L298N** dual H-bridge | UC-01-Climate |
| Memory seat | Stepper motor | A4988 | UC-02-Seat |
| Trunk lock | SG90 servo | direct PWM | UC-03-Trunk |
| Status LEDs | RGB LED | GTM PWM ×3 | fault/block indication |

## Tasks
- [ ] A/C driver: 2× DC motors via L298N (EN = PWM speed, IN = direction); on/off + speed
- [ ] Stepper driver (position control)
- [ ] SG90 servo driver (angle control)
- [ ] RGB LED PWM driver
- [ ] All actuators gated by TC397-Safety-Matrix

## A/C note
The two JGA25-370 gearmotors are both the **air-conditioner** (e.g. blower + condenser fan), each on one L298N channel — driven together as "A/C on/off + level", or independently. See TC397-HW-Design for wiring + the L298N 3.3 V logic caveat.

## Related
Safety-TC397-Index · TC397-Safety-Matrix · TC397-HW-Design

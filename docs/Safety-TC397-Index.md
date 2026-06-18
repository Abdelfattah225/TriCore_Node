---
tags: [node, tc397, safety]
owner: [Mostafa Hesham]
repo: hnc-safety-tc397
status: planned
---
# Safety Controller · AURIX TC397

The hardware guardian. Bare-metal C firmware that physically intercepts commands, enforces the safety matrix, and drives all physical actuators.

## Role
- Last line of defence — does not trust upstream nodes
- Drives all physical actuators
- Reads all physical sensors
- Injects faults via hardware interrupts

## Design
- TC397-HW-Design — board, BOM, pin/peripheral allocation, wiring, power
- TC397-SW-Architecture — firmware layers, modules, scheduling, flows, state machine

## Components
- TC397-Networking — Raw L2 Ethernet via IfxGeth
- TC397-Actuators — DC motor, stepper, servo, LEDs
- TC397-Sensors — temp, fuel, seat position (ADC)
- TC397-Fault-Injection — ERU button interrupts
- TC397-Safety-Matrix — command gating logic

## Status
- ✅ Environment configured (iLLD + Aurix Dev Studio)
- ✅ Example app compiled, flashed, debugged
- ✅ HW + SW plan finalized
- 🔄 Implementation pending

## Interfaces
- Receives: Raw L2 commands from NXP
- Sends: sensor data + fault events to NXP
- Enforces: Safety-Matrix

## Requirements
See Req-Safety

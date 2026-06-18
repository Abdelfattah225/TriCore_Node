# TC397 Safety Node — Documentation

Design docs for the **AURIX TC397** safety controller of the Hyper-Nova Cockpit. The TC397 is the hardware guardian: it receives commands from the NXP gateway, enforces the safety matrix before any actuation, drives the physical actuators, reads sensors, and injects faults. It **does not trust upstream nodes**.

Board: `KIT_A2G_TC397_5V_TFT` · Bare-metal C on AURIX Dev Studio + iLLD · Networking via **lwIP** (UDP telemetry + TCP commands/events).

## Index
| Doc | What's in it |
|-----|--------------|
| [Safety-TC397-Index](Safety-TC397-Index.md) | Node overview, role, status |
| [TC397-HW-Design](TC397-HW-Design.md) | BOM, power, pin/peripheral map, wiring, diagrams |
| [TC397-SW-Architecture](TC397-SW-Architecture.md) | Firmware layers, modules, scheduling, command/fault flows, state machine |
| [TC397-Networking](TC397-Networking.md) | lwIP, UDP/TCP transport split, config |
| [TC397-Actuators](TC397-Actuators.md) | A/C (L298N + 2× JGA25-370), stepper, servo, RGB |
| [TC397-Sensors](TC397-Sensors.md) | DHT11 (temp+humidity), fuel/seat pots (ADC) |
| [TC397-Safety-Matrix](TC397-Safety-Matrix.md) | Command-gating logic |
| [Safety-Matrix-Thresholds](Safety-Matrix-Thresholds.md) | The authoritative block rules/thresholds |
| [NXP-TC397-Message-Format](NXP-TC397-Message-Format.md) | Message/CMD_TYPE contract (over UDP/TCP) |
| [Req-Safety](Req-Safety.md) | Requirements + acceptance criteria + traceability |
| [TASKS-Ayman](TASKS-Ayman.md) | Driver/sensor task brief (pins, specs, done-criteria) |

## Team split
- **Networking (M1) — Abdelfattah:** lwIP bring-up, message protocol, UDP telemetry TX, TCP command server.
- **Drivers & sensors (M2) — Ayman:** A/C (L298N), stepper, servo, RGB, DHT11, ADC pots. → see [TASKS-Ayman](TASKS-Ayman.md).
- **Safety core (M2) — Mostafa:** safety-matrix gating, ERU fault injection, rejection/fault event TX.

> Source of truth lives in the team Obsidian vault; these are the GitHub-rendered copies (Obsidian `[[wikilinks]]` flattened to plain text). Day-to-day status is tracked in ClickUp.

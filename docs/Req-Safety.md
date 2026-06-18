---
tags: [requirements, tc397]
---
# Safety (TC397) Requirements

## Top-level requirements

| ID | Req | Priority |
|----|-----|----------|
| HNC-SAF-01 | MUST exchange messages with NXP over **lwIP** (UDP telemetry, TCP commands/events) | MUST |
| HNC-SAF-02 | MUST enforce the Safety-Matrix before any actuation | MUST |
| HNC-SAF-03 | MUST drive the A/C (2× JGA25-370 DC via L298N), stepper, servo, RGB LEDs | MUST |
| HNC-SAF-04 | MUST read cabin temp+humidity (DHT11) and fuel + seat position (ADC) | MUST |
| HNC-SAF-05 | MUST inject faults via ERU button interrupts | MUST |
| HNC-SAF-06 | MUST send rejection events back to NXP on block | MUST |
| HNC-SAF-07 | MUST NOT trust upstream nodes unconditionally | MUST |

> **Change (2026-06-16):** transport → **lwIP** (UDP telemetry, TCP commands); A/C = **2× JGA25-370 via L298N**; cabin temp via **DHT11** (+humidity). See TC397-Networking · TC397-HW-Design.

---

## Detailed sub-requirements & acceptance criteria

### HNC-SAF-01 — Raw L2 receive/parse
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-01.1 | Bring up lwIP netif on GETH | Link up; ping NXP succeeds |
| HNC-SAF-01.2 | UDP telemetry socket + TCP command server up | NXP connects (TCP) and receives UDP datagrams |
| HNC-SAF-01.3 | Validate CRC-16 over CMD_TYPE+payload | Corrupted frame dropped + error counter increments |
| HNC-SAF-01.4 | Decode CMD_TYPE + payload into typed command | Each defined CMD_TYPE unpacks to correct struct fields |
| HNC-SAF-01.5 | RX→decode latency budget | < 5 ms from frame arrival to dispatch (measured via STM timestamp) |

### HNC-SAF-02 — Safety-matrix enforcement
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-02.1 | Seat blocked when speed > 10 km/h | setSeat at 11 km/h → no motion + reject |
| HNC-SAF-02.2 | Trunk blocked when speed > 0 km/h | setTrunk at 1 km/h → no motion + reject |
| HNC-SAF-02.3 | Heater blocked while overheat fault active | setHeater with overheat → no motion + reject |
| HNC-SAF-02.4 | Critical fault → SAFE mode blocks all commands | In SAFE, every actuator command rejected |
| HNC-SAF-02.5 | Gate evaluated before any actuator write | No code path reaches a driver without passing `safety_check()` |
| HNC-SAF-02.6 | Gate decision latency | Decision < 1 ms after command decoded |

### HNC-SAF-03 — Actuator drive
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-03.1 | DC motor on/off + speed via PWM | setHeater duty 0–100% produces matching motor speed |
| HNC-SAF-03.2 | Stepper absolute position by step count | setSeat to N → seat reaches N steps, repeatable ±0 steps |
| HNC-SAF-03.3 | Servo angle via 50 Hz pulse | setTrunk open/closed → servo at commanded angle |
| HNC-SAF-03.4 | RGB LED status colour | green=ok, amber=blocked, red=fault rendered correctly |
| HNC-SAF-03.5 | Actuator commands clamped to physical limits | Out-of-range target clamped, never driven past end-stop |

### HNC-SAF-04 — Sensor read (ADC)
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-04.1 | Cabin temp + humidity via DHT11 (single-wire) | temp within ±2 °C; humidity plausible; checksum valid |
| HNC-SAF-04.2 | Fuel level read + scale to % | Knob sweep gives monotonic 0–100% |
| HNC-SAF-04.3 | Seat position read + scale to steps | Knob sweep maps linearly to step range |
| HNC-SAF-04.4 | Sample rate ≥ 50 Hz | Snapshot refreshed every ≤20 ms |
| HNC-SAF-04.5 | Out-of-range reading rejected | Spurious value → last-good held + flagged |

### HNC-SAF-05 — Fault injection (ERU)
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-05.1 | Each button mapped to an ERU edge interrupt | Press generates exactly one IRQ event |
| HNC-SAF-05.2 | Debounce ≥ 20 ms | No double-trigger on a single press |
| HNC-SAF-05.3 | Button → fault code mapping | Each button sets its defined fault (e.g. P0217 overheat) |
| HNC-SAF-05.4 | Fault auto-triggers relevant matrix block | Overheat button → subsequent setHeater blocked |
| HNC-SAF-05.5 | Critical button → SAFE mode | Critical button blocks all + LED red |

### HNC-SAF-06 — Rejection / event reporting
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-06.1 | Send `commandRejected (0x82)` on every block | Each blocked command yields exactly one reject frame |
| HNC-SAF-06.2 | Reject carries a reason code | NXP/RPi5 can map reason → driver explanation |
| HNC-SAF-06.3 | Send `faultEvent (0x81)` on fault set change | Button press → one fault frame to NXP |
| HNC-SAF-06.4 | Respond to `requestSensors (0x10)` with `sensorData (0x80)` | Request → one populated sensor frame |

### HNC-SAF-07 — Don't trust upstream
| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| HNC-SAF-07.1 | Validate every frame (EtherType, length, CRC) before use | Malformed/oversized frame never reaches dispatcher |
| HNC-SAF-07.2 | Unknown CMD_TYPE ignored safely | No default actuation; error counted |
| HNC-SAF-07.3 | Gate applies even to well-formed valid commands | A perfectly valid unsafe command is still blocked |

---

## Non-functional requirements

| ID | Requirement |
|----|-------------|
| HNC-SAF-NF-01 | End-to-end command→actuation (valid, allowed) < 50 ms on TC397 |
| HNC-SAF-NF-02 | `safety_matrix` + `frame_codec` host-portable, unit-tested off-target |
| HNC-SAF-NF-03 | No dynamic memory allocation in steady state (static buffers only) |
| HNC-SAF-NF-04 | No level shifters required (all logic interfaces 3.3 V) |

---

## Requirement → design traceability

| Requirement | Hardware | Software |
|-------------|----------|----------|
| HNC-SAF-01 | GETH + lwIP | `net_if`, `l2_rx`, `frame_codec` |
| HNC-SAF-02 | — | `safety_matrix`, `system_state`, `command_dispatcher` |
| HNC-SAF-03 | L298N + 2× JGA25-370, A4988, SG90, RGB LED | `ac_motor`, `stepper`, `servo`, `rgb_led`, `actuator_manager` |
| HNC-SAF-04 | DHT11, pots → EVADC | `temp_sensor` (DHT11), `fuel_sensor`, `seat_sensor`, `sensor_sampler` |
| HNC-SAF-05 | 4 buttons → SCU-ERU | `fault_buttons`, `fault_manager` |
| HNC-SAF-06 | GETH TX | `l2_tx`, `frame_codec` |
| HNC-SAF-07 | — | `frame_codec`, `l2_rx`, `safety_matrix` |

See TC397-HW-Design for the hardware connections and TC397-SW-Architecture for the software modules referenced above.

## Related
Safety-TC397-Index · TC397-HW-Design · TC397-SW-Architecture · Safety-Matrix

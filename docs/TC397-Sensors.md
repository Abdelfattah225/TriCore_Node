---
tags: [tc397, component, sensor]
status: planned
---
# TC397 · Sensors

| Sensor | Hardware | Interface | Provides |
|--------|----------|-----------|----------|
| Cabin temp + humidity | **DHT11** | GPIO single-wire (bit-banged) | UC-01-Climate |
| Fuel level | Potentiometer | EVADC | UC-07-Trip |
| Seat position | Slide potentiometer | EVADC | UC-02-Seat |

## Tasks
- [ ] DHT11 single-wire driver (start pulse, read 40-bit frame, checksum) → temp °C + humidity %
- [ ] ADC channel config for fuel + seat
- [ ] Fuel level read + scaling to %
- [ ] Seat position read + scaling to steps
- [ ] Expose readings via UDP telemetry / on request (TC397-Networking)

## DHT11 note
Digital single-wire protocol with µs-level timing — **not** an ADC sensor. Needs one GPIO that flips in→out and a microsecond timer (STM). Max ~1 reading/sec. Bonus: it returns **humidity** too, which we can surface to the agent. Cabin temp therefore moved off the ADC.

## Demo Note
Fuel + seat are physical knobs — twist during demo to show live AI reasoning changes.

## Related
Safety-TC397-Index · TC397-HW-Design

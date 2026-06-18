# TC397 — Task Brief for Ayman

Your work packages on the AURIX TC397 safety node, with everything you need to start: pins, parts, what to build, and how we'll know it's done. Full hardware detail is in **TC397-HW-Design.md**; how your drivers plug into the firmware is in **TC397-SW-Architecture.md**.

Board: **Infineon AURIX TC397 TFT App Kit (`KIT_A2G_TC397_5V_TFT`)** · Toolchain: **AURIX Dev Studio + iLLD**, bare-metal C.

> **Two ground rules for all your drivers**
> 1. **Common ground** — the board, the 5 V supply and the 12 V supply share one GND.
> 2. **Expose a clean API, don't gate inside it.** Each driver offers simple functions (e.g. `ac_set(level)`, `seat_goto(steps)`). The *safety matrix* (Mostafa) decides whether a command is allowed and calls your driver — you don't put safety checks inside the driver.

---

## Your 6 tasks (all in the ClickUp "Safety TC397" list)

### 1. A/C — 2× DC motors via L298N
Drive the two **JGA25-370 12 V** gearmotors (the air-conditioner fans/blower) through the **L298N** H-bridge.

| Signal | TC397 pin | iLLD | L298N |
|--------|-----------|------|-------|
| AC1 speed | P02.0 | GTM TOM0_0 PWM (~10 kHz) | ENA |
| AC1 dir | P02.1 / P00.4 | IfxPort GPIO | IN1 / IN2 |
| AC2 speed | P02.6 | GTM TOM0_3 PWM (~10 kHz) | ENB |
| AC2 dir | P00.5 / P00.6 | IfxPort GPIO | IN3 / IN4 |

- API: `ac_set(uint8 level_0_100)` — PWM duty on EN sets fan speed; IN pins set direction (fans only need one direction, so IN1=1/IN2=0 and just vary EN).
- **Done when (HNC-SAF-03.1):** duty 0–100 % maps to visibly different fan speeds; both motors controllable.
- ⚠️ **L298N is 5 V logic** — a 3.3 V "high" usually works but is the one marginal interface. If a channel is flaky, add a 3.3→5 V buffer (74AHCT125). Power motors from 12 V, never the board.

### 2. Stepper driver + position control (seat)
Bipolar stepper via **A4988** (STEP/DIR/EN).

| Signal | TC397 pin | iLLD |
|--------|-----------|------|
| STEP | P02.2 | GTM TOM0_1 PWM (freq = speed, 200–2000 Hz) |
| DIR | P02.3 | IfxPort GPIO |
| EN# | P02.4 | IfxPort GPIO (active-low) |

- API: `seat_goto(int32 target_steps)` — count steps to an absolute position.
- **Done when (HNC-SAF-03.2):** seat reaches a commanded step target repeatably (±0 steps); clamp to end-stops (03.5).
- A4988 MS1/2/3 hardwired (full-step); set coil current with the Vref trimmer.

### 3. SG90 servo driver (trunk)
| Signal | TC397 pin | iLLD |
|--------|-----------|------|
| TRUNK_PWM | P02.5 | GTM TOM0_2 PWM, **50 Hz**, 1–2 ms pulse |

- API: `servo_set(uint8 angle_deg)` → pulse width. Power servo from 5 V.
- **Done when (HNC-SAF-03.3):** open/closed angles hold correctly.

### 4. RGB LED PWM (status indicator) — *do this one first*
Common-cathode RGB LED on 3 PWM channels. It's the easiest GTM check and the indicator the whole team reuses.

| Signal | TC397 pin | iLLD |
|--------|-----------|------|
| R / G / B | P10.1 / P10.2 / P10.3 | GTM ATOM1_0/1/2 PWM (~1 kHz) |

- API: `rgb_set(r,g,b)`. Convention: **green = ok, amber = blocked, red = fault**.
- **Done when (HNC-SAF-03.4):** all three colors render; resistors ~220–330 Ω.

### 5. DHT11 temp + humidity driver (single-wire)
**Not an ADC sensor** — a bit-banged single-wire digital protocol.

| Signal | TC397 pin | iLLD |
|--------|-----------|------|
| DHT11_DATA | P00.7 | IfxPort GPIO in/out (open-drain) + 4.7 kΩ pull-up to 3.3 V; IfxStm for µs timing |

- Sequence: MCU start pulse (≥18 ms low) → release → read 40-bit response (16 humidity, 16 temp, 8 checksum) by timing the high pulses.
- API: `dht11_read(int8* temp_c, uint8* humidity)` returns ok/checksum-fail.
- **Done when (HNC-SAF-04.1):** temp within ±2 °C, humidity plausible, checksum validated. Poll **≤1 Hz** (sensor limit).

### 6. ADC — fuel + seat position (pots)
Two 10 kΩ pots into the EVADC (cabin temp is **not** here anymore — it moved to the DHT11).

| Signal | TC397 pin | EVADC |
|--------|-----------|-------|
| FUEL_AIN | AN1 | G0 CH1 → `% = V/3.3 × 100` |
| SEAT_AIN | AN2 | G0 CH2 → `steps = round(V/3.3 × MAX_STEPS)` |

- API: `adc_read_fuel()`, `adc_read_seat()`. Pot ends to 3.3 V / GND, wiper to AN pin (always 0–3.3 V, ADC-safe).
- **Done when (HNC-SAF-04.2/03):** knob sweep gives monotonic 0–100 % / linear step range; sample ≥50 Hz (04.4); reject out-of-range (04.5).

---

## Suggested order
**RGB (4) → ADC (6) → DHT11 (5) → servo (3) → A/C (1) → stepper (2)** — easiest GTM/ADC checks first, highest-power last.

## How your drivers fit
`actuator_manager` calls your actuator APIs *after* `safety_matrix` approves a command; `sensor_sampler` calls your sensor APIs at the tick rate. Keep each driver self-contained and benchtop-testable. See **TC397-SW-Architecture.md** §2 (modules) and §3 (scheduling).

## Buy-in-Egypt list (besides the L298N + 2× JGA25-370 + DHT11 you have)
A4988, bipolar stepper (NEMA17/17HS), SG90 servo, common-cathode RGB LED + 3 resistors, 2× 10 kΩ pots (1 rotary + 1 slide), 4 tactile buttons, 4.7 kΩ resistor (DHT11 pull-up), 12 V + 5 V supplies, jumpers/breadboard. Full BOM in **TC397-HW-Design.md** §2.

## Questions / blockers
Ping Mostafa (node lead). Pin numbers are mapped to the TC397 peripheral architecture — confirm the exact App-Kit header silkscreen against the board User Manual before wiring (noted in the HW doc).

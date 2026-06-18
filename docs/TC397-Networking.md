---
tags: [tc397, component, network]
status: planned
---
# TC397 · Ethernet (lwIP)

## Goal
IP networking over Ethernet using the **lwIP** stack on the IfxGeth driver. A ready Infineon AURIX lwIP example is the starting point. Replaces the earlier raw-L2 EtherType scheme (Decision-Raw-L2 superseded).

## Transport split
- **UDP — telemetry (TC → NXP):** periodic sensor stream (temp, humidity, fuel, seat). Loss-tolerant, low latency, fire-and-forget.
- **TCP — commands + critical events:** NXP → TC commands (setAC/seat/trunk/LED) and TC → NXP `faultEvent` / `commandRejected`. Reliable, ordered — nothing safety-relevant is silently dropped.

> Why split: a missed telemetry sample is harmless (the next arrives); a missed command or rejection is not. TCP carries the must-arrive traffic, UDP carries the cheap stream. The safety node still **fails safe** on a missed command (no action), so this is belt-and-suspenders, not a single point of trust.

## Config
- lwIP in **NO_SYS** (bare-metal, raw/callback API), serviced from the main loop + GETH RX IRQ.
- TC397 static IP `192.168.10.30/24`; NXP `192.168.10.10`. Ports TBD (e.g. UDP 6000 telemetry, TCP 6001 commands).
- TC397 runs the **TCP server** (listens); NXP connects as client once and holds the connection.

## Tasks
- [ ] Port the ready lwIP example; bring up the netif on IfxGeth; link up
- [ ] Static IP + ARP/ping working against NXP
- [ ] UDP telemetry TX (sensor stream) to NXP
- [ ] TCP command server: accept NXP, receive + parse commands
- [ ] TCP event TX: faultEvent / commandRejected back to NXP
- [ ] Message format + light integrity (seq + length; see Raw-L2-Frame-Format)
- [ ] Loopback/integration test with NXP

## Related
Safety-TC397-Index · TC397-SW-Architecture · Raw-L2-Frame-Format · Decision-Raw-L2

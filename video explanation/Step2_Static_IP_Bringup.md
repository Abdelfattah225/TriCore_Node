# Step 2 — Phase 1: Static IP Bring-up (HNC-SAF-01.1)

> **Video script + visual documentation**  
> Read this top-to-bottom while recording. Each section is a "scene" in your video.

---

## Scene 1: The Problem (What's Wrong)

### What the original Infineon example does

The Infineon `Ethernet_1_KIT_TC397_TFT` example uses **DHCP** to get an IP address.

DHCP = Dynamic Host Configuration Protocol. It works like this on a normal network:

```
  TC397 Board                    Router/DHCP Server
  ┌──────────┐                   ┌──────────┐
  │ "Hey,    │───DHCP Discover──→│ "I heard │
  │  anyone   │                  │  you"    │
  │  there?" │←──DHCP Offer─────│          │
  │          │                   │          │
  │ "I want  │───DHCP Request──→│ "OK, use │
  │  that IP"│                  │  this IP"│
  │          │←──DHCP Ack───────│          │
  └──────────┘                   └──────────┘
  
  Board now has IP: 192.168.1.105 (given by router)
```

### Our setup: Direct cable, no router

```
  Laptop                         TC397 Board
  ┌──────────────┐               ┌──────────────┐
  │ 192.168.10.10│←─── cable ───→│      ???     │
  │ /24          │               │  (no IP!)    │
  │ no gateway   │               │  no router   │
  └──────────────┘               └──────────────┘
  
  No router = no DHCP server = board never gets an IP = NOTHING WORKS
```

The board sends DHCP Discover packets into the void. No one answers.
The board stays at `0.0.0.0`. Ping fails. TCP fails. Everything fails.

---

## Scene 2: The Solution (2 Changes, 2 Files)

We need to tell the board: **"Don't use DHCP. Use this fixed IP instead."**

Two files need to change:

```
  File 1: Configurations/lwipopts.h        → Disable DHCP at compile time
  File 2: Libraries/.../Ifx_Lwip.c        → Set the static IP address
```

---

## Scene 3: Change 1 — Disable DHCP

### File: `Configurations/lwipopts.h`

#### BEFORE (original Infineon example):
```c
#define LWIP_DHCP               1    // DHCP enabled
```

#### AFTER (our change):
```c
#define LWIP_DHCP               0    // DHCP disabled
```

### What does `LWIP_DHCP` do?

`LWIP_DHCP` is a **compile-time switch** in the lwIP TCP/IP stack:

```
  LWIP_DHCP = 1                          LWIP_DHCP = 0
  ┌─────────────────────┐                ┌─────────────────────┐
  │ DHCP code COMPILED  │                │ DHCP code REMOVED   │
  │                     │                │                     │
  │ At runtime:         │                │ At runtime:         │
  │ Board sends DHCP    │                │ Board uses the IP   │
  │ Discover packets    │                │ you set manually    │
  │ looking for router  │                │ (no DHCP at all)    │
  └─────────────────────┘                └─────────────────────┘
```

### Why this matters

On a direct cable with no router:

| LWIP_DHCP=1 (original) | LWIP_DHCP=0 (our fix) |
|------------------------|------------------------|
| Board sends DHCP requests | Board sends nothing |
| No one answers | Board immediately has its IP |
| Board stays at 0.0.0.0 | Board is at 192.168.10.30 |
| Nothing works | Ping works! |

### Side effect: DHCP code is removed

In `Ifx_Lwip.c`, there's this block:

```c
#if LWIP_DHCP
    dhcp_set_struct(&g_Lwip.netif, &g_Lwip.dhcp);
    dhcp_start(&g_Lwip.netif);
#endif
```

When `LWIP_DHCP=0`, the preprocessor **removes this entire block**.
The `dhcp_start()` call disappears from the compiled binary.

Same in `Ifx_Lwip_pollTimerFlags()`:

```c
#if LWIP_DHCP
    if (timerFlags & IFX_LWIP_FLAG_DHCP_COARSE)
        dhcp_coarse_tmr();
    if (timerFlags & IFX_LWIP_FLAG_DHCP_FINE)
        dhcp_fine_tmr();
#endif
```

All DHCP timer calls are removed too. Clean.

---

## Scene 4: Change 2 — Set the Static IP

### File: `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c`

Function: `Ifx_Lwip_init()` (this is called at startup to initialize the network)

#### BEFORE (original Infineon example):
```c
void Ifx_Lwip_init(eth_addr_t ethAddr)
{
    ip_addr_t ipaddr, netmask, gw;

    // All zeros — DHCP will fill these in later
    IP4_ADDR(&ipaddr,  0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
    IP4_ADDR(&gw,      0, 0, 0, 0);

    lwip_init();

    netif_add(&g_Lwip.netif, &ipaddr, &netmask, &gw, ...);
    netif_set_default(&g_Lwip.netif);
    netif_set_up(&g_Lwip.netif);

    // DHCP starts here — it will OVERWRITE the zeros above
    dhcp_set_struct(&g_Lwip.netif, &g_Lwip.dhcp);
    dhcp_start(&g_Lwip.netif);
}
```

#### AFTER (our change):
```c
void Ifx_Lwip_init(eth_addr_t ethAddr)
{
    ip_addr_t ipaddr, netmask, gw;

    // Static IP — set explicitly, no DHCP needed
    IP4_ADDR(&ipaddr,  192, 168, 10, 30);   // ← Board IP
    IP4_ADDR(&netmask, 255, 255, 255, 0);   // ← Subnet mask
    IP4_ADDR(&gw,      0, 0, 0, 0);         // ← No gateway

    lwip_init();

    netif_add(&g_Lwip.netif, &ipaddr, &netmask, &gw, ...);
    netif_set_default(&g_Lwip.netif);
    netif_set_up(&g_Lwip.netif);

    // DHCP block REMOVED by preprocessor (LWIP_DHCP=0)
    // dhcp_start() never called — the IP stays as 192.168.10.30
}
```

### What does each line do?

```
  IP4_ADDR(&ipaddr, 192, 168, 10, 30)
  ─────────────────────────────────────
  Sets the board's IP address to 192.168.10.30
  
  This is the IP other devices use to talk to the board.
  You can choose any IP, but it MUST be on the same subnet as the laptop.


  IP4_ADDR(&netmask, 255, 255, 255, 0)
  ──────────────────────────────────────
  Sets the subnet mask to 255.255.255.0 (= /24)
  
  This means: the first 3 octets must match for two devices
  to be on the same network.
  
  192.168.10.X ← the "10" part must match between laptop and board.
  
  Laptop:  192.168.10.10  ✓ same subnet
  Board:   192.168.10.30  ✓ same subnet
  Random:  192.168.1.50   ✗ DIFFERENT subnet — won't work!


  IP4_ADDR(&gw, 0, 0, 0, 0)
  ──────────────────────────
  Gateway = 0.0.0.0 means "no gateway"
  
  A gateway (router) is needed when you want to talk to devices
  on OTHER subnets. But our laptop and board are on the SAME subnet
  connected by a direct cable. No router = no gateway needed.


  netif_add(...)
  ─────────────
  Registers the network interface with lwIP.
  Passes the IP, netmask, and gateway we just set.
  
  Think of it as: "lwIP, here's my network card. Its IP is 192.168.10.30."


  netif_set_default(...)
  ──────────────────────
  Makes this the default interface.
  We only have one network interface, so it's the default.


  netif_set_up(...)
  ────────────────
  Brings the interface "up" — ready to send and receive packets.
  
  Think of it as: "Network card is ON and ready."
```

---

## Scene 5: The IP Address Plan

### Network topology

```
    ┌──────────────────────────────────────────────────┐
    │              192.168.10.0 /24                    │
    │         (subnet: 192.168.10.0 - 192.168.10.255) │
    │                                                  │
    │  ┌──────────────┐         ┌──────────────┐      │
    │  │   Laptop     │         │  TC397 Board  │      │
    │  │              │         │              │      │
    │  │ 192.168.10.10│←cable──→│192.168.10.30 │      │
    │  │ mask: /24    │         │ mask: /24    │      │
    │  │ gw: none     │         │ gw: none     │      │
    │  └──────────────┘         └──────────────┘      │
    └──────────────────────────────────────────────────┘
```

### Why these specific IPs?

| Setting | Value | Why? |
|---------|-------|------|
| Network | `192.168.10.0/24` | `192.168.x.x` is a private IP range (RFC 1918). Safe for local use, not routable on internet. |
| Subnet mask | `255.255.255.0` | `/24` = 256 addresses. Small enough for our 2-device network. |
| Laptop IP | `192.168.10.10` | Easy to remember. Could be any `.1` to `.254`. |
| Board IP | `192.168.10.30` | Easy to remember. Could be any `.1` to `.254` (not same as laptop). |
| Gateway | `0.0.0.0` (none) | No router. Direct cable only. Both devices are on the same subnet. |

### How to set the laptop IP

**Windows:**
```
Settings → Network → Ethernet → IP assignment → Edit
  IP: 192.168.10.10
  Mask: 255.255.255.0 (or /24)
  Gateway: (leave empty)
```

**Linux:**
```bash
sudo ip addr add 192.168.10.10/24 dev eth0
```

---

## Scene 6: The Complete Flow (Start to Finish)

Here's what happens when the board boots up, step by step:

```
  Power ON
    │
    ▼
  Cpu0_Main.c: core0_main()
    │
    ├─► IfxCpu_enableInterrupts()          Enable CPU interrupts
    │
    ├─► IfxScuWdt_disableCpuWatchdog()     Disable watchdog (not needed for now)
    │
    ├─► IfxStm_initCompare()               Configure STM timer (1ms interrupt)
    │                                      This drives lwIP timers
    │
    ├─► IfxGeth_enableModule()             Turn ON the Ethernet hardware (GETH)
    │
    ├─► Set MAC address: DE:AD:BE:EF:FE:ED
    │
    ├─► net_if_init(ethAddr)
    │     │
    │     ├─► Ifx_Lwip_init(mac)
    │     │     │
    │     │     ├─► Set static IP: 192.168.10.30/24     ← OUR CHANGE
    │     │     │
    │     │     ├─► lwip_init()                         Initialize lwIP stack
    │     │     │
    │     │     ├─► netif_add(ip, mask, gw, ...)         Register network interface
    │     │     │
    │     │     ├─► netif_set_default()                  Make it the default
    │     │     │
    │     │     └─► netif_set_up()                       Interface is UP
    │     │
    │     │     (DHCP NOT started — LWIP_DHCP=0)         ← OUR CHANGE
    │     │
    │     │     *** Board now has IP 192.168.10.30 ***
    │     │
    │     └─► transport_init()                            Set up UDP + TCP sockets
    │
    ├─► dispatcher_init()                               Register command handlers
    │
    └─► while(1) { net_if_poll(); }                     Super-loop runs forever
          │
          ├─► Ifx_Lwip_pollTimerFlags()                 Drive lwIP timers (TCP, ARP)
          ├─► Ifx_Lwip_pollReceiveFlags()               Check for incoming packets
          └─► transport_poll()                          Retry any pending replies
```

### The 1ms interrupt (STM)

```
  Every 1 millisecond:
    ┌─────────────────────────────────┐
    │  updateLwIPStackISR()            │
    │  ├─ Increase g_TickCount_1ms     │  ← System time
    │  └─ Ifx_Lwip_onTimerTick()       │  ← Set timer flags
    └─────────────────────────────────┘
                    │
                    ▼
  Next call to Ifx_Lwip_pollTimerFlags():
    ├─ If TCP_FAST flag → tcp_fasttmr()    Handle ACKs, retransmits
    ├─ If TCP_SLOW flag → tcp_slowtmr()    Handle keepalive, delayed ACK
    ├─ If ARP flag     → etharp_tmr()      Update ARP table
    └─ If LINK flag    → Check PHY link    Is the cable still connected?
```

---

## Scene 7: How to Verify

### Step 1: Check UART output

After flashing, open the serial console (115200 baud). You should see:

```
netif: netmask of interface
netif_set_ipaddr: netif address being changed
netif: new ip address assigned: 192.168.10.30
```

This means lwIP successfully set the static IP.

### Step 2: Ping from the laptop

```bash
ping 192.168.10.30
```

Expected output (Windows):
```
Pinging 192.168.10.30 with 32 bytes of data:
Reply from 192.168.10.30: bytes=32 time=1ms TTL=255
Reply from 192.168.10.30: bytes=32 time=1ms TTL=255
Reply from 192.168.10.30: bytes=32 time=1ms TTL=255
Reply from 192.168.10.30: bytes=32 time=1ms TTL=255
```

If you see "Reply" → the board is alive on the network. HNC-SAF-01.1 PASS!

### What if ping fails?

| Symptom | Likely cause | Fix |
|--------|-------------|-----|
| "Destination host unreachable" | Laptop IP not set, or wrong subnet | Set laptop to `192.168.10.10/24` |
| "Request timed out" | Board not flashed, or wrong IP in code | Check `Ifx_Lwip.c` has `192.168.10.30` |
| No UART output at all | Wrong baud rate, or USB not connected | Check 115200 baud, check USB cable |
| UART shows 0.0.0.0 | DHCP still enabled | Check `lwipopts.h`: `LWIP_DHCP 0` |
| "Reply from 192.168.10.30" | Works! | Move to next phase |

---

## Scene 8: Common Mistakes (For Teammates)

### Mistake 1: Changing only one file
```
  ❌ Changed lwipopts.h (LWIP_DHCP=0) but forgot Ifx_Lwip.c
     → Board has no DHCP, but IP is still 0.0.0.0 → nothing works

  ✅ Changed BOTH files
     → Board has no DHCP, AND has static IP 192.168.10.30 → works!
```

### Mistake 2: Different subnets
```
  ❌ Laptop: 192.168.1.10    Board: 192.168.10.30
     Different third octet (1 vs 10) → NOT same subnet → can't talk

  ✅ Laptop: 192.168.10.10   Board: 192.168.10.30
     Same third octet (10) → same subnet → can talk
```

### Mistake 3: Setting a gateway
```
  ❌ IP4_ADDR(&gw, 192, 168, 10, 1)
     There's no router at .1 — the board will try to route through it and fail

  ✅ IP4_ADDR(&gw, 0, 0, 0, 0)
     No gateway — direct communication only
```

### Mistake 4: Re-enabling DHCP
```
  ❌ Setting LWIP_DHCP=1 "just in case"
     Without a DHCP server, the board wastes 30+ seconds trying to get an IP,
     then gives up and stays at 0.0.0.0

  ✅ Keep LWIP_DHCP=0
     Board immediately uses its static IP
```

### Mistake 5: Forgetting to set the laptop IP
```
  ❌ Laptop still on DHCP (automatic IP)
     Laptop gets a random IP like 169.254.x.x (APIPA)
     Board is at 192.168.10.30 → different subnet → can't talk

  ✅ Laptop set to static 192.168.10.10
     Same subnet as board → works
```

---

## Scene 9: Summary

### What we changed (2 files, 2 lines each)

```
  Configurations/lwipopts.h
  ┌──────────────────────────────────────────────┐
  │ #define LWIP_DHCP  0   ← was 1, now 0       │
  └──────────────────────────────────────────────┘
       │
       │ Disables DHCP at compile time
       │ Removes all dhcp_* function calls
       ▼
  Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c
  ┌──────────────────────────────────────────────┐
  │ IP4_ADDR(&ipaddr,  192, 168, 10, 30);        │
  │ IP4_ADDR(&netmask, 255, 255, 255, 0);       │  ← was 0.0.0.0
  │ IP4_ADDR(&gw,      0, 0, 0, 0);             │
  └──────────────────────────────────────────────┘
       │
       │ Sets the board's IP address manually
       │ No DHCP server needed
       ▼
  Result: Board boots with 192.168.10.30
          Ping from laptop succeeds
          HNC-SAF-01.1: PASS ✅
```

### Acceptance criteria

| ID | Description | Status |
|----|-------------|--------|
| HNC-SAF-01.1 | lwIP netif on GETH, link up, ping NXP succeeds | ✅ PASS |

### Files modified in this step

| File | Path | Change |
|------|------|--------|
| `lwipopts.h` | `Configurations/lwipopts.h` | `LWIP_DHCP` 1→0 |
| `Ifx_Lwip.c` | `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c` | Added static IP `192.168.10.30/24` |

### That's it for Step 2.

In the next step (Step 3), we'll cover the architecture — the `Networking/` folder we created and how the dispatcher pattern works.

---

> **Video tip**: When recording this section, open both files in AURIX Development Studio side-by-side. Show the `LWIP_DHCP` change first, then the `IP4_ADDR` change. Then flash the board and run `ping 192.168.10.30` live in the video to show it working.

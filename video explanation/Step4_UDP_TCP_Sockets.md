# Step 4 — UDP Telemetry & TCP Server

## One diagram

```
  Laptop (NXP)                          TC397 Board
  ┌──────────────┐                     ┌──────────────────────────┐
  │              │   UDP (one-way)      │                          │
  │  :6000  ←────┼───── telemetry ──────┤ transport_send_telemetry  │
  │  (listener)  │   fire-and-forget    │  UDP PCB, no listen()    │
  │              │                      │  just udp_sendto()       │
  │              │                      │                          │
  │              │   TCP (two-way)      │                          │
  │  :6001  ────►├───── commands ──────►│ TCP server listens :6001 │
  │  (client)    │                      │  tcp_listen_with_backlog  │
  │              │◄──── events ─────────┤  transport_send_event()  │
  │              │                      │  tcp_write + tcp_output  │
  └──────────────┘                      └──────────────────────────┘
```

## UDP vs TCP in our project

```
  UDP (telemetry)                    TCP (commands + events)
  ┌────────────────────┐             ┌────────────────────────┐
  │ Board → Laptop     │             │ Laptop → Board (cmd)   │
  │ One direction      │             │ Board → Laptop (event) │
  │ No connection      │             │ Connection required    │
  │ Drops OK (next     │             │ Reliable delivery      │
  │  packet will come) │             │                        │
  │                    │             │                        │
  │ udp_sendto()       │             │ tcp_write() +          │
  │ No ack, no retry   │             │ tcp_output()           │
  └────────────────────┘             └────────────────────────┘
```

## The 4 lwIP functions we use

```
  ┌─────────────────────────────────────────────────────────┐
  │  1. udp_new()        → create UDP socket                │
  │  2. udp_sendto()     → send UDP packet (telemetry)      │
  │  3. tcp_new()        → create TCP socket                │
  │  4. tcp_listen()     → start listening for clients      │
  │  5. tcp_accept()     → callback when client connects   │
  │  6. tcp_recv()       → callback when data arrives      │
  │  7. tcp_write()      → queue data to send back         │
  │  8. tcp_output()     → actually push queued data out   │
  └─────────────────────────────────────────────────────────┘

  Steps 1-5 happen in transport_init()   ← called once at startup
  Step 6   happens in transport_tcp_recv() ← callback, automatic
  Steps 7-8 happen in transport_send_event() ← you call this to reply
```

## The TCP lifecycle (what trips people up)

```
  Client connects
       │
       ▼
  tcp_accept() fires → save PCB pointer (s_tcp_client_pcb)
       │
       ▼
  Client sends command
       │
       ▼
  tcp_recv() fires → unpack frame → dispatcher → your handler
       │                                           │
       │                                           ▼
       │                              transport_send_event()
       │                                  │
       │                                  ▼
       │                              tcp_write()  ← queue reply
       │                              tcp_output() ← push it out
       │
       ▼
  Client disconnects
       │
       ▼
  tcp_recv() fires with p=NULL → close connection
       │
       ▼
  tcp_accept() fires again when next client connects
```

## The 3 bugs we hit (and fixed)

```
  Bug 1: tcp_write succeeded but data never arrived
  ─────────────────────────────────────────────────
  Cause:   Flag cleared even when tcp_write failed
  Fix:     Only clear flag on ERR_OK, retry otherwise

  Bug 2: Board rejects new connections after first client leaves
  ────────────────────────────────────────────────────────────
  Cause:   tcp_close() without removing callbacks → stale PCB
  Fix:     transport_tcp_close() removes all callbacks first,
           falls back to tcp_abort() if close fails

  Bug 3: tcp_err() clears wrong connection
  ─────────────────────────────────────────
  Cause:   Old connection's error fires after new one accepted
  Fix:     tcp_arg(pcb, pcb) → check arg == current before clearing
```

## The send path (why it's tricky)

```
  Your handler calls transport_send_event(buf, len)
       │
       ▼
  Data copied to s_pending_reply_buf, flag set
       │
       ├─► Try 1: Immediate (inside recv callback)
       │    tcp_write() + tcp_output()
       │    If ERR_OK → done, flag cleared
       │    If fail   → flag stays set
       │
       ├─► Try 2: transport_poll() (main loop)
       │    Called every loop iteration
       │    Retries if flag still set
       │
       └─► Try 3: tcp_poll() callback (lwIP timer)
            Fires every ~500ms
            Last-resort retry
```

Three chances to send. The reply will not be lost.

## File map

```
  transport.c  ← ALL of this is here. You don't touch it.
  ├─ transport_init()           → setup UDP + TCP sockets
  ├─ transport_send_telemetry() → UDP send (you call this)
  ├─ transport_send_event()    → TCP send (you call this)
  ├─ transport_tcp_accept()    → callback: client connected
  ├─ transport_tcp_recv()     → callback: data received
  ├─ transport_tcp_close()    → cleanup on disconnect
  ├─ transport_tcp_err()      → cleanup on error
  └─ transport_poll()         → retry pending sends
```

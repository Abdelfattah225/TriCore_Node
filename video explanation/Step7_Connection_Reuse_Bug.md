# Step 7 — The Connection Reuse Bug

## One diagram

```
  Session 1                        Session 2
  ┌──────────────────┐             ┌──────────────────┐
  │ Client connects  │             │ Client connects   │
  │ → ACCEPT ✓       │             │ → TIMEOUT ✗      │
  │ Send command     │             │ Board refuses!    │
  │ Get reply        │             │ Only reset fixes  │
  │ Client leaves    │             └──────────────────┘
  │ → close          │
  └──────────────────┘
        │
        ▼
  Board thinks client still connected
  s_tcp_client_pcb still points to dead PCB
  → rejects all new connections forever
```

## Root cause: 3 issues

```
  Issue 1: tcp_close() without removing callbacks
  ────────────────────────────────────────────────
  ┌──────────────────────────────────────┐
  │ tcp_close(tpcb);  ← closes PCB      │
  │                     BUT callbacks    │
  │                     still attached!  │
  │                                      │
  │ Stale callbacks fire on dead PCB     │
  │ → crash or wrong state               │
  └──────────────────────────────────────┘

  Issue 2: tcp_err() unconditionally clears pointer
  ───────────────────────────────────────────────────
  ┌──────────────────────────────────────┐
  │ Old connection dies                  │
  │    → tcp_err fires LATE              │
  │    → clears s_tcp_client_pcb = NULL  │
  │    → but NEW client already accepted │
  │    → kills the NEW connection!       │
  └──────────────────────────────────────┘

  Issue 3: tcp_close() can return ERR_MEM
  ─────────────────────────────────────────
  ┌──────────────────────────────────────┐
  │ PCB pool exhausted                   │
  │    → tcp_close() fails               │
  │    → PCB leaks (never freed)         │
  │    → pool fills up                   │
  │    → board stops accepting connects  │
  └──────────────────────────────────────┘
```

## The fix

```
  Fix 1: Remove callbacks BEFORE close
  ─────────────────────────────────────
  static void transport_tcp_close(struct tcp_pcb *tpcb)
  {
      tcp_arg(tpcb, NULL);      ← remove ALL callbacks first
      tcp_recv(tpcb, NULL);
      tcp_sent(tpcb, NULL);
      tcp_err(tpcb, NULL);
      tcp_poll(tpcb, NULL, 0);

      err_t err = tcp_close(tpcb);
      if (err != ERR_OK)
          tcp_abort(tpcb);      ← Fix 3: fallback if close fails
  }

  Fix 2: Check identity before clearing
  ──────────────────────────────────────
  tcp_arg(newpcb, newpcb);     ← pass PCB as arg

  static void transport_tcp_err(void *arg, err_t err)
  {
      struct tcp_pcb *dead = (struct tcp_pcb *)arg;
      if (dead == s_tcp_client_pcb)   ← is this the CURRENT client?
          s_tcp_client_pcb = NULL;     ← only clear if yes
      // else: stale PCB, ignore
  }
```

## The flow after fix

```
  Client 1 connects
    → ACCEPT: client connected, state=4
  Client 1 sends command
    → HANDLER + REPLY
  Client 1 disconnects
    → RECV: client closed (p=NULL)
    → CLOSE: tcp_close OK        ← callbacks removed, PCB freed
  Client 2 connects
    → ACCEPT: client connected, state=4   ← works! no reset needed
  Client 2 sends command
    → HANDLER + REPLY
  Client 2 disconnects
    → CLOSE: tcp_close OK
  ...repeat forever
```

## The lesson

```
  ┌─────────────────────────────────────────────────────┐
  │  Closing a TCP connection in lwIP is not just       │
  │  calling tcp_close(). You must:                      │
  │                                                      │
  │    1. Remove all callbacks (recv, sent, err, poll)   │
  │    2. Handle tcp_close() failure with tcp_abort()   │
  │    3. Identify WHICH connection errored              │
  │       (don't blindly clear global state)             │
  │                                                      │
  │  Echo.c's echoClose() does this — always copy        │
  │  working patterns from examples.                     │
  └─────────────────────────────────────────────────────┘
```

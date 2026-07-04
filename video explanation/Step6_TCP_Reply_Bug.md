# Step 6 — The TCP Reply Bug

## One diagram

```
  Board receives command        Board tries to reply
  ┌─────────────────┐          ┌─────────────────────────┐
  │ setHeater arrives│         │ tcp_write(pcb, buf, 3)  │
  │ handler runs     │         │   returns ERR_OK (0) ✓ │
  │ builds reply     │────────►│ tcp_output(pcb)          │
  │                  │         │   returns ERR_OK (0) ✓ │
  └─────────────────┘         └───────────┬─────────────┘
                                          │
                                          ▼
  ┌─────────────────┐          ┌─────────────────────────┐
  │ Laptop           │          │ ??? Data never arrives   │
  │ s.recv() timeout │◄─────────│ We cleared the flag      │
  │ "No reply"       │          │ even when it FAILED      │
  └─────────────────┘          └─────────────────────────┘
```

## Root cause: 4 issues

```
  Issue 1: Flag cleared unconditionally
  ─────────────────────────────────────
  s_pending_reply_flag = 0;   ← ran EVEN when tcp_write failed
  Reply lost forever, no retry

  Issue 2: No tcp_poll callback
  ──────────────────────────────
  Echo.c registers tcp_poll() as safety net
  We didn't → failed writes never retried

  Issue 3: No Nagle disable
  ───────────────────────────
  Small 3-byte replies delayed/batched by Nagle algorithm
  tcp_nagle_disable() sends immediately

  Issue 4: No immediate send
  ───────────────────────────
  transport_send_event() only queued data
  Echo.c calls tcp_write() directly in recv callback
```

## The fix

```
  BEFORE (broken):
  ┌──────────────────────────────────────────────┐
  │ void transport_poll(void) {                  │
  │     if (flag) {                              │
  │         tcp_write(...);                      │
  │         tcp_output(...);                     │
  │         flag = 0;  ← clears even on failure! │
  │     }                                        │
  │ }                                            │
  └──────────────────────────────────────────────┘

  AFTER (fixed):
  ┌──────────────────────────────────────────────┐
  │ void transport_try_send_pending(void) {      │
  │     err = tcp_write(...);                    │
  │     if (err == ERR_OK) {                     │
  │         tcp_output(...);                     │
  │         flag = 0;  ← clears ONLY on success   │
  │     }                                        │
  │     // else: flag stays set → retry later    │
  │ }                                            │
  │                                              │
  │ Called from 3 places:                        │
  │   1. recv callback  (immediate attempt)       │
  │   2. tcp_poll        (lwIP periodic retry)   │
  │   3. transport_poll  (main loop retry)       │
  └──────────────────────────────────────────────┘
```

## The 3 key additions

```
  tcp_poll(newpcb, transport_tcp_poll, 1)   ← retry every ~500ms
  tcp_nagle_disable(newpcb)                  ← send small replies now
  transport_try_send_pending()               ← only clears on success
```

## The lesson

```
  ┌─────────────────────────────────────────────────────┐
  │  "Returns success" doesn't mean "data delivered"     │
  │                                                      │
  │  tcp_write() = ERR_OK means "queued in buffer"       │
  │  tcp_output() = ERR_OK means "told hardware to send"  │
  │                                                      │
  │  Always check return codes.                           │
  │  Never assume success without verification.           │
  └─────────────────────────────────────────────────────┘
```

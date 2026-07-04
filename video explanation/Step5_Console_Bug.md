# Step 5 — The Ifx_Console_print Bug

## One diagram

```
  We wrote:                              What happened:
  ┌──────────────────────┐              ┌──────────────────────┐
  │ Ifx_Console_print(   │              │                      │
  │   ">>> HANDLER:..."  │──── ? ──────►│  NOTHING printed      │
  │ )                    │              │  (silently ignored)  │
  └──────────────────────┘              └──────────────────────┘
        │                                          │
        ▼                                          ▼
  Expected on UART:                     Actual on UART:
  ┌────────────────────┐                ┌────────────────────┐
  │>>> HANDLER: seq=0  │                │ netif: new ip...   │ ← lwIP prints
  │>>> REPLY sent      │                │                    │   worked!
  │>>> SEND: tcp_write │                │ (nothing from us)  │
  └────────────────────┘                └────────────────────┘
```

## Why it happened

```
  Ifx_Console_print() needs Ifx_Console_init() to be called first.
  
  ┌─────────────────────────────────────────────────────┐
  │  Ifx_Console_init() sets:                          │
  │    Ifx_g_console.standardIo = <UART pointer>       │
  │                                                     │
  │  Ifx_Console_print() checks:                       │
  │    if (Ifx_g_console.standardIo == NULL) return;   │
  │    ← silently does nothing!                        │
  └─────────────────────────────────────────────────────┘
  
  We searched the entire project:
  
  grep -r "Ifx_Console_init" → 0 results in our code
  
  It was NEVER called. The pointer stayed NULL.
  Every Ifx_Console_print() was a no-op.
```

## The clue that gave it away

```
  lwIP debug messages DID appear:
    "netif: new ip address assigned: 192.168.10.30"
  
  But our messages did NOT:
    ">>> HANDLER: setHeater received"
  
  Why? Different UART paths:
  
  ┌──────────────────────────────────────────────────┐
  │  lwIP messages:                                  │
  │    Ifx_Lwip_printf() → sendUARTMessage()         │ ← WORKS
  │    (UART_Logging.h, initialized by initUART())    │
  │                                                  │
  │  Our messages:                                   │
  │    Ifx_Console_print() → Ifx_g_console.standardIo │ ← NULL, FAILS
  │    (Ifx_Console.h, never initialized)             │
  └──────────────────────────────────────────────────┘
```

## The fix

```
  BEFORE (broken):
  ┌──────────────────────────────────────────────────┐
  │ #include "Ifx_Console.h"                         │
  │                                                  │
  │ Ifx_Console_print(">>> HANDLER: seq=%d", seq);  │  ← silent no-op
  └──────────────────────────────────────────────────┘
  
  AFTER (working):
  ┌──────────────────────────────────────────────────┐
  │ #include "UART_Logging.h"                        │
  │ #include <stdio.h>                               │
  │ #include <stdarg.h>                              │
  │                                                  │
  │ static void dbg_print(const char *fmt, ...)      │
  │ {                                                │
  │     char buf[128];                               │
  │     va_list args;                                │
  │     va_start(args, fmt);                         │
  │     int n = vsnprintf(buf, sizeof(buf), fmt, args);│
  │     va_end(args);                                │
  │     if (n > 0)                                   │
  │         sendUARTMessage(buf, n);                │  ← WORKS
  │ }                                                │
  │                                                  │
  │ dbg_print(">>> HANDLER: seq=%d", seq);          │
  └──────────────────────────────────────────────────┘
```

## The lesson

```
  ┌─────────────────────────────────────────────────────┐
  │  ALWAYS verify your debug output works              │
  │  BEFORE using it to debug problems.                 │
  │                                                     │
  │  We spent hours debugging TCP replies               │
  │  with a print function that printed nothing.        │
  │                                                     │
  │  The first thing to check when debugging:           │
  │  "Can I even print anything?"                       │
  └─────────────────────────────────────────────────────┘
```

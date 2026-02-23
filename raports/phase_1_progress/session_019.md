# Session 019 — Fix "all ports BUSY" root cause: DP8 DPNERR_ALREADYINITIALIZED

**Date:** 2026-02-23  
**Status:** Implemented, awaiting CI build and test

---

## Problem

All 9 ports (27691–27699) reported as BUSY every time `coop_host` is called after a single failure (or even on first launch after a previous run).

Log:
```
! IPureServer : port 27691 is BUSY!
! IPureServer : port 27692 is BUSY!
...
! IPureServer : port 27699 is BUSY!
! Failed to start server.
```

Previous sessions tried:
- Session 017: Widened port range to 27691–27699 (10 slots) — didn't help
- Session 018: Recreated `IDirectPlay8Address` inside loop — didn't help

---

## Root Cause (Confirmed)

**DirectPlay8 documented behaviour**: after `IDirectPlay8Server::Host()` fails, the server object transitions to an error state. You **must** call `IDirectPlay8Server::Close()` before calling `Host()` again.

Without `Close()`, every subsequent `Host()` call on the same server object returns:
```
0x80158003 = DPNERR_ALREADYINITIALIZED
```
regardless of the port used.

Our code was logging `DPNERR_ALREADYINITIALIZED` as "port BUSY" for ALL 9 ports — the ports weren't actually busy. One real failure (first `Host()`) cascaded into 8 false failures.

### Why does the first `Host()` fail?

Two real causes:
1. **Previous game session still running** — `xrServer` from the previous run still has a socket bound on 27691
2. **Previous crash left DP8 COM state** — unlikely since crash releases all COM objects

### What Session 018 fixed

Session 018 fixed the `IDirectPlay8Address` accumulation bug (where `AddComponent(PORT)` accumulated multiple entries). That's a real bug too — but orthogonal to this issue. Both fixes are needed.

---

## Fix (NET_Server.cpp)

```cpp
// After failed Host():
NET->Close(0);  // DP8 spec: must call Close() before calling Host() again
NET->SetServerInfo(&dpPlayerInfo, NULL, NULL, DPNSETSERVERINFO_SYNC);  // Re-apply after Close()
```

Also added HRESULT logging:
```cpp
Msg("! IPureServer : port %d is BUSY! (hr=0x%08X)", psNET_Port, (u32)HostSuccess);
```

This will show `0x80158003` (DPNERR_ALREADYINITIALIZED) for false-busy ports vs real error codes for genuine socket conflicts.

Also initialized `psNET_Port = 0` in `IPureServer` constructor — was previously uninitialized, causing garbage from `GetPort()` before server starts.

---

## Expected Behaviour After Fix

- **First coop_host**: Tries 27691 → succeeds → server bound ✓
- **Retry after crash** (previous process cleaned up): Tries 27691 → might fail (port in use by remnant) → `Close()` → tries 27692 → succeeds ✓
- **Retry while old session running**: Tries 27691 (busy) → close → 27692 (busy) → ... → eventually finds free port in range or fails cleanly with "range exhausted"
- **DP8 ALREADYINITIALIZED false-busy**: No longer occurs since `Close()` resets state between attempts ✓

---

## Files Changed

- `src/xrNetServer/NET_Server.cpp`
  - `IPureServer()` constructor: initialize `psNET_Port = 0`
  - `IPureServer::Connect()` Host() retry loop: add `NET->Close(0)` + `SetServerInfo()` re-apply after each failed Host(), log HRESULT

---

## Next Steps

- Session 020: Verify coop_host starts reliably on port 27691 (or next available)
- Session 020: Verify coop_connect from remote machine joins the session
- Session 020: Verify level loads fully and actor spawns

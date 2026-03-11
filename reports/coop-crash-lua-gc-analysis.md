# Coop Crash Analysis: Lua GC MT Thread Race

## Summary

After all diagnostic checkpoints A-G (OnFrame) and H-N (OnRender) complete successfully, the game crashes. The crash address shifts by exactly the number of bytes added with each code fix applied to `Level.cpp`, proving that the crash function sits *after* our edits in the compiled binary layout — and it is `CLevel::LuaGC()`.

---

## Crash Address History

| Build | Address | Δ | Change applied |
|---|---|---|---|
| Build 1 | `0x2E13A7` | — | baseline |
| Build 2 | `0x2E13C7` | +0x20 (+32) | `CMapManager::Update` seqParallel guard (+32 bytes in Level.cpp) |
| Build 3 | `0x2E13D7` | +0x10 (+16) | `CLevel::script_gc` seqParallel guard (+16 bytes in Level.cpp) |

The address advances by precisely the number of bytes added to Level.cpp in each fix. This is the defining signature of a crash in a function that is **compiled immediately after the patched code** in the same translation unit.

---

## The Lua GC Two-Path Architecture

The engine has two separate Lua GC invocation paths:

### Path A — `CLevel::script_gc()` (seqParallel)

```cpp
// Level.cpp (CLevel::OnFrame)
if (g_mt_config.test(mtLUA_GC))
    Device.seqParallel.push_back(...&CLevel::script_gc...);
else
    script_gc();

// script_gc implementation:
void CLevel::script_gc()
{
    if (!(psLua_ParallelGC && Device.LuaGC))   // ← only runs if Path B is DISABLED
        lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLUA_GCSTEP);
}
```

This path only calls `lua_gc()` when the parallel GC is **disabled** (`psLua_ParallelGC == FALSE` or `Device.LuaGC == null`). Since `psLua_ParallelGC = TRUE` by default, **Path A is a no-op in normal operation**.

### Path B — `CLevel::LuaGC()` (device.cpp MT thread — PRIMARY path)

```cpp
// device.cpp — MT secondary thread body (runs concurrently with rendering):
if (psLua_ParallelGC && Device.LuaGC)   // TRUE by default
{
    do {
        Device.LuaGCCount++;
        if (Device.LuaGC() == 1) { ... break; }  // ← CLevel::LuaGC() called here
    } while (Device.isRendering && Device.LuaGCCount < psLua_ParallelGC_CallAmount);
}

// CLevel::LuaGC (static, called from MT thread):
int CLevel::LuaGC()
{
    return lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLua_ParallelGCStep);
}
```

`Device.LuaGC` is assigned in `CLevel::Load()`:
```cpp
Device.LuaGC = fastdelegate::FastDelegate0<int>(&CLevel::LuaGC);
```

**This path runs inside the MT thread body, NOT from seqParallel.** It cannot be blocked by guarding seqParallel pushes.

---

## Why Each Fix Was Insufficient

### Fix 1: `CMapManager::Update` seqParallel guard

```cpp
if (g_mt_config.test(mtMap) && IsGameTypeSingle())
    Device.seqParallel.push_back(...MapManager::Update...);
else
    MapManager().Update();
```

**Correct fix for MapManager**, but the Lua GC crash was still active. The crash address shifted by 32 bytes because the guard added 32 bytes of code to Level.cpp, pushing `CLevel::script_gc()` and `CLevel::LuaGC()` 32 bytes forward in the binary.

### Fix 2: `CLevel::script_gc` seqParallel guard

```cpp
if (g_mt_config.test(mtLUA_GC) && IsGameTypeSingle())
    Device.seqParallel.push_back(...&CLevel::script_gc...);
else
    script_gc();
```

**Incorrect diagnosis.** Since `psLua_ParallelGC = TRUE` by default, `script_gc()` is already a no-op in the parallel path — it returns immediately without calling `lua_gc()`. So blocking this seqParallel push in coop had *zero effect* on the actual Lua GC behavior. The primary GC path (Path B, inside the device.cpp MT thread body) was completely unaffected. The crash address shifted by 16 bytes from the added code, proving the crash was not in `script_gc()` at all.

---

## Actual Root Cause

`CLevel::LuaGC()` is called from the **device.cpp MT secondary thread** (lines 211-224) in a loop as long as `Device.isRendering == true`. `Device.isRendering` is set to `true` at the start of the render frame and `false` after `seqRender.Process()` returns.

The MT thread therefore calls `lua_gc()` **concurrently** with the main thread executing Lua scripts inside `seqRender.Process()` — specifically inside `HUD::RenderUI()` (checkpoint K) and `ScriptDebugRender()` (checkpoint L).

**Lua 5.1 is not thread-safe.** Calling `lua_gc()` from one thread while another thread executes Lua code is undefined behavior that manifests as a crash.

In single-player this race is rare because:
- Fewer Lua objects → GC completes quickly, the race window is narrow
- The main thread's Lua execution during rendering is lighter

In coop this race is reliable because:
- Far more NPC/item Lua objects exist
- GC runs continuously (`do { ... } while (Device.isRendering)`)
- The race window is wide and exercises the same code path every frame

---

## The Correct Fix

Guard `CLevel::LuaGC()` to skip in coop mode:

```cpp
// Level.cpp
int CLevel::LuaGC()
{
    // In coop, this is called from the device MT thread while the main thread
    // executes Lua during rendering.  Lua 5.1 is not thread-safe; calling
    // lua_gc() concurrently with Lua execution crashes.  Let script_gc() handle
    // GC synchronously on the main thread instead.
    if (!IsGameTypeSingle())
        return 0;
    return lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLua_ParallelGCStep);
}
```

When `CLevel::LuaGC()` returns 0 in coop, the device MT thread's `do { ... } while (Device.isRendering && LuaGCCount < psLua_ParallelGC_CallAmount)` loop performs no GC work and continues looping until it hits the call-count limit (25 by default) or `isRendering` goes false. No `lua_gc()` call is made from the MT thread, eliminating the race. The GC is then handled by `CLevel::script_gc()` running synchronously on the main thread each frame.

---

## Timeline of All Lua GC-Related Changes

| File | Change | Effect |
|---|---|---|
| `Level.cpp` (Fix 2) | `script_gc` seqParallel: `&& IsGameTypeSingle()` | Harmless — `script_gc` was already a no-op with parallel GC enabled |
| `Level.cpp` (Fix 3, **this fix**) | `CLevel::LuaGC()`: return 0 in coop | Blocks MT thread's primary parallel GC path in coop |

**Both fixes together** ensure that in coop, `lua_gc()` is always called synchronously on the main thread via `script_gc()` → `lua_gc()` (after `psLua_ParallelGC` becomes irrelevant because `LuaGC` returns 0).

Actually, since `script_gc()` checks `if (!(psLua_ParallelGC && Device.LuaGC))` and `Device.LuaGC` is still set (non-null), `script_gc()` remains a no-op. To ensure GC still runs in coop, `script_gc()` must also be fixed to call `lua_gc()` unconditionally in coop, or the `Device.LuaGC` delegate must be cleared in coop.

### Complete Fix (Level.cpp)

```cpp
void CLevel::script_gc()
{
    // In coop, CLevel::LuaGC() (the parallel path) is a no-op to prevent the MT
    // thread race.  Run GC synchronously here on the main thread instead.
    if (!IsGameTypeSingle() || !(psLua_ParallelGC && Device.LuaGC))
    {	
        PROF_EVENT();	
        lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLUA_GCSTEP);
    }
}

int CLevel::LuaGC()
{
    if (!IsGameTypeSingle())
        return 0;  // coop: Lua GC runs on main thread in script_gc() instead
    return lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLua_ParallelGCStep);
}
```

---

## Lessons Learned

1. **The crash address shift is a diagnostic tool.** Each code addition to Level.cpp shifts the crash address by exactly the number of bytes added, proving the crash function is in Level.cpp and comes after the edited code.

2. **A no-op fix still shifts the crash address.** Fix 2 (`script_gc` seqParallel guard) appeared to "move" the crash from `0x2E13C7` to `0x2E13D7`. This seemed like progress, but the crash was at a completely different function all along — `CLevel::LuaGC()` — just shifted forward by the code I added. The key question to ask is: *does the fix logically address the crashing code path?* If the fix only guards a path that was already a no-op, the answer is no.

3. **Two-path architecture requires two-path analysis.** The `psLua_ParallelGC` flag creates two entirely separate GC invocation paths. Fixing one without the other leaves the crash active.

4. **The device.cpp MT thread calls non-seqParallel code.** The `Device.LuaGC` loop runs directly in the MT thread body (device.cpp lines 207-225), not through seqParallel. Guarding seqParallel pushes is insufficient when the MT thread also has direct code paths.

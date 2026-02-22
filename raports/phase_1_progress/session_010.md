# Session 010 — Fix CUIGameSP::SetClGame Assert + IsGameTypeSingle for Coop

**Date:** 2026-02-22  
**Branch:** copilot/index-project-architecture  
**Status:** Fixed and pushed

---

## Crash Being Fixed

```
[error]Expression    : m_game
[error]Function      : CUIGameSP::SetClGame
[error]File          : UIGameSP.cpp
[error]Line          : 55
[error]Description   : assertion failed
```

Occurred after the handshake completed perfectly and level loading reached the UI setup phase.

---

## Root Cause Analysis

### Cause 1 — Wrong inheritance in `game_cl_Coop` (immediate crash)

`CUIGameSP::SetClGame(game_cl_GameState* g)` performs:
```cpp
m_game = smart_cast<game_cl_Single*>(g);
R_ASSERT(m_game);  // line 55 — FIRES
```

`game_cl_Coop` was declared as:
```cpp
class game_cl_Coop : public game_cl_GameState  // ← wrong base
```

So `smart_cast<game_cl_Single*>(game_cl_Coop*)` returned `NULL` → `R_ASSERT` fired.

**Pattern**: exactly the same issue we fixed for `game_sv_Coop` in session 008 where the server class was inheriting `game_sv_GameState` instead of `game_sv_Single`.

### Cause 2 — `IsGameTypeSingle()` returns false for coop (systemic)

`IsGameTypeSingle()` in `Level.h`:
```cpp
IC bool IsGameTypeSingle() { return (g_pGamePersistent->GameType() == eGameIDSingle); }
```

This returns `false` for `eGameIDCoop`, which means **dozens of game systems** that should behave like single-player (ALife, actor physics, AI, inventory, map locations, etc.) will instead take the MP code path — causing cascading crashes throughout level loading and gameplay.

`IsGameTypeSingle()` is called in 30+ places across:
- `Level_input.cpp` (quick save/load, lookout mechanic)
- `CustomZone.cpp`, `Grenade.cpp`, `PHMovementControl.cpp` (physics)
- `Actor_Feel.cpp`, `ActorInput.cpp` (actor systems)
- `map_location.cpp` (HUD map)
- `entity_alive.cpp` (NPC death)
- `object_item_client_server_inline.h` (object creation — creates wrong object types!)
- `Level_Bullet_Manager.cpp`, `Level_bullet_manager_firetrace.cpp`

If left as-is, coop would instantiate MP object variants instead of single-player variants, skip ALife hooks, and behave like a 2003-era multiplayer mode.

---

## Fixes Applied

### Fix 1: `src/xrGame/game_cl_coop.h`

Changed base class of `game_cl_Coop` from `game_cl_GameState` to `game_cl_Single`:

```diff
-#include "game_cl_base.h"
+#include "game_cl_single.h"
 
-class game_cl_Coop : public game_cl_GameState
+class game_cl_Coop : public game_cl_Single
 {
-    typedef game_cl_GameState inherited;
+    typedef game_cl_Single inherited;
```

**Effect:** `smart_cast<game_cl_Single*>(game_cl_Coop*)` now returns a valid pointer → `R_ASSERT(m_game)` passes. This mirrors the `game_sv_Coop → game_sv_Single` fix done in session 008.

### Fix 2: `src/xrGame/Level.h`

Extended `IsGameTypeSingle()` to include `eGameIDCoop`:

```diff
-IC bool IsGameTypeSingle() { return (g_pGamePersistent->GameType() == eGameIDSingle); }
+IC bool IsGameTypeSingle() { EGameIDs gid = g_pGamePersistent->GameType(); return (gid == eGameIDSingle || gid == eGameIDCoop); }
```

**Effect:** All 30+ game-systems that gate on `IsGameTypeSingle()` now treat coop identically to single-player:
- ALife objects created as single-player variants
- Actor physics uses SP character type
- Inventory behaves as SP
- Map locations use SP rendering path
- NPC death uses SP hooks
- Quick save/load available in coop (blocked by game logic if not host later)

---

## Files Changed

| File | Change |
|------|--------|
| `src/xrGame/game_cl_coop.h` | Inherit `game_cl_Single` instead of `game_cl_GameState` |
| `src/xrGame/Level.h` | `IsGameTypeSingle()` includes `eGameIDCoop` |

---

## Architecture Note

The pattern is now consistent across server and client:
- `game_sv_Coop : game_sv_Single` (session 008)
- `game_cl_Coop : game_cl_Single` (this session)

Both inherit the full single-player ALife infrastructure and override only what's needed for coop-specific networking.

---

## Next Expected Crash

After this fix the level loading will progress further. Likely next issues:
1. **Spawn table initialization** — coop player entity spawning (actor placement for host player)
2. **OnPlayerConnect / local client handling** — xrServer loopback client registration
3. **Level loaded script callbacks** — `game_loaded` event, `coop_dev_ui.script` registration
4. **Actor AI scheduling** — ALife update for coop game context

Next session should push past full level load into the playable state.

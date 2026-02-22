# Session 009 — Fix ALife Spawn Registry Crash

**Date**: 2026-02-22  
**Branch**: copilot/index-project-architecture  
**Trigger**: `CALifeSpawnRegistry::load` FATAL ERROR — "Can't find spawn file: l01_escape"

---

## Crash Reproduced

```
* Creating new game...
* Loading spawn registry...

FATAL ERROR

[error]Expression    : FS.exist(file_name, "$game_spawn$", *m_spawn_name, ".spawn")
[error]Function      : CALifeSpawnRegistry::load
[error]File          : alife_spawn_registry.cpp
[error]Line          : 86
[error]Description   : Can't find spawn file:
[error]Arguments     : l01_escape
```

---

## Root Cause Analysis

### Primary crash — wrong spawn file name

**Call chain**:
```
coop_host l01_escape
  → KERNEL:start("l01_escape/coop/alife/new", "localhost")
  → IGame_Persistent::params::parse_cmd_line("l01_escape/coop/alife/new")
      m_game_or_spawn = "l01_escape"   ← parsed from item[0]
      m_game_type     = "coop"
      m_alife         = "alife"
      m_new_or_load   = "new"
  → net_start2() → Server->Connect() → game_sv_Coop::Create(options)
  → game_sv_Single::Create(options)
  → xr_new<CALifeSimulator>(&server(), &options)
  → CALifeSimulator::ctor reads m_game_params.m_game_or_spawn = "l01_escape"
  → load("l01_escape", false, true)   [new_only=true]
  → CALifeUpdateManager::new_game("l01_escape")
  → CALifeSpawnRegistry::load("l01_escape")
  → FS.exist("$game_spawn$/l01_escape.spawn")  ← DOES NOT EXIST
  → R_ASSERT3 → FATAL ERROR
```

**Why this is wrong**: In Anomaly 1.5.3 (and all S.T.A.L.K.E.R. games using a single global spawn),
there is exactly **one** spawn file: `$game_spawn$/all.spawn`. There is **no** `l01_escape.spawn`.
The first component of the server options string is used as **both**:
1. The **level name** (parsed by `parse_level_name()` for `map_data.m_name` and for `level_name()`
   before ALife is initialized)
2. The **spawn file name** (used by `CALifeSimulator` as `m_game_or_spawn` → `spawns().load()`)

For single player, the options string is `"all/single/alife/new"`, so `m_game_or_spawn = "all"` → 
`all.spawn` ✓. Our `coop_host l01_escape` set the level name as the first component, which is
correct for `level_name()` but wrong for the spawn file.

### Secondary crash — LoadTitle for coop using wrong Lua function

In `alife_update_manager.cpp:302`:
```cpp
g_pGamePersistent->LoadTitle(true, g_pGameLevel->name());
```
This is called during `new_game()`, before `map_data.m_name` is set by `net_start2()` (so the map
name is ""). With `change_tip = true` and `m_game_type = "coop"`:
```cpp
bool is_single = !xr_strcmp(m_game_params.m_game_type, "single"); // → false for coop
// falls to else:
R_ASSERT(ai().script_engine().functor("loadscreen.get_mp_tip_number", m_functor));
```
Anomaly has no `loadscreen.get_mp_tip_number` (MP-only function stripped), so `R_ASSERT` fails.

---

## Fixes Applied

### Fix 1 — `src/xrGame/game_sv_coop.cpp` (`game_sv_Coop::Create`)

Before calling `inherited::Create(options)`, patch `m_game_params.m_game_or_spawn = "all"` when
this is a new game (`/new` in options):

```cpp
// In Anomaly there is only ONE spawn file: all.spawn.
// m_game_params.m_game_or_spawn is used by CALifeSimulator as the SPAWN FILE name,
// but coop_host passes the LEVEL name as the first options component.
if (strstr(*options, "/new"))
    xr_strcpy(g_pGamePersistent->m_game_params.m_game_or_spawn, "all");
```

**Why this is correct**:
- `options` string (e.g. `"l01_escape/coop/alife/new"`) is NOT modified.
- `game_sv_Single::level_name()` when ALife is not yet init calls `inherited::level_name(options)`
  which parses `item[0]` from `options` = `"l01_escape"` → still correct.
- `CALifeSimulator` reads `m_game_params.m_game_or_spawn = "all"` → `spawns().load("all")` →
  opens `$game_spawn$/all.spawn` ✓
- After ALife init, `level_name()` returns `alife().level_name()` which reads the current level
  from the ALife actor's position in `all.spawn` → Cordon / `l01_escape` ✓
- For saved-game loads (`/load`), `m_game_or_spawn` is already the save file name (set by
  `game_sv_Single::restart_simulator`), so the `/new` guard correctly skips the patch.

### Fix 2 — `src/xrGame/GamePersistent.cpp` (`LoadTitle`)

Added `"coop"` to the `is_single` check so coop uses Anomaly's single-player loadscreen tips:

```cpp
bool is_single = !xr_strcmp(m_game_params.m_game_type, "single")
              || !xr_strcmp(m_game_params.m_game_type, "coop");
```

---

## Expected Outcome

With these two fixes:
- `coop_host l01_escape` no longer crashes at "Loading spawn registry..."
- ALife loads from `all.spawn` (same data as single player) ✓
- Loadscreen tips use the correct single-player Lua function ✓
- The level name `l01_escape` is correctly used for geometry loading in `net_start_client3()`
- ALife actor starts at Cordon (l01_escape) as defined in Anomaly's `all.spawn`

---

## Lookahead — Next Potential Issues

After this fix, the next likely crash zone is the actor spawn during `M_SPAWN`/`M_CONFIGURING`:
- `game_cl_Coop::OnConnected()` → needs to handle actor spawning for coop
- `synchronize_client()` / `game_configured` flag — should work via single-player codepath
- Level loading (`R_ASSERT2(Load(level_id))`) — requires that `alife().level_name()` returns a
  valid installed level name

---

## Files Changed

| File | Change |
|------|--------|
| `src/xrGame/game_sv_coop.cpp` | Patch `m_game_or_spawn = "all"` before ALife init for new games |
| `src/xrGame/GamePersistent.cpp` | `is_single` includes "coop" in `LoadTitle()` |
| `raports/phase_1_progress/session_009.md` | This report |

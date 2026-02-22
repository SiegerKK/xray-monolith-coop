# Session 008 — Root-cause analysis: level loading crash (Post-prefetch)

**Date:** 2026-02-22  
**Status:** Fixes applied, awaiting CI build

---

## Symptom

Game loaded past prefetch (sessions 004-005 fixes) and past the Lua crash (session 006 fix),
but crashed "during level loading" with log ending at:

```
[COOP][SV] Host started | session_id=0x00004BA2 ... level=
```

The `level=` field was empty (cosmetic bug, not the cause). No explicit error message
was logged — the crash was a C++ `R_ASSERT2`.

---

## Full Root-Cause Analysis

### Root Cause 1 — `CALifeSimulator` assertion: FATAL

In `alife_simulator.cpp` constructor:
```cpp
R_ASSERT2(
    xr_strlen(p.m_game_or_spawn) &&
    !xr_strcmp(p.m_alife,"alife") &&
    !xr_strcmp(p.m_game_type,"single"),   // ← REQUIRES "single" exactly
    "Invalid server options!"
);
```

With coop options `l01_escape/coop/alife/new`:
- `m_game_type = "coop"` → assertion FAILS → crash

This would only be triggered IF `game_sv_Coop` tried to create `CALifeSimulator`.
In the prior session, `game_sv_Coop` inherited from `game_sv_GameState` (not `game_sv_Single`)
and didn't create ALife at all.

### Root Cause 2 — No ALife: game world empty

With `game_sv_Coop` inheriting from `game_sv_GameState`, no `CALifeSimulator` was created.
Result: no spawn objects in `xrServer::entities`, no actor, no NPCs, no anomalies.

The level technically "loaded" (terrain, geometry) but the game world was empty.
Without an actor entity, `net_start5`'s `Game().local_player->net_Export()` would run
on an uninitialized player state, or game systems expecting ALife would null-deref.

### Root Cause 3 — Coop options format: `/coop` missing `/alife/new`

`coop_host l01_escape` generated options string `l01_escape/coop`.
`params::parse_cmd_line` maps:
- `m_game_or_spawn = "l01_escape"`  
- `m_game_type = "coop"`  
- `m_alife = ""`       ← ALife NOT triggered
- `m_new_or_load = ""`

This meant `net_start1` entered the "non-alife" Level_ID check branch instead of
the ALife branch (like single player does with `l01_escape/single/alife/new`).

### Root Cause 4 — `CalculateLevelCrc32()` called for coop

```cpp
if (!IsGameTypeSingle())
    CalculateLevelCrc32();  // opens $level$/level.geom via R_ASSERT2
```

For coop (not single), this was called. `level.geom` might be absent for some Anomaly levels,
causing `R_ASSERT2(geom, "failed to open level.geom file")` crash.

### Root Cause 5 — `level=` empty in Host started log

`game_sv_Coop::Create` searched for `/level/` prefix in options which never existed.
Fixed to use `parse_level_name(options)` which correctly reads the first `/`-delimited token.

---

## Fixes Applied

### Fix 1: `game_sv_coop.h` — Inherit from `game_sv_Single`

```cpp
// BEFORE:
class game_sv_Coop : public game_sv_GameState
// AFTER:
class game_sv_Coop : public game_sv_Single
```

Benefits:
- ALife (`m_alife_simulator`) support inherited for free
- `OnCreate`, `OnTouch`, `OnDetach`, `CanHaveFriendlyFire` — all implemented
- `GetStartGameTime`, `GetGameTime` — ALife-aware implementations inherited
- `custom_sls_default()` returns `true` when ALife is active → `SLS_Default` delegates to ALife
- `Update()` — ALife tick inherited

### Fix 2: `game_sv_coop.cpp` — `Create()` delegates to `game_sv_Single::Create`

```cpp
void game_sv_Coop::Create(shared_str& options)
{
    inherited::Create(options);  // ← creates CALifeSimulator if /alife in options
    ...
    shared_str lvl = parse_level_name(options);  // correct level name extraction
    ...
    m_type = eGameIDCoop;  // override m_type (game_sv_Single ctor set it to eGameIDSingle)
}
```

Removed `switch_Phase(GAME_PHASE_INPROGRESS)` call (already done by `game_sv_Single::Create`).
Removed broken `/level/` parsing.

### Fix 3: `console_commands.cpp` — Options format with ALife

```cpp
// BEFORE:
xr_sprintf(sv_opts, "%s/coop", args);
// AFTER:
xr_sprintf(sv_opts, "%s/coop/alife/new", args);
```

Now `m_alife = "alife"` → ALife subsystem triggers.
`m_new_or_load = "new"` → new game (not load from save).

### Fix 4: `alife_simulator.cpp` — Accept "coop" game type

```cpp
// BEFORE:
!xr_strcmp(p.m_game_type,"single")
// AFTER:
(!xr_strcmp(p.m_game_type,"single") || !xr_strcmp(p.m_game_type,"coop"))
```

Also: coop uses `"single"` as save directory token to reuse the same save format:
```cpp
LPCSTR save_type = !xr_strcmp(p.m_game_type, "coop") ? "single" : p.m_game_type;
```

### Fix 5: `Level_network_start_client.cpp` — Skip CRC32 for coop

```cpp
// BEFORE:
if (!IsGameTypeSingle())
    CalculateLevelCrc32();
// AFTER:
if (!IsGameTypeSingle() && g_pGamePersistent->GameType() != eGameIDCoop)
    CalculateLevelCrc32();
```

### Fix 6: `xrServer_info.cpp` — Treat coop like single in `SendServerInfoToClient`

```cpp
if (IsGameTypeSingle() || (game && game->Type() == eGameIDCoop))
{
    SendConfigFinished(new_client);
    return;
}
```

Without this, for coop with `m_server_logo == NULL && m_server_rules == NULL`
the second guard (`if (!m_server_logo || !m_server_rules)`) already handled it.
The explicit check is cleaner and more defensive.

### Fix 7: `GamePersistent.cpp` — Include coop in single-player UI paths

```cpp
if (m_game_params.m_e_game_type == eGameIDSingle ||
    m_game_params.m_e_game_type == eGameIDCoop)
    g_current_keygroup = _sp;  // single-player key bindings
```

Similarly for `game_loaded()` `game_loaded` intro event — coop uses the single-player
load screen and key-press flow (not the MP lobby flow).

### Fix 8: `x_ray.cpp` — Load stage count for coop

```cpp
if ((g_pGamePersistent->GameType() == eGameIDSingle || g_pGamePersistent->GameType() == eGameIDCoop)
    && !xr_strcmp(g_pGamePersistent->m_game_params.m_alife, "alife"))
    max_load_stage = 17;  // ALife adds more loading stages
```

---

## Expected Outcome After CI Build

With these fixes, `coop_host l01_escape` should:
1. Parse options `l01_escape/coop/alife/new` correctly
2. Create `game_sv_Coop` (inheriting `game_sv_Single`)
3. Initialize `CALifeSimulator` — spawn all level objects (actors, NPCs, anomalies, items)
4. Client loads level, receives M_SPAWN messages for all entities
5. Actor spawns, game world is populated
6. Player enters the level in the Cordon map

---

## Remaining Known Risks (Phase 1.D)

| Risk | Severity | Notes |
|------|----------|-------|
| ALife save path for coop | Medium | Phase 1 uses "single" save dir — acceptable for now |
| `net_start5` `M_CLIENTREADY` with coop actor | Low | `local_player` initialized in ctor |
| `IsGameTypeSingle()` checks in scripts | Medium | Many Lua scripts check `game_type`, may need patches |
| Multiple clients connecting | High | Phase 1.C: second client auth flow untested |

---

## Files Changed This Session

| File | Change |
|------|--------|
| `src/xrGame/game_sv_coop.h` | Base class → `game_sv_Single` |
| `src/xrGame/game_sv_coop.cpp` | `Create()`: delegate to Single, fix level_name, override m_type |
| `src/xrGame/console_commands.cpp` | `coop_host` options: `/coop` → `/coop/alife/new` |
| `src/xrGame/alife_simulator.cpp` | Assert: accept "coop" + save dir mapping |
| `src/xrGame/Level_network_start_client.cpp` | Skip CRC32 for coop |
| `src/xrGame/xrServer_info.cpp` | `SendServerInfoToClient`: treat coop like single |
| `src/xrGame/xrServer_Connect.cpp` | `get_map_download_url`: suppress warning for direct_connect |
| `src/xrGame/GamePersistent.cpp` | `UpdateGameType` + `game_loaded`: include coop in single paths |
| `src/xrEngine/x_ray.cpp` | Load stage count: include coop |

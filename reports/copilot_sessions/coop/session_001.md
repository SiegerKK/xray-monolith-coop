# Coop Session 001 — Phase 1: Cooperative Multiplayer Scaffolding

**Date:** 2026-03-08  
**Goal:** Enable hosting a coop server and connecting via `localhost` or IP through the in-game console.

---

## Summary

Phase 1 is complete. The engine can now represent, create, and route to a cooperative game mode. Two new console commands (`coop_host`, `coop_connect`) allow a player to start a coop session or join one.

---

## Changes Made

### New Files

| File | Purpose |
|------|---------|
| `src/xrGame/game_sv_coop.h` | `game_sv_Coop : game_sv_Single` — server-side coop game state |
| `src/xrGame/game_sv_coop.cpp` | Implementation: sets `m_type = eGameIDCooperative`, disables friendly fire |
| `src/xrGame/game_cl_coop.h` | `game_cl_Coop : game_cl_Single` — client-side coop game state |
| `src/xrGame/game_cl_coop.cpp` | Implementation: inherits all SP HUD and input behaviour |

### Modified Files

| File | Change |
|------|--------|
| `src/xrServerEntities/gametype_chooser.h` | Added `eGameIDCooperative = u32(1) << 7` to `EGameIDs` enum (bit 7, no conflict) |
| `src/xrServerEntities/clsid_game.h` | Added `CLSID_SV_GAME_COOP` and `CLSID_CL_GAME_COOP` macros |
| `src/xrServerEntities/object_factory_register.cpp` | Registered `game_sv_Coop` and `game_cl_Coop` in the object factory under `#ifndef NO_SINGLE` |
| `src/xrGame/GamePersistent.cpp` | `GameTypeToString()` → `"coop"`/`"cooperative"`; `ParseStringToGameType()` accepts both; `UpdateGameType()` maps coop to `_sp` key group |
| `src/xrGame/game_base.cpp` | `getCLASS_ID()` maps `eGameIDCooperative` → `CLSID_SV_GAME_COOP` / `CLSID_CL_GAME_COOP` |
| `src/xrGame/Level_start.cpp` | `net_start1()`: `"coop"` routes to plain `xrServer` (not `xrGameSpyServer`) |
| `src/xrGame/Level.h` | Added `IsGameTypeSingleOrCoop()` inline helper for Phase 3 use |
| `src/xrGame/console_commands.cpp` | Added `CCC_CoopHost` + `CCC_CoopConnect` commands with input sanitization |
| `src/xrGame/xrGame.vcxproj` | Added all four new files to `ClCompile`/`ClInclude` sections |

---

## Console Commands

### `coop_host [save_name]`

Starts a new coop game or loads an existing save as the host.

- **No args:** `op_server = "all/coop/alife/new"`, starts fresh on `all` map
- **With save name:** `op_server = "<save>/coop/alife/load"`, loads `<save>.scop`
- `op_client = "localhost/name=<player_name>"`
- `psNET_direct_connect` is set to `FALSE` so real DirectPlay8 sockets are used

### `coop_connect [ip_or_hostname]`

Connects to a coop host.

- **No args:** connects to `localhost:1235`
- **With IP/hostname:** connects to `<ip>:1235`
- `op_server = ""` (no server started on client), `op_client = "<host>/name=<player_name>/port=1235"`
- `psNET_direct_connect` is set to `FALSE`

---

## Architecture Decisions

1. **Inherit from SP, not MP:** `game_sv_Coop : game_sv_Single` keeps the ALife simulator stack intact. `game_cl_Coop : game_cl_Single` preserves all SP HUD behaviour.
2. **`eGameIDCooperative` at bit 7:** No collision with existing game ID bits (SP=bit 0, DM=bit 1, TDM=bit 2, etc.).
3. **`xrServer` not `xrGameSpyServer`:** Coop uses the LAN server path, avoiding the GameSpy dependency.
4. **`IsGameTypeSingleOrCoop()`:** Prepared for Phase 3 where 200+ `IsGameTypeSingle()` checks will be reviewed and selectively widened to include coop.
5. **Input sanitization:** `Coop_SanitizeString()` replaces `/` (op-string separator) and `%` (printf format specifier) in all user input with `_`.

---

## Security

- **Op-string field injection via `/`:** Fixed by stripping `/` from user-supplied save names, IPs, and player names.
- **Printf format-string abuse via `%`:** Fixed by stripping `%` from the same inputs.
- All `xr_sprintf` calls are guarded by named-constant length checks before execution.

---

## Next Steps (Phase 2)

- `g_all_actors: xr_vector<CActor*>` — registry of all player actors
- `CALifeSwitchManager::update_switch()` — union of all players' online zones
- Level change synchronization (ACK protocol)
- Basic spawn handling for joining clients

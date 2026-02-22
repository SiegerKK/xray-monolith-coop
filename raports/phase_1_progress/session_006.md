# Session 006 — Fix Lua crash: SwitchToMultiplayerMenu + GameTypeToString + CheckNewPlayer

## Problem
`coop_host l01_escape` crashed with:
```
FATAL ERROR
[error]Function      : CScriptEngine::lua_pcall_failed
LUA error: z:/mnt/data/anomaly\gamedata\scripts\ui_main_menu.script:281:
    attempt to call method 'OnButton_multiplayer_clicked' (a nil value)
```

## Root Cause Analysis

### Crash 1 — `SwitchToMultiplayerMenu()` (immediate Lua fatal)
When level loading fails for coop, `Level_start.cpp::net_start6` calls
`MainMenu()->SwitchToMultiplayerMenu()` → `m_startDialog->Dispatch(2,1)` →
Anomaly's Lua script receives event 2 and tries to call `OnButton_multiplayer_clicked`
which does not exist on Anomaly 1.5.3's stripped-down main menu → FATAL Lua error.

**Fix:** `Level_start.cpp` — compute `is_coop` flag before the error handlers; guard
all three `SwitchToMultiplayerMenu()` calls behind `!is_coop`.

### Crash 2 — `CheckNewPlayer` R_ASSERT (empty expression in release build)
`game_sv_base.cpp::GAME_EVENT_CREATE_PLAYER_STATE` calls `CheckNewPlayer(CL)`, which
begins with `smart_cast<xrGameSpyServer*>(m_server)` + `R_ASSERT(gs_server)`. For
coop, `m_server` is plain `xrServer` (not `xrGameSpyServer`) → `gs_server = nullptr`
→ R_ASSERT fires. In a release build `_TRE(expr) = ""`, giving
`[error]Expression    :` with nothing.

**Fix:** `game_sv_base.cpp` — add `if (Type() == eGameIDCoop) break;` before
`CheckNewPlayer(CL)` in the `GAME_EVENT_CREATE_PLAYER_STATE` handler.

### Bug 3 — `GameTypeToString` missing `eGameIDCoop` case
`game_cl_GameState::set_type_name("coop")` → `ParseStringToGameType` →
`eGameIDCoop`, then `GameTypeToString(eGameIDCoop)` fell into the default `"---"`
case. This set `m_game_params.m_game_type = "---"` and caused
`UpdateGameType()` → `ParseStringToGameType("---")` → `eGameIDNoGame = 0`, breaking
`IsGameTypeSingle()` semantics for coop during level load.

**Fix:** `GamePersistent.cpp` — add `case eGameIDCoop: return "coop";` in
`GameTypeToString()`.

## Files Changed
- `src/xrGame/Level_start.cpp` — guard `SwitchToMultiplayerMenu()` for coop
- `src/xrGame/GamePersistent.cpp` — add `eGameIDCoop` → `"coop"` in `GameTypeToString`
- `src/xrGame/game_sv_base.cpp` — skip `CheckNewPlayer` for coop game type

## Next Session
Level should now load further. Likely next issues:
- `game_configured` never becoming TRUE (server not sending `M_SV_CONFIG_NEW_CLIENT`)
- `CalculateLevelCrc32()` for non-single-player path consuming time
- `net_start5` sending `M_CLIENTREADY` and `Game().local_player` being NULL for coop

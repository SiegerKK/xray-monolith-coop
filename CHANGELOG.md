# Changelog

## Unreleased

### Fix crash at "CL: On Update Request [4]"

Game crashed after ~4 weapon-stats timer cycles (~2-4 minutes on level).

**Root cause:** `WeaponUsageStatistic` is an MP-only subsystem. Three code paths
incorrectly treated coop as multiplayer via `GameID() != eGameIDSingle`:

| File | Line | Change |
|------|------|--------|
| `src/xrGame/Level.cpp` | ~896 | `GameID() != eGameIDSingle` → `!IsGameTypeSingleOrCoop()` in `M_STATISTIC_UPDATE` handler |
| `src/xrGame/game_cl_base_weapon_usage_statistic.cpp` | `Update()` | Early-return when `IsGameTypeSingleOrCoop()` — stops sending `M_STATISTIC_UPDATE` packets in coop |
| `src/xrGame/game_cl_base_weapon_usage_statistic.cpp` | `OnUpdateRequest()` | Safe iterator check: `FindPlayer()` result validated before dereference |
| `src/xrGame/xrServer.cpp` | ~670 | `GameID() != eGameIDSingle` → `!IsGameTypeSingleOrCoop()` prevents `static_cast<game_sv_mp*>` UB (game_sv_Coop is NOT game_sv_mp) |

---

## v0.1-host-stable *(2026-03-09)*

> **Checkpoint**: Coop host can load a test level and play without crashes.
> Tag this commit as `v0.1-host-stable` before starting DirectPlay replacement work.
>
> Commit: `ec670174b8c6cd6666b2908ea160f54c7f34023e`
> Branch: `copilot/implement-cooperation-server`

### C++ crash fixes

- **`src/xrGame/alife_simulator.cpp`**  
  `CALifeSimulator()` R_ASSERT2 required `m_game_type == "single"` — extended to accept `"coop"`.

- **`src/Layers/xrRender/ModelPool.cpp`**  
  `CModelPool::Prefetch()` built section `"prefetch_visuals_coop"` (doesn't exist) → crash.  
  Fixed with `section_exist()` guard, falls back to `"prefetch_visuals_single"`.

- **`src/xrEngine/IGame_ObjectPool.cpp`**  
  Same pattern for `"prefetch_objects_coop"` → falls back to `"prefetch_objects_single"`.

- **`src/xrGame/GamePersistent.cpp`**  
  `IsGameTypeSingle()` returned `false` for coop in `LoadTitle`, `CanBePaused`,  
  `OnAppActivate`, `OnAppDeactivate`, `game_loaded` — replaced with `IsGameTypeSingleOrCoop()`.

- **`src/xrGame/Actor_Network.cpp`**  
  Same `IsGameTypeSingle()` → `IsGameTypeSingleOrCoop()` in `Actor()` assertion and `net_Spawn()` guard.

- **`src/xrNetServer/NET_Server.cpp`**  
  Port-scan loop treated any `NET->Host()` HRESULT as "port busy".  
  Fixed: only `DPNERR_ADDRESSING` (0x80158040) retries; any other HRESULT is fatal-fast.  
  Ports moved to 50000–50039.

- **`src/xrNetServer/NET_Client.cpp`**  
  Matching client-side port range fix.

- **`src/xrNetServer/NET_Common.h`**  
  `START_PORT` / `END_PORT` updated to 50000/50039.

### Lua crash fixes (nil `db.actor` guards)

- **`gamedata/scripts/ranks.script`**  
  `get_obj_rank_name(nil)` and `get_player_reputation()` crash when actor not yet spawned.

- **`gamedata/scripts/gameplay_radioactive_water.script`**  
  `actor_on_footstep` → `save_var(db.actor, …)` intentionally crashes on nil.  
  `actor_on_update` → `db.actor:hit()` crashes on nil.

- **`gamedata/scripts/aaaa_script_fixes_mp.script`**  
  `npc_on_update_force_trader_update` and `trans_outfit` callbacks without actor guard.

- **`gamedata/scripts/sim_squad_bounty.script`**  
  TimeEvent timers fire before actor spawn.

### Engine/Lua wiring fixes

- **`src/xrGame/ui_export_script.cpp`**  
  `CMainMenu::SwitchToMultiplayerMenu` exported to Lua.

- **`gamedata/scripts/aaaa_script_fixes_mp.script`**  
  `OnButton_multiplayer_clicked` was `nil` on Anomaly main menu Lua class (stripped in SP-only game).  
  Fixed by injecting the method from `main_menu_on_init` callback.

- **`src/xrNetServer/NET_Server.cpp:250-251`**  
  `/coop` option in server options sets `psNET_direct_connect = TRUE`, bypassing DirectPlay  
  for the host process (same as single-player loopback). DirectPlay is only needed for remote clients.

### Documentation

- **`reports/coop-milestone-directplay-analysis.md`**  
  Strategic analysis: DirectPlay is NOT a blocker for host testing.  
  `psNET_direct_connect = TRUE` already bypasses it.  
  Includes roadmap for ENet-based transport replacement (Phase 2).

---

## How to create the tag on GitHub

Go to:  
<https://github.com/SiegerKK/xray-monolith-coop/releases/new>

- **Tag**: `v0.1-host-stable`
- **Target**: `copilot/implement-cooperation-server` (or SHA `ec670174`)
- **Title**: `v0.1 Host-Stable — coop host loads level without crashes`
- **Description**: paste the C++ and Lua fix sections above

---

## What comes next (Phase 2)

Replace `IDirectPlay8Server`/`IDirectPlay8Client` in `xrNetServer/` with ENet (UDP reliable):

| DirectPlay API | ENet replacement |
|---|---|
| `CoCreateInstance(CLSID_DirectPlay8Server)` | `enet_host_create(address, max_clients, …)` |
| `IDirectPlay8Server::SendTo()` | `enet_peer_send(peer, channel, packet)` |
| `IDirectPlay8Client::Connect()` | `enet_host_connect(host, address, channels, …)` |
| `DPN_MSGID_CREATE_PLAYER` | `ENET_EVENT_TYPE_CONNECT` |
| `DPN_MSGID_DESTROY_PLAYER` | `ENET_EVENT_TYPE_DISCONNECT` |
| `DPN_MSGID_RECEIVE` | `ENET_EVENT_TYPE_RECEIVE` |

Files to rewrite: `NET_Server.cpp`, `NET_Client.cpp`, `NET_Common.h` (~800 lines total).  
Everything above `IPureServer`/`IPureClient` (game logic, ALife, Lua) stays unchanged.

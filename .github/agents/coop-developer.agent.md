---
name: Coop Developer
description: Senior C++ game engine developer specializing in implementing cooperative multiplayer for the X-Ray Monolith engine (S.T.A.L.K.E.R. Anomaly fork).
---

## Instructions

### Your primary role
Implement cooperative multiplayer for the X-Ray Monolith engine. You understand the full architecture as documented in `reports/big-architecture-analyse-1.md` (sections 1–14). You make **surgical, minimal changes** that keep the codebase stable while progressively adding coop capabilities.

### Codebase structure you must know

**Key source directories:**
- `src/xrEngine/` — core engine (console commands, events, app lifecycle)
- `src/xrGame/` — game logic (actors, ALife, console commands, game modes)
- `src/xrNetServer/` — networking (DirectPlay 8 wrapper, NET_Client/NET_Server)
- `src/xrServerEntities/` — shared server/client entities (game types, object factory)
- `src/xrPhysics/` — ODE physics
- `gamedata/scripts/` — Lua scripts

**Critical files for coop work:**
| File | Purpose |
|------|---------|
| `src/xrServerEntities/gametype_chooser.h` | `EGameIDs` enum — add `eGameIDCooperative` here |
| `src/xrServerEntities/clsid_game.h` | CLSID constants — add `CLSID_SV_GAME_COOP`, `CLSID_CL_GAME_COOP` |
| `src/xrServerEntities/object_factory_register.cpp` | Factory registration — register new game mode classes |
| `src/xrGame/GamePersistent.cpp` | `ParseStringToGameType()` — add "coop" string mapping |
| `src/xrGame/game_base.cpp` | `getCLASS_ID()` — add `eGameIDCooperative` branch |
| `src/xrGame/console_commands.cpp` | `CCC_RegisterCommands()` — register coop console commands |
| `src/xrGame/game_sv_single.cpp/.h` | SP server — base class for `game_sv_Coop` |
| `src/xrGame/game_sv_mp.cpp/.h` | MP server — used for player management in coop |
| `src/xrGame/game_cl_single.cpp/.h` | SP client — base class for `game_cl_Coop` |
| `src/xrGame/Actor_Network.cpp` | `g_actor` global, `net_Spawn/Destroy` — add `g_all_actors` |
| `src/xrGame/alife_switch_manager.cpp` | Online/offline object switching — extend for multiple players |
| `src/xrEngine/xr_ioc_cmd.cpp` | `CCC_Start`, `CCC_Disconnect` — reference for command structure |
| `src/xrEngine/x_ray.cpp` | `CApplication::OnEvent` for `KERNEL:start` — game launch flow |
| `src/xrNetServer/NET_Common.h` | Port constants: `START_PORT_LAN_SV=1235` |
| `src/xrNetServer/NET_Client.cpp` | Client connect, `server_name` parsing from options string |
| `src/xrGame/Level_start.cpp` | Server creation: must allow "coop" to use `xrServer` (not GameSpy) |

### How the game launch flow works

The full chain from console command to running game:
```
Console: "start server(<map>/coop/alife/new) client(localhost/name=Player)"
  → CCC_Start::Execute()               [xr_ioc_cmd.cpp:330]
  → Engine.Event.Defer("KERNEL:start", op_server, op_client)
  → CApplication::OnEvent(eStart)      [x_ray.cpp:~1395]
  → g_pGamePersistent->PreStart(op_server)  — parses game_type, alife, new_or_load
  → CLevel::net_Start(op_server, op_client)
  → net_start1(): creates xrServer (if single/coop) or xrGameSpyServer (if MP)
  → xrServer::Connect()               [xrServer_Connect.cpp]
      → getCLASS_ID("coop", true)     → CLSID_SV_GAME_COOP
      → game_sv_Coop::Create()        → starts ALifeSimulator
  → NET_Client::Connect(op_client)    [NET_Client.cpp:~380]
      → server_name parsed from op_client (before first '/')
      → DirectPlay8 connect to server_name:1235
```

### Server/client options string format

**op_server** (parsed by `IGame_Persistent::params::parse_cmd_line()`):
```
<save_or_spawn_name>/<game_type>/<alife_flag>/<new_or_load>
```
- Position 0: level/save name (e.g., `"all"`, `"l01_escape"`, `"my_save"`)
- Position 1: game type (e.g., `"single"`, `"coop"`, `"deathmatch"`)
- Position 2: ALife flag (`"alife"` or empty)
- Position 3: mode (`"new"` or `"load"`)

**op_client** (parsed by NET_Client::Connect):
```
<server_host>[/name=<player_name>][/port=<port>][/portcl=<client_port>]
```

### `psNET_direct_connect` — critical flag
- `TRUE` in SP: bypasses real network stack (loopback only, no real sockets)
- **Must be `FALSE` for coop**: real DirectPlay8 sockets needed
- When `FALSE` + `localhost`: works for LAN/same-machine testing on port 1235

### Key design principles for coop implementation

1. **Host is authoritative**: ALife simulator runs only on host. Clients receive spawned objects via standard `M_SPAWN` packets.
2. **`g_actor` = local player's actor**: Never change this. Add `g_all_actors` alongside it.
3. **Inherit from SP, not MP**: `game_sv_Coop : game_sv_Single` keeps ALife; `game_cl_Coop : game_cl_Single` keeps SP HUD.
4. **`IsGameTypeSingleOrCoop()`**: Add this helper next to `IsGameTypeSingle()` in `src/xrGame/Level.h` for places where coop should behave like SP.
5. **Minimal diffs**: Change the fewest possible files per phase. Prefer adding new files over modifying shared ones.

### Implementation phases

**Phase 1 (current priority):**
- [ ] Add `eGameIDCooperative` to `EGameIDs` enum
- [ ] Add `CLSID_SV_GAME_COOP` / `CLSID_CL_GAME_COOP` to `clsid_game.h`
- [ ] Add "coop" to `ParseStringToGameType()` and `GameTypeToString()`
- [ ] Add `eGameIDCooperative` branch to `getCLASS_ID()`
- [ ] Create `game_sv_coop.h/.cpp` (thin wrapper over `game_sv_Single`)
- [ ] Create `game_cl_coop.h/.cpp` (thin wrapper over `game_cl_Single`)
- [ ] Register both in `object_factory_register.cpp`
- [ ] Allow "coop" to use `xrServer` in `Level_start.cpp`
- [ ] Add `CCC_CoopHost` and `CCC_CoopConnect` to `console_commands.cpp`

**Phase 2:**
- [ ] `g_all_actors: xr_vector<CActor*>` — registry of all player actors
- [ ] `CALifeSwitchManager::update_switch()` — union of all players' online zones
- [ ] Level change synchronization (ACK protocol)

**Phase 3:**
- [ ] `IsGameTypeSingleOrCoop()` helper and gradual substitution
- [ ] AI targeting multiple players
- [ ] Coop HUD additions (teammate markers)

**Phase 4:**
- [ ] Coop save/load system (host saves ALife + per-player state)
- [ ] Lua callbacks for coop events

### Console command implementation reference

Every console command is a class inheriting `IConsole_Command`:

```cpp
class CCC_MyCommand : public IConsole_Command {
public:
    CCC_MyCommand(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    
    virtual void Execute(LPCSTR args) {
        // implementation
        Engine.Event.Defer("KERNEL:start",
            u64(xr_strdup(op_server)),   // must xr_strdup — freed by event handler
            u64(xr_strdup(op_client)));
    }
    
    virtual void Info(TInfo& I) {
        xr_strcpy(I, "[arg] — description");
    }
};
// Registration in CCC_RegisterCommands():
CMD1(CCC_MyCommand, "my_command");
```

Macros in `src/xrEngine/xr_ioc_cmd.h`:
- `CMD1(cls, name)` — command with only name
- `CMD2(cls, name, arg2)` — command with one extra constructor arg
- etc.

### Build system context
The project uses Visual Studio project files. Main game DLL is `xrGame.dll` built from `src/xrGame/`. When adding new `.cpp` files, they must be added to the corresponding `.vcxproj` project file. The `src/xrServerEntities/` files are shared between server entities DLL and xrGame.

### Testing approach for Phase 1
After implementing the new commands:
1. `coop_host` without args → should start a new game with ALife on port 1235
2. `coop_host my_save` → should load `my_save.scop` as a coop session
3. `coop_connect` → should connect to `localhost:1235`
4. `coop_connect 192.168.1.x` → should connect to remote IP

Validate by checking:
- No crash on `coop_host` (ALife initializes correctly)
- `Level.Server` is non-NULL (server started)
- `Level.Server->game` is instance of `game_sv_Coop`
- `psNET_direct_connect` is `FALSE` during coop sessions

### Known pitfalls

1. **`PreStart(NULL)` crash**: Pass `xr_strdup("")` not `0` when no server options in `coop_connect`.
2. **GameSpy registration**: `xrGameSpyServer` requires GameSpy SDK — coop must use plain `xrServer`.
3. **`IsGameTypeSingle()` = false**: With `eGameIDCooperative`, 200+ checks return false. Some are intentional (MP network behavior), others break UI/HUD. Fix only what's broken, incrementally.
4. **`NO_SINGLE` defines**: `game_sv_Coop` needs ALife, so it must be compiled with `NO_SINGLE` undefined.
5. **Object factory registration order**: New game classes must be registered before the factory is used (at DLL load time).
6. **`STRCONCAT` macro**: Used for building command strings — safe with stack buffers only.

### Reference documentation
Full architecture analysis is in `reports/big-architecture-analyse-1.md`:
- §12: Detailed component analysis with exact file/line citations
- §13: Full architectural proposal with class diagrams
- §14: Phase 1 detailed spec (console commands, file change map, code samples)




# Coop Implementation — Knowledge Base Update
---

## 1. Архитектурные решения (зафиксированы навсегда)

### 1.1 Новый игровой тип `eGameIDCooperative`

| Что | Где | Детали |
|-----|-----|--------|
| `eGameIDCooperative = u32(1) << 7` | `src/xrServerEntities/gametype_chooser.h:17` | Бит 7 — нет коллизий со штатными типами |
| `CLSID_SV_GAME_COOP` / `CLSID_CL_GAME_COOP` | `src/xrServerEntities/clsid_game.h:222-223` | `MK_CLSID('S','V','_','C','O','O','P',' ')` |
| `"coop"` / `"cooperative"` | `src/xrGame/GamePersistent.cpp:233-258` | `GameTypeToString` и `ParseStringToGameType` |
| Маппинг `eGameIDCooperative → CLSID_SV/CL_GAME_COOP` | `src/xrGame/game_base.cpp` | `getCLASS_ID()` |
| Регистрация в фабрике | `src/xrServerEntities/object_factory_register.cpp` | Под `#ifndef NO_SINGLE` |
| Роутинг на `xrServer` (не GameSpy) | `src/xrGame/Level_start.cpp:357` | `if (!IsGameTypeSingleOrCoop())` |
| `UpdateGameType` → `_sp` key group | `src/xrGame/GamePersistent.cpp:270` | Coop использует те же настройки, что и SP |

### 1.2 Новые классы игрового режима

| Класс | Файл | База | Суть |
|-------|------|------|------|
| `game_sv_Coop` | `src/xrGame/game_sv_coop.h/.cpp` | `game_sv_Single` | Сохраняет ALife-стек; `m_type = eGameIDCooperative`; дружественный огонь выключен |
| `game_cl_Coop` | `src/xrGame/game_cl_coop.h/.cpp` | `game_cl_Single` | Наследует весь SP HUD и ввод |

### 1.3 Консольные команды

| Команда | Класс | Поведение |
|---------|-------|-----------|
| `coop_host [save_name]` | `CCC_CoopHost` | Без аргументов: `all/coop/alife/new`. С аргументом: `<save>/coop/alife/load`. `psNET_direct_connect = FALSE`. |
| `coop_connect [ip]` | `CCC_CoopConnect` | Без аргументов: `localhost:1235`. С IP: `<ip>:1235`. `psNET_direct_connect = FALSE`. |
| Реализация | `src/xrGame/console_commands.cpp:2385-3231` | `CMD1(CCC_CoopHost, "coop_host")` и `CMD1(CCC_CoopConnect, "coop_connect")` |
| Санитизация | `Coop_SanitizeString()` в том же файле | Заменяет `/` и `%` на `_` во всех пользовательских строках |

### 1.4 `IsGameTypeSingleOrCoop()` — главный инструмент

```cpp
// src/xrGame/Level.h:446
IC bool IsGameTypeSingleOrCoop() {
    return (g_pGamePersistent->GameType() == eGameIDSingle
         || g_pGamePersistent->GameType() == eGameIDCooperative);
}
```

**Это свободная функция, не метод.** Вызывать `IsGameTypeSingleOrCoop()`,
никогда `Level().IsGameTypeSingleOrCoop()`.

---

## 2. Все исправленные краши

### 2.1 C++ краши (в порядке исправления)

#### Инициализация сервера

| Краш | Файл | Причина | Фикс |
|------|------|---------|------|
| `alife_simulator` R_ASSERT2 | `alife_simulator.cpp` | Требовал `game_type == "single"` | Принимает `"coop"` |
| `ModelPool` prefetch | `ModelPool.cpp` | Секция `prefetch_visuals_coop` отсутствует | Fallback на `_single` |
| `IGame_ObjectPool` prefetch | `IGame_ObjectPool.cpp` | Секция `prefetch_objects_coop` отсутствует | Fallback на `_single` |
| `NET_Server.cpp` Host retry | `src/xrNetServer/NET_Server.cpp` | Ошибки `Host()` на Wine трактовались как «порт занят» | Только `DPNERR_ADDRESSING` → retry |
| GameSpy file_transfers/screenshot_proxies | `src/xrGame/xrServer_Connect.cpp:67` | Создавались при `Type != eGameIDSingle` | Исключён `eGameIDCooperative` |

#### Игровой цикл (ProcessGameEvents / OnFrame)

| Краш | Файл:строка | Причина | Фикс |
|------|-------------|---------|------|
| `WeaponUsageStatistic::OnBullet_Check_Result` | `src/xrGame/Actor_Network.cpp:1921` | `GameID() != eGameIDSingle` пускало вызов в coop | `!IsGameTypeSingleOrCoop()` |
| `WeaponUsageStatistic` Send_Check_Respond | `src/xrGame/Level.cpp:946` | Та же проблема в ProcessGameEvents | `!IsGameTypeSingleOrCoop()` |
| `m_item_respawner.update()` | `src/xrGame/game_sv_base.cpp:665` | MP-система ресспауна запускалась в coop | `!IsGameTypeSingleOrCoop()` |
| `AddDelayedEvent` / `GE_HIT` e_src null | `src/xrGame/game_sv_base.cpp:786,898` | MP-пути без null-проверок | `IsGameTypeSingleOrCoop()` parity |
| `GameTaskManager` обращение | `src/xrGame/Level.cpp:1055` | `IsGameTypeSingle()` → false в coop | `IsGameTypeSingleOrCoop()` |

#### Смерть/урон актора

| Краш | Файл:строка | Причина | Фикс |
|------|-------------|---------|------|
| `entity_alive.cpp` Hit/Die | `src/xrGame/entity_alive.cpp:305,323,330` | `IsGameTypeSingle()` → false | `IsGameTypeSingleOrCoop()` |
| `Actor_Movement.cpp` g_cl_CheckControls | `src/xrGame/Actor_Movement.cpp:317` | Та же причина | `IsGameTypeSingleOrCoop()` |

#### HUD / оружие

| Краш | Файл:строка | Причина | Фикс |
|------|-------------|---------|------|
| `player_hud.cpp` нулевой `m_parent_hud_item` | `src/xrGame/player_hud.cpp:881-885, 1145-1152, 1702-1710, 1843-1888` | CHudItem уничтожается пока ещё прикреплён | Null-guard + `clear_stale_attached_item` в деструкторе |
| `updateMovementLayerState` null ptr | `src/xrGame/player_hud.cpp:1269-1271` | `m_attached_items[0]->m_parent_hud_item->NeedBlendAnm()` без guard | Явный null-guard на `m_parent_hud_item` |
| `player_hud.cpp` allow_script_anim / inertion_allowed / OnMovementChanged / net_Relcase | `src/xrGame/player_hud.cpp:880-907, 1716-1730, 1808-1812, 1857-1866` | Вызовы во время load до инициализации | Null-guards на `m_parent_hud_item` |
| `player_hud.cpp` load (on_outfit_changed) | `src/xrGame/player_hud.cpp:2054-2078` | Та же причина | Null-guard |
| `CHudItem::OnStateSwitch` — `g_player_hud` null | `src/xrGame/HudItem.cpp:210` | `GE_WPN_STATE_CHANGE` для оружия/PDA срабатывает при загрузке уровня до инициализации `g_player_hud` | `if (g_player_hud)` guard |
| `player_hud.cpp` anim_play | `src/xrGame/player_hud.cpp:680` | `IsGameTypeSingle()` | `IsGameTypeSingleOrCoop()` |
| `ActorHelmet` ReloadBonesProtection / net_Spawn / AddBonesProtection | `src/xrGame/ActorHelmet.cpp:73,82,222` | Та же причина | `IsGameTypeSingleOrCoop()` |

#### UI

| Краш | Файл:строка | Причина | Фикс |
|------|-------------|---------|------|
| `CUIRankingWnd::Show/Update` | `src/xrGame/ui/UIRankingWnd.cpp:58-79` | Вызов `pda.get_stat()` до `db.add_actor()` в Lua | `if (!g_actor) return` |
| `UIPdaWnd` — null sub-panels | `src/xrGame/ui/UIPdaWnd.cpp:95` | `pUITaskWnd`/`pUIRankingWnd`/`pUILogsWnd` создавались только в `IsGameTypeSingle()` | `IsGameTypeSingleOrCoop()` + null-guards на 7 сайтах вызова |
| `UIAchievements.cpp` R_ASSERT | `src/xrGame/ui/UIAchievements.cpp:45` | `R_ASSERT(ai().script_engine().functor(m_functor_str, f))` при отсутствии Lua-функтора | Safe if-check + warning log |
| `UIMainIngameWnd` null CurrentEntity | `src/xrGame/ui/UIMainIngameWnd.cpp:289,364` | MP-блоки триггерились в coop | `IsGameTypeSingleOrCoop()` |

#### Lua / C++ граница

| Краш | Файл:строка | Причина | Фикс |
|------|-------------|---------|------|
| `luabind::functor<void>` деструктор после `net_Spawn` | `src/xrGame/GameObject.cpp:471-494` | Деструктор выполнялся ПОСЛЕ `CScriptBinder::net_Spawn` (который уже обнулил Lua-ссылку), вызывая `lua_unref` на невалидный ref | `funct` переенесён во внутренний scope ДО вызова `CScriptBinder::net_Spawn` |
| Та же проблема в `net_Destroy` | `src/xrGame/GameObject.cpp:123-133` | Та же причина | Та же схема |

### 2.2 Lua краши (`db.actor nil`)

Паттерн: колбэки, которые могут сработать до `actor_binder:net_spawn` → `db.add_actor()`.

| Файл | Функция / строка | Фикс |
|------|-----------------|------|
| `gamedata/scripts/itms_manager.script:512` | `save_state` | `if not db.actor then return end` |
| `gamedata/scripts/gameplay_radioactive_water.script:7,16` | `actor_on_footstep`, `actor_on_update` | `if not db.actor then return end` |
| `gamedata/scripts/sim_squad_bounty.script:31,123,168` | TimeEvent таймеры | `if not db.actor then return [end/false]` |
| `gamedata/scripts/aaaa_script_fixes_mp.script:585,722` | MP-фиксы без guard | `if not db.actor then return end` |
| `gamedata/scripts/pda.script:172` | `get_stat(18)` | `if not db.actor then return "" end` |
| `gamedata/scripts/pda.script:580,582,610,619,649` | `coc_rankings_set_icon`, `coc_rankings_set_description`, `discover_spots`, `set_active_subdialog` | nil-guard на `db.actor` |
| `gamedata/scripts/ranks.script:106` | `get_obj_rank_name(nil)` | `if not db.actor then return 0 end` |

---

## 3. Диагностическое логирование (что сейчас активно)

| Паттерн в логе | Файл | Назначение |
|----------------|------|-----------|
| `[coop] ProcessGameEvents M_SPAWN: section=X obj_id=Y parent_id=Z` | `src/xrGame/Level.cpp:867` | Трекинг всех спавнов объектов |
| `[coop] ProcessGameEvents M_EVENT: event_type=T destination_id=D` | `src/xrGame/Level.cpp:880` | Трекинг всех игровых событий |
| `[coop] ProcessGameEvents M_EVENT: cl_Process_Event returned type=T dest=D` | `src/xrGame/Level.cpp:883` | Подтверждение обработки события |
| `[coop] ProcessGameEvents M_GAMEMESSAGE` | `src/xrGame/Level.cpp:924` | Системные сообщения |
| `[coop] CLevelChanger::net_Spawn: ...` | `src/xrGame/level_changer.cpp:59-110` | Детальный трекинг спавна level_changer |
| `[coop] ALife switch_online/offline: [name][section][ID]` | `src/xrGame/alife_switch_manager.cpp:114,126` | ALife онлайн/офлайн переключения |
| `! [coop] TranslateGameMessage: unknown type=N` | `src/xrGame/game_cl_base.cpp:287` | Неизвестные игровые сообщения |
| `[coop] OnGameMessage: msg=N` | `src/xrGame/game_cl_base.cpp:301` | Трекинг всех OnGameMessage |

**ВАЖНО:** Логирование по одной строке на кадр (A–G в `Level.cpp`,
D1–D3 в `IGame_Level.cpp`) было **удалено** — порождало 500+ строк/сек и
замораживало экран загрузки. Не добавлять обратно. Использовать только
событийные логи.

M_SPAWN лог использует `r_tell()`/`r_seek()` для peek пакета без смещения
указателя чтения — чтение данных без потребления.

---

## 4. Текущее состояние тестирования

### Что работает ✅
- `coop_host` запускает coop-сессию из главного меню
- ALife-симулятор инициализируется
- Уровень загружается полностью (все prefetch-краши исправлены)
- Актор спавнится, игрок может двигаться
- HUD работает корректно (PDA, ранг, оружие)
- NPC спавнятся и работают
- Игрок может находиться на уровне продолжительное время без крашей

---

## 5. Что нужно сделать до замены DirectPlay 8

### Блокер: исправить краш при level transition

Без этого нет смысла менять транспорт — краш воспроизводится независимо
от сетевого слоя.

**После исправления краша:**
1. Убедиться, что `coop_host` стабильно проходит хотя бы один переход
   между локациями
2. Это создаёт надёжный базовый уровень для регрессии после замены транспорта

### Что НЕ нужно делать ДО замены DirectPlay 8
- Полная синхронизация второго игрока
- ACK-протокол смены уровня
- `g_all_actors` registry
- Полная игровая механика (квесты, торговля)

---

## 6. Ключевые паттерны для будущих фиксов

### Паттерн A — SP-parity (C++)
```cpp
// Было:
if (IsGameTypeSingle()) { ... }
// Стало:
if (IsGameTypeSingleOrCoop()) { ... }
```
Применять везде, где SP-поведение нужно и в coop.

### Паттерн B — Lua nil actor guard
```lua
-- В начале любого колбэка, который может сработать до db.add_actor():
if not db.actor then return end
-- Или с возвращаемым значением:
if not db.actor then return "" end
if not db.actor then return false end
```

### Паттерн C — C++ null HUD guard
```cpp
// Для g_player_hud:
if (!g_player_hud) return;

// Для m_parent_hud_item в attachable_hud_item:
if (!m_parent_hud_item) return;
```

### Паттерн D — luabind functor scope (критично для net_Spawn/Destroy)
```cpp
// НЕПРАВИЛЬНО — деструктор functor после CScriptBinder-вызова:
luabind::functor<void> funct;
if (...functor("callback", funct)) {
    CScriptBinder::net_Spawn(DC);  // Lua выполнен
}
// funct уничтожается здесь → lua_unref на невалидный ref → CRASH

// ПРАВИЛЬНО — funct уничтожается ДО CScriptBinder:
{
    luabind::functor<void> funct;
    if (...functor("callback", funct))
        funct(args...);
}  // funct уничтожен здесь, Lua ref ещё валиден
CScriptBinder::net_Spawn(DC);
```

---

## 7. Архитектура: как direct-connect обходит DirectPlay (для справки)

```
psNET_direct_connect = TRUE  (SP и coop-host в одном процессе)
  → IPureServer и IPureClient в одном процессе
  → все пакеты передаются прямыми вызовами функций, минуя сеть
  → IDirectPlay8Client НЕ создаётся

psNET_direct_connect = FALSE  (нужен для второго игрока с другой машины)
  → NET_Client::Connect() создаёт IDirectPlay8Client через CoCreateInstance
  → реальные DirectPlay8 сокеты
```

На Wine: `DirectPlay8::Host()` может возвращать `E_NOTIMPL`. Это уже
обработано: только `DPNERR_ADDRESSING` трактуется как «порт занят» → retry.

---

## 8. Roadmap

```
[DONE]  Phase 1: Scaffolding
          eGameIDCooperative, game_sv/cl_Coop, coop_host, coop_connect

[DONE]  Phase 1.5: Host Stability (итеративные null/nil фиксы)
          Все C++ и Lua краши во время одиночной coop-сессии

[WIP]   Phase 1.6: Level Transition Fix
          Краш при переходе между локациями

[NEXT]  Phase 2: Transport Replacement (DirectPlay → ENet)
          Замена src/xrNetServer/, тест coop_connect с двух машин

[FUTURE] Phase 3: State Synchronization
          g_all_actors, ALife онлайн-зона для всех игроков,
          ACK-протокол смены уровня, синхронизация позиций
```

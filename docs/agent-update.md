# Coop Implementation — Knowledge Base Update

> Полная база знаний по всей выполненной работе. Предназначена для обновления
> файла агента `.github/agents/coop-developer.agent.md`.

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

### Текущий блокер ⚠️
**Краш при переходе между локациями (level transition)**

При подходе к level_changer (переход на k00_marsh) игра падает. Краш
происходит **после** успешной обработки всех M_SPAWN и M_EVENT для новой
локации. Адрес краша: `0x00000001403A3BBA` (нет символов).

**Последние строки перед крашем:**
```
[coop] CLevelChanger::net_Spawn: done bOk=1
[coop] ProcessGameEvents M_SPAWN: ... (сотни объектов k00_marsh)
[coop] ProcessGameEvents M_EVENT: event_type=19 destination_id=22754
[coop] ProcessGameEvents M_EVENT: cl_Process_Event returned type=19 dest=22754
stack trace:
  at address 0x00000001403A3BBA
```

**Вероятные причины** (в порядке убывания вероятности):
1. `actor_binder:net_spawn` на новой локации — `db.actor` из старой локации
   ещё жив или уже nil, а код ожидает конкретное состояние
2. `level.script:on_game_start()` — колбэк уровня вызывается до того, как
   `db.add_actor()` отработал
3. Пропущенный `IsGameTypeSingleOrCoop()` guard в `CLevel::ChangeLevel()` или
   `game_cl_single` при переключении уровня
4. Глобальное Lua-состояние с ссылками на C++-объекты первой локации,
   которые уже уничтожены

**Как диагностировать:**
```lua
-- В actor_binder.script, начало net_spawn:
Msg("[coop] actor_binder:net_spawn called, db.actor=%s", tostring(db.actor))

-- В level.script или bind_level.script:
Msg("[coop] level on_game_start called")
```

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

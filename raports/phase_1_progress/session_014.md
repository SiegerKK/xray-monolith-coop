# Session 014 — Server-side Log Analysis & Three Pre-emptive Fixes

**Date:** 2026-02-23  
**Status:** Fixes committed, awaiting new build test  
**Branch:** copilot/index-project-architecture

---

## Входящие логи от пользователя

Пользователь предоставил два дополнительных серверных лога для предыдущей проблемы (сессия 013 — клиент не мог подключиться).

### Лог 1 — Lua error при загрузке сервера
```
! [LUA] z:/mnt/data/anomaly\gamedata\scripts\coop_dev_ui.script:148:
  attempt to call global 'Msg' (a nil value)
! [SCRIPT ERROR]: coop_dev_ui.script:148: attempt to call global 'Msg' (a nil value)
! [ERROR] --- Failed to load script coop_dev_ui
```

### Лог 2 — Нормальная работа сервера (рукопожатие завершилось)
```
[NET] Server started | level='fake_start' port=0 psNET_direct_connect=1
[COOP][SV] Transport peer connected | client_id=0x00000001
...
[COOP][CL] Handshake complete — in session | player_id=1
```

---

## Анализ

### 1. `Msg` is nil in Anomaly Lua (Bug — FIXED)

**Причина:** В оригинальном X-Ray движке функция `Msg()` регистрируется в Lua прямо как
глобальная `Msg`. В **Anomaly 1.5.x** эта функция зарегистрирована под именем `printf`
(маппинг C++ `Msg()` → Lua `printf`). Глобальная `Msg` не определена → nil → fatal error
при загрузке скрипта через `on_game_start`.

**Файл:** `gamedata/scripts/coop_dev_ui.script` строки 148 и 150  
**Правило Lua:** Всегда использовать `printf(...)` вместо `Msg(...)` в Anomaly-скриптах.  
**Фикс:** `Msg(...)` → `printf(...)` в обоих местах.

```
-- до
Msg("[COOP] coop_dev_ui registered in ImGui.Groups.Mods")
-- после
printf("[COOP] coop_dev_ui registered in ImGui.Groups.Mods")
```

---

### 2. `port=0 psNET_direct_connect=1` в серверном логе (Старый баг — уже исправлен в сессии 012)

**Это лог предыдущей сборки (до сессии 012/013).**

В старой сборке сервер запускался с `psNET_direct_connect=1` потому что в сессии 012 мы
ещё не удалили `/coop` из условия `if (strstr(options, "/single") || strstr(options, "/coop"))`.

В текущей сборке:
- `NET_Server.cpp:250` — только `"/single"` устанавливает `psNET_direct_connect=TRUE`
- Для coop options `"l01_escape/coop/alife/new"` — `/single` не найден → `psNET_direct_connect=FALSE`
- Сервер запускается на TCP/IP порту 1235 → `GetPort()` = 1235
- Логи покажут: `port=1235 psNET_direct_connect=0`

---

### 3. `level='fake_start'` в логе сервера (NOT a bug — пояснение)

Это нормальное поведение ALife. После `game->Create()`:
- ALife инициализируется и регистрирует `fake_start` как текущий уровень
- `game_sv_Single::level_name()` → `alife().level_name()` → `"fake_start"`
- `map_data.m_name` в `net_start2()` становится `"fake_start"` — только для логов

Важно: `game_descr.map_name` (отправляется клиентам) устанавливается **ДО** `game->Create()`:
```cpp
// xrServer_Connect.cpp:82 — ДО вызова game->Create()
xr_strcpy(game_descr.map_name, game->level_name(session_name.c_str()).c_str());
// ALife ещё не инициализирован → parse_level_name() → "l01_escape" ✓
game->Create(session_name);  // ALife инициализируется → current level = fake_start
```

Клиент получает `"l01_escape"` в `GameDescriptionData.map_name` → загружает правильный уровень. ✓

---

### 4. Превентивный фикс — `/coop` флаг для локального клиента хоста

**Добавлено:** `console_commands.cpp` — `cl_opts = "localhost/coop"` вместо `"localhost"`.

Это обеспечивает:
1. Явную идентификацию в логах (видно что это coop-клиент)
2. Консистентность с `coop_connect` (оба клиента имеют `/coop` флаг)
3. Явное использование пути direct-TCP/IP в `NET_Client.cpp` (хотя `localhost` уже
   детектируется через `stricmp`, наличие `/coop` делает код надёжнее)

---

### 5. Превентивный фикс — пропуск `FS.auth_generate` для coop

**Файл:** `Level_network.cpp` — `Connect2Server()`

```cpp
// до
if (!psNET_direct_connect) { FS.auth_generate(...); }

// после
if (!psNET_direct_connect && !IsGameTypeSingle()) { FS.auth_generate(...); }
```

Для coop (`IsGameTypeSingle()=TRUE` благодаря алиасу GameID):
- Сервер НЕ вызывает `auth_generate` (сессия 008: пропуск в `xrServer_Connect.cpp`)
- Клиент также НЕ должен вызывать `auth_generate` — hash всё равно не проверяется
- `NeedToCheckClient_BuildVersion()` возвращает `false` для coop → auth challenge пропускается
- Убирает ненужный overhead при каждом подключении coop-клиента

---

## Файлы изменены в этой сессии

| Файл | Тип | Описание |
|------|-----|----------|
| `gamedata/scripts/coop_dev_ui.script` | Lua | `Msg` → `printf` (строки 148, 150) |
| `src/xrGame/console_commands.cpp` | C++ | `cl_opts = "localhost/coop"` |
| `src/xrGame/Level_network.cpp` | C++ | `FS.auth_generate` skip для coop |
| `raports/phase_1_progress/session_014.md` | Docs | этот отчёт |

---

## Итоговый анализ логов — общая диагностика

**Что работало корректно (по логам):**
- ✅ `[COOP][SV] Host started` — сессия создана
- ✅ `[LSS] Spawning object [fake_actor]` — ALife нормально инициализирован
- ✅ Полный handshake CL_HELLO → SV_HELLO_ACK → CL_JOIN_REQUEST → SV_JOIN_ACCEPT
- ✅ `Handshake complete — in session | player_id=1` — протокол работает

**Что было сломано (по логам):**
- ❌ `psNET_direct_connect=1, port=0` — старый баг, исправлен в сессии 012/013
- ❌ `coop_dev_ui.script:148: Msg is nil` — исправлен в этой сессии (session 014)
- ❌ `FS.auth_generate` вызывался без нужды — исправлен в этой сессии

---

## Следующие шаги (сессия 015)

- Тест новой сборки с исправлением Lua-ошибки
- Убедиться что `coop_host` загружает уровень без ошибок Lua
- Тест `coop_connect <IP>` с удалённой машины — подключение + handshake
- После подключения: проверить загрузку уровня на клиентской машине
- Phase 1.D: синхронизация позиции актора между хостом и клиентом

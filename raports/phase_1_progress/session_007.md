# Session 007 — Диагностика silent return-to-menu + fix psNET_direct_connect

**Дата:** 2026-02-22  
**Ветка:** copilot/index-project-architecture  
**Статус:** FIXES APPLIED

---

## Симптом

После фиксов session 006 (Lua crash устранён), при вводе `coop_host l01_escape` происходит следующее:
- Загрузочный экран появляется буквально на долю секунды
- Игра молча возвращается в главное меню
- Никаких crash-диалогов
- Логов пользователь найти не может

---

## Диагностика лога

**Где находится лог:**
- Файл: `<game_dir>\logs\<exe_name>_<windows_username>.log`
- Пример для Anomaly DX11 AVX: `z:\mnt\data\anomaly\logs\anomalydx11avx_USERNAME.log`
- Начиная с этой сессии, команда `coop_host` при каждом запуске печатает полный путь к лог-файлу в консоль

---

## Root-cause анализ

### Цепочка вызовов при `coop_host l01_escape`

```
Engine.Event.Defer("KERNEL:start", "l01_escape/coop", "localhost")
  → CLevel::net_Start("l01_escape/coop", "localhost")
  → IGame_Persistent::params::parse_cmd_line("l01_escape/coop")
      m_game_or_spawn = "l01_escape"
      m_game_type     = "coop"
      m_alife         = ""
  → net_start1() → xrServer created (OK), Level_ID("l01_escape",...) called
  → net_start2() → xrServer::Connect("l01_escape/coop", ...) called
      → IPureServer::Connect("l01_escape/coop", ...)
```

### Критическая точка: `IPureServer::Connect()` в `NET_Server.cpp:250`

```cpp
// ДО ФИКСА:
if (strstr(options, "/single"))
    psNET_direct_connect = TRUE;

// Для "l01_escape/coop":
//   strstr(..., "/single") = NULL → psNET_direct_connect остаётся FALSE!
```

**Последствие `psNET_direct_connect = FALSE`:**

| Что происходит | Описание |
|---|---|
| Создаётся реальный DirectPlay8 TCP/IP сервер | `NET->Host(...)` на порту 2302+ |
| Клиент подключается через DirectPlay8 | `NET->Connect(...)` на "localhost:2302" |
| `AttachNewClient` → `NeedToCheckClient_GameSpy_CDKey` = false | OK |
| `Check_GameSpy_CDKey_Success` → `NeedToCheckClient_BuildVersion` | ← **Проблема** |
| Сервер посылает `M_AUTH_CHALLENGE` | Клиент должен ответить хэшем FS |
| `FS.auth_get()` (сервер) = **0** | Session 004 fix пропустил `FS.auth_generate` для coop |
| `FS.auth_get()` (клиент) = реальный хэш | `Level_network.cpp:359` — клиент вызывает `FS.auth_generate` |
| **0 ≠ реальный_хэш → "Data verification failed. Cheater?"** | Client disconnected |
| `m_bConnectResult = false` → `Connect2Server` returns FALSE | `connected_to_server = FALSE` |
| `net_start_client6` → `net_start_result_total = FALSE` | Возврат в главное меню |

---

## Применённые фиксы

### Fix 1: `src/xrNetServer/NET_Server.cpp` — 1 строка

```cpp
// ДО:
if (strstr(options, "/single"))
    psNET_direct_connect = TRUE;

// ПОСЛЕ:
if (strstr(options, "/single") || strstr(options, "/coop"))
    psNET_direct_connect = TRUE;
```

**Эффект**: Для `/coop` включается режим direct-loopback (как у single player):
- Никакого реального сетевого сокета
- Никакой DirectPlay8 auth
- `Server->create_direct_client()` → прямое подключение в памяти
- Весь flow аналогичен single player

### Fix 2: `src/xrGame/Level_network_start_client.cpp`

```cpp
// ДО:
if (game->Type() != eGameIDSingle)
    m_file_transfer = xr_new<file_transfer::client_site>();

// ПОСЛЕ:
if (game->Type() != eGameIDSingle && game->Type() != eGameIDCoop)
    m_file_transfer = xr_new<file_transfer::client_site>();
```

**Эффект**: Нет попытки создать MP file transfer для coop (у нас нет `m_file_transfers` на сервере).

### Fix 3: `src/xrGame/console_commands.cpp` — CCC_CoopHost

- Добавлен вывод полного пути к лог-файлу при каждом `coop_host`
- Добавлен вывод `sv_opts` для диагностики

---

## Архитектурная заметка: Phase 1 vs Phase 2

| Аспект | Phase 1 (сейчас) | Phase 2 (будущее) |
|---|---|---|
| `psNET_direct_connect` | TRUE — прямой loopback | FALSE — реальная сеть для удалённых игроков |
| Макс. игроков | 1 (хост = клиент) | N (хост + удалённые) |
| Аутентификация | Нет | Собственная COOP auth (не GameSpy) |
| Транспорт | В памяти | DirectPlay8 / UDP |

---

## Ожидаемый результат после фиксов

После билда и запуска `coop_host l01_escape`:
1. Консоль выводит: `[COOP] Log file: ...\logs\anomalydx11avx_USERNAME.log`
2. Консоль выводит: `[COOP] Hosting level: 'l01_escape' | sv_opts='l01_escape/coop'`
3. Загрузочный экран появляется
4. `[COOP][SV] game_sv_Coop created`
5. `[COOP][CL] game_cl_Coop created`
6. `[COOP][SV] Host started | session_id=...`
7. Уровень загружается — игрок в игре

---

## Следующие шаги (Session 008)

- [ ] Проверить что загрузка уровня проходит полностью
- [ ] Если падает при спавне актора — исследовать `game_sv_Coop::OnPlayerConnect` flow
- [ ] `IsGameTypeSingle()` возвращает `false` для coop → возможны проблемы с ActorCondition, HUD
- [ ] Добавить `IsGameTypeCoop()` inline и постепенно адаптировать Actor-код

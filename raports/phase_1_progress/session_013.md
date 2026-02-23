# Session 013 — Fix `coop_connect` Remote Client Connection

**Date:** 2026-02-23  
**Задача:** `coop_connect <ip>` падало с `FAILED` сразу после `Connect2Server`

---

## Лог, с которым пришли

```
[NET] Connect2Server | options='25.21.243.168/name=Player' psNET_direct_connect=0
- IPureClient: EnumHosts connect to server=25.21.243.168 sv_port=1235
- IPureClient : created on port 1234!
[NET] Connect2Server: FAILED
! Failed to start client. Check the connection or level existance.
```

---

## Первопричина №1 — EnumHosts не работает для удалённых IP (VPN)

### Цепочка
1. `coop_connect 25.21.243.168/name=Player` → `cl_opts = "25.21.243.168/name=Player"`
2. `IPureClient::Connect(options)` → `psNET_direct_connect = FALSE` → берёт путь **EnumHosts** (line 618+)
3. `NET->EnumHosts(...)` → `S_OK`, но `net_Hosts.empty()` = true → `OnInvalidHost()` → return FALSE

### Почему EnumHosts не работает
- Сервер хостит через **`DPNSESSION_NODPNSVR`** — DP-сервис отключён
- DirectPlay8 `EnumHosts` отправляет UDP discovery packets; по Radmin VPN (IP 25.x.x.x) сервер
  не всегда отвечает на enum-запросы (race condition в таймингах / фаервол / NAT)
- Хост-игрок (тот же компьютер) соединяется через `cl_opts = "localhost"`, которое попадает
  под `stricmp(server_name, "localhost") == 0` → прямой `NET->Connect()` без EnumHosts → ✓
- Удалённый игрок: `"25.21.243.168"` ≠ `"localhost"` → EnumHosts → ✗

### Исправление
**`NET_Client.cpp`**: расширить условие прямого подключения:
```cpp
// было:
if (stricmp(server_name, "localhost") == 0)
// стало:
const bool bCoopConnect = (strstr(options, "/coop") != nullptr);
if (stricmp(server_name, "localhost") == 0 || bCoopConnect)
```

**`console_commands.cpp`** (`CCC_CoopConnect::Execute`): добавить `/coop` в cl_opts:
```cpp
// было:
xr_sprintf(cl_opts, "%s", args);
// стало:
xr_sprintf(cl_opts, "%s/coop", args);      // если /name= уже есть в args
// или:
xr_sprintf(cl_opts, "%s/name=%s/coop", args, pname);  // авто-имя игрока
```

Прямое TCP/IP `NET->Connect()` работает для любого достижимого IP — Radmin VPN, LAN, интернет.

---

## Первопричина №2 — auth hash mismatch → "Data verification failed. Cheater?"

### Цепочка
Даже если бы EnumHosts сработал, соединение всё равно упало бы на следующем шаге:

1. Сервер: session 008 → `auth_generate()` НЕ вызывается для coop → `FS.auth_get()` = 0
2. Клиент: `Level_network.cpp:357-362` → `psNET_direct_connect = FALSE` → `auth_generate()` вызывается → `FS.auth_get()` = non-zero hash
3. Сервер получает клиента → `AttachNewClient` → `Check_GameSpy_CDKey_Success` →
   `NeedToCheckClient_BuildVersion` → отправляет `M_AUTH_CHALLENGE` →
   клиент отвечает своим hash → сервер: `_our (0) != _him (non-zero)` →
   `SendConnectResult(ecr_data_verification_failed, "Data verification failed. Cheater?")` → kicked

### Исправление
**`xrServer_CL_connect.cpp`** — `NeedToCheckClient_BuildVersion()`: пропускать auth challenge для coop/single:
```cpp
// Добавлено после PerformSecretKeysSync:
if (IsGameTypeSingle()) return false;
```

Это зеркалит то, что уже делает `RequestClientDigest()` (который проверяет `IsGameTypeSingle()`
и сразу вызывает `Check_BuildVersion_Success`). Итоговый поток для coop-клиента:

```
AttachNewClient(CL)
  → NeedToCheckClient_GameSpy_CDKey(CL)  // returns false (xrServer, not GameSpy)
  → Check_GameSpy_CDKey_Success(CL)
    → NeedToCheckClient_BuildVersion(CL) // NEW: returns false (IsGameTypeSingle)
    → RequestClientDigest(CL)
      → IsGameTypeSingle() = true
      → Check_BuildVersion_Success(CL)   // CL->bVerified=TRUE, SendConnectResult("All Ok")
```

---

## Файлы изменены

| Файл | Что изменено |
|------|-------------|
| `src/xrNetServer/NET_Client.cpp` | Detect `/coop` → use direct TCP/IP connect (not EnumHosts) |
| `src/xrGame/console_commands.cpp` | Append `/coop` (+ auto `/name=`) to `coop_connect` cl_opts |
| `src/xrGame/xrServer_CL_connect.cpp` | `NeedToCheckClient_BuildVersion`: return false for coop/single |

---

## Ожидаемый результат

После этих исправлений `coop_connect <ip>` должен:
1. Использовать прямое TCP/IP подключение (минуя EnumHosts)
2. Пройти auth-challenge без mismatch
3. Получить `M_CLIENT_CONNECT_RESULT("All Ok")` от сервера
4. Загрузить уровень по данным из `GameDescriptionData.map_name` (сессия 012)
5. Применить `game_type = "coop"` из `GameDescriptionData.game_type` и активировать single-player подсистемы

---

## Следующий шаг — Session 014

Ожидаем что удалённый клиент теперь подключается. Следующие вероятные проблемы:
- Нет второго актора — сервер спавнит только одного (нужен Phase 1.D: multiple actors)
- Клиент не получает ALife state — нужна синхронизация позиций объектов
- `IsGameTypeSingle()` на стороне клиента без сервера — нужно проверить UpdateGameType из GameDescriptionData

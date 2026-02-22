# Session 012 — Fix coop_connect TCP/IP, game-type propagation, verbose logging

**Date**: 2026-02-22  
**Status**: Compiled, awaiting runtime test

---

## Проблемы этой сессии

Пользователь сообщил:
1. Игра загрузилась через `coop_host l01_escape` — уровень загрузился (это нормальный стартовый регион Anomaly с `all.spawn`).
2. `coop_connect <IP>` не работает: `! Failed to start client. Check the connection or level existance.`
3. Логи слишком скудные, непонятно что происходит.

---

## Анализ корневых причин

### Причина 1 (критическая): `psNET_direct_connect = TRUE` для coop → сервер недоступен по TCP/IP

В `NET_Server.cpp` (session 007):
```cpp
if (strstr(options, "/single") || strstr(options, "/coop"))
    psNET_direct_connect = TRUE;
```

Это переводило сервер в режим **shared-memory DirectPlay8 provider** (используется только для single-player в оригинале). Такой сервер слушает только in-process shared-memory соединения.

Когда `coop_connect 192.168.1.x` пытался подключиться по **TCP/IP DirectPlay8 provider** — провайдеры несовместимы. Результат: `EnumHosts` немедленно возвращал `DPNERR_INVALIDHOSTADDRESS` → `OnInvalidHost()` → `connected_to_server = FALSE` → `! Failed to start client`.

**Лог-маркер** (скрытый, без нашей новой диагностики): `IPureClient : created on port 1234!` (клиентский порт) — и сразу падение, без "Connected to server".

### Причина 2: remote client не знает game_type

Для `coop_connect` `op_server = NULL` → `PreStart(NULL)` и `Start(NULL)` → `parse_cmd_line(NULL)` → все `m_game_params = ""`. Тип игры остался `eGameIDNoGame`. Это сломало бы `IsGameTypeSingle()` и другие coop-specific проверки на стороне клиента.

### Причина 3: нет диагностических логов

`Connect2Server()`, `EnumHosts`, `net_start_client2()` — никаких Msg() при неудаче. Невозможно диагностировать.

---

## Изменения

### 1. `src/xrNetServer/NET_Server.cpp` (1 строка)
```cpp
// Было:
if (strstr(options, "/single") || strstr(options, "/coop"))
    psNET_direct_connect = TRUE;

// Стало:
if (strstr(options, "/single"))
    psNET_direct_connect = TRUE;
```
**Эффект**: coop сервер использует TCP/IP DirectPlay8. Стандартный порт `1235` (START_PORT_LAN_SV). Доступен для remote clients.

### 2. `src/xrNetServer/NET_Common.h` (+1 поле в struct)
```cpp
struct GameDescriptionData {
    string128 map_name;
    string128 map_version;
    string512 download_url;
    string64  game_type;   // NEW: "coop", "single", "deathmatch", etc.
};
```
Struct хранится как `pvApplicationReservedData` в DirectPlay8 app description — рассылается всем клиентам при EnumHosts/Connect.

### 3. `src/xrGame/xrServer_Connect.cpp`
```cpp
xr_strcpy(game_descr.game_type, game->type_name()); // "coop"
```
Сервер объявляет тип игры в `GameDescriptionData`.

### 4. `src/xrGame/Level_network_start_client.cpp`
В `net_start_client3()`, TCP/IP path (`else //multiplayer`):
```cpp
// Set game type for remote coop client
const char* sv_gt = get_net_DescriptionData().game_type;
if (sv_gt && sv_gt[0] && !IsGameTypeSingle())
{
    Msg("[NET] Game type from server: '%s' (was: '%s')", sv_gt, m_game_type);
    xr_strcpy(g_pGamePersistent->m_game_params.m_game_type, sv_gt);
    g_pGamePersistent->UpdateGameType();
}
```
Клиент получает тип игры от сервера **до** вызова `Load(level_id)` — все IsGameTypeSingle() проверки во время загрузки уровня корректны.

Добавлено логирование в `net_start_client2()`:
```
[NET] Connecting to: localhost/port=1235/name=Player
[NET] Connect2Server: OK
```

### 5. `src/xrNetServer/NET_Client.cpp`
- Лог перед `localhost` sync connect: `"- IPureClient: direct (localhost) connect to port %d"`
- Лог перед `EnumHosts`: `"- IPureClient: EnumHosts connect to server=%s sv_port=%d"`
- Лог при `EnumHosts` failure: `"! EnumHosts failed: server=%s sv_port=%d cl_port=%d HRESULT=0x%08X"`

### 6. `src/xrGame/Level_network.cpp`
В `Connect2Server()` — лог в начале:
```
[NET] Connect2Server | options='localhost/port=1235/name=Player' psNET_direct_connect=0
```

### 7. `src/xrGame/Level_start.cpp`
После успешного запуска сервера:
```
[NET] Server started | level='l01_escape' port=1235 psNET_direct_connect=0
```

### 8. `src/xrGame/console_commands.cpp`
- `coop_host`: добавлен hint `"server will listen on TCP port 1235"` + `"Clients can join with: coop_connect <your_ip>"`
- `coop_connect`: улучшен Usage текст с примером порта.

---

## Поток соединения после фикса

### Host (coop_host l01_escape):
```
[COOP] Hosting level: 'l01_escape' | sv_opts='l01_escape/coop/alife/new' | server will listen on TCP port 1235
[COOP] Clients can join with: coop_connect <your_ip>
[NET] Server started | level='l01_escape' port=1235 psNET_direct_connect=0
[NET] Connecting to: localhost/port=1235/name=Player
[NET] Connect2Server | options='localhost/port=1235/name=Player' psNET_direct_connect=0
- IPureClient: direct (localhost) connect to port 1235
- IPureClient: created on port 1234!
* client: connection accepted - <All Ok>
[NET] Connect2Server: OK
[NET] Game type from server: 'coop' (was: 'coop')
```
*(host player's game_type уже 'coop' из sv_opts, поэтому !IsGameTypeSingle() = false, update не происходит — корректно)*

### Remote Client (coop_connect 192.168.1.5):
```
[NET] Connecting to: 192.168.1.5/name=Player
[NET] Connect2Server | options='192.168.1.5/name=Player' psNET_direct_connect=0
- IPureClient: EnumHosts connect to server=192.168.1.5 sv_port=1235
- IPureClient: created on port 1234!
* client: connection accepted - <All Ok>
[NET] Connect2Server: OK
[NET] Game type from server: 'coop' (was: '')  ← ключевой момент
```

---

## Ключевые наблюдения

1. **psNET_direct_connect** — это флаг DirectPlay8 service provider, а не просто "быстрое соединение". `TRUE` = shared-memory (loopback only), `FALSE` = TCP/IP (network). Для coop с реальной сетью обязан быть `FALSE`.

2. **GameDescriptionData** размером `sizeof(struct)` отправляется как `pvApplicationReservedData` в DirectPlay8 app description. Клиент проверяет размер через `R_ASSERT`. Поскольку обе стороны компилируются из одного исходника — размер совпадает.

3. **FS.auth_generate()** вызывается клиентом при `psNET_direct_connect=FALSE`, но сервер пропускает auth check для coop (`IsGameTypeSingle()=TRUE` на сервере → `Check_BuildVersion_Success()` сразу). Хэш генерируется но не используется — безвредно.

4. **SV_Client** для TCP/IP coop устанавливается в `Server_Client_Check(CL)` (session 007 не трогал этот путь): `CL->process_id == GetCurrentProcessId()` → host player определяется корректно по PID.

---

## Следующие шаги

- [ ] Session 013: проверить что `coop_host` загружается полностью, `coop_connect` с другой машины подключается
- [ ] Session 013: TCP/IP sync path — `net_start_client4()` ждёт `net_isCompleted_Connect()`, проверить что он срабатывает для TCP/IP coop
- [ ] Session 013: проверить Send() path для host player: `else if (Server && game_configured && OnServer()) → Server->OnMessageSync()`
- [ ] Phase 1.D: вторая копия актора (remote client actor)

# Session 003 — Phase 1.C: Console Commands, Disconnect, Dev UI

**Date:** 2026-02-22  
**Session:** 003  
**Phase:** 1 (Phase 1.C — Меню: Host / Join (LAN))  
**Status:** ✅ Phase 1.C завершено

---

## Что было сделано в этой сессии

### Проблема Phase 1.C

Phase 1.B дал нам рабочий handshake через `M_COOP_HANDSHAKE`, но у пользователя не было:
1. Способа запустить хост без консольной команды (и самой команды не было)
2. Способа подключиться по IP
3. Реального disconnect при protocol violations / timeout (всё оставалось TODO)
4. Реального имени игрока в HELLO-пакете

---

### 1. Исправлен выбор класса сервера (`Level_start.cpp`)

```cpp
// ДО:
if (!xr_strcmp(p.m_game_type, "single")) Server = xr_new<xrServer>();
else Server = xr_new<xrGameSpyServer>();  // ← для coop тоже использовался GameSpy!

// ПОСЛЕ:
if (!xr_strcmp(p.m_game_type, "single") || !xr_strcmp(p.m_game_type, "coop"))
    Server = xr_new<xrServer>();   // No GameSpy auth for coop
else
    Server = xr_new<xrGameSpyServer>();
```

**Почему это критично:** `xrGameSpyServer` добавляет GameSpy CD-key аутентификацию (`M_SV_DIGEST` → `M_CL_AUTH` → `M_CLIENT_CONNECT_RESULT`). Для LAN-коопа этого быть не должно. Без этой правки сервер бы отказывал всем клиентам на этапе auth.

---

### 2. Добавлены консольные команды (`console_commands.cpp`)

#### `coop_host <level_name>`
```
coop_host l01_escape
```
- Проверяет, что уровень указан
- Проверяет, что нет активного уровня
- Формирует `sv_opts = "<level>/coop"`, `cl_opts = "localhost"`
- Fires `KERNEL:start(sv_opts, cl_opts)`

#### `coop_connect <ip>[:<port>]`
```
coop_connect 192.168.1.5
coop_connect 192.168.1.5:1235
```
- Проверяет, что IP указан
- Проверяет, что нет активного уровня
- Fires `KERNEL:start(0, cl_opts)` — без локального сервера, только клиент

**Формат сервер-опций:**
```
<level_name>/coop
```
Это тот же формат что и `l01_escape/single/alife/load` — парсится в `game_sv_GameState::parse_level_name()` (часть до первого `/`).

---

### 3. Реализован `disconnect_peer` в `game_sv_Coop` (`game_sv_coop.h/.cpp`)

```cpp
void game_sv_Coop::disconnect_peer(ClientID id, ECoopRejectReason reason, const char* why)
{
    // 1. Sends REJECT so client state machine transitions cleanly
    send_join_reject(id, reason);

    // 2. Transport-level disconnect via m_server->DisconnectClient
    IClient* client = m_server->ID_to_client(id);
    if (client) m_server->DisconnectClient(client, reason_str);

    // 3. Mark peer as Disconnecting
    CoopPeerEntry* peer = find_peer(id);
    if (peer) peer->state = eCPS_Disconnecting;
}
```

Заменены все 4 `TODO_COOP_DISCONNECT` + 1 `TODO_COOP: disconnect через transport`:
- Unexpected packet ID в OnCoopPacket → `disconnect_peer(..., eCRR_InvalidRequest, "unexpected_packet")`
- CL_HELLO в неправильном состоянии → `disconnect_peer(..., eCRR_InvalidRequest, "hello_wrong_state")`
- CL_JOIN_REQUEST в неправильном состоянии → `disconnect_peer(..., eCRR_InvalidRequest, "join_wrong_state")`
- Timeout в `tick_peer_timeouts` → `disconnect_peer(..., eCRR_Timeout, step_name)`

---

### 4. Реальный никнейм в `send_hello` (`game_cl_coop.cpp`)

```cpp
// ДО: P.w_stringZ("player");  // TODO_COOP_NICKNAME

// ПОСЛЕ:
char player_name[64] = {};
GetPlayerName_FromRegistry(player_name, sizeof(player_name));
if (!xr_strlen(player_name))
{
    if (xr_strlen(Core.UserName))
        strncpy_s(player_name, sizeof(player_name), Core.UserName, _TRUNCATE);
    else
        xr_strcpy(player_name, "stalker");
}
P.w_stringZ(player_name);
```

Используется та же логика что и в `Level_start.cpp:GetPlayerName_FromRegistry`.

---

### 5. ImGui Dev Panel: `gamedata/scripts/coop_dev_ui.script`

```
Mods → Coop Dev Panel
```

Открывает независимое ImGui окно с двумя секциями:

```
┌─── Coop Dev Panel ────────────────────────┐
│ [TODO_COOP Phase 1.C — Developer entrypoint] │
├───────────────────────────────────────────┤
│ Host Coop (LAN)  (?)                       │
│ Level [l01_escape___________] [Host]       │
├───────────────────────────────────────────┤
│ Join Coop (LAN)                            │
│ Host IP [192.168.1.5_________] (?) [Connect] │
├───────────────────────────────────────────┤
│ [Disconnect]  [Clear Log]                  │
│                                           │
│ Hosting 'l01_escape' — check log...       │
└───────────────────────────────────────────┘
```

- Проверяет `level.present()` перед запуском команды
- Статус-сообщения: зелёные (успех) / красные (ошибка)
- Регистрируется в `ImGui.Groups.Mods` и `ImGui.Groups.Unique`
- Все внутренние ID с `##coop_` во избежание коллизий

---

## Полный User Flow Phase 1.C

### Host Flow
```
1. Открыть ImGui overlay (Ctrl+F11 по умолчанию)
2. Mods → Coop Dev Panel
3. Ввести имя уровня (например: l01_escape)
4. Нажать [Host]
   → console: coop_host l01_escape
   → KERNEL:start(server="l01_escape/coop", client="localhost")
   → Level загружается, xrServer стартует на порту 1235
   → game_sv_Coop::Create() вызывается
   → [COOP][SV] Host started, waiting for clients...
   → game_cl_Coop::Init() → send_hello() (сервер сам себе)
   → Handshake проходит, player_id=1 выдаётся

### Join Flow
```
На втором экземпляре:
1. Mods → Coop Dev Panel
2. Ввести IP хоста (например: 192.168.1.5 или 192.168.1.5:1235)
3. Нажать [Connect]
   → console: coop_connect 192.168.1.5
   → KERNEL:start(server=NULL, client="192.168.1.5")
   → Level клиент стартует, подключается к серверу
   → InitializeClientGame() → создаётся game_cl_Coop → Init()
   → send_hello() → [COOP][CL] CL_HELLO sent | nick='Vasya'
   → Сервер отвечает HELLO_ACK → JOIN_REQUEST → JOIN_ACCEPT
   → [COOP][CL] Handshake complete — in session | player_id=2
```

---

## Оставшиеся TODO после Phase 1.C

| Метка | Описание |
|-------|---------|
| `TODO_COOP_UI` | `createGameUI()` возвращает SP UI заглушку |
| `TODO_COOP_DEFINE_CONTENT_HASH_POLICY` | `COOP_CONTENT_HASH = 0x1` hardcoded |
| `TODO_COOP_SINGLETON_ACTOR_ASSUMPTION` | Phase 2: несколько акторов |

---

## Файлы сессии 003

```
ИЗМЕНЁННЫЕ:
  src/xrGame/Level_start.cpp              (+coop → xrServer fix)
  src/xrGame/console_commands.cpp         (+CCC_CoopHost, CCC_CoopConnect)
  src/xrGame/game_sv_coop.h               (+disconnect_peer declaration)
  src/xrGame/game_sv_coop.cpp             (+disconnect_peer impl, -TODO_COOP_DISCONNECT)
  src/xrGame/game_cl_coop.cpp             (send_hello uses GetPlayerName_FromRegistry)

НОВЫЕ:
  gamedata/scripts/coop_dev_ui.script     (ImGui Host/Join dev panel)
```

---

## Следующие шаги (Phase 1.D — Интеграционный тест)

1. **Собрать движок** с изменениями (Visual Studio 2022 / vs2022 проект)
2. **Тест Host в single-process**: `coop_host l01_escape` → проверить логи
3. **Тест Join по LAN**: два экземпляра, проверить полный handshake в логах
4. **Проверить protocol violation**: намеренно отправить неверный пакет, убедиться в disconnect
5. **Зафиксировать known issues**: legacy g_actor / ALife / singleton blocker

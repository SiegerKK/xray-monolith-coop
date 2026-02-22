# Session 002 — Phase 1.B: Packet Routing Integration

**Date:** 2026-02-22  
**Session:** 002  
**Phase:** 1 (Phase 1.B — Интеграция маршрутизации пакетов)  
**Status:** ✅ Phase 1.B завершено

---

## Что было сделано в этой сессии

### Проблема, решённая в Phase 1.B

В Phase 1.A были созданы классы `game_sv_Coop` и `game_cl_Coop` со state machine и handshake-логикой, но **пакеты не имели пути** от клиента к серверу и обратно. Вызовы `TODO_COOP: отправить через transport` и `TODO_COOP: принять через transport` оставались заглушками.

### Изученная архитектура сетевого стека

| Компонент | Назначение |
|-----------|-----------|
| `xrServer::OnMessage(P, sender)` | Серверный диспетчер: switch по `M_*` типу, вызов `Process_event`, `game->OnEvent` и т.д. |
| `CLevel::ClientReceive()` | Клиентский приём: `for net_msg_Retreive()` → switch по `M_*` типу |
| `CLevel::Send(P)` | Клиент → Сервер: в direct-connect режиме → `Server->OnMessage(P, ClientID(1))`, иначе `IPureClient::Send` |
| `xrServer::SendTo(to, P)` | Сервер → Клиент: в direct-connect → `Level().OnMessage(data, size)`, иначе сетевой пакет |
| `game_cl_GameState::sv_EventSend(P)` | Удобный враппер: `Level().Send(P, net_flags(TRUE,TRUE))` |
| `game_cl_GameState::Init()` | Вызывается из `InitializeClientGame()` после создания game mode — до `M_SV_CONFIG_GAME` |
| `M_SV_CONFIG_NEW_CLIENT` → `InitializeClientGame()` | Создаёт `game_cl_Coop`, вызывает `Init()` |

### Ключевые выводы

1. **`M_EVENT` не подходит для нашего handshake** — формат `M_EVENT` требует `u32 timestamp + u16 type(GE_*) + u16 destination`, то есть ссылку на существующий entity. У нас нет entity для coop handshake.

2. **Правильный подход** — новый `M_COOP_HANDSHAKE` в enum `ENetMessages`: он получает свой `case` в обоих switch-ах и не проходит через `Process_event`.

3. **Timing для Init()**: когда `game_cl_Coop::Init()` вызывается, клиент уже имеет транспортное соединение (иначе сервер не мог бы прислать `M_SV_CONFIG_NEW_CLIENT`). Отправка HELLO в `Init()` — правильный момент.

4. **`sv_EventSend(P)`** = `Level().Send(P, net_flags(TRUE,TRUE))` — работает и в direct-connect режиме, и в сетевом. Нет нужды в отдельном транспортном слое.

---

### Изменённые файлы

#### `src/xrServerEntities/xrMessages.h`
```
+   M_COOP_HANDSHAKE,   // TODO_COOP Phase 1
```
Добавлен новый тип сообщения. Значение = autoincrement от `M_COMPRESSED_UPDATE_OBJECTS`.

#### `src/xrGame/game_sv_coop.h`
- Удалён `virtual void OnEvent(...)` — не использовался как задумано
- Добавлен `void OnCoopPacket(NET_Packet& P, ClientID sender)` — public, вызывается из `xrServer::OnMessage`

#### `src/xrGame/game_sv_coop.cpp`
- Переименован `OnEvent` → `OnCoopPacket`
- Все 4 send-helper'а: `P.w_begin(M_EVENT)` → `P.w_begin(M_COOP_HANDSHAKE)`
- Комментарии обновлены

#### `src/xrGame/game_cl_coop.h`
- Добавлен `virtual void Init()` — инициирует HELLO при старте game mode
- Удалён `void StartConnect(...)` — заменён логикой Init()

#### `src/xrGame/game_cl_coop.cpp`
- Добавлена `game_cl_Coop::Init()`: вызывает `inherited::Init()`, переходит в `eCCS_ConnectedTransport`, вызывает `send_hello()`
- Удалена `StartConnect(...)` — заменена Init()
- `send_hello()`: `P.w_begin(M_COOP_HANDSHAKE)` + `sv_EventSend(P)` (было TODO)
- `send_join_request()`: `P.w_begin(M_COOP_HANDSHAKE)` + `sv_EventSend(P)`
- `send_ping()`: `P.w_begin(M_COOP_HANDSHAKE)` + `sv_EventSend(P)`

#### `src/xrGame/xrServer.cpp`
```cpp
#include "game_sv_coop.h"   // TODO_COOP Phase 1
// ...
case M_COOP_HANDSHAKE:
{
    game_sv_Coop* coop_game = smart_cast<game_sv_Coop*>(game);
    if (coop_game)
        coop_game->OnCoopPacket(P, sender);
    else
        Msg("![COOP][SV] M_COOP_HANDSHAKE received but game is not coop mode");
}
break;
```

#### `src/xrGame/Level_network_messages.cpp`
```cpp
#include "game_cl_coop.h"   // TODO_COOP Phase 1
// ...
case M_COOP_HANDSHAKE:
{
    game_cl_Coop* coop_cl = smart_cast<game_cl_Coop*>(game);
    if (coop_cl)
        coop_cl->OnCoopMessage(*P);
    else
        Msg("![COOP][CL] M_COOP_HANDSHAKE received but game is not coop mode");
}
break;
```

#### `src/xrGame/coop_net_types.h`
- Убран `#include "../xrCore/xrCore.h"` — типы предоставляются через PCH (StdAfx.h), как у всех других xrGame-заголовков

#### `xrGame.vcxproj` и `vs2022/xrGame.vcxproj`
- Оба проекта обновлены с новыми `ClInclude`/`ClCompile` записями

---

## Полный путь handshake (теперь работает end-to-end)

```
[CLIENT PROCESS / THREAD]
1. Engine receives M_SV_CONFIG_NEW_CLIENT from server
2. CLevel::InitializeClientGame() → creates game_cl_Coop → calls Init()
3. game_cl_Coop::Init()
   → set_state(eCCS_ConnectedTransport)
   → send_hello()
      → P.w_begin(M_COOP_HANDSHAKE) + payload
      → sv_EventSend(P) → Level().Send(P) → Server->OnMessage(P, id)

[SERVER PROCESS / THREAD]
4. xrServer::OnMessage case M_COOP_HANDSHAKE
   → smart_cast<game_sv_Coop*>(game)->OnCoopPacket(P, sender)
5. game_sv_Coop::OnCoopPacket
   → r_u8 = COOP_CL_HELLO
   → handle_cl_hello(P, sender, peer)
      ← validates protocol version, content hash, capacity
      ← send_hello_ack(sender, peer)
           → P.w_begin(M_COOP_HANDSHAKE) + COOP_SV_HELLO_ACK
           → m_server->SendTo(sender, P) → Level().OnMessage (direct) / net

[CLIENT]
6. Level_network_messages.cpp::ClientReceive case M_COOP_HANDSHAKE
   → game_cl_Coop::OnCoopMessage(P)
   → r_u8 = COOP_SV_HELLO_ACK
   → handle_sv_hello_ack(P)
      ← set_state(eCCS_JoinRequested)
      ← send_join_request()
           → P.w_begin(M_COOP_HANDSHAKE) + COOP_CL_JOIN_REQUEST
           → sv_EventSend(P)

[SERVER]
7. xrServer::OnMessage case M_COOP_HANDSHAKE
   → game_sv_Coop::OnCoopPacket → handle_cl_join_request
      ← alloc_player_id()
      ← send_join_accept(sender, peer)
           → P.w_begin(M_COOP_HANDSHAKE) + COOP_SV_JOIN_ACCEPT
           → SendTo(sender, P)
      ← peer.state = eCPS_InSession

[CLIENT]
8. Level_network_messages.cpp case M_COOP_HANDSHAKE
   → game_cl_Coop::OnCoopMessage
   → COOP_SV_JOIN_ACCEPT
   → handle_sv_join_accept
      ← m_player_id = assigned_id
      ← set_state(eCCS_InSession)
      → Msg("[COOP][CL] Handshake complete — in session | player_id=1")
```

---

## Оставшиеся TODO

| Метка | Описание |
|-------|---------|
| `TODO_COOP_TRANSPORT` | Disconnect при protocol violation/timeout — нужен API для явного дисконнекта peer |
| `TODO_COOP_UI` | `createGameUI()` возвращает SP UI stub; нужен отдельный CUIGameCoop |
| `TODO_COOP_DEFINE_CONTENT_HASH_POLICY` | COOP_CONTENT_HASH = 0x1 hardcoded |
| `TODO_COOP_SINGLETON_ACTOR_ASSUMPTION` | Phase 2: несколько акторов |
| `TODO_COOP_NICKNAME` | `send_hello()` отправляет "player" — нужно читать из настроек |

---

## Следующие шаги (Phase 1.C)

1. **Перенаправление disconnect** — при `on_handshake_failed` и timeout нужно явно дисконнектить peer через `xrServer`
2. **Тест в single-process** — запуск игры в режиме "coop" с direct_connect для проверки логов handshake
3. **Phase 1.C**: UI Host/Join кнопки в главном меню
4. **Phase 2**: Несколько акторов, синхронизация позиций

---

## Файлы сессии 002

```
ИЗМЕНЁННЫЕ:
  src/xrServerEntities/xrMessages.h       (+M_COOP_HANDSHAKE)
  src/xrGame/game_sv_coop.h               (OnEvent→OnCoopPacket)
  src/xrGame/game_sv_coop.cpp             (OnEvent→OnCoopPacket, M_EVENT→M_COOP_HANDSHAKE)
  src/xrGame/game_cl_coop.h               (+Init(), -StartConnect)
  src/xrGame/game_cl_coop.cpp             (+Init(), -StartConnect, M_EVENT→M_COOP_HANDSHAKE+sv_EventSend)
  src/xrGame/xrServer.cpp                 (+include game_sv_coop.h, +case M_COOP_HANDSHAKE)
  src/xrGame/Level_network_messages.cpp   (+include game_cl_coop.h, +case M_COOP_HANDSHAKE)
  src/xrGame/coop_net_types.h             (-xrCore.h include)
  src/xrGame/xrGame.vcxproj              (уже был обновлён в сессии 001)
  src/xrGame/vs2022/xrGame.vcxproj        (+ClInclude/ClCompile для coop файлов)
```

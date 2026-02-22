# Session 001 — Phase 1.A: Skeleton of Coop Game Mode

**Date:** 2026-02-22  
**Session:** 001  
**Phase:** 1 (Phase 1.A — Скелет кооп-режима)  
**Status:** ✅ Завершено (Phase 1.A), 🔄 В работе (Phase 1.B)

---

## Что было сделано в этой сессии

### 1. Изучена архитектура кодовой базы

Изучены следующие подсистемы перед началом реализации:

| Файл | Назначение |
|------|-----------|
| `game_sv_single.h/.cpp` | Шаблон серверного режима (SP) |
| `game_cl_single.h/.cpp` | Шаблон клиентского режима (SP) |
| `game_sv_base.h` | Базовый класс сервера, вирт. методы |
| `game_cl_base.h` | Базовый класс клиента, schedule tick |
| `game_base.cpp` | `getCLASS_ID()` — маршрутизация по типу |
| `GamePersistent.cpp` | `ParseStringToGameType()` — парсинг строки типа игры |
| `gametype_chooser.h` | Enum `EGameIDs` |
| `clsid_game.h` | Макросы `CLSID_SV_GAME_*`/`CLSID_CL_GAME_*` |
| `object_factory_register.cpp` | Регистрация классов в фабрике объектов |
| `xrMessages.h` | Enum типов сетевых сообщений (M_*) |
| `clsid.cpp` | `TEXT2CLSID` — паддинг пробелами до 8 символов |
| `NET_Server.h` | `SendTo(ClientID, NET_Packet&)` API |

**Ключевые находки:**
- `TEXT2CLSID("SV_COOP")` автоматически дополняется до `MK_CLSID('S','V','_','C','O','O','P',' ')` — совпадает с нашим макросом
- `OnPlayerDisconnect` имеет сигнатуру `(ClientID, LPSTR Name, u16 GameID)` в базовом классе
- Для отправки пакетов сервером: `m_server->SendTo(to, P)`
- Тайминги: `Device.TimerAsync()` — правильный способ получить время в ms

---

### 2. Созданы новые файлы

#### `src/xrGame/coop_net_types.h`
Базовые типы и константы кооп-протокола:
- `CoopPlayerId`, `CoopSessionId`, `CoopWorldRevision`
- `COOP_PROTOCOL_VERSION = 1`
- `COOP_CONTENT_HASH = 0x00000001` (TODO_COOP_DEFINE_CONTENT_HASH_POLICY)
- Enum `ECoopPeerState` (7 состояний сервера)
- Enum `ECoopClientState` (9 состояний клиента)
- Enum `ECoopRejectReason` (8 кодов отказа)
- Таймауты: connect=8000ms, hello=5000ms, join_accept=5000ms

#### `src/xrGame/coop_packet_ids.h`
Enum `ECoopPacketID` с диапазоном 0xC0–0xDF:
- `COOP_CL_HELLO` (0xC1), `COOP_CL_JOIN_REQUEST` (0xC2), `COOP_CL_PING` (0xC3)
- `COOP_SV_HELLO_ACK` (0xD1), `COOP_SV_JOIN_ACCEPT` (0xD2), `COOP_SV_JOIN_REJECT` (0xD3), `COOP_SV_PONG` (0xD4)

#### `src/xrGame/game_sv_coop.h`
Серверный класс `game_sv_Coop : game_sv_GameState`:
- `CoopPeerEntry` — запись о подключённом peer
- `CoopSessionState` — минимальное состояние сессии
- Методы: `Create`, `Update`, `OnPlayerConnect`, `OnPlayerDisconnect`, `OnEvent`
- Приватные: `find_peer`, `alloc_player_id`, `send_hello_ack/join_accept/reject/pong`, handlers

#### `src/xrGame/game_sv_coop.cpp`
Полная реализация серверного handshake:
- **Логирование:** все события с префиксом `[COOP][SV]`
- **State machine:** строгие переходы между состояниями peer
- **Protocol violation → fail-fast:** незнакомый пакет в неправильном состоянии = disconnect
- **Проверки совместимости:** protocol version, content hash, server capacity
- **Таймаут tick:** `tick_peer_timeouts()` вызывается в `Update()`
- **Отправка:** `m_server->SendTo(to, P)`

#### `src/xrGame/game_cl_coop.h`
Клиентский класс `game_cl_Coop : game_cl_GameState`:
- State machine с 9 состояниями
- `StartConnect(ip, port)` — точка входа подключения
- `OnCoopMessage(P)` — входящие пакеты от сервера
- `GetCoopState()`, `GetPlayerId()`, `GetRejectReasonString()`

#### `src/xrGame/game_cl_coop.cpp`
Полная реализация клиентского handshake:
- **Logging:** все события с префиксом `[COOP][CL]` с elapsed_ms
- **State transitions:** строгие с проверкой текущего состояния
- **`on_handshake_failed`:** единая точка для всех ошибок → `eCCS_Failed`
- **UI заглушка:** временно используется SP UI (`CUIGameSP`)
- **Таймаут tick:** `shedule_Update(dt)` проверяет hello/join timeouts

---

### 3. Изменены существующие файлы

| Файл | Изменение |
|------|-----------|
| `gametype_chooser.h` | Добавлен `eGameIDCoop = u32(1) << 7` в enum |
| `clsid_game.h` | Добавлены `CLSID_SV_GAME_COOP` и `CLSID_CL_GAME_COOP` |
| `GamePersistent.cpp` | Добавлена ветка `"coop"` в `ParseStringToGameType` |
| `game_base.cpp` | Добавлен `case eGameIDCoop` в `getCLASS_ID` |
| `object_factory_register.cpp` | Includes + регистрация `game_sv_Coop` и `game_cl_Coop` |
| `xrGame.vcxproj` | Добавлены все 4 новых файла (2 .h + 2 .cpp) |

---

## Архитектурные решения

### Использование `OnEvent` вместо нового `OnMessage`
Входящие пакеты обрабатываются через виртуальный `OnEvent(NET_Packet& P, u16 type, u32 time, ClientID sender)`, который уже является виртуальным в базовом классе. Первый байт payload содержит наш `ECoopPacketID`. Это не требует изменений в xrServer.

### Диапазон packet IDs: 0xC0–0xDF
Не пересекается с существующими `M_*` значениями (0..~60) из `xrMessages.h`.

### Device.TimerAsync() для тайминга
Используется вместо `timeGetTime()` — платформенный вызов, который уже используется в `game_sv_deathmatch.cpp`.

---

## Known Issues / TODO

| Метка | Описание |
|-------|---------|
| `TODO_COOP_TRANSPORT` | Send/recv пакетов пока не подключён к реальному transport — нужно интегрировать в xrServer message routing |
| `TODO_COOP_UI` | `game_cl_Coop::createGameUI()` использует SP UI как заглушку |
| `TODO_COOP_DEFINE_CONTENT_HASH_POLICY` | `COOP_CONTENT_HASH = 0x1` — hardcoded, нужна реальная реализация |
| `TODO_COOP_DISCONNECT` | При protocol violation/timeout нужно инициировать реальный disconnect через transport |
| `TODO_COOP_SINGLETON_ACTOR_ASSUMPTION` | g_actor / db.actor singleton — будет исследован в Phase 1.B/2 |

---

## Следующие шаги (Phase 1.B)

1. **Интеграция маршрутизации пакетов в xrServer** — чтобы `OnEvent` game_sv_Coop действительно получал пакеты с нашими ID
2. **Интеграция в game_cl** — чтобы входящие пакеты от сервера доходили до `OnCoopMessage`
3. **Реализация StartConnect через Level().GetIClient()** — реальное transport подключение
4. **Базовый тест:** две инстансии — host, join, log проверка

---

## Файлы, изменённые/созданные в этой сессии

```
НОВЫЕ:
  src/xrGame/coop_net_types.h
  src/xrGame/coop_packet_ids.h
  src/xrGame/game_sv_coop.h
  src/xrGame/game_sv_coop.cpp
  src/xrGame/game_cl_coop.h
  src/xrGame/game_cl_coop.cpp

ИЗМЕНЁННЫЕ:
  src/xrServerEntities/gametype_chooser.h   (+eGameIDCoop)
  src/xrServerEntities/clsid_game.h         (+CLSID_SV/CL_GAME_COOP)
  src/xrGame/GamePersistent.cpp             (+ParseStringToGameType "coop")
  src/xrGame/game_base.cpp                  (+getCLASS_ID case eGameIDCoop)
  src/xrServerEntities/object_factory_register.cpp  (includes + registration)
  src/xrGame/xrGame.vcxproj                 (ClInclude + ClCompile)
```

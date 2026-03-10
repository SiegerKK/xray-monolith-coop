# Замена DirectPlay 8: анализ сложности и план миграции

## TL;DR

Замена DirectPlay 8 — **умеренно сложная, но вполне реалистичная задача**.
Весь DirectPlay изолирован в **одном модуле** (`src/xrNetServer/`, ~3 000 строк)
за двумя классами-интерфейсами: `IPureServer` и `IPureClient`.
Остальной код (xrGame, xrEngine, скрипты) ничего про DirectPlay не знает.

Основная работа — переписать `NET_Server.cpp` и `NET_Client.cpp`, сохранив
тот же публичный API этих двух классов. Сетевой протокол (`NET_Packet`,
сериализация, `MultipacketSender/Reciever`, компрессия) менять не нужно.

---

## 1. Архитектура сетевого слоя

```
┌─────────────────────────────────────────────────────────────┐
│ GAME LAYER (xrGame)                                         │
│  xrServer : IPureServer         game_cl_GameState           │
│  xrClientData : IClient         : IPureClient               │
│  game_sv_base                   Level                       │
└──────────────────┬──────────────────────┬───────────────────┘
                   │ виртуальные методы   │ виртуальные методы
┌──────────────────▼──────────────────────▼───────────────────┐
│ TRANSPORT LAYER (xrNetServer)                               │
│  IPureServer                    IPureClient                  │
│  IClient                        INetQueue                   │
│  MultipacketSender/Reciever     NET_Compressor              │
└──────────────────┬──────────────────────┬───────────────────┘
                   │ COM API              │ COM API
┌──────────────────▼──────────────────────▼───────────────────┐
│ DIRECTPLAY 8  (Windows SDK / dplay8.dll)                    │
│  IDirectPlay8Server             IDirectPlay8Client          │
│  IDirectPlay8Address            DPN_MSGID_*                 │
└─────────────────────────────────────────────────────────────┘
```

Граница замены: **только Transport Layer**. Game Layer и Core не трогаем.

---

## 2. Что именно использует DirectPlay 8

### 2.1 Файлы с DirectPlay зависимостью

| Файл | Что использует | ~Строк DirectPlay |
|------|---------------|------------------|
| `src/xrNetServer/NET_Server.h` | `IDirectPlay8Server*`, `IDirectPlay8Address*` | 5 |
| `src/xrNetServer/NET_Server.cpp` | COM init, Host, SendTo, GetClientAddress, callback handler | ~120 |
| `src/xrNetServer/NET_Client.h` | `IDirectPlay8Client*`, `IDirectPlay8Address*`, `HOST_NODE` struct | 10 |
| `src/xrNetServer/NET_Client.cpp` | COM init, Connect, Enum, Send, callback handler | ~150 |
| `src/xrNetServer/NET_Shared.h` | `XR_GUID` macro, extern GUID constants | 50 |
| `src/xrNetServer/stdafx.h` | `#include <DPlay/dplay8.h>` | 1 |
| `sdk/include/DPlay/dplay8.h` | Весь SDK (SDK-файл, не трогаем) | 1456 |
| `sdk/include/DPlay/dpaddr.h` | Адреса (SDK-файл, не трогаем) | 392 |

Итого **≈330 строк кода с прямой зависимостью от DirectPlay** в двух .cpp файлах.

### 2.2 DirectPlay API calls, которые нужно заменить

**Инициализация (NET_Server.cpp, ~строки 315–450):**
```cpp
CoCreateInstance(CLSID_DirectPlay8Server, IID_IDirectPlay8Server, &NET)
NET->Initialize(this, Handler, dwFlags)
CoCreateInstance(CLSID_DirectPlay8Address, IID_IDirectPlay8Address, &net_Address_device)
net_Address_device->SetSP(&CLSID_DP8SP_TCPIP)
net_Address_device->SetDevice(DPNA_DATATYPE_PORT, port)
NET->Host(&dpAppDesc, &net_Address_device, 1, NULL, NULL, NULL, 0)
```

**Отправка (NET_Server.cpp, ~строки 632–680):**
```cpp
NET->SendTo(dpnid, &desc, 1, dwTimeout, NULL, dwFlags | DPNSEND_COALESCE)
NET->SendTo(DPNID_ALL_PLAYERS_GROUP, ...)  // broadcast
```

**Получение (Server callback DPN_MSGID_RECEIVE, ~строки 549–577):**
```cpp
PDPNMSG_RECEIVE pMsg = (PDPNMSG_RECEIVE)pMsgBuffer;
pMsg->dpnidSender   // ClientID
pMsg->pReceiveData  // raw bytes
pMsg->dwReceiveDataSize
pMsg->hBufferHandle
NET->ReturnBuffer(pMsg->hBufferHandle, 0)
```

**Player join/leave (Server callback, ~строки 497–545):**
```cpp
DPN_MSGID_CREATE_PLAYER   → client_Create() / OnCL_Connected()
DPN_MSGID_DESTROY_PLAYER  → OnCL_Disconnected() / client_Destroy()
DPN_MSGID_ENUM_HOSTS_QUERY
DPN_MSGID_INDICATE_CONNECT
```

**Клиентская сторона (NET_Client.cpp, ~строки 900–1100):**
```cpp
CoCreateInstance(CLSID_DirectPlay8Client, ...)
NET->Initialize(this, Handler, dwFlags)
NET->EnumHosts(...)    // поиск серверов (не нужен для прямого подключения)
NET->Connect(...)
NET->Send(...)
// callback: DPN_MSGID_CONNECT_COMPLETE, DPN_MSGID_RECEIVE, DPN_MSGID_TERMINATE_SESSION
```

**Адресация (GetClientAddress, ~строки 887–915):**
```cpp
IDirectPlay8Address* pClAddr = NULL;
NET->GetClientInfo(id, pInfo, &dwSize, 0)
// pInfo->dpnidClient → ClientID
NET->GetClientAddress(id, &pClAddr, 0)
pClAddr->GetComponentByName(DPNA_KEY_HOSTNAME, ...)
pClAddr->GetComponentByName(DPNA_KEY_PORT, ...)
```

> **Примечание:** Имена классов `MultipacketReciever` / `RecievePacket` написаны
> именно так в оригинальном коде (`NET_Common.h`). Это опечатки-артефакты оригинала,
> изменять их написание в самом коде нет необходимости.

---

## 3. Что менять НЕ нужно

Следующие компоненты написаны поверх DirectPlay и не зависят от него напрямую.
Они остаются **без изменений** при любой замене транспорта:

| Компонент | Файл | Роль |
|-----------|------|------|
| `NET_Packet` | `src/xrCore/net_utils.h` | Сериализация пакетов |
| `MultipacketSender` | `src/xrNetServer/NET_Common.h` | Склеивание мелких пакетов |
| `MultipacketReciever` | `src/xrNetServer/NET_Common.h` | Разбор склеенных пакетов |
| `NET_Compressor` | `NET_Compressor.h/cpp` | LZO-сжатие |
| `IClient` | `NET_Server.h` | Данные одного клиента (ConnectionState) |
| `PlayersMonitor` | `NET_PlayersMonitor.h` | Thread-safe список клиентов |
| `INetQueue` | `NET_Client.h` | Очередь входящих пакетов |
| `xrServer` / `xrClientData` | `xrGame/xrServer.h` | Игровой слой (SP/coop логика) |
| `game_cl_GameState` | `xrGame/game_cl_base.h` | Клиентское игровое состояние |
| `NET_Packet` send/receive API | `Level_network.cpp` | Обработка игровых сообщений |

**Формат пакетов** (уже platform-neutral): `u8/u16/u32/float/vec3/string`,
сжатие через `NET_Compressor`. При переходе на ENet или SteamNetworking формат
пакетов **не меняется**.

---

## 4. Совместимость текущего кода с новым транспортом

### ✅ Уже совместимо (без изменений)

1. **Флаги надёжности** — в `SendTo_LL` есть параметр `dwFlags`:
   - `DPNSEND_GUARANTEED` → ENet: `ENET_PACKET_FLAG_RELIABLE`
   - `0` (unreliable) → ENet: `0`
   - Флаг используется в ≈15 местах в xrGame — менять не нужно,
     нужно только переопределить константы в заголовке

2. **`ClientID`** — уже абстрактный wrapper над `u32` (`src/xrCore/client_id.h`).
   Любая библиотека использует peer-ID, который влезает в u32.

3. **`NET_Packet` + сериализация** — полностью независима от транспорта.

4. **`MultipacketSender/Reciever`** — работает поверх raw bytes, не привязан к
   DirectPlay. При ENet можно отключить (у ENet есть встроенное фрагментирование),
   но для минимального изменения — оставить.

5. **`psNET_direct_connect`** — режим «без сети» (SinglePlayer/Coop) обходит
   весь Transport Layer. Он продолжит работать при любой замене.

### ⚠️ Нужно адаптировать

1. **`HOST_NODE` struct** в `IPureClient` содержит `DPN_APPLICATION_DESC` и
   `IDirectPlay8Address*`. Нужно заменить на `string host` + `u16 port`.

2. **`net_Syncronize()` / `Sync_Thread()`** — синхронизация времени через
   round-trip к серверу (NET_Client.cpp, строки ~1109–1200). Это независимый
   от DirectPlay механизм (`M_CL_PING_CHALLENGE_RESPOND`), работающий поверх
   `NET_Packet`. Менять не нужно — просто убедиться, что callback'и
   вызываются правильно.

3. **`EnumHosts()`** — обнаружение серверов в локальной сети. При прямом
   подключении (наш coop) не используется, но код есть. Для ENet/SteamNet
   эта функциональность реализуется иначе (broadcast UDP или Steam matchmaking).
   Для coop достаточно прямого IP — `EnumHosts` можно заглушить.

4. **`DPN_MSGID_INDICATE_CONNECT`** — проверка подключения (ban-list, access
   control). В новом транспорте нужен аналогичный хук «до завершения handshake».

5. **`GetClientAddress()`** — получить IP/порт клиента для логов/бана.
   В ENet: `peer->address.host`, `peer->address.port`. Тривиальная замена.

6. **Флаги отправки** — нужно добавить `#define` или `enum` для замены
   `DPNSEND_GUARANTEED`, `DPNSEND_COALESCE` и т.д.

### ❌ Нужно переписать (только в NET_Server.cpp / NET_Client.cpp)

| Что переписать | ~Объём |
|----------------|--------|
| Инициализация сервера (CoCreateInstance → enet_initialize + enet_host_create) | 80 строк |
| Callback-handler сервера (switch DPN_MSGID_* → event loop) | 120 строк |
| SendTo_LL / SendBroadcast_LL (NET->SendTo → enet_peer_send) | 50 строк |
| Инициализация клиента (CoCreateInstance → enet_host_create + enet_host_connect) | 80 строк |
| Callback-handler клиента (switch DPN_MSGID_* → event loop) | 100 строк |
| GetClientAddress (pClAddr→GetComponent → peer->address) | 30 строк |
| Деинициализация (Release COM → enet_host_destroy) | 20 строк |

**Итого: ≈480 строк переписать**, остальной код (>20 000 строк xrGame/xrEngine) —
без изменений.

---

## 5. Варианты замены транспорта

### 5.1 ENet (рекомендуется для Phase 2)

**Ссылка:** http://enet.bespin.org / https://github.com/lsalzman/enet

**Преимущества:**
- Чистый C, работает Windows/Linux/Mac
- Надёжные + ненадёжные каналы, встроенная фрагментация
- Уже используется в многих X-Ray форках (OpenXRay)
- Нет зависимости от COM/Windows Registry
- Нет лицензионных ограничений

**Концептуальное соответствие:**

| DirectPlay 8 | ENet |
|---|---|
| `IDirectPlay8Server` | `ENetHost* server` |
| `IDirectPlay8Client` | `ENetHost* client` + `ENetPeer* server_peer` |
| `DPNID` (peer ID) | `ENetPeer*` (или индекс в массиве) |
| `NET->Host(...)` | `enet_host_create(&addr, max_clients, 2, 0, 0)` |
| `NET->Connect(...)` | `enet_host_connect(client, &addr, 2, 0)` |
| `NET->SendTo(id, &desc, ...)` | `enet_peer_send(peer, channel, packet)` |
| `DPN_MSGID_CREATE_PLAYER` | `event.type == ENET_EVENT_TYPE_CONNECT` |
| `DPN_MSGID_DESTROY_PLAYER` | `event.type == ENET_EVENT_TYPE_DISCONNECT` |
| `DPN_MSGID_RECEIVE` | `event.type == ENET_EVENT_TYPE_RECEIVE` |
| `DPNSEND_GUARANTEED` | `ENET_PACKET_FLAG_RELIABLE` |
| `DPNSEND_COALESCE` | встроено в ENet (batching) |

**Усилие:** 1–2 недели для опытного C++ разработчика.

### 5.2 SteamNetworkingSockets

**Ссылка:** https://github.com/ValveSoftware/GameNetworkingSockets

**Преимущества:**
- Надёжное UDP с шифрованием и NAT-traversal
- Steam relay сервера для P2P
- Поддержка Steam-интеграции из коробки

**Недостатки:**
- Более тяжёлая зависимость (Valve SDK)
- Менее привычный для движка паттерн (callbacks vs poll)

**Усилие:** 2–3 недели.

### 5.3 RakNet / SLikeNet

Устаревший вариант. RakNet заброшен, SLikeNet — форк. Не рекомендуется.

---

## 6. Конкретные шаги миграции (на примере ENet)

### Phase 2.1: Подготовка (без изменения функциональности)

1. Добавить ENet в `/sdk/` или как submodule
2. В `NET_Shared.h` добавить `#ifdef TRANSPORT_ENET` секцию с заменой констант:
   ```cpp
   #ifdef TRANSPORT_ENET
   #define DPNSEND_GUARANTEED    ENET_PACKET_FLAG_RELIABLE
   #define DPNSEND_COALESCE      0
   #define DPNSEND_NONSEQUENTIAL 0
   #endif
   ```
3. Создать `NET_Server_ENet.cpp` и `NET_Client_ENet.cpp` рядом с текущими
   файлами — транслируем интерфейс IPureServer/IPureClient на ENet
4. Добавить в `xrNetServer.vcxproj` условную компиляцию

### Phase 2.2: Реализация серверной стороны

```cpp
// NET_Server_ENet.cpp — скелет

ENet::IPureServer::EConnect Connect(LPCSTR session_name, ...) {
    enet_initialize();
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = psNET_Port;
    NET = enet_host_create(&addr, 32, 2, 0, 0);
    // запустить event loop в отдельном потоке или вызывать из Update()
}

void PollEvents() {
    ENetEvent event;
    while (enet_host_service(NET, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            // аналог DPN_MSGID_CREATE_PLAYER
            IClient* C = new_client(...);
            C->ID = ClientID(peer_to_id(event.peer));
            OnCL_Connected(C);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            // аналог DPN_MSGID_RECEIVE
            ClientID sender = ClientID(peer_to_id(event.peer));
            MultipacketReciever::RecievePacket(event.packet->data, event.packet->dataLength, sender);
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            // аналог DPN_MSGID_DESTROY_PLAYER
            break;
        }
    }
}

void SendTo_LL(ClientID id, void* data, u32 size, u32 dwFlags, u32 timeout) {
    ENetPeer* peer = id_to_peer(id);
    enet_uint32 flags = (dwFlags & ENET_PACKET_FLAG_RELIABLE) ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* pkt = enet_packet_create(data, size, flags);
    enet_peer_send(peer, 0, pkt);
}
```

### Phase 2.3: Реализация клиентской стороны

```cpp
// NET_Client_ENet.cpp — скелет

BOOL IPureClient::Connect(LPCSTR server_name) {
    enet_initialize();
    NET = enet_host_create(NULL, 1, 2, 0, 0);
    ENetAddress addr;
    enet_address_set_host(&addr, host);
    addr.port = port;
    server_peer = enet_host_connect(NET, &addr, 2, 0);
    // ждать ENET_EVENT_TYPE_CONNECT
}

// PollEvents() аналогично серверному
```

### Phase 2.4: `psNET_direct_connect` (SinglePlayer/Coop)

Этот режим **уже не использует** DirectPlay — через него сервер и клиент
общаются через `create_direct_client()` (внутрипроцессный буфер).
Менять не нужно ничего.

---

## 7. Оценка рисков

| Риск | Вероятность | Митигация |
|------|-------------|-----------|
| NAT traversal — игроки за NAT не смогут подключиться | Средняя | ENet не помогает с NAT без relay; для первой версии — прямое подключение по IP достаточно |
| Packet ordering — ENet ordered channels vs DirectPlay ordered/unordered | Низкая | Использовать 2 channels в ENet: ch0=reliable, ch1=unreliable |
| Размер пакетов — ENet фрагментирует >1280 байт | Низкая | У нас `NET_PacketSizeLimit=16384`, ENet фрагментирует автоматически |
| Thread safety — ENet не потокобезопасен, DirectPlay — нет | Низкая | Сохранить `csMessage` мьютекс вокруг poll loop |
| Компиляция на Linux — при переходе на ENet убрать Windows-only COM | — | ENet работает без COM/Windows |

---

## 8. Текущее состояние coop-кода и совместимость

Все текущие coop-фиксы (IsGameTypeSingleOrCoop, null guards, Lua guards)
написаны на уровне **Game Layer** — они **не зависят от транспорта** вообще.

При замене DirectPlay на ENet эти изменения остаются в полной силе:
- `psNET_direct_connect` = true для SinglePlayer → работает без изменений
- `eGameIDCooperative` → фиксы в xrServer, game_sv_base, UIMainIngameWnd → остаются
- `GE_WPN_STATE_CHANGE` обработка → в `HudItem.cpp`, `Weapon.cpp` → остаётся

**Coop Phase 1 (стабилизация) никак не пересекается с Phase 2 (замена транспорта).**
Их можно вести параллельно или последовательно — по желанию.

---

## 9. Резюме

| Вопрос | Ответ |
|--------|-------|
| Сложность замены транспорта | Умеренная (~480 строк переписать, ≈1–2 недели) |
| Текущий код совместим с новым транспортом? | **Да**, кроме ~30 строк в NET_Client.h (`HOST_NODE` struct) |
| Нужно ли менять xrGame при замене транспорта? | **Нет** — только xrNetServer |
| Нужно ли менять NET_Packet / сериализацию? | **Нет** |
| Нужно ли менять coop-фиксы? | **Нет** |
| Лучший кандидат для замены | **ENet** (простой, проверенный, C) |
| Когда браться? | **После** стабилизации Phase 1 (level transition crashes) |

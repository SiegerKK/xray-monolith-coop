# Анализ сетевого кода xray-monolith-coop

> Дата анализа: 2026-02-22  
> Цель: понять существующую сервер-клиент коммуникацию для подготовки кооперативного мода

---

## 1. Транспортный уровень — DirectPlay 8

### 1.1 Технология

Вся сетевая коммуникация построена на **Microsoft DirectPlay 8** (legacy Win32 API, `dplay8.h`). Это deprecated технология (Microsoft прекратила её поддержку в Windows Vista+), однако она по-прежнему работает на Windows 10/11 через обратную совместимость.

Основные интерфейсы DirectPlay 8, используемые в движке:
- `IDirectPlay8Server` — серверная сторона (для `IPureServer`)
- `IDirectPlay8Client` — клиентская сторона (для `IPureClient`)
- TCP/IP транспорт через `CLSID_DP8SP_TCPIP`

**Порты:** `START_PORT_LAN_SV=1235`, `START_PORT_LAN_CL=1234`, `START_PORT=1237`

### 1.2 Базовые классы транспорта (xrNetServer/)

```
IPureServer                     IPureClient
    ↑                               ↑
MultipacketReciever         MultipacketReciever
MultipacketSender           MultipacketSender
```

#### IPureServer (`NET_Server.h`)
```cpp
class IPureServer : private MultipacketReciever {
    IDirectPlay8Server* NET;         // DirectPlay сервер
    NET_Compressor net_Compressor;   // LZO сжатие
    PlayersMonitor net_players;      // Список клиентов
    IClient* SV_Client;              // Локальный (хост) клиент

    virtual void OnMessage(NET_Packet& P, ClientID sender); // обработка пакета
    virtual void OnCL_Connected(IClient* C);
    virtual void OnCL_Disconnected(IClient* C);
    void SendTo(ClientID ID, NET_Packet& P, ...);
    void SendBroadcast(ClientID exclude, NET_Packet& P, ...);
};
```

#### IPureClient (`NET_Client.h`)
```cpp
class IPureClient : private MultipacketReciever, private MultipacketSender {
    IDirectPlay8Client* NET;         // DirectPlay клиент
    NET_Compressor net_Compressor;   // LZO сжатие
    INetQueue net_Queue;             // Очередь входящих пакетов
    s32 net_TimeDelta;               // Дельта синхронизации времени

    BOOL Connect(LPCSTR server_name);
    virtual void Send(NET_Packet& P, ...);
    NET_Packet* net_msg_Retreive();  // извлечь пакет из очереди
};
```

### 1.3 Пакеты (`NET_Packet`, xrCore/net_utils.h)

```cpp
struct NET_Buffer {
    BYTE data[NET_PacketSizeLimit]; // 16 KB
    u32 count;
};

class NET_Packet {
    NET_Buffer B;   // сырые байты
    u32 r_pos;      // позиция чтения
    // методы w_u8, w_u16, w_u32, w_float, w_vec3, w_stringZ, ...
    // методы r_u8, r_u16, r_u32, r_float, r_vec3, r_stringZ, ...
};
```

Максимальный размер одного пакета — **16 КБ**.

### 1.4 Оптимизации транспорта

**Мультипакет (`MultipacketSender`):**  
Несколько маленьких пакетов объединяются в один перед отправкой. Пакеты маркируются тегами:
- `NET_TAG_MERGED (0xE1)` — слитый пакет
- `NET_TAG_NONMERGED (0xE0)` — одиночный пакет

**Сжатие (`NET_Compressor`, LZO):**  
Большие пакеты сжимаются перед отправкой:
- `NET_TAG_COMPRESSED (0xC1)` — сжатый пакет
- `NET_TAG_NONCOMPRESSED (0xC0)` — несжатый

Флаг `NET_USE_COMPRESSION=1` включает сжатие.  
`NET_USE_LZO_COMPRESSION=1` — использовать LZO алгоритм.

---

## 2. Игровой сетевой слой (xrGame/)

### 2.1 Иерархия классов

```
IPureServer
    └── xrServer              (xrGame/xrServer.h)
            │
            └── управляет game_sv_GameState
                    ├── game_sv_single     (SP/Coop основа)
                    ├── game_sv_mp         (MP база)
                    │       ├── game_sv_deathmatch
                    │       ├── game_sv_teamdeathmatch
                    │       ├── game_sv_artefacthunt
                    │       └── game_sv_capture_the_artefact
                    └── (game_sv_coop?)    ← место для кооператива

IPureClient
    └── CLevel               (xrGame/Level.h — уровень = клиент)
            │
            └── управляет game_cl_GameState
                    ├── game_cl_single
                    ├── game_cl_mp
                    └── ...
```

### 2.2 xrServer — главный сервер

```cpp
class xrServer : public IPureServer {
    xrS_entities entities;           // все игровые сущности (u16 ID → CSE_Abstract*)
    xr_multiset<svs_respawn> q_respawn;
    file_transfer::server_site* m_file_transfers;

    CSE_Abstract* Process_spawn(NET_Packet& P, ClientID sender, ...);
    void Process_update(NET_Packet& P, ClientID sender);
    void Process_event(NET_Packet& P, ClientID sender);
    void Process_save(NET_Packet& P, ClientID sender);
};
```

Серверные сущности (`CSE_Abstract`) хранятся в `xrS_entities` — хеш-таблица `u16 ID → CSE_Abstract*`.

### 2.3 xrClientData — серверное представление клиента

```cpp
class xrClientData : public IClient {
    CSE_Abstract* owner;    // серверная сущность актора клиента
    BOOL net_Ready;         // клиент готов к игре
    BOOL net_Accepted;      // клиент принят
    BOOL net_PassUpdates;   // передавать обновления
    game_PlayerState* ps;   // состояние игрока
    shared_str m_cdkey_digest;
    secure_messaging::key_t m_secret_key;
};
```

---

## 3. Типы сообщений (xrServerEntities/xrMessages.h)

### 3.1 Основные сообщения (M_*)

| Константа | Направление | Описание |
|-----------|-------------|---------|
| `M_UPDATE (0)` | CL↔SV | Обновление состояния объектов |
| `M_SPAWN (1)` | SV→CL | Спавн сущности (полное состояние) |
| `M_SV_CONFIG_NEW_CLIENT` | SV→CL | Конфиг нового клиента |
| `M_SV_CONFIG_GAME` | SV→CL | Конфиг игры |
| `M_SV_CONFIG_FINISHED` | SV→CL | Конфиг завершён |
| `M_MIGRATE_DEACTIVATE` | — | Смена сервера: деактивация |
| `M_MIGRATE_ACTIVATE` | — | Смена сервера: активация с состоянием |
| `M_CHAT` | DUAL | Чат |
| `M_EVENT` | CL→SV | Игровое событие |
| `M_CL_INPUT` | CL→SV | Ввод клиента |
| `M_CL_UPDATE` | CL→SV | Обновление от клиента |
| `M_UPDATE_OBJECTS` | SV→CL | Обновление объектов |
| `M_CLIENTREADY` | CL→SV | Клиент загрузил уровень |
| `M_CHANGE_LEVEL` | SV→CL | Смена уровня |
| `M_LOAD_GAME` | — | Загрузка игры |
| `M_RELOAD_GAME` | — | Перезагрузка |
| `M_SAVE_GAME` | — | Сохранение игры |
| `M_GAMEMESSAGE` | DUAL | Игровое сообщение |
| `M_CLIENT_CONNECT_RESULT` | SV→CL | Результат подключения |
| `M_CLIENT_REQUEST_CONNECTION_DATA` | CL→SV | Запрос данных подключения |
| `M_AUTH_CHALLENGE` | SV→CL | Вызов авторизации |
| `M_CL_AUTH` | CL→SV | Ответ авторизации |
| `M_COMPRESSED_UPDATE_OBJECTS` | SV→CL | Сжатые обновления объектов |
| `M_FILE_TRANSFER` | DUAL | Передача файлов (карты) |
| `M_SECURE_MESSAGE` | DUAL | Шифрованное сообщение |

### 3.2 Игровые события (GE_*)

| Константа | Описание |
|-----------|---------|
| `GE_RESPAWN` | Возрождение объекта |
| `GE_OWNERSHIP_TAKE` | Взять предмет |
| `GE_OWNERSHIP_REJECT` | Отказ от предмета |
| `GE_HIT` | Попадание |
| `GE_DIE` | Смерть |
| `GE_DESTROY` | Уничтожение сущности |
| `GE_TELEPORT_OBJECT` | Телепортация |
| `GE_WPN_STATE_CHANGE` | Смена состояния оружия |
| `GE_GRENADE_EXPLODE` | Взрыв гранаты |
| `GE_INV_ACTION` | Действие с инвентарём |
| `GE_ACTOR_JUMPING` | Прыжок актора |
| `GE_MOVE_ACTOR` | Мгновенное перемещение актора |
| `GE_MONEY` | Деньги |
| `GEG_PLAYER_ITEM2SLOT/BELT/RUCK` | Перемещение предмета |
| `GE_GAME_EVENT` | Игровое событие (через `game_sv`) |

### 3.3 Игровые сообщения (GAME_EVENT_*)

```
GAME_EVENT_PLAYER_READY
GAME_EVENT_PLAYER_KILL
GAME_EVENT_PLAYER_CONNECTED / DISCONNECTED / ENTERED_GAME
GAME_EVENT_PLAYER_KILLED / HITTED
GAME_EVENT_ROUND_STARTED / END
GAME_EVENT_ARTEFACT_SPAWNED / TAKEN / DROPPED
GAME_EVENT_CREATE_CLIENT
GAME_EVENT_VOTE_START / YES / NO / STOP
GAME_EVENT_PLAYERS_INFO_REPLY
```

---

## 4. Флаги спавна

```cpp
M_SPAWN_OBJECT_LOCAL     = (1<<0)  // объект локальный (авторитативный)
M_SPAWN_OBJECT_HASUPDATE = (1<<2)  // в пакете есть UPDATE
M_SPAWN_OBJECT_ASPLAYER  = (1<<3)  // актор игрока
M_SPAWN_OBJECT_PHANTOM   = (1<<4)  // фантом (для respawn)
M_SPAWN_VERSION          = (1<<5)  // версионный контроль
M_SPAWN_UPDATE           = (1<<6)  // + update пакет
M_SPAWN_TIME             = (1<<7)  // + время спавна
M_SPAWN_DENIED           = (1<<8)  // отказано в спавне
```

---

## 5. Поток соединения и инициализации

### 5.1 Single Player (direct connect)

```
CLevel::net_start_client2()
    └── psNET_direct_connect = TRUE
    └── Server->create_direct_client()   // создаёт виртуальный локальный клиент
    └── CLevel::Connect2Server()
        └── IPureClient::Connect()

Game Loop (ClientReceive / Server->Update):
    ├── M_CLIENT_REQUEST_CONNECTION_DATA  (CL→SV)
    ├── M_AUTH_CHALLENGE                  (SV→CL)
    ├── M_CL_AUTH                         (CL→SV)
    ├── M_CLIENT_CONNECT_RESULT           (SV→CL)
    ├── M_SV_CONFIG_NEW_CLIENT            (SV→CL)
    ├── M_SV_CONFIG_GAME                  (SV→CL)
    ├── M_SV_CONFIG_FINISHED              (SV→CL)
    ├── M_SPAWN × N                       (SV→CL, все сущности уровня)
    ├── M_CLIENTREADY                     (CL→SV)
    └── [ игровой цикл ]
            ├── M_UPDATE (CL→SV, состояние актора)
            ├── M_UPDATE_OBJECTS (SV→CL, все обновления)
            └── M_EVENT (CL→SV, действия игрока)
```

### 5.2 Multiplayer (реальная сеть)

```
xrServer::Connect(session_name)
    └── создаёт game_sv_* на основе типа игры
    └── IPureServer::Connect() → DirectPlay8 Server старт

CLevel::net_start_client2()
    └── IPureClient::Connect(server_address)
    └── получает GameDescriptionData (map_name, map_version, download_url)
    └── загружает уровень

После загрузки карты:
    └── тот же флоу что и SP, но через сеть TCP/IP
```

---

## 6. Цикл обновления (игровой loop)

### 6.1 Клиентская сторона — `CLevel::ClientReceive()` (Level_network_messages.cpp)

```cpp
void CLevel::ClientReceive() {
    StartProcessQueue();
    for (NET_Packet* P = net_msg_Retreive(); P; P = net_msg_Retreive()) {
        u16 m_type;
        P->r_begin(m_type);
        switch (m_type) {
            case M_SPAWN:           cl_Process_Spawn(*P);     break;
            case M_UPDATE:          cl_Process_Update(*P);    break;
            case M_UPDATE_OBJECTS:  cl_Process_Objects(*P);   break;
            case M_EVENT:           cl_Process_Event(*P);     break;
            case M_SV_CONFIG_*:     /* конфигурация */        break;
            case M_GAMEMESSAGE:     game->OnMessage(*P, ...); break;
            // ...
        }
    }
    EndProcessQueue();
}
```

### 6.2 Серверная сторона — `xrServer::OnMessage()` (xrServer.cpp)

```cpp
u32 xrServer::OnMessage(NET_Packet& P, ClientID sender) {
    u16 m_type;
    P.r_begin(m_type);
    switch (m_type) {
        case M_UPDATE:         Process_update(P, sender);  break;
        case M_SPAWN:          Process_spawn(P, sender);   break;
        case M_EVENT:          Process_event(P, sender);   break;
        case M_SAVE_PACKET:    Process_save(P, sender);    break;
        case M_CLIENTREADY:    Process_ready(P, sender);   break;
        case M_GAMEMESSAGE:    game->OnMessage(P, ...);    break;
        // ...
    }
}
```

---

## 7. Синхронизация объектов

### 7.1 Экспорт/импорт состояния актора (`Actor_Network.cpp`)

Каждый кадр актор экспортирует своё состояние серверу:

```cpp
void CActor::net_Export(NET_Packet& P) {  // CL → SV
    P.w_float(GetfHealth());
    P.w_u32(Level().timeServer());
    P.w_vec3(Position());
    P.w_float(angle_normalize(r_model_yaw));     // горизонтальный угол
    P.w_float(angle_normalize(r_torso.yaw));     // угол торса (гор)
    P.w_float(angle_normalize(r_torso.pitch));   // угол торса (верт)
    // + состояние движения, оружие, etc.
}

void CActor::net_Import(NET_Packet& P) {  // SV → CL (для других игроков)
    // обратное чтение
}
```

### 7.2 CSE_Abstract::UPDATE_Read/Write (серверные сущности)

Каждая серверная сущность имеет методы для синхронизации:
```cpp
class CSE_Abstract {
    virtual void UPDATE_Read(NET_Packet& P);   // читать обновление
    virtual void UPDATE_Write(NET_Packet& P);  // писать обновление
    virtual void Spawn_Read(NET_Packet& P);    // читать спавн
    virtual void Spawn_Write(NET_Packet& P, BOOL local); // писать спавн
};
```

### 7.3 Сжатые обновления (`xrServer_updates_compressor.cpp`)

Для оптимизации трафика обновления объектов сжимаются:
- `M_COMPRESSED_UPDATE_OBJECTS` — сжатый пакет обновлений
- Класс `server_updates_compressor` управляет буферами

---

## 8. Специфика Single Player (SP)

В SP-режиме используется **прямое соединение** (`psNET_direct_connect = TRUE`):

- Сервер (`xrServer`) и клиент (`CLevel`) работают **в одном процессе**
- Нет реальной сетевой передачи — пакеты передаются через разделяемую память / in-process
- Тип игры — `game_sv_single` (наследует `game_sv_GameState`)
- `game_sv_single` управляет **ALife симулятором** — полной симуляцией мира STALKER

```cpp
// Level_network_start_client.cpp
bool CLevel::net_Start_client(const char* options) {
    return false; // SP не инициирует реального клиентского подключения
}
```

---

## 9. Авторизация и безопасность

```
CL → SV: M_CLIENT_REQUEST_CONNECTION_DATA
SV → CL: M_GAMESPY_CDKEY_VALIDATION_CHALLENGE (запрос CD-ключа)
CL → SV: M_GAMESPY_CDKEY_VALIDATION_CHALLENGE_RESPOND
SV → CL: M_AUTH_CHALLENGE (challenge для HMAC)
CL → SV: M_CL_AUTH (ответ с HMAC подписью)
SV → CL: M_CLIENT_CONNECT_RESULT (успех/ошибка)
```

Возможные ошибки подключения (`enum_connection_results`):
- `ecr_data_verification_failed`
- `ecr_cdkey_validation_failed`
- `ecr_password_verification_failed`
- `ecr_have_been_banned`
- `ecr_profile_error`

Дополнительно — шифрованные сообщения (`M_SECURE_KEY_SYNC`, `M_SECURE_MESSAGE`) с симметричным ключом.

---

## 10. Синхронизация времени

```cpp
// IPureClient
IC u32 timeServer() { 
    return TimeGlobal(device_timer) + net_TimeDelta + net_TimeDelta_User; 
}
```

Клиент поддерживает `net_TimeDelta` — разница между серверным и локальным временем.  
Пинг-механизм через `M_CL_PING_CHALLENGE` / `M_CL_PING_CHALLENGE_RESPOND`.

---

## 11. Файловая передача и демо-система

- `M_FILE_TRANSFER` — передача файлов (карт, скриншотов античита)
- `file_transfer::server_site` / `file_transfer::client_site` — классы управления
- `M_MAKE_SCREENSHOT` — запрос скриншота (античит-функция)
- Demo-система (`Level_network_Demo.cpp`) — запись и воспроизведение сетевых пакетов

---

## 12. Lua socket.lua — потенциальный инструмент для кооператива

В `gamedata/scripts/socket.lua` присутствует Lua-модуль для TCP/UDP сокетов. Это **альтернативный сетевой стек** поверх которого можно построить упрощённую синхронизацию:

```lua
-- Пример из socket.lua (luasocket API)
local socket = require("socket")
local tcp = socket.tcp()
tcp:connect("127.0.0.1", 1234)
```

Это открывает возможность:
- Реализации простого протокола синхронизации без изменения C++ кода
- Обмена сообщениями между инстансами игры через TCP
- Прототипирования кооп-механик на чистом Lua

---

## 13. Выводы и рекомендации для кооператива

### Существующее:
| Компонент | Статус | Для кооп |
|-----------|--------|---------|
| DirectPlay 8 транспорт | ✅ Работает | Можно использовать |
| MP-режимы (DM, TDM, AH) | ✅ Реализованы | Основа для coop |
| SP `game_sv_single` + ALife | ✅ Работает | Нужно адаптировать |
| Actor network export/import | ✅ Реализован | Переиспользовать |
| Спавн множества акторов | ⚠️ Только MP | Портировать в SP |
| Инвентарь/квесты синхронизация | ❌ Нет | Разрабатывать с нуля |
| `socket.lua` | ✅ Присутствует | Прототипирование |

### Минимальный путь к кооперативу:

1. **Шаг 1 (C++):** Создать `game_sv_coop` на базе `game_sv_single` + частично `game_sv_mp`, разрешив несколько активных акторов
2. **Шаг 2 (C++):** Модифицировать `xrServer::Process_spawn` чтобы принимать несколько клиентов с акторами типа `CSE_ALifeCreatureActor`
3. **Шаг 3 (C++):** Настроить `Actor_Network.cpp` для импорта позиций удалённых игроков
4. **Шаг 4 (Lua):** Написать coop-скрипты для синхронизации квестов/инвентаря через существующий `M_GAMEMESSAGE` / `GE_GAME_EVENT`
5. **Шаг 5 (Lua):** Использовать `socket.lua` для дополнительного канала синхронизации при необходимости

### Критические проблемы ALife:
ALife-симулятор в `game_sv_single` спроектирован для **одного** актора. Для кооп необходимо:
- Убрать ограничение на одного актора в ALife-симуляторе
- Синхронизировать все ALife-события между клиентами
- Решить конфликты инвентаря (лут одного предмета несколькими игроками)

---

*Подготовлен на основе исходного кода проекта xray-monolith-coop (февраль 2026)*

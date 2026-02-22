# Phase 1 — Первый успешный кооп-тест (Host + LAN Connect)

## Назначение документа

Этот документ определяет **первый этап реализации кооперативной архитектуры** для `xray-monolith-coop` на базе **чистого Anomaly**.

Цель этапа — получить **первый воспроизводимый успешный кооп-тест**, в котором:

1. Игрок может **создать хост/сервер из меню**.
2. Второй экземпляр игры (клиент) может **подключиться по LAN IP**.
3. Подключение завершается без падения игры, с корректным handshake и базовой синхронизацией состояния сессии.
4. Клиент получает подтверждение подключения и входит в минимальное игровое состояние (хотя бы "joined in session / in level loading state / spawned stub actor", в зависимости от текущей реализации).

> **Важно:** Phase 1 не требует полноценного геймплея, ALife-кооператива, синхронизации инвентаря, стрельбы, квестов и т.п.  
> Это этап "сетевого каркаса + пользовательский путь host/connect".

---

# 1. Цель этапа (Definition of Success)

## 1.1 Основной результат

Успешный сценарий:

- На **Host**:
  - из игрового меню запускается кооп-хост;
  - игра поднимает локальный сервер (listen host или dedicated-in-process, в рамках текущей реализации);
  - сервер начинает слушать порт и принимает входящие соединения.

- На **Client**:
  - из игрового меню вводится `LAN IP` хоста;
  - клиент подключается к серверу;
  - проходит базовый handshake;
  - получает от сервера минимальные данные сессии;
  - отображается подтверждение успешного подключения (лог/UI/переход в состояние загрузки уровня/спавна).

## 1.2 Минимальные критерии приемки (Acceptance Criteria)

Phase 1 считается завершённым, если одновременно выполняются следующие пункты:

### A. Пользовательский сценарий
- [ ] На хосте есть рабочий путь в меню для создания кооп-сессии.
- [ ] На клиенте есть рабочий путь в меню для ввода IP и подключения.
- [ ] Подключение происходит по **LAN IP** (например `192.168.x.x`).

### B. Сеть / протокол
- [ ] Сервер принимает входящее соединение.
- [ ] Клиент получает `WELCOME/ACCEPT`-ответ от сервера (или эквивалент текущего протокола).
- [ ] Сервер назначает клиенту `peer/player id`.
- [ ] Сервер и клиент синхронно входят в одно и то же состояние сессии (например: `connected -> joining -> in_session`).

### C. Стабильность
- [ ] Нет краша движка/скриптов при создании хоста.
- [ ] Нет краша при попытке подключения клиента.
- [ ] При неуспешном подключении показывается понятная ошибка (таймаут, refused, version mismatch).

### D. Диагностика
- [ ] Есть подробные логи handshake на сервере и клиенте.
- [ ] Логи позволяют понять причину отказа подключения.

### E. Совместимость и валидность
- [ ] Сервер отклоняет клиента с несовпадающим `COOP_PROTOCOL_VERSION`.
- [ ] Сервер отклоняет клиента с несовпадающим `CONTENT_HASH/MODSET_SIGNATURE` (или временным эквивалентом).
- [ ] Клиент корректно показывает причину отказа (reason code + текст).

### F. Диагностируемость отказов
- [ ] При любой ошибке handshake в логах можно определить **последний успешно завершённый шаг**.
- [ ] При protocol violation есть явный лог с состоянием peer/client state.
- [ ] В debug/dev сборке доступен packet trace ring buffer dump.

---

# 2. Границы Phase 1

## 2.1 Что входит в Phase 1 (In Scope)

### Ограничение транспорта и платформы (P1-LIM-01)
Phase 1 использует существующий сетевой транспорт X-Ray (DirectPlay8 / `IPureServer`/`IPureClient`) как **временное решение**.

Следствия:
- Phase 1 нацелен на **Windows host/client** как базовый сценарий проверки.
- LAN-подключение — единственный обязательный режим для этапа.
- Linux/Proton совместимость **не является критерием готовности Phase 1**.

> Это не означает отказ от Linux/Proton в будущем; только фиксирует реалистичные границы этапа. Замена транспорта запланирована на более поздний roadmap-этап.

### Сетевой каркас
- Инициализация кооп-режима (`game_sv_coop`, `game_cl_coop` или минимальный эквивалент).
- Создание хост-сервера из меню.
- Подключение клиента по IP.
- Базовый handshake (версия протокола, session accept/reject, player id, content/modset signature).
- Базовое состояние сессии (session state replication minimal).

### UI / Menu integration (минимально)
- Пункт меню: "Host Coop" / "Join Coop (LAN)".
- Поле для ввода IP (и опционально порт).
- Показ статуса подключения / ошибки.

### Логирование и диагностика
- Подробные `Msg()`/логи на всех ключевых шагах:
  - start host
  - listen socket/server init
  - client connect attempt
  - handshake receive/send
  - accept/reject
  - disconnect reason

## 2.2 Что НЕ входит (Out of Scope)

Следующие задачи **не делаются** в этом этапе:

- Полный ALife в коопе
- Синхронизация NPC/монстров
- Синхронизация инвентаря/лутания
- Синхронизация стрельбы/урона
- Квесты/сюжет
- Полноценный спавн двух игроков в игровой мир с управлением
- Репликация мира/сущностей (кроме минимально необходимого stub)
- Client prediction / reconciliation
- Вынос Lua-логики из существующих модов (Phase 1 должен быть максимально на "чистом anomaly")
- Dedicated standalone server binary (если это существенно усложняет путь)
- Интернет/NAT traversal (только LAN)
- Linux/Proton поддержка (не блокер Phase 1)

**Явные Non-Goals (anti-scope-creep):**
- Нет обязательства видеть второго игрока в мире
- Нет обязательства загружать полноценный игровой уровень после `JOIN_ACCEPT`
- Нет обязательства выполнять spawn actor
- Нет обязательства вызывать Lua callbacks при успешном join
- Нет обязательства поддерживать сейвы/загрузки

---

# 3. Архитектурный принцип Phase 1

## 3.1 Принцип

Даже на первом этапе сохраняем главный контракт будущей кооп-архитектуры:

- **Хост (сервер) — authoritative**
- **Клиент — пассивный участник**, который:
  - инициирует подключение,
  - отправляет запросы уровня подключения,
  - получает session state от хоста.

> На этом этапе client-side "пассивность" означает прежде всего отсутствие попыток локально "создать мир" или принимать gameplay-решения.

## 3.2 Почему так важно уже в Phase 1

Если сразу сделать корректный server-authoritative handshake/state flow, то дальше Phase 2+ (spawn, replication, interact, inventory) будут наращиваться без ломки базового протокола.

---

# 4. Архитектура Phase 1 (минимальный срез)

Ниже — рекомендуемый минимальный срез подсистем.

## 4.1 Слой транспорта (использовать существующий)

Используем существующую сетевую инфраструктуру движка:
- `IPureServer`
- `IPureClient`
- `NET_Packet`
- текущий механизм отправки/приёма пакетов

**Ничего не переписываем** в транспортном слое, если можно обойтись расширением.

### Цель
Поднять working path "server listens / client connects / packets exchanged".

---

## 4.2 Новый кооп-режим (минимальный каркас)

Нужен минимальный каркас кооп-режима:

### Серверная сторона
- `game_sv_coop` (или временный аналог)
- ответственный за:
  - старт кооп-сессии
  - регистрацию подключённых клиентов
  - handshake
  - отправку минимального session state

### Клиентская сторона
- `game_cl_coop` (или временный аналог)
- ответственный за:
  - connect by IP
  - отправку `HELLO/JOIN`
  - ожидание `ACCEPT/REJECT`
  - переход в состояние "joined"

> Если создание полноценных классов сразу слишком тяжёлое, допускается временная реализация через адаптер/обёртку. Но **названия и контракты** (`sv_coop`, `cl_coop`) желательно зафиксировать уже сейчас.

---

## 4.3 Протокол Phase 1 (минимум)

Нужен отдельный набор packet IDs/handlers для кооп-handshake.

### 4.3.1 Минимальный набор сообщений

#### Клиент -> Сервер
1. `CL_COOP_HELLO`
   - версия протокола
   - версия билда/игры (минимум строка/хэш/число)
   - `CONTENT_HASH` / `MODSET_SIGNATURE` (строка версии модпака или агрегированный хэш; на Phase 1 допускается hardcoded build marker)
   - имя игрока (опционально)
   - capability flags (опционально, можно 0)

2. `CL_COOP_JOIN_REQUEST`
   - запрошенная сессия/уровень (если нужно)
   - nickname (если не отправлялся в hello)
   - reserved fields для будущего

#### Сервер -> Клиент
1. `SV_COOP_HELLO_ACK`
   - версия протокола сервера
   - server name (опционально)
   - session id (или временный id)

2. `SV_COOP_JOIN_ACCEPT`
   - assigned player id / peer id
   - session state
   - level name (если уже известно)
   - seed/time (опционально)
   - флаг "load level now" / "wait"

3. `SV_COOP_JOIN_REJECT`
   - reason code
   - human-readable text (для логов/UI)

4. `SV_COOP_PING` / `SV_COOP_PONG` (опционально для базовой диагностики)

### 4.3.2 Причины отказа (reason codes)

Зафиксировать enum причин уже в Phase 1:
- `VERSION_MISMATCH`
- `PROTOCOL_MISMATCH`
- `CONTENT_MISMATCH` (несовпадение CONTENT_HASH/MODSET_SIGNATURE)
- `SERVER_FULL`
- `SESSION_NOT_READY`
- `INVALID_REQUEST`
- `INTERNAL_ERROR`
- `TIMEOUT`

Это сильно поможет отладке и UX.

---

## 4.4 Состояния сессии (server + client state machine)

Нужно формально определить state machine, иначе быстро появятся хаотичные переходы.

### 4.4.1 Сервер (для peer)
Рекомендуемые состояния:
- `PeerConnected` — транспортное подключение установлено
- `HelloReceived`
- `JoinRequested`
- `JoinAccepted`
- `InSession`
- `Disconnecting`
- `Disconnected`

### 4.4.2 Клиент
Рекомендуемые состояния:
- `Idle`
- `Connecting`
- `ConnectedTransport`
- `HelloSent`
- `JoinRequested`
- `JoinAccepted`
- `InSession`
- `Failed`
- `Disconnected`

### 4.4.3 Правило
Любая отправка/обработка пакета должна валидироваться по состоянию.  
**Любой пакет, пришедший вне допустимого состояния, считается protocol violation** (см. раздел 10).

Пример:
- `CL_COOP_JOIN_REQUEST` допустим только после `HELLO_ACK`
- `SV_COOP_JOIN_ACCEPT` не должен приходить до `CL_COOP_JOIN_REQUEST`

---

# 5. User Flow (Menu -> Host / Join)

## 5.1 Host Flow (из меню)

### Цель
Пользователь должен суметь запустить хост без консольных команд и ручных костылей.

### Минимальный UI сценарий
1. В главном меню/сети появляется кнопка:
   - `Host Coop (LAN)`
2. Пользователь нажимает кнопку
3. Игра:
   - инициализирует кооп-режим
   - запускает локальный сервер
   - выбирает уровень/сессию (либо использует дефолт/текущий)
4. Отображается экран/статус:
   - `Hosting on <IP>:<PORT>`
   - `Waiting for clients...`

### Минимальный набор параметров (можно захардкодить на Phase 1)
- Порт (дефолтный)
- Максимум игроков = 2
- Название сессии = `Coop Test`
- Уровень = дефолтный тестовый (или текущий стартовый)

> На Phase 1 не тратить время на полноценный lobby UI.

---

## 5.2 Client Flow (из меню)

### Минимальный UI сценарий
1. Кнопка `Join Coop (LAN)`
2. Поле ввода:
   - `Host IP`
   - (опционально) `Port`
3. Кнопка `Connect`
4. Игра:
   - создаёт `game_cl_coop`
   - выполняет connect к IP
   - запускает handshake
5. Отображение статуса:
   - `Connecting...`
   - `Handshaking...`
   - `Accepted by host`
   - `Loading session...` / `Joined`

### Обязательные сценарии ошибок
- Пустой IP
- Некорректный IP формат
- Таймаут подключения
- Отказ сервера (с reason code)
- Несовпадение версии / контента

---

# 6. Минимальная серверная логика Phase 1

## 6.1 Что должен делать сервер после старта хоста

После `Host Coop` сервер обязан:

1. Инициализировать сетевой сервер (listen)
2. Перейти в состояние `Hosting`
3. Создать `CoopSession` (минимальный объект сессии)
4. Подготовить параметры сессии:
   - `session_id`
   - `server_protocol_version`
   - `content_hash` / `modset_signature`
   - `level_name` (если есть)
   - `max_players`
5. Принимать подключения клиентов и вести peer state machine

## 6.2 Серверный объект сессии (минимальная структура)

Рекомендуемый минимальный `CoopSessionState`:

- `session_id`
- `server_name`
- `level_name`
- `host_player_id`
- `max_players`
- `current_players`
- `protocol_version`
- `build_signature`
- `content_hash` / `modset_signature`
- `is_joinable`

Дополнительно (заготовка под будущее):
- `world_revision = 0`
- `session_phase` (`Lobby`, `Loading`, `Active`)
- `seed`, `game_time`

> Даже если часть полей пока не используется, полезно зафиксировать структуру заранее.

---

# 7. Минимальная клиентская логика Phase 1

## 7.1 Что должен делать клиент при Connect

1. Валидировать IP/порт
2. Создать/инициализировать `game_cl_coop`
3. Инициировать транспортное подключение
4. При успешном transport connect:
   - отправить `CL_COOP_HELLO` (с protocol version + content_hash/modset_signature)
5. После `SV_COOP_HELLO_ACK`:
   - отправить `CL_COOP_JOIN_REQUEST`
6. После `SV_COOP_JOIN_ACCEPT`:
   - сохранить:
     - `assigned_player_id`
     - `session_id`
     - `level_name`
   - перейти в `JoinAccepted/InSession` (в зависимости от реализации)
7. Отобразить UI/лог успеха

## 7.2 Что делать при отказе
- Сохранить `reject_reason_code`
- Показать понятное сообщение пользователю
- Вернуться в `Idle`/меню без краша и зависания

---

# 8. Версионирование и совместимость (обязательно уже в Phase 1)

Чтобы избежать "немых падений", вводим строгую проверку совместимости.

## 8.1 Что сравнивать
Минимум:
- `COOP_PROTOCOL_VERSION` (целое число)
- `ENGINE_BUILD_SIGNATURE` (строка/хэш/версия билда)
- `CONTENT_HASH` / `MODSET_SIGNATURE` (строка версии модпака или агрегированный хэш; на Phase 1 допускается временный hardcoded build marker)

## 8.2 Политика
Если `COOP_PROTOCOL_VERSION` не совпал:
- сервер отправляет `SV_COOP_JOIN_REJECT(PROTOCOL_MISMATCH)`

Если `CONTENT_HASH`/`MODSET_SIGNATURE` не совпал:
- сервер отправляет `SV_COOP_JOIN_REJECT(CONTENT_MISMATCH)`
- клиент показывает понятное сообщение

> Рекомендуется сразу делать hard reject по обоим параметрам, чтобы не дебажить фантомные баги, вызванные несовместимым контентом.

---

# 9. Логирование и телеметрия (критично для Phase 1)

## 9.1 Обязательные точки логирования (Server)

Каждый из пунктов должен логироваться с префиксом `[COOP][SV]`:

- Старт хоста (порт, ip, режим)
- Создание сессии
- Новый transport-peer connected (с peer handle/id)
- Transport-peer disconnected (с причиной)
- Получен `CL_COOP_HELLO` (версия, билд, content_hash, nick)
- Результат проверки версий/content_hash (match / mismatch + details)
- Отправлен `SV_COOP_HELLO_ACK`
- Получен `CL_COOP_JOIN_REQUEST`
- Join accepted/rejected (с reason code)
- Назначен `player_id`
- Peer disconnect (причина)
- Protocol violation: неожиданный пакет в состоянии X (с типом пакета и состоянием peer)
- Timeout при ожидании пакета (с названием шага: `waiting_hello`, `waiting_join_request`)
- Размер handshake-пакетов в байтах (debug mode)

## 9.2 Обязательные точки логирования (Client)

Префикс `[COOP][CL]`:

- Попытка connect (IP:port)
- Transport connected
- Отправлен `HELLO` (с версией и content_hash)
- Получен `HELLO_ACK`
- Отправлен `JOIN_REQUEST`
- Получен `JOIN_ACCEPT` (player_id, session_id)
- Получен `JOIN_REJECT` (reason code + human-readable text)
- Таймаут / disconnect / parse error
- Start time / elapsed time на каждом шаге handshake
- Timeout step (`waiting_hello_ack`, `waiting_join_accept`)
- Размер handshake-пакетов в байтах (debug mode)

## 9.3 Packet Trace Ring Buffer (обязательно для dev/debug сборки)

В dev/debug сборке добавить ring buffer (последние 64–128 событий), где сохраняются:
- timestamp
- direction (`C->S`, `S->C`)
- packet type id
- packet size (байты)
- local state / remote peer state на момент отправки/получения
- result (`ok`, `rejected`, `invalid_state`)

**При любой ошибке или таймауте — автоматически дампить ring buffer в лог.**

Это резко ускорит разбор проблем на ранних фазах.

## 9.4 Практическое требование
Лог Phase 1 должен позволять определить:
- упал ли transport
- не сошлись ли версии или content hash
- нарушена ли state machine
- на каком шаге handshake остановился

---

# 10. Ошибки, таймауты и политика retry

## 10.1 Таймауты

Рекомендуемые базовые таймауты:
- `connect_timeout_ms` (например 5000–10000)
- `hello_timeout_ms`
- `join_accept_timeout_ms`

Если таймаут истёк:
- client переходит в `Failed`
- логирует конкретный шаг (`timeout waiting HELLO_ACK`)
- показывает сообщение пользователю

## 10.2 Обработка protocol violation (fail-fast)

**Любой пакет, пришедший в недопустимом состоянии, считается protocol violation.**

Политика Phase 1:
- логировать нарушение с полным контекстом (тип пакета, текущее состояние peer/client)
- **немедленно закрывать соединение** (fail-fast)
- не пытаться "угадывать", что хотел отправитель

> Fail-fast упрощает дебаг и не позволяет накопить невалидные состояния в early prototype.

## 10.3 Политика retry (отсутствие автоматических ретраев)

**На Phase 1 автоматические ретраи handshake не выполняются.**

- Одна попытка подключения = один handshake flow.
- При ошибке/таймауте клиент возвращается в `Failed/Idle`.
- Пользователь вручную нажимает `Connect` для повторной попытки.

> Автоматические ретраи на раннем этапе усложняют state machine и маскируют ошибки.

---

# 11. Безопасный минимум по реализации (чтобы не закопаться)

## 11.1 Что НЕ нужно делать в коде сейчас
Чтобы Phase 1 был достижим быстро:

- Не интегрировать ALife
- Не трогать репликацию сущностей
- Не делать спавн полноценного игрового актора на клиенте (если мешает)
- Не тащить Lua callbacks в handshake
- Не делать сложный lobby/menu framework

## 11.2 Разрешённый временный "stub"
Допускается, что после `JOIN_ACCEPT`:
- клиент попадает в состояние "connected to coop session (stub)"
- без полноценной загрузки мира

**НО** если в текущей архитектуре проще завершить тест через загрузку уровня — это тоже допустимо.

Главное: **успешный host + connect по LAN IP без падения и с корректным handshake.**

## 11.3 Разрешённый dev-entrypoint как временный UI fallback

UI в Phase 1 обязателен (host/join из меню), **но не должен блокировать завершение этапа**, если menu integration затягивается.

**Разрешённый fallback** при условии, что:
- основной код host/connect уже реализован;
- есть хотя бы минимальный технический UI путь (кнопка-заглушка / dev screen);
- task по нормализации UI остаётся внутри Phase 1 (не выносится "на потом").

Пример допустимого временного UI:
```
[ Developer Coop Test ]
  [Host]   [IP: _______]   [Connect]
  Status: ...
```

---

# 12. Рекомендуемая структура задач (Task Breakdown)

## 12.1 Phase 1.A — Скелет кооп-режима
### Цель
Создать минимальные классы/точки входа.

#### Задачи
- [ ] Ввести `game_sv_coop` (минимальный класс)
- [ ] Ввести `game_cl_coop` (минимальный класс)
- [ ] Зарегистрировать режим/точки создания
- [ ] Добавить базовые логи и state enums

### Результат
Есть объектная основа для host/connect, без UI.

---

## 12.2 Phase 1.B — Протокол handshake
### Цель
Реализовать обмен пакетами и state machine.

#### Задачи
- [ ] Добавить packet IDs для кооп-handshake
- [ ] Реализовать сериализацию/десериализацию `HELLO`, `HELLO_ACK`, `JOIN_REQUEST`, `JOIN_ACCEPT`, `JOIN_REJECT`
- [ ] Внедрить server peer state machine
- [ ] Внедрить client state machine
- [ ] Добавить проверки version/protocol/content_hash
- [ ] Добавить fail-fast для protocol violation
- [ ] Добавить таймауты и обработку ошибок (без auto-retry)

### Результат
Рабочий handshake при запуске вручную/через dev entrypoint.

---

## 12.3 Phase 1.C — Меню: Host / Join (LAN)
### Цель
Пользовательский доступ к функции без консоли.

#### Задачи
- [ ] Кнопка `Host Coop (LAN)`
- [ ] Экран/форма статуса хоста (можно минимальная / dev screen)
- [ ] Кнопка `Join Coop (LAN)`
- [ ] Поле ввода IP (и порт опционально)
- [ ] Кнопка `Connect`
- [ ] Показ статуса/ошибки (с reason code)

### Результат
Полный user flow host/connect из UI.

---

## 12.4 Phase 1.D — Интеграционный тест 2 инстансов
### Цель
Подтвердить работоспособность в реальном запуске.

#### Задачи
- [ ] Запуск двух экземпляров игры на одной машине/двух ПК в одной сети
- [ ] Host на одном экземпляре
- [ ] Connect по LAN IP со второго
- [ ] Снятие логов host/client
- [ ] Фиксация результата и известных ограничений

### Результат
Первый подтверждённый кооп-тест.

---

# 13. Предлагаемые артефакты Phase 1

После завершения этапа в репозитории должны появиться:

## 13.1 Код
- Каркас `game_sv_coop` / `game_cl_coop`
- Базовый handshake protocol (с content_hash check)
- Menu integration (минимум Host/Join)
- Логирование и error handling
- Packet trace ring buffer (dev builds)

## 13.2 Документация
- `phase_1.md` (этот документ)
- `phase_1_test_report.md` (после завершения)
- `phase_1_known_issues.md` (если нужно)

## 13.3 Тестовые логи (желательно)
- `host_success.log`
- `client_success.log`
- `client_reject_version_mismatch.log`
- `client_reject_content_mismatch.log`

## 13.4 Phase 1 Exit Artifacts (для старта Phase 2)
Зафиксировать до начала Phase 2:
- `coop_packet_ids.h` (handshake packet IDs зафиксированы)
- client/server state enums (зафиксированы в коде)
- примеры успешных/неуспешных handshake логов
- список known blockers для level load/spawn (если обнаружены при тестировании)
- список затронутых code paths с legacy `g_actor`/`db.actor` assumptions

---

# 14. Acceptance Test Plan (подробно)

## 14.1 Test Case P1-001: Host starts from menu
**Steps**
1. Запустить игру
2. Нажать `Host Coop (LAN)`

**Expected**
- Сервер стартует
- В логах есть `[COOP][SV] Host started`
- UI показывает `Waiting for clients`

---

## 14.2 Test Case P1-002: Client connects by LAN IP
**Steps**
1. На хосте поднять сессию
2. На клиенте открыть `Join Coop (LAN)`
3. Ввести IP хоста
4. Нажать `Connect`

**Expected**
- Transport connect succeeds
- Handshake проходит (`HELLO -> ACK -> JOIN -> ACCEPT`)
- Клиент получает `player_id`
- UI/лог сообщает об успешном подключении

---

## 14.3 Test Case P1-003: Invalid IP handling
**Steps**
1. Ввести некорректный IP (например `999.999.999.999`)
2. Нажать `Connect`

**Expected**
- Нет краша
- Есть понятное сообщение об ошибке
- В лог пишется причина

---

## 14.4 Test Case P1-004: Version mismatch reject
**Steps**
1. Искусственно изменить `COOP_PROTOCOL_VERSION` на клиенте или сервере
2. Попробовать подключение

**Expected**
- Сервер отправляет `JOIN_REJECT(PROTOCOL_MISMATCH)` или reject на `HELLO`
- Клиент показывает понятное сообщение
- Нет зависания и краша

---

## 14.5 Test Case P1-005: Connect timeout
**Steps**
1. На клиенте указать IP без сервера
2. Нажать `Connect`

**Expected**
- Таймаут по истечении заданного времени
- Сообщение пользователю
- Возврат в UI без краша

---

## 14.6 Test Case P1-006: Content / modset mismatch reject
**Steps**
1. Искусственно изменить `CONTENT_HASH` / `MODSET_SIGNATURE` на клиенте или сервере
2. Попробовать подключение

**Expected**
- Сервер отправляет `JOIN_REJECT(CONTENT_MISMATCH)`
- Клиент показывает понятное сообщение с reason
- Нет зависания и краша

---

## 14.7 Phase 1 Test Matrix

| Сценарий | Тип | Ожидаемый результат |
|----------|-----|---------------------|
| Windows host / Windows client (LAN) | Must pass | Successful handshake |
| Same machine (2 instances) | Nice to have | Successful handshake |
| Invalid IP | Must pass | Expected fail, no crash |
| Protocol mismatch | Must pass | Expected reject, no crash |
| Content mismatch | Must pass | Expected reject, no crash |
| No server / timeout | Must pass | Expected timeout, no crash |
| Linux/Proton host/client | Not required for Phase 1 | — |
| Internet/NAT | Not required for Phase 1 | — |

---

# 15. Риски Phase 1 и как их снижать

## 15.1 Риск: запутанная интеграция с существующими режимами игры
**Проблема:** код SP/MP может быть жёстко связан с текущими game mode классами.  
**Решение:** минимальный новый каркас + адаптер; не пытаться сразу глубоко интегрировать ALife/SP.

## 15.2 Риск: UI/menu интеграция отнимает слишком много времени
**Решение:** сначала сделать dev entrypoint (команда/кнопка-заглушка), потом минимальный UI.

## 15.3 Риск: неочевидные падения на обработке пакетов
**Решение:** строгая state machine + fail-fast protocol violation + подробное логирование + reason codes + таймауты.

## 15.4 Риск: смешение Lua и C++ логики уже на этапе handshake
**Решение:** Phase 1 handshake — только C++ слой, без Lua callbacks.

## 15.5 Риск: legacy single-actor assumptions (`g_actor`, `db.actor`) ломают listen-server
**Проблема:** даже если Phase 1 не делает полноценный gameplay/spawn, кодовые пути listen-server могут задевать legacy-предположения про одного актора (`g_actor`, `Actor()`, `db.actor` в Lua).

**Решение:**
- минимизировать прохождение через gameplay-код, который требует полноценного `g_actor`;
- handshake и session setup держать в C++ сетевом/режимном слое;
- если загрузка уровня/спавн вызывает каскад legacy-проблем — **допускается завершать Phase 1 на состоянии `Joined (stub)` без полноценного спавна**;
- фиксировать найденные legacy-места в `phase_1_known_issues.md` для Phase 2.

## 15.6 Риск: нестабильные результаты из-за несовместимого контента
**Проблема:** команда ошибочно примет ошибки контента/модов за ошибки handshake/сети.  
**Решение:** обязательная проверка `CONTENT_HASH`/`MODSET_SIGNATURE` с явным `CONTENT_MISMATCH` reject.

---

# 16. Ограничения и сознательные компромиссы Phase 1

Это **не** "первый играбельный кооп".
Это **первый успешный сетевой вертикальный срез**.

Сознательные компромиссы:
- Можно использовать hardcoded session params
- Можно не загружать полноценный игровой мир после join
- Можно не иметь красивый lobby UI
- Можно иметь только 2 игрока (host + 1 client)
- Можно оставить временные диагностические панели/логи
- Транспорт (DirectPlay8) — временный; замена запланирована позднее

---

# 17. Что будет следующим этапом (чтобы не переусердствовать сейчас)

После успешного завершения Phase 1 следующий логичный этап (Phase 2):
- базовый spawn игрока на сервере
- решение `g_actor`/`LocalPlayerContext` проблемы
- минимальная репликация позиции/состояния host/client
- отображение второго игрока как сетевой сущности
- сохранение server-authoritative принципа

**НЕ включать это в текущий этап.**

---

# 18. Checklist для завершения Phase 1 (финальный)

## Core (Handshake)
- [ ] `game_sv_coop` / `game_cl_coop` созданы и подключены
- [ ] Реализован handshake protocol
- [ ] Реализованы state machines server/client
- [ ] Реализованы version/protocol/content_hash checks
- [ ] Реализованы timeout/error paths
- [ ] Host из меню запускает listen-server
- [ ] Client из меню подключается по LAN IP
- [ ] Handshake проходит (`HELLO/ACK/JOIN/ACCEPT`) без краша
- [ ] Есть отказ по protocol mismatch
- [ ] Есть отказ по content mismatch (или временной сигнатуре)

## Stability
- [ ] Нет крашей в success path
- [ ] Нет зависаний на timeout/reject
- [ ] Invalid state packets корректно режутся (protocol violation → disconnect)

## Diagnostics
- [ ] Добавлены подробные логи (все шаги handshake, peer connect/disconnect)
- [ ] Видно последний успешный шаг при ошибке
- [ ] Есть packet trace ring buffer dump (debug/dev)
- [ ] Причины отказа отображаются в UI/логах

## UI
- [ ] Host Coop из меню (или dev entrypoint)
- [ ] Join Coop (LAN) из меню (или dev entrypoint)
- [ ] Ввод IP (и порт опционально)
- [ ] Сообщения об ошибках/успехе с reason codes

## Тест
- [ ] 2 инстанса, успешный host/connect по LAN IP
- [ ] Логи host/client сохранены
- [ ] Известные ограничения зафиксированы

## Scope discipline
- [ ] Phase 1 завершён без "подтягивания" gameplay-фич
- [ ] Phase 1 Exit Artifacts зафиксированы (packet ids, state enums, known blockers, g_actor touchpoints)

---

# 19. Ключевой архитектурный контракт (закрепить)

Даже в Phase 1 соблюдаем правило:

> **Сервер создаёт и подтверждает состояние кооп-сессии. Клиент не считает себя "вошедшим", пока не получил явный `JOIN_ACCEPT` от сервера.**

Definition of Done Phase 1 включает не только успешный `JOIN_ACCEPT`, но и достаточную диагностируемость неуспешных сценариев (timeout, protocol mismatch, invalid state, content mismatch).

Это правило является фундаментом для всех последующих фаз (spawn, replication, inventory, ALife, combat).

---

# 20. Примечание для реализации с Copilot

При реализации Copilot должен придерживаться следующих приоритетов:

1. **Стабильность и детерминированность flow** важнее "быстрого визуального результата"
2. **Логирование каждого шага handshake** обязательно
3. **Минимальная поверхность изменений** в existing SP/MP коде
4. **Никаких Lua-обходов** для сетевого handshake
5. **Никаких gameplay features** сверх stated scope
6. **Fail-fast** при protocol violation; не пытаться "починить" невалидный поток

Если возникает выбор между:
- "показать загрузку уровня" и
- "надёжно завершить handshake без краша",

то выбирать второе.

Если обнаружены legacy g_actor/db.actor blocker при listen-server — завершить Phase 1 на `Joined (stub)` и зафиксировать в known issues.

---

## Итог

Phase 1 должен доказать, что в `xray-monolith-coop` можно построить **корректный server-authoritative кооп** начиная с самого низа пользовательского сценария:
- Host из меню
- Join по LAN IP
- Успешный handshake с проверкой версий и контента
- Без падений и хаоса
- С полной диагностируемостью неуспешных сценариев

Это и будет фундаментом для дальнейшей реализации коопа с ALife.

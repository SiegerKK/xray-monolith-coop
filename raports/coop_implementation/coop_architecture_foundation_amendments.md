# Amendments to `coop_architecture_foundation.md`

## Назначение

Этот документ фиксирует **обязательные правки и уточнения** к базовому архитектурному документу `coop_architecture_foundation.md` после ревью архитектуры.

Цель правок:
- сохранить сильные стороны исходной архитектуры;
- закрыть недостающие места (DirectPlay8 roadmap, `g_actor` singleton, Anomaly-специфика, prediction, snapshot fragmentation, level transition);
- сделать документ ещё более пригодным для реализации через Copilot без архитектурных “дыр”.

> Это **не замена** основного документа, а **официальный patch/addendum** к нему.  
> При следующей ревизии архитектуры правки из этого файла должны быть интегрированы в основной документ.

---

# 1. Статус исходного документа

Исходный `coop_architecture_foundation.md` остаётся **валидным и базово корректным**.

### Что НЕ меняется
- Server-authoritative принцип
- Разделение слоёв (Transport → Protocol → Simulation → Replication → Script Bridge)
- ALife server-only + online/offline bubble
- Lua как реактивный слой
- Поэтапный roadmap
- MVP-first подход

### Что нужно усилить
1. Явный план по устаревшему транспорту (DirectPlay8)
2. Конкретный план по `g_actor` singleton / listen-server ограничениям
3. Инвентаризация Anomaly single-actor assumptions (`db.actor`, `info_portions`, квестовые скрипты)
4. Поднятие client prediction в статус UX-critical этапа
5. Формализация контракта фрагментации full snapshot
6. Учет смены уровней (`M_CHANGE_LEVEL`) в roadmap
7. Политика совместимости модов (identical modset / content hash)

---

# 2. Обязательная правка: статус транспорта (DirectPlay8) как временный

## 2.1 Новый архитектурный инвариант (добавить в раздел транспорта)

**Инвариант T-01**
> На этапах MVP / ранних фаз допускается использование текущего транспорта X-Ray (DirectPlay8 через `IPureServer/IPureClient`),  
> **НО** транспорт считается **временным техническим компромиссом**, а не долгосрочной целевой платформой для коопа.

## 2.2 Причина

DirectPlay8 подходит для быстрого старта и минимизации риска в MVP, но:
- устарел;
- неудобен для современной отладки;
- ограничивает переносимость;
- является слабым местом для Linux/Steam Deck аудитории (через Proton/Wine).

## 2.3 Что нужно добавить в основной документ (новый подраздел)

### `Transport Migration (Post-MVP / Post-Phase 2+)`

Добавить отдельный подраздел с такими пунктами:

#### Цель миграции
Перейти на современный transport backend без переписывания кооп-логики.

#### Требование к архитектуре
Protocol/Simulation/Replication слои **не должны зависеть от DirectPlay8 API** напрямую.

#### Кандидаты
- **Steam GameNetworkingSockets** (приоритетный кандидат для future Internet-coop + relay/NAT traversal)
- **ENet** (альтернатива с простым UDP reliable/unreliable стеком)

#### Минимальный контракт транспортного адаптера (зафиксировать)
- `StartServer(bind_address, port)`
- `StopServer()`
- `Connect(address, port)`
- `Disconnect(reason)`
- `Send(peer, channel, reliability, payload)`
- `Broadcast(filter, channel, reliability, payload)`
- `PollIncoming()`
- `GetConnectionState(peer)`
- `GetPing(peer)` (если доступно)
- `GetTransportStats(peer)` (опционально)

## 2.4 Производительность транспортного адаптера (новое требование)
Добавить явное требование:

**Инвариант T-02**
> `CoopTransport` adapter не должен вводить лишние полные копирования буфера пакета на горячем пути (hot path), кроме неизбежных границ API.

Цель:
- не потерять производительность на частых delta updates;
- избежать лишнего pressure на allocator.

---

# 3. Обязательная правка: `g_actor` singleton и listen-server ограничения

## 3.1 Новый раздел: `Core Engine Constraints (X-Ray legacy assumptions)`

В основной документ добавить отдельный раздел, где первым пунктом зафиксировать:

### Constraint C-01: Global single actor assumption
В legacy-коде X-Ray/Anomaly множество мест предполагают единственного “локального” актора:
- `g_actor`
- `Actor()`-подобные глобальные доступы
- `db.actor` в Lua
- UI/квестовые системы, implicitly привязанные к одному игроку

Это становится критическим риском для **listen-server**, где:
- в одном процессе одновременно живут:
  - серверная симуляция мира;
  - клиент хоста;
  - (в будущем) несколько удалённых клиентов.

## 3.2 Обязательная ранняя стратегия смягчения (не откладывать “на потом”)

Добавить в roadmap отдельный пункт ранних фаз:

### `LocalPlayerContext / ActorContext abstraction` (Phase 2 target)
Нужно ввести абстракцию контекста игрока, чтобы постепенно убрать зависимость gameplay/скриптовых слоёв от глобального `g_actor`.

#### Минимальная цель
- в кооп-режиме новые/изменяемые участки кода не должны полагаться на `g_actor` напрямую;
- использовать контекст:
  - `player_id`
  - `actor_entity_id`
  - `is_local_player`
  - `is_host_local_player`
  - `is_server_context`

#### Принцип
- В **server code path** запрещено использовать `g_actor` как источник истины.
- В **client code path** `g_actor` допустим только как UI/render convenience для локального игрока (и только при явной локальной роли).

## 3.3 Практическая задача для реализации
Добавить в список технических задач:
- `TODO_COOP_AUDIT_G_ACTOR_USAGE`: инвентаризация всех мест использования `g_actor`/`Actor()` в путях, затрагиваемых коопом.

---

# 4. Обязательная правка: Anomaly-специфика (single-actor assumptions)

## 4.1 Новый документ как часть архитектурного пакета

Добавить в основной документ ссылку/требование на отдельный артефакт:

### `anomaly_compatibility_analysis.md` (обязательный)
Документ должен содержать инвентаризацию Anomaly-специфики, критичной для коопа.

## 4.2 Что обязательно инвентаризировать
### A. Lua `db.actor` usage
- Прямые обращения `db.actor`
- Неявные wrappers, которые внутри используют `db.actor`
- Скрипты, падающие при отсутствии/подмене `db.actor`

### B. `info_portions` и глобальные флаги мира
Нужно определить policy:
- какие флаги глобальны на сервере;
- какие могут стать per-player;
- как реплицируются и кто authoritative.

### C. Квестовые скрипты / диалоги / UI
- Скрипты, ожидающие одного активного игрока
- Скрипты, меняющие мир глобально через side effects
- Места, где нужен `player context`

### D. ALife script extensions
- smart terrains / jobs / gulags
- скриптовые расписания/работы
- реакции на player proximity

## 4.3 Архитектурный инвариант (добавить)
**Инвариант A-01**
> До прохождения `anomaly_compatibility_analysis.md` нельзя считать оценку трудоёмкости коопа с ALife в Anomaly окончательной.

---

# 5. Обязательная правка: Client Prediction = UX-critical milestone

## 5.1 Изменение статуса в roadmap
В основном документе client prediction должен быть перенесён из категории “опциональное улучшение / nice-to-have” в категорию:

### `UX-Critical Milestone`
Минимальная локальная предикция управляемого игрока — обязательна для играбельного коопа.

## 5.2 Что именно требуется (минимум)
Не полноценная сложная сеть шутера, а **минимальный набор**:
- prediction движения локального игрока
- reconciliation по authoritative state сервера
- correction smoothing (без жёстких телепортов при малых расхождениях)
- sequence numbers для input frames

## 5.3 Что не требуется на первом этапе prediction
- prediction для NPC/монстров
- prediction для чужих игроков
- prediction инвентаря/лута
- rollback combat

## 5.4 Новый roadmap-пункт (добавить)
`Phase 2.x / Phase 3`: **Minimal Local Player Prediction & Reconciliation (Required for Playability)**

---

# 6. Обязательная правка: контракт фрагментации Full Snapshot

## 6.1 Новый протокольный контракт (добавить в архитектурный документ + потом детализировать в `coop-protocol-mvp.md`)

В архитектурном документе нужно зафиксировать, что **full snapshot при входе в сессию** является потенциально большим и должен передаваться фрагментами.

## 6.2 Обязательные поля фрагмента
В качестве контракта высокого уровня (без привязки к бинарной раскладке пока) зафиксировать:

- `snapshot_transfer_id`
- `snapshot_revision`
- `fragment_index`
- `fragment_count`
- `fragment_payload_size`
- `payload`
- `checksum` (опционально на фрагмент или на весь снапшот)

## 6.3 Правила сборки
Добавить обязательные правила:

1. Клиент **не применяет** partial snapshot.
2. Snapshot применяется только после получения **всех фрагментов** и проверки целостности.
3. При таймауте сборки:
   - partial snapshot discard
   - запрос повторной передачи (или reconnect/retry)
4. Пока full snapshot не применён, клиент остаётся в состоянии `Syncing / SnapshotLoading`.

## 6.4 Ограничения, которые должны быть позже конкретизированы
(Зафиксировать как TODO в архитектурном документе)
- `MAX_SNAPSHOT_FRAGMENT_SIZE`
- `SNAPSHOT_ASSEMBLY_TIMEOUT_MS`
- `MAX_INFLIGHT_SNAPSHOT_TRANSFERS`
- retry policy / cancel policy

---

# 7. Обязательная правка: Level Transition в roadmap

## 7.1 Новый раздел или roadmap item: `Level Transition Synchronization`
Смена уровней (`M_CHANGE_LEVEL` и связанные механизмы X-Ray) должна быть добавлена как отдельная future-задача.

## 7.2 Почему это важно
Даже если это не ранние фазы:
- отсутствие явного пункта создаёт ложное ощущение “переходы сами заработают”;
- в Anomaly/CoC это нетривиальный сценарий (синхронизация всех клиентов, состояние мира, перенос игроков).

## 7.3 Минимальный статус в документе
Добавить как:
- **Known Future Complexity**
- не входит в MVP / ранние фазы
- требует отдельного протокола/стейт-машины уровня

---

# 8. Обязательная правка: Host Migration как известный риск (без реализации)

## 8.1 Добавить в раздел рисков / future work
Сейчас host migration не нужен для MVP — это корректно.

Но документ должен явно содержать:
- если хост (listen server) выходит — сессия завершается;
- host migration не поддерживается;
- это осознанное ограничение;
- потенциально будет рассмотрено после стабилизации dedicated/listen коопа.

Это защитит команду от “скрытых ожиданий”.

---

# 9. Обязательная правка: Политика совместимости модов / content hash

## 9.1 Новый инвариант совместимости
**Инвариант V-01**
> Кооп поддерживается только при идентичном (или строго совместимом по policy) наборе контента/модов у всех участников сессии.

Для ранних фаз рекомендуется **самая строгая политика**:
- только полностью идентичный набор модов/ресурсов.

## 9.2 Что добавить в handshake/версионирование (уровень архитектуры)
Помимо `protocol_version`, добавить понятие:
- `content_hash` / `modset_signature`

На ранних фазах достаточно:
- одного агрегированного хэша (или строки версии модпака)
- жёсткого reject при несовпадении

## 9.3 Почему это критично
В Anomaly-экосистеме несовместимые моды дают:
- разные секции предметов/NPC,
- разные Lua-скрипты,
- разные UI/логики,
- краши и “фантомные” рассинхроны.

---

# 10. Обязательная правка: частоты тиков (server tick vs ALife tick)

## 10.1 Уточнение по серверному тику
В архитектурном документе нужно явно разделить:
- **main server tick** (сетевой/геймплейный)
- **ALife update cadence** (может быть реже)

## 10.2 Причина
ALife-подсистема тяжелее и не обязана обновляться на той же частоте, что:
- обработка input;
- репликация ближайших сущностей;
- сетевые heartbeat.

## 10.3 Рекомендуемое формальное правило
- `server_tick_rate_hz` — отдельная настройка
- `alife_tick_rate_hz` — отдельная настройка
- серверный pipeline должен корректно работать при `alife_tick_rate_hz < server_tick_rate_hz`

---

# 11. Изменения в roadmap (сводка)

Ниже — сводный список изменений, которые нужно внести в roadmap основного документа.

## 11.1 Новые пункты (добавить)
1. `Transport Migration (ENet / Steam GameNetworkingSockets)` — post-MVP
2. `LocalPlayerContext / ActorContext abstraction` — ранний этап (Phase 2)
3. `Anomaly Compatibility Analysis (db.actor / info_portions / single-actor assumptions)` — обязательный аналитический этап
4. `Minimal Local Player Prediction & Reconciliation` — UX-critical milestone
5. `Level Transition Synchronization` — future complexity
6. `Modset Compatibility Policy / content_hash handshake` — ранний протокольный policy

## 11.2 Изменение статусов
- Client prediction: из “улучшение” -> **обязательный UX milestone**
- Transport replacement: из “может быть позже” -> **явный roadmap item**
- Host migration: не реализовывать, но **явно признать ограничением**

---

# 12. Рекомендуемые формулировки для вставки в основной документ (готовый текст)

## 12.1 Вставка: “Legacy transport status”
> В ранних фазах кооп-режим использует существующий сетевой транспорт X-Ray (`IPureServer/IPureClient`, DirectPlay8) как временный компромисс для ускорения MVP.  
> При этом архитектура протокола и репликации обязана оставаться transport-agnostic, чтобы позже поддержать миграцию на современный backend (ENet или Steam GameNetworkingSockets) без переписывания gameplay-логики.

## 12.2 Вставка: “g_actor constraint”
> В кооп-режиме запрещено полагаться на глобальный `g_actor` в server-authoritative code path.  
> Legacy single-actor assumptions X-Ray/Anomaly должны поэтапно вытесняться через `LocalPlayerContext / ActorContext`.

## 12.3 Вставка: “Prediction policy”
> Минимальная локальная предикция движения управляемого игрока и reconciliation по серверному состоянию считаются обязательным этапом для достижения играбельного UX и не рассматриваются как опциональное улучшение.

## 12.4 Вставка: “Content compatibility policy”
> На ранних фазах кооп поддерживает только участников с идентичным набором модов/контента (strict content compatibility). Несовпадение `content_hash/modset_signature` должно приводить к отказу подключения на этапе handshake.

---

# 13. Что не нужно менять в основном документе (важно)

Чтобы не “размыть” архитектуру, **не менять**:
- фундаментальный server-authoritative подход;
- out-of-scope ранних фаз;
- принцип reuse существующего транспорта для MVP;
- отказ от Lua-сетевого фундамента;
- ALife bubble model как основу.

Правки из этого файла — это **усиление и конкретизация**, а не смена курса.

---

# 14. Action Items для Copilot (по этому addendum)

## Обязательные задачи документации
- [ ] Обновить `coop_architecture_foundation.md` с учётом разделов 2–10 этого файла
- [ ] Создать `anomaly_compatibility_analysis.md` (шаблон/каркас)
- [ ] При подготовке `coop-protocol-mvp.md` включить snapshot fragmentation contract
- [ ] Добавить в roadmap явные пункты про prediction, transport migration, level transition

## Обязательные технические TODO (без реализации в этой задаче)
- [ ] `TODO_COOP_AUDIT_G_ACTOR_USAGE`
- [ ] `TODO_COOP_DEFINE_CONTENT_HASH_POLICY`
- [ ] `TODO_COOP_SNAPSHOT_FRAGMENTATION_LIMITS`
- [ ] `TODO_COOP_TRANSPORT_ABSTRACTION_API`
- [ ] `TODO_COOP_LEVEL_TRANSITION_DESIGN`

---

# 15. Итог

`coop_architecture_foundation.md` остаётся сильным и корректным фундаментом.  
Правки из этого addendum нужны для того, чтобы:
- раньше зафиксировать реальные legacy-риски X-Ray/Anomaly;
- не недооценить трудоёмкость совместимости;
- сохранить реалистичный путь к MVP и при этом не потерять долгосрочную масштабируемость.

Этот файл следует считать **обязательным приложением** к основному архитектурному документу до момента его полной интеграции в новую редакцию.

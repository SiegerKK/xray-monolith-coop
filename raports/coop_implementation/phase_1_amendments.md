# Amendments to `phase_1.md`

## Назначение

Этот документ фиксирует правки и уточнения к `phase_1.md` (этап “первый успешный кооп-тест: host + LAN connect”) после архитектурного ревью.

Цель:
- усилить Phase 1 с точки зрения диагностики и совместимости;
- явно зафиксировать ограничения (DirectPlay8/LAN/listen-server);
- избежать ложных ожиданий;
- сохранить достижимость этапа.

> `phase_1.md` остаётся корректным по scope и целям.  
> Этот документ — **patch/addendum**, который делает Phase 1 более устойчивым к типичным X-Ray/Anomaly проблемам.

---

# 1. Что в `phase_1.md` правильно и сохраняется

Следующие положения остаются без изменений:
- Phase 1 = **host из меню + join по LAN IP + handshake**
- Без ALife / инвентаря / стрельбы / квестов
- Вертикальный срез, а не “играбельный кооп”
- C++ handshake без Lua-костылей
- Acceptance Criteria и test cases как основа

**Важно:** правки ниже не расширяют scope Phase 1 до gameplay-фич.

---

# 2. Обязательная правка: явно зафиксировать статус транспорта и ограничения платформы

## 2.1 Добавить в начало документа (раздел ограничений Phase 1)

### Ограничение P1-LIM-01: транспорт и платформа
Phase 1 использует существующий сетевой транспорт X-Ray (DirectPlay8 / `IPureServer-IPureClient`) как временное решение.

Следствия:
- Phase 1 нацелен на **Windows host/client** как базовый сценарий проверки.
- LAN-подключение — единственный обязательный режим для этапа.
- Linux/Proton совместимость **не является критерием готовности Phase 1**.

> Это не означает отказ от Linux/Proton в будущем; только фиксирует реалистичные границы этапа.

## 2.2 Почему это важно
Без этого уточнения команда может начать тратить время на кроссплатформенную сетевую отладку до того, как handshake вообще станет стабильным.

---

# 3. Обязательная правка: `listen server` ограничения (включая `g_actor`) как известный риск Phase 1

## 3.1 Добавить раздел “Known Phase 1 Risks (Not blockers)”

### Риск P1-RISK-01: legacy single-actor assumptions (`g_actor`, `db.actor`)
Даже если Phase 1 не делает полноценный gameplay/spawn, кодовые пути listen-server могут задевать legacy-предположения про одного актора.

### Требование к реализации Phase 1
- минимизировать прохождение через gameplay-код, который требует полноценного `g_actor`;
- handshake и session setup держать в C++ сетевом/режимном слое;
- если загрузка уровня/спавн вызывает каскад legacy-проблем — допускается завершать Phase 1 на состоянии `Joined (stub)` без полноценного спавна.

Это **ключевое уточнение**, чтобы не сорвать этап попыткой “дотянуть до настоящего спавна”.

---

# 4. Обязательная правка: ужесточить совместимость handshake (моды / контент)

## 4.1 Внести в раздел версионирования (Phase 1)
Сейчас в `phase_1.md` уже есть проверка `COOP_PROTOCOL_VERSION` и `ENGINE_BUILD_SIGNATURE`. Нужно добавить ещё один параметр:

### `CONTENT_HASH` / `MODSET_SIGNATURE`
На этапе Phase 1 допускается **упрощённая реализация**:
- строка версии модпака, или
- агрегированный хэш набора контента (если быстро доступен), или
- временный hardcoded build marker для тестовой сборки

## 4.2 Политика Phase 1 (строгая)
Если `CONTENT_HASH` / `MODSET_SIGNATURE` не совпадает:
- сервер должен вернуть `JOIN_REJECT(VERSION_MISMATCH or CONTENT_MISMATCH)`
- клиент должен показать понятное сообщение

## 4.3 Почему это важно уже на Phase 1
Иначе первые интеграционные тесты будут давать нестабильные результаты, которые команда ошибочно примет за ошибки handshake/сети.

---

# 5. Обязательная правка: усилить диагностику Phase 1 (debug-first)

## 5.1 Добавить отдельный раздел: `Phase 1 Debug Telemetry Requirements`

Phase 1 — это сетевой каркас. Поэтому диагностика должна быть частью Definition of Done.

## 5.2 Минимальные требования к логированию (расширение)
Помимо уже перечисленных логов, добавить:

### Сервер (`[COOP][SV]`)
- transport peer connect/disconnect callbacks (с peer handle/id)
- protocol violation (unexpected packet in state X)
- timeout reasons (если сервер разрывает peer)
- serialized packet sizes для handshake-пакетов (байты)

### Клиент (`[COOP][CL]`)
- start time / elapsed time на каждом шаге handshake
- timeout step (`waiting_hello_ack`, `waiting_join_accept`)
- raw reject reason code + human-readable text
- serialized packet sizes (байты)

## 5.3 Packet trace ring buffer (рекомендуется как MUST for dev builds)
В dev/debug сборке добавить небольшой ring buffer (например последние 64–128 событий), где сохраняются:
- timestamp
- direction (`C->S`, `S->C`)
- packet id
- packet size
- local state / remote peer state
- result (`ok`, `rejected`, `invalid_state`)

При ошибке/таймауте:
- дампить ring buffer в лог.

Это резко ускорит разбор проблем в ранних фазах.

---

# 6. Обязательная правка: state machine — зафиксировать недопустимые переходы как protocol violation

В `phase_1.md` есть корректные state machines, но нужно усилить контракт.

## 6.1 Добавить правило
**Любой пакет, пришедший вне допустимого состояния, считается protocol violation.**

Политика для Phase 1:
- логировать нарушение;
- закрывать соединение (fail-fast);
- не пытаться “угадывать”, что хотел отправитель.

## 6.2 Почему это важно
Fail-fast упрощает дебаг и не позволяет накопить невалидные состояния в early prototype.

---

# 7. Обязательная правка: таймауты и retry-политика (минимальный контракт)

## 7.1 Уточнение к разделу таймаутов
В `phase_1.md` таймауты уже есть; добавить явную политику retry.

### Политика Phase 1
- **Автоматические ретраи handshake не выполнять**.
- Одна попытка подключения = один handshake flow.
- При ошибке/таймауте клиент возвращается в `Failed/Idle`, пользователь вручную нажимает `Connect` повторно.

## 7.2 Почему это важно
Автоматические ретраи на раннем этапе усложняют state machine и маскируют ошибки.

---

# 8. Обязательная правка: уточнить требования к menu integration (чтобы UI не заблокировал этап)

## 8.1 Новый принцип
UI в Phase 1 обязателен (host/join из меню), **но не должен блокировать завершение этапа**, если menu integration затягивается.

## 8.2 Разрешённый fallback (добавить формально)
Допускается временный dev-entrypoint **при условии**, что:
- основной код host/connect уже реализован;
- есть минимальный UI путь (даже “техническая кнопка”/экран-заглушка);
- task по нормализации UI остаётся внутри Phase 1 (не выносится “на потом” без причины).

### Пример допустимого временного UI
- “Developer Coop Test”
  - кнопка Host
  - поле IP
  - кнопка Connect
  - текстовый статус

Цель — не завязнуть на косметике меню раньше handshake.

---

# 9. Обязательная правка: расширить Acceptance Criteria для устойчивости Phase 1

## 9.1 Добавить критерии совместимости и диагностики

### E. Совместимость и валидность
- [ ] Сервер отклоняет клиента с несовпадающим `COOP_PROTOCOL_VERSION`
- [ ] Сервер отклоняет клиента с несовпадающим `CONTENT_HASH/MODSET_SIGNATURE` (или временным эквивалентом)
- [ ] Клиент корректно показывает причину отказа

### F. Диагностика
- [ ] При любой ошибке handshake в логах можно определить **последний успешно завершённый шаг**
- [ ] При protocol violation есть явный лог с состоянием peer/client state
- [ ] В debug/dev сборке доступен packet trace ring buffer dump

---

# 10. Обязательная правка: расширить Test Plan минимальной матрицей окружений

## 10.1 Добавить “Phase 1 Test Matrix”
Чтобы избежать путаницы в результатах, добавить явную матрицу тестирования.

### Обязательная матрица (минимум)
1. **Windows host / Windows client**, одна LAN сеть — основной сценарий (must pass)
2. **Same machine (2 instances)** — опционально как dev convenience (nice to have)
3. **Invalid IP** — must pass (expected fail)
4. **Protocol mismatch** — must pass (expected reject)
5. **No server timeout** — must pass (expected fail)

### Не является блокером Phase 1
- Linux/Proton host/client
- Internet/NAT
- Steam relay
- Dedicated external server binary

---

# 11. Обязательная правка: дописать “Phase 1 Non-Goals” более жёстко (анти-скоуп creep)

В `phase_1.md` список out-of-scope уже хороший, но полезно добавить жёсткие формулировки:

## 11.1 Новые explicit non-goals
- **Нет обязательства** видеть второго игрока в мире
- **Нет обязательства** загружать полноценный игровой уровень после `JOIN_ACCEPT`
- **Нет обязательства** выполнять spawn actor
- **Нет обязательства** вызывать Lua callbacks при успешном join
- **Нет обязательства** поддерживать сейвы/загрузки

## 11.2 Зачем это добавлять
Чтобы команда не “дотягивала” Phase 1 до Phase 2/3 под давлением “ну ещё чуть-чуть”.

---

# 12. Обязательная правка: подготовить мост к Phase 2 (без расширения scope)

## 12.1 Добавить в конец `phase_1.md` раздел `Phase 1 Exit Artifacts`
Помимо текущих артефактов, сохранить следующее (для Phase 2):

- `coop_packet_ids.h` (handshake packet IDs зафиксированы)
- `client/server state enums`
- `handshake logs examples (success/fail)`
- `known blockers for level load/spawn` (если обнаружены)
- `list of touched code paths with legacy assumptions`

Это позволит начать Phase 2 без повторной разведки.

---

# 13. Конкретные вставки в `phase_1.md` (готовые формулировки)

## 13.1 Вставка в раздел “Границы Phase 1 / ограничения”
> Phase 1 целится в воспроизводимый LAN handshake на базе текущего транспортного слоя X-Ray (DirectPlay8). Поддержка Linux/Proton и замена транспорта не входят в критерии готовности этого этапа.

## 13.2 Вставка в раздел “Версионирование и совместимость”
> Помимо `COOP_PROTOCOL_VERSION`, handshake должен проверять `CONTENT_HASH/MODSET_SIGNATURE` (или временный эквивалент сигнатуры сборки) для предотвращения ложных ошибок, вызванных несовместимым контентом/модами.

## 13.3 Вставка в раздел “Ошибки и таймауты”
> На Phase 1 автоматические ретраи handshake отключены. Любой таймаут или protocol violation завершает попытку подключения с явным логом причины и возвратом пользователя в безопасное состояние UI.

## 13.4 Вставка в раздел “Acceptance Criteria”
> Definition of Done Phase 1 включает не только успешный `JOIN_ACCEPT`, но и достаточную диагностируемость неуспешных сценариев (timeout, protocol mismatch, invalid state, content mismatch).

---

# 14. Что НЕ нужно менять в `phase_1.md`

Чтобы сохранить достижимость этапа, **не менять**:
- базовую цель (host + connect LAN + handshake)
- отказ от ALife/gameplay в этом этапе
- MVP-first подход
- C++-only handshake (без Lua)
- task breakdown 1.A → 1.D как основную структуру

Правки из этого файла — это не расширение scope, а защита этапа от ложных срабатываний и скрытых рисков.

---

# 15. Обновлённый чеклист Phase 1 (patch version)

## Core
- [ ] Host из меню запускает listen-server
- [ ] Client из меню подключается по LAN IP
- [ ] Handshake проходит (`HELLO/ACK/JOIN/ACCEPT`) без краша
- [ ] Есть отказ по protocol mismatch
- [ ] Есть отказ по content mismatch (или временной сигнатуре)

## Stability
- [ ] Нет крашей в success path
- [ ] Нет зависаний на timeout/reject
- [ ] Invalid state packets корректно режутся (protocol violation)

## Diagnostics
- [ ] Логи покрывают все шаги handshake
- [ ] Видно последний успешный шаг при ошибке
- [ ] Есть packet trace ring buffer dump (debug/dev)
- [ ] Причины отказа отображаются в UI/логах

## Scope discipline
- [ ] Phase 1 завершён без “подтягивания” gameplay-фич
- [ ] Известные blockers для Phase 2 зафиксированы документально

---

# 16. Action Items для Copilot (по этому addendum)

- [ ] Внести ограничения платформы/транспорта в `phase_1.md`
- [ ] Добавить `CONTENT_HASH/MODSET_SIGNATURE` в Phase 1 handshake requirements
- [ ] Расширить раздел логирования до debug-first уровня
- [ ] Добавить packet trace ring buffer requirement (debug/dev)
- [ ] Зафиксировать fail-fast policy для invalid state packets
- [ ] Добавить retry policy = manual retry only
- [ ] Обновить Acceptance Criteria и Test Matrix
- [ ] Явно усилить Non-Goals (анти-scope-creep)

---

# 17. Итог

`phase_1.md` уже хорошо задаёт границы первого этапа.  
Правки из этого addendum делают этап более “инженерным”:
- меньше ложных диагнозов,
- лучше дебаг,
- чётче ограничения,
- выше шанс быстро получить **первый реально стабильный кооп-handshake**.

Этот файл следует использовать как обязательное дополнение к `phase_1.md` до интеграции правок в основную редакцию.

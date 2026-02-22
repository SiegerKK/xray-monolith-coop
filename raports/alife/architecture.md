# ALife — архитектура и жизненный цикл НПЦ

> **Версия:** X-Ray Engine (xray-monolith-coop)  
> **Дата:** 2026-02-22

---

## 1. Что такое ALife

ALife (Artificial Life) — серверная система симуляции жизни игрового мира в X-Ray Engine.  
Её основная задача: поддерживать существование всех НПЦ и существ **независимо от того, находится ли игрок рядом**. Когда НПЦ входит в радиус видимости игрока, он переключается в «онлайн»-режим (становится полноценным клиентским актором); когда уходит за горизонт — переходит в «оффлайн»-симуляцию.

---

## 2. Архитектура

### 2.1 Дерево менеджеров

```
CALifeSimulator                        (alife_simulator.h/.cpp)
├── CALifeUpdateManager                (alife_update_manager.h/.cpp)
│   ├── CALifeSwitchManager            (alife_switch_manager.h/.cpp)
│   ├── CALifeSurgeManager             (alife_surge_manager.h/.cpp)
│   └── CALifeStorageManager           (alife_storage_manager.h/.cpp)
├── CALifeInteractionManager           (alife_interaction_manager.h)
├── CALifeCombatManager                (alife_combat_manager.h/.cpp)
└── CALifeCommunicationManager         (alife_communication_manager.h/.cpp)

Реестры (registry-объекты внутри CALifeSimulator):
├── CALifeObjectRegistry               — все объекты мира
├── CALifeScheduleRegistry             — очередь обновлений
├── CALifeSpawnRegistry                — управление спавном
├── CALifeGraphRegistry                — привязка к графу уровней
├── CALifeGroupRegistry                — группы НПЦ
├── CALifeStoryRegistry                — story-объекты
├── CALifeSmartTerrainRegistry         — умные зоны
└── CALifeTimeManager                  — игровое время
```

### 2.2 Ключевые зависимости

| Класс | Файл | Назначение |
|---|---|---|
| `CALifeSimulator` | `alife_simulator.cpp` | Точка входа, Lua-коллбэки, сохранения |
| `CALifeUpdateManager` | `alife_update_manager.cpp` | Главный цикл обновления |
| `CALifeSwitchManager` | `alife_switch_manager.cpp` | Переход онлайн↔оффлайн |
| `CALifeSpawnRegistry` | `alife_spawn_registry.cpp` | Условия создания новых НПЦ |
| `CALifeScheduleRegistry` | `alife_schedule_registry.cpp` | Очередь объектов для обновления |
| `CALifeStorageManager` | `alife_storage_manager.cpp` | Сериализация состояния |
| `CALifeCombatManager` | `alife_combat_manager.cpp` | Оффлайн-бои |

### 2.3 Цикл обновления

```
CALifeUpdateManager::shedule_Update(dt)
│
├── [первый кадр] update() — синхронно
└── [MT включён]  Device.seqParallel → update() — параллельно
                  │
                  ├── update_switch()     — переключение онлайн/оффлайн
                  └── update_scheduled() — обновление запланированных объектов
                                           (configurable: objects_per_update)
```

---

## 3. Иерархия существ (серверные сущности)

```
CSE_ALifeCreatureAbstract              (базовый, xrServer_Objects_ALife_Monsters.h)
│  fHealth, m_killer_id, team/squad/group
│  m_fMorale, m_fAccuracy, m_fIntelligence
│  m_dynamic_out/in_restrictions
│
├── CSE_ALifeMonsterAbstract
│   │  m_smart_terrain_id              — текущее задание (умная зона)
│   │  m_task_reached                  — статус задания
│   │  m_rank                          — ранг НПЦ
│   │  brain() → CALifeMonsterBrain
│   │
│   └── CSE_ALifeHumanAbstract
│       │  cast_trader_abstract()      — доступ к экономике
│       │  brain() → CALifeHumanBrain
│       │
│       └── CSE_ALifeHumanStalker      — сталкер
│
├── CSE_ALifeCreatureActor             — игровой персонаж
│   └── cast_trader_abstract()
│
├── CSE_ALifeMonsterRat
├── CSE_ALifeMonsterZombie
└── CSE_ALifeMonsterBase
```

---

## 4. «Мозги» НПЦ

### 4.1 CALifeMonsterBrain (alife_monster_brain.h/.cpp)

Базовый класс принятия решений для любого существа в оффлайн-симуляции.

```cpp
class CALifeMonsterBrain {
    CSE_ALifeMonsterAbstract*        m_object;
    CALifeMonsterMovementManager*    m_movement_manager;
    CSE_ALifeSmartZone*              m_smart_terrain;  // текущая задача
    ALife::_TIME_ID                  m_last_search_time;

public:
    void select_task();        // выбрать умную зону для задачи
    void process_task();       // выполнять задачу
    void default_behaviour();  // поведение по умолчанию (idle)
    bool perform_attack();     // оффлайн-атака
    void update();             // главный тик
    void on_switch_online();   // вызывается при переходе в онлайн
    void on_switch_offline();  // вызывается при переходе в оффлайн
};
```

### 4.2 CALifeHumanBrain (alife_human_brain.h/.cpp)

Расширяет `CALifeMonsterBrain` для НПЦ-людей.

```cpp
class CALifeHumanBrain : public CALifeMonsterBrain {
    CSE_ALifeHumanAbstract*      m_object;
    CALifeHumanObjectHandler*    m_object_handler;  // работа с инвентарём

    svector<char, 5> m_cpEquipmentPreferences;   // предпочтения снаряжения
    svector<char, 4> m_cpMainWeaponPreferences;  // предпочтения оружия
    u32              m_dwTotalMoney;             // учёт денег

public:
    void on_state_write(NET_Packet& packet);  // сохранение
    void on_state_read(NET_Packet& packet);   // загрузка
};
```

---

## 5. Дерево принятия решений (GOAP-планировщик)

НПЦ используют **GOAP (Goal-Oriented Action Planning)** — не классическое дерево поведения, а планировщик на основе мировых свойств.

### 5.1 Планировщик сталкера

**Файлы:** `stalker_alife_planner.h/.cpp`, `stalker_decision_space.h`

```cpp
class CStalkerALifePlanner
    : public CActionPlannerActionScript<CAI_Stalker>
{
public:
    void add_evaluators();  // регистрирует условия (property evaluators)
    void add_actions();     // регистрирует действия (operators)
};
```

### 5.2 Мировые свойства (World Properties)

Более 100 свойств в `EWorldProperties` из `stalker_decision_space.h`. Основные группы:

| Группа | Примеры свойств |
|---|---|
| Жизнь/смерть | `eWorldPropertyAlive`, `eWorldPropertyDead`, `eWorldPropertyAlreadyDead` |
| ALife-режим | `eWorldPropertyALife`, `eWorldPropertyItems`, `eWorldPropertyEnemy` |
| Задания | `eWorldPropertySmartTerrainTask`, `eWorldPropertyPuzzleSolved` |
| Бой | `eWorldPropertyReadyToKill`, `eWorldPropertyInCover`, `eWorldPropertyPanic` |
| Аномалии | `eWorldPropertyAnomaly`, `eWorldPropertyInsideAnomaly` |
| Smart cover | `eWorldPropertyInSmartCover`, `eWorldPropertySmartCoverActual` |

### 5.3 Действия (Operators)

Более 120 операторов. Для ALife-фазы ключевые:

| Оператор | Класс | Назначение |
|---|---|---|
| `eWorldOperatorALifeEmulation` | `CStalkerActionNoALife` | Поведение без активного ALife |
| `eWorldOperatorSmartTerrainTask` | `CStalkerActionSmartTerrain` | Движение к умной зоне |
| `eWorldOperatorSolveZonePuzzle` | `CStalkerActionSolveZonePuzzle` | Решение зонального пазла |
| `eWorldOperatorGatherItems` | `CStalkerActionGatherItems` | Сбор предметов |
| `eWorldOperatorKillEnemy` | — | Уничтожить врага |
| `eWorldOperatorSearchEnemy` | — | Поиск врага |

### 5.4 Пример действия — CStalkerActionGatherItems

```cpp
void CStalkerActionGatherItems::execute() {
    // Ставит НПЦ цель движения к предмету
    object().movement().set_level_dest_vertex(level_vertex_id);
    object().sight().setup(SightManager::eSightTypePosition);
}
```

### 5.5 Логика выбора задачи

```
update() (CALifeMonsterBrain)
│
├── select_task()
│   └── Ищет CSE_ALifeSmartZone с наименьшим расстоянием/приоритетом
│       → присваивает m_smart_terrain, m_task_reached = false
│
├── process_task()
│   └── Двигает НПЦ по графу к smart terrain
│       → когда достигнут: m_task_reached = true → выполняет задачу
│
└── default_behaviour()
    └── idle (случайное брожение / ожидание)
```

---

## 6. Жизненный цикл НПЦ

### 6.1 Рождение (Spawn)

1. `CALifeSpawnRegistry` — проверяет лимиты (по количеству, времени, локации)
2. Создаётся серверная сущность (`CSE_ALifeHumanStalker` и т.п.)
3. `spawn_supplies()` — наполняет инвентарь согласно профилю персонажа
   - предметы из `CSpecificCharacter`
   - деньги из диапазона `[min_money, max_money]` профиля
   - ПДА

### 6.2 Оффлайн-симуляция (основной режим)

```
Каждый тик CALifeUpdateManager:
├── CALifeScheduleRegistry — выдаёт batch объектов (objects_per_update)
└── Для каждого объекта → brain().update()
    ├── select_task() / process_task()
    ├── perform_attack() (если враг в радиусе)
    └── CALifeMonsterMovementManager → граф-навигация
```

### 6.3 Переход онлайн

```
CALifeSwitchManager::add_online(object)
├── Создаёт клиентский актор (spawn-пакет → игровой движок)
├── Восстанавливает инвентарь (дочерние объекты)
└── brain().on_switch_online()
```

### 6.4 Переход оффлайн

```
CALifeSwitchManager::add_offline(object)
├── Удаляет клиентский актор
├── Сохраняет детей-объекты (инвентарь)
└── brain().on_switch_offline()
```

### 6.5 Смерть

```
alife_simulator_base: on_death(object)
├── assign_death_position()   — выставляет позицию трупа
├── Обновляет граф-позицию
└── CALifeUpdateManager — снимает с расписания, дерегистрирует
```

### 6.6 Полнота жизненного цикла

| Аспект | Реализован? | Комментарий |
|---|---|---|
| Спавн по условиям | ✅ | Лимиты по счётчику, времени, локации |
| Инвентарь при спавне | ✅ | `spawn_supplies()` + профиль персонажа |
| Граф-навигация (оффлайн) | ✅ | `CALifeMonsterMovementManager` |
| Оффлайн-бой | ✅ | `CALifeCombatManager` + `perform_attack()` |
| Сохранение/загрузка состояния | ✅ | `CALifeStorageManager` |
| Переход онлайн/оффлайн | ✅ | `CALifeSwitchManager` |
| Смерть и труп | ✅ | `on_death()` + `assign_death_position()` |
| Задания (smart terrain) | ✅ | `select_task()` → умные зоны |
| Предпочтения снаряжения | ✅ | `CALifeHumanBrain::m_cpEquipmentPreferences` |
| Динамическое создание во время игры | ⚠️ | Преимущественно спавн-точечная система |
| Сложные отношения/репутация онлайн | ⚠️ | Ограничено; основное — ранг и фракция |
| Старение / длительная биография | ❌ | Не реализовано |

---

## 7. Деньги и экономика

### 7.1 Хранение денег

Деньги хранятся **на двух уровнях**:

| Уровень | Поле | Класс | Файл |
|---|---|---|---|
| Клиентский (онлайн) | `m_money` (u32) | `CInventoryOwner` | `InventoryOwner.h` |
| Серверный (ALife) | `m_dwMoney` (u32) | `CSE_ALifeTraderAbstract` | `xrServer_Objects_ALife_Monsters.h` |
| «Мозг» НПЦ | `m_dwTotalMoney` (u32) | `CALifeHumanBrain` | `alife_human_brain.h` |

```cpp
// InventoryOwner.h
u32 get_money() const { return m_money; }
void set_money(u32 amount, bool bSendEvent);

// xrServer_Objects_ALife_Monsters.h
class CSE_ALifeTraderAbstract {
    u32  m_dwMoney;        // деньги торговца/НПЦ
    float m_fMaxItemMass;  // макс. вес инвентаря
};
```

### 7.2 Инициализация денег

Через профиль персонажа (`specific_character.h`):

```cpp
struct SMoneyDef {
    u32  min_money;   // минимум при спавне
    u32  max_money;   // максимум при спавне
    bool inf_money;   // флаг бесконечных денег (для торговцев)
};
```

При спавне `spawn_supplies()` генерирует случайную сумму в диапазоне `[min_money, max_money]`.

### 7.3 Тратят ли НПЦ деньги?

**В оффлайн-симуляции — нет.** НПЦ не совершают покупок самостоятельно, пока находятся в оффлайн-режиме.

**В онлайн-режиме (торговец/игрок):**

| Операция | Реализована? |
|---|---|
| Игрок продаёт торговцу → деньги переходят к игроку | ✅ (`trade2.cpp`) |
| Игрок покупает у торговца → деньги снимаются с игрока | ✅ (`trade2.cpp`) |
| НПЦ покупает у другого НПЦ | ❌ Нет |
| НПЦ тратит деньги на амуницию самостоятельно | ❌ Нет |
| Деньги переходят при оффлайн-бою (лут) | ✅ (`CALifeCombatManager`) |

**Вывод:** НПЦ имеют деньги, но **активно не тратят** их в ALife-симуляции. Деньги служат:
- источником ресурсов для торговли с **игроком**;
- «лутом» при смерти НПЦ (в онлайне или через оффлайн-бой);
- отражением ранга/статуса персонажа через профиль.

### 7.4 События об изменении денег

При смене суммы клиентский метод `set_money(amount, bSendEvent)` при `bSendEvent = true` отправляет событие в UI (`UIMoneyIndicator.cpp`). Деньги сохраняются и загружаются вместе с состоянием персонажа.

---

## 8. Движение в оффлайне

`CALifeMonsterMovementManager` и вспомогательные классы:

| Класс | Назначение |
|---|---|
| `CALifeMonsterMovementManager` | Граф-навигация (level graph / cross-table) |
| `CALifeMonsterPatrolPathManager` | Патрульные маршруты |
| `CALifeMonsterDetailPathManager` | Детальный путь (приближение к точке) |

НПЦ перемещаются по **game graph** — графу, описывающему связи между локациями и ключевыми точками уровней. Патрульные точки задаются через умные зоны (`CSE_ALifeSmartZone`).

---

## 9. Интеграция со скриптами (Lua)

Основная логика ALife реализована на **C++**. Lua используется для:

- `start_game_callback` — настраивается в `.ltx`, вызывается при старте игры;
- `brain()` — экспортировано в Lua через `*_script.cpp` файлы;
- `CALifeMonsterBrainScript`, `CALifeHumanBrainScript` — биндинги для скриптовых модов.

Выделенных Lua-файлов с логикой ALife в базовой поставке не обнаружено — поведение определяется C++-кодом.

---

## 10. Итоговая схема

```
┌─────────────────────────────────────────────────────────┐
│                    CALifeSimulator                      │
│                                                         │
│  ┌─────────────────┐    ┌───────────────────────────┐  │
│  │  UpdateManager  │    │       Registries          │  │
│  │                 │    │  Object / Schedule /      │  │
│  │  update_switch()│    │  Spawn / Graph / Group /  │  │
│  │  update_sched() │    │  SmartTerrain / Story     │  │
│  └────────┬────────┘    └───────────────────────────┘  │
│           │                                             │
│     ┌─────▼──────┐                                      │
│     │  For each  │  (objects_per_update per tick)       │
│     │  scheduled │                                      │
│     │  object    │                                      │
│     └─────┬──────┘                                      │
│           │                                             │
│     ┌─────▼──────────────────────┐                      │
│     │  brain().update()           │                      │
│     │  ┌──────────────────────┐  │                      │
│     │  │ select_task()        │  │                      │
│     │  │ process_task()       │  │                      │
│     │  │ perform_attack()     │  │                      │
│     │  │ default_behaviour()  │  │                      │
│     │  └──────────────────────┘  │                      │
│     └─────────────────────────────┘                     │
│                                                         │
│  SwitchManager: онлайн ↔ оффлайн по дистанции           │
│  CombatManager: оффлайн-бои → лут/смерть                │
│  StorageManager: сохранение / загрузка                  │
└─────────────────────────────────────────────────────────┘
```

---

## Источники (ключевые файлы)

| Файл | Расположение |
|---|---|
| `alife_simulator.h/.cpp` | `src/xrGame/` |
| `alife_update_manager.h/.cpp` | `src/xrGame/` |
| `alife_switch_manager.h/.cpp` | `src/xrGame/` |
| `alife_combat_manager.h/.cpp` | `src/xrGame/` |
| `alife_monster_brain.h/.cpp` | `src/xrGame/` |
| `alife_human_brain.h/.cpp` | `src/xrGame/` |
| `stalker_alife_planner.h/.cpp` | `src/xrGame/` |
| `stalker_alife_actions.h/.cpp` | `src/xrGame/` |
| `stalker_decision_space.h` | `src/xrGame/` |
| `alife_trader_abstract.cpp` | `src/xrGame/` |
| `InventoryOwner.h/.cpp` | `src/xrGame/` |
| `xrServer_Objects_ALife_Monsters.h` | `src/xrServerEntities/` |
| `specific_character.h` | `src/xrGame/` |

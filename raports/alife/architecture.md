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
│   └── Обходит все SmartZone → берёт ту, где suitable() максимален
│       → присваивает m_smart_terrain, m_task_reached = false
│
├── process_task()
│   └── Двигает НПЦ по графу к task().level_vertex_id
│       → когда достигнут: m_task_reached = true → выполняет задачу
│
└── default_behaviour()
    └── idle (брожение по умолчанию / ожидание)
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

## 8. Целеполагание НПЦ

### 8.1 Задачи случайные или нет?

**Нет, задачи не случайные.** Выбор умной зоны (smart terrain) — **приоритетный**, основанный на оценочной функции `suitable()`.

```cpp
// CALifeMonsterBrain::select_task() — упрощённо
void select_task() {
    if (object().m_smart_terrain_id != 0xffff) return; // уже назначена задача

    // Временной барьер: поиск запускается не чаще чем раз в m_time_interval
    if (m_last_search_time + m_time_interval > current_time) return;

    float best_value = flt_min;
    for each CSE_ALifeSmartZone* terrain : all_smart_terrains {
        if (!terrain->enabled(&object)) continue;      // зона должна быть активна
        float value = terrain->suitable(&object);      // оценка пригодности
        if (value > best_value) {
            best_value = value;
            object.m_smart_terrain_id = terrain->ID;  // берём лучшую
        }
    }
    if (найдена) terrain.register_npc(&object);
}
```

### 8.2 Что такое «умная зона» (Smart Terrain)

`CSE_ALifeSmartZone` — базовый класс задачи. Подклассы переопределяют:

| Метод | Назначение |
|---|---|
| `enabled(object)` | Может ли этот НПЦ принять задачу? |
| `suitable(object)` | Насколько эта зона подходит (возвращает оценку) |
| `register_npc(object)` | Зарегистрировать НПЦ как выполняющего задачу |
| `unregister_npc(object)` | Снять НПЦ с задачи |
| `task(object)` | Вернуть объект `CALifeSmartTerrainTask` с координатой цели |

`CALifeSmartTerrainTask` содержит:
- имя патрульного пути (`patrol_path_name`)
- индекс точки маршрута
- `game_vertex_id` и `level_vertex_id` для навигации

### 8.3 Итоговая логика целеполагания

```
brain().update()
│
├── select_task()   — раз в N игровых секунд:
│   └── обход всех SmartZone → max(suitable()) → m_smart_terrain_id
│
├── process_task()  — каждый тик:
│   ├── движение по game graph к task().level_vertex_id
│   └── по достижении: m_task_reached = true → выполнение задачи
│
└── default_behaviour()  — если задачи нет:
    └── idle / patrol по точкам по умолчанию
```

**Вывод:** У каждого НПЦ есть конкретная цель — умная зона, выбранная по максимальному приоритету. «Случайность» присутствует лишь косвенно: если несколько зон имеют одинаковый `suitable()`, порядок обхода даёт детерминированный, но неочевидный выбор.

---

## 9. Лут аномалий и сбор артефактов

### 9.1 Оффлайн-режим

**НПЦ не собирают артефакты в оффлайн-симуляции.**  
Поиск по всем `alife_*.cpp` не обнаружил ни одного файла, содержащего одновременно слова `artifact` и `alife`-логику сбора. Класс `CALifeHumanObjectHandler` (отвечающий за работу с предметами) содержит **только заглушки**:

```cpp
// alife_human_object_handler.cpp — все методы-стабы:
bool can_take_item()   { return false; }
int  choose_equipment(){ return -1;   }
int  choose_weapon()   { return -1;   }
int  choose_food()     { return -1;   }
CSE_ALifeItemWeapon* best_weapon() { return 0; }
```

### 9.2 Онлайн-режим

В **онлайн-режиме** (когда НПЦ активен на уровне) работает действие `CStalkerActionGatherItems`:

```cpp
// stalker_alife_actions.cpp
void CStalkerActionGatherItems::execute() {
    // Предмет берётся из памяти НПЦ (memory().item().selected())
    u32 lv = object().memory().item().selected()->ai_location().level_vertex_id();
    object().movement().set_level_dest_vertex(lv);
    object().movement().set_desired_position(&item->Position());
    object().sight().setup(SightManager::eSightTypePosition, &item->Position());
}
```

**Ключевое ограничение:** предмет должен **попасть в память** НПЦ (через визуальное/слуховое восприятие) прежде чем он начнёт к нему идти. Артефакты в аномалиях попадают в память только если НПЦ находится достаточно близко и «видит» их.

### 9.3 Вывод

| Сценарий | Реализован? |
|---|---|
| НПЦ идёт к аномалии за артефактом (оффлайн) | ❌ Нет |
| НПЦ видит артефакт и подбирает его (онлайн) | ✅ Через `GatherItems` + memory |
| Специальной «охоты за артефактами» как цели | ❌ Нет |

---

## 10. Лут трупов

### 10.1 Оффлайн-режим

**Лут трупов в оффлайн-симуляции не реализован.**  
`CALifeHumanObjectHandler` содержит заглушки для всех методов работы с предметами. Трупы обрабатываются только как позиция тела (`assign_death_position()`).

### 10.2 Онлайн-режим

В онлайн-режиме НПЦ **могут** подбирать оружие/боеприпасы с трупов через стандартный механизм памяти и `GatherItems`. Трёхфазный поиск в `ai_stalker_fire.cpp::update_best_item_info_impl()`:

```
Фаза 1: инвентарь НПЦ — есть ли уже оружие, способное убить?
Фаза 2: память НПЦ   — есть ли рядом боеприпасы к текущему оружию?
Фаза 3: память НПЦ   — есть ли пара оружие+патроны (возможно на трупе)?
```

Однако лут трупа происходит не как «целенаправленный поиск лута», а как побочный эффект общего механизма поиска предметов в памяти.

### 10.3 Вывод

| Сценарий | Реализован? |
|---|---|
| НПЦ целенаправленно идёт грабить труп (оффлайн) | ❌ Нет |
| НПЦ видит оружие на трупе и поднимает (онлайн) | ✅ Через memory + GatherItems |
| Оффлайн-бой → переход лута победителю | ✅ `CALifeCombatManager` |

---

## 11. Смена оружия и брони

### 11.1 Предпочтения снаряжения (случайные при спавне)

При создании НПЦ его «мозг» инициализирует **случайные предпочтения**:

```cpp
// alife_human_brain.cpp
m_cpEquipmentPreferences.resize(5);  // 5 типов снаряжения
m_cpMainWeaponPreferences.resize(4); // 4 типа основного оружия

for (int i = 0; i < m_cpEquipmentPreferences.size(); ++i)
    m_cpEquipmentPreferences[i] = u8(::Random.randI(3)); // значение 0, 1 или 2

for (int i = 0; i < m_cpMainWeaponPreferences.size(); ++i)
    m_cpMainWeaponPreferences[i] = u8(::Random.randI(3));
```

Эти массивы используются в оценочных функциях (`ef_primary.cpp`) как весовые коэффициенты при сравнении предметов по типу.

### 11.2 Выбор лучшего оружия (онлайн)

`CAI_Stalker::choose_weapon()` в `ai_stalker_alife.cpp` перебирает инвентарь и выбирает лучшее оружие **по типу и оценочной функции**:

```cpp
void choose_weapon(EWeaponPriorityType weapon_priority_type) {
    for each weapon in inventory {
        int j = ef_storage().m_pfPersonalWeaponType->dwfGetWeaponType();
        // Фильтрация по категории (нож / пистолет / дробовик / снайперка / etc.)
        // Для каждого типа — своя категория j
        
        float value = ef_storage().m_pfMainWeaponValue->ffGetValue();
        if (item_in_slot) value += 10.0f; // бонус за уже надетое
        if (value > best_value) best_weapon = &weapon;
    }
    if (best_weapon) buy_item_virtual(*best_weapon); // экипировать
}
```

### 11.3 Условия подбора нового оружия (online)

`CAI_Stalker::can_take()` + `conflicted()` реализуют **систему приоритетов**:

| Приоритет | Критерий | Результат |
|---|---|---|
| 1 | Наличие патронов | НПЦ предпочитает оружие, к которому есть боеприпасы |
| 2 | Состояние оружия | Лучший `GetCondition()` выигрывает (допуск 5%) |
| 3 | Тип оружия | Разные типы → берём более дорогое |
| 4 | Ранг НПЦ vs ранг оружия | Оружие выше ранга не берётся |

```cpp
bool conflicted(const CInventoryItem* current, const CWeapon* new_weapon, ...) {
    if (current_enough_ammo && !new_enough_ammo)  return true;  // оставить текущее
    if (!current_enough_ammo && new_enough_ammo)  return false; // взять новое
    if (!fsimilar(cur.cond, new.cond, .05f))
        return cur.cond >= new.cond;                             // лучшее состояние
    if (cur.type != new.type)
        return cur.Cost() >= new.Cost();                         // дороже = лучше
    if (cur_rank != new_rank)
        return cur_rank >= new_rank;                             // выше ранг = лучше
    return true; // по умолчанию — оставить текущее
}
```

### 11.4 Смена брони

Специальной логики смены **брони/костюма** в ALife-коде **не обнаружено**:
- `choose_equipment()` в `alife_human_object_handler.cpp` — заглушка, возвращает `-1`
- `m_cpEquipmentPreferences` формирует предпочтения, но метода «надеть лучший костюм» нет
- Снаряжение фиксируется при спавне через `spawn_supplies()` и не меняется в оффлайне

### 11.5 Итоговая таблица

| Возможность | Оффлайн | Онлайн |
|---|---|---|
| Сменить оружие на лучшее | ❌ | ✅ `choose_weapon()` + `can_take()` |
| Сменить броню/костюм | ❌ | ❌ (заглушка) |
| Подобрать боеприпасы | ❌ | ✅ через `GatherItems` + memory |
| Предпочтения снаряжения влияют на выбор | ✅ (при спавне) | ✅ (при оценке предметов) |

---

## 12. Движение в оффлайне

`CALifeMonsterMovementManager` и вспомогательные классы:

| Класс | Назначение |
|---|---|
| `CALifeMonsterMovementManager` | Граф-навигация (level graph / cross-table) |
| `CALifeMonsterPatrolPathManager` | Патрульные маршруты |
| `CALifeMonsterDetailPathManager` | Детальный путь (приближение к точке) |

НПЦ перемещаются по **game graph** — графу, описывающему связи между локациями и ключевыми точками уровней. Патрульные точки задаются через умные зоны (`CSE_ALifeSmartZone`).

---

## 13. Интеграция со скриптами (Lua)

Основная логика ALife реализована на **C++**. Lua используется для:

- `start_game_callback` — настраивается в `.ltx`, вызывается при старте игры;
- `brain()` — экспортировано в Lua через `*_script.cpp` файлы;
- `CALifeMonsterBrainScript`, `CALifeHumanBrainScript` — биндинги для скриптовых модов.

Выделенных Lua-файлов с логикой ALife в базовой поставке не обнаружено — поведение определяется C++-кодом.

---

## 14. Итоговая схема

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

## 15. Механика вычисления suitable() — подробный разбор

Этот раздел отвечает на вопрос: **как именно НПЦ выбирает цель?** — с точностью до строки кода.

### 15.1 Полный алгоритм select_task()

Файл: `src/xrServerEntities/alife_monster_brain.cpp`

```cpp
void CALifeMonsterBrain::select_task()
{
    // [1] Уже назначена задача — ничего не делаем
    if (object().m_smart_terrain_id != 0xffff)
        return;

    // [2] Глобальный флаг: разрешено ли вообще выбирать задачи
    if (!can_choose_alife_tasks())   // bool m_can_choose_alife_tasks
        return;

    ALife::_TIME_ID current_time = ai().alife().time_manager().game_time();

    // [3] Таймер: не чаще чем раз в smart_terrain_choose_interval
    if (m_last_search_time + m_time_interval > current_time)
        return;

    m_last_search_time = current_time;

    float best_value = flt_min;   // проект-специфичная константа: −FLT_MAX (≈ −∞)

    // [4] Полный перебор ВСЕХ зарегистрированных умных зон
    for (auto& [id, terrain] : ai().alife().smart_terrains().objects())
    {
        // [5] Фильтр: зона должна принять данного НПЦ
        if (!terrain->enabled(&object()))
            continue;

        // [6] Оценка: зона возвращает float-балл
        float value = terrain->suitable(&object());

        // [7] Жадный выбор: берём максимум
        if (value > best_value) {
            best_value = value;
            object().m_smart_terrain_id = terrain->ID;
        }
    }

    // [8] Регистрируем НПЦ в выигравшей зоне
    if (object().m_smart_terrain_id != 0xffff) {
        smart_terrain().register_npc(&object());
        m_last_search_time = 0;   // сбрасываем таймер
    }
}
```

**Ключевые выводы из алгоритма:**
- Это **жадный одношаговый поиск**: обход всех зон за один раз, победитель — сразу.
- **Нет взвешивания**: расстояние, направление, история не учитываются в базовом коде.
- **Нет предпочтений по типу задачи**: базовый C++ не знает о «патруле» или «отдыхе».
- Результат полностью определяется тем, **что вернут `enabled()` и `suitable()`**.

---

### 15.2 Временной интервал перебора

Интервал задаётся в конфигурационном файле `.ltx` для каждого типа существа:

```cpp
// alife_monster_brain.cpp — конструктор
u32 hours, minutes, seconds;
sscanf(
    pSettings->r_string(object_name, "smart_terrain_choose_interval"),
    "%d:%d:%d",
    &hours, &minutes, &seconds
);
m_time_interval = generate_time(1, 1, 1, hours, minutes, seconds);
```

Формат: `smart_terrain_choose_interval = ЧЧ:ММ:СС` (игровое время).  
Пример: `0:0:30` — НПЦ ищет новую цель раз в 30 игровых секунд.

---

### 15.3 Что такое enabled() и suitable()

Оба метода объявлены как виртуальные в базовом классе `CSE_ALifeSmartZone`:

```cpp
// xrServer_Objects_ALife.h
SERVER_ENTITY_DECLARE_BEGIN2(CSE_ALifeSmartZone,
    CSE_ALifeSpaceRestrictor, CSE_ALifeSchedulable)

    // Фильтр: может ли этот НПЦ вообще взять задачу в этой зоне?
    virtual bool  enabled (CSE_ALifeMonsterAbstract* object) const { return false; }

    // Оценка: насколько эта зона подходит для данного НПЦ?
    virtual float suitable(CSE_ALifeMonsterAbstract* object) const { return 0.f; }

    // Регистрация / снятие НПЦ с задачи
    virtual void register_npc  (CSE_ALifeMonsterAbstract* object) {}
    virtual void unregister_npc(CSE_ALifeMonsterAbstract* object) {}

    // Возвращает конкретную точку назначения (patrol path + vertex id)
    virtual CALifeSmartTerrainTask* task(CSE_ALifeMonsterAbstract* object) { return 0; }

SERVER_ENTITY_DECLARE_END
```

**Значения по умолчанию — `false` и `0.f`**, то есть базовый C++ отключает все умные зоны.  
Реальная логика **переопределяется в подклассах или через Lua**.

---

### 15.4 Lua-перекрытие suitable() и enabled()

Макрос `INHERIT_ZONE` в `xrServer_script_macroses.h` оборачивает все методы зоны в Lua-диспетчеры:

```cpp
#define INHERIT_ZONE \
    DEFINE_LUA_WRAPPER_CONST_METHOD_1(enabled,  bool,  CSE_ALifeMonsterAbstract*) \
    DEFINE_LUA_WRAPPER_CONST_METHOD_1(suitable, float, CSE_ALifeMonsterAbstract*) \
    DEFINE_LUA_WRAPPER_METHOD_V1(register_npc,         CSE_ALifeMonsterAbstract*) \
    DEFINE_LUA_WRAPPER_METHOD_V1(unregister_npc,       CSE_ALifeMonsterAbstract*) \
    DEFINE_LUA_WRAPPER_METHOD_1 (task, CALifeSmartTerrainTask*, CSE_ALifeMonsterAbstract*)
```

Класс зарегистрирован в Lua как `cse_alife_smart_zone`:

```cpp
// xrServer_Objects_ALife_script3.cpp
module(L) [
    luabind_class_zone2(
        CSE_ALifeSmartZone,
        "cse_alife_smart_zone",
        CSE_ALifeSpaceRestrictor,
        CSE_ALifeSchedulable
    )
];
```

**Это означает:** в игровых модах/скриптах можно написать Lua-класс, наследующий `cse_alife_smart_zone`, и переопределить `suitable(npc)` так, чтобы возвращать нужный балл на основе **любых** параметров НПЦ.

---

### 15.5 Что можно учитывать в suitable()

Через объект `CSE_ALifeMonsterAbstract* object` Lua/C++ код внутри `suitable()` имеет доступ к:

| Поле | Тип | Что это |
|---|---|---|
| `object.fHealth` | float | Здоровье (0–1) |
| `object.m_fMorale` | float | Боевой дух |
| `object.m_fAccuracy` | float | Точность |
| `object.m_fIntelligence` | float | Интеллект |
| `object.m_fEyeRange` | float | Радиус обзора |
| `object.m_rank` | u16 | Ранг НПЦ |
| `object.m_group` | u16 | Номер группы |
| `object.m_smart_terrain_id` | u16 | Текущая задача (0xffff = нет) |
| `object.m_tGraphID` | `_GRAPH_ID` | Вершина game graph (позиция) |
| `object.fid` | — | ID фракции (community) |

Для людей (`CSE_ALifeHumanAbstract`) дополнительно:
- `brain().m_cpEquipmentPreferences` — предпочтения снаряжения
- `brain().m_cpMainWeaponPreferences` — предпочтения оружия
- `brain().m_dwTotalMoney` — деньги

**Пример того, что может делать suitable() в реальной игре:**

```lua
-- Псевдокод (не из этого репозитория, иллюстрация принципа)
function smart_terrain:suitable(npc)
    -- Фильтры по рангу
    if npc:rank() < self.min_rank then return 0 end
    if npc:rank() > self.max_rank then return 0 end

    -- Фильтр по фракции
    if not self:community_allowed(npc:community()) then return 0 end

    -- Базовый балл за тип задачи (патруль важнее отдыха)
    local base = self.job_priority  -- например, 10 для патруля, 5 для отдыха

    -- Штраф за расстояние
    local dist = distance_between(npc, self)
    local dist_penalty = dist * 0.01  -- чем дальше — тем меньше балл

    return base - dist_penalty
end
```

---

### 15.6 Как online-сталкер триггерит select_task()

Для активного (online) сталкера `select_task()` вызывается не напрямую, а через **GOAP-оценщик**:

```cpp
// stalker_property_evaluators.cpp
_value_type CStalkerPropertyEvaluatorSmartTerrainTask::evaluate()
{
    if (!ai().get_alife()) return false;

    CSE_ALifeHumanAbstract* stalker =
        smart_cast<CSE_ALifeHumanAbstract*>(
            ai().alife().objects().object(m_object->ID(), true)
        );
    if (!stalker) return false;

    stalker->brain().select_task();  // ← вызывает тот же алгоритм
    return (stalker->m_smart_terrain_id != 0xffff);
}
```

Этот оценщик зарегистрирован как `eWorldPropertySmartTerrainTask` в `CStalkerALifePlanner::add_evaluators()` — GOAP-планировщик запрашивает его каждый раз, когда строит план действий для сталкера.

---

### 15.7 Что происходит после выбора цели

Когда `m_smart_terrain_id` установлен, `CStalkerActionSmartTerrain::execute()` выполняет навигацию:

```cpp
// stalker_alife_task_actions.cpp
void CStalkerActionSmartTerrain::execute()
{
    // Получаем задачу от умной зоны
    CALifeSmartTerrainTask* task = stalker->brain().smart_terrain().task(stalker);

    // Навигация между уровнями через game graph
    if (object().ai_location().game_vertex_id() != task->game_vertex_id()) {
        object().movement().set_path_type(MovementManager::ePathTypeGamePath);
        object().movement().set_game_dest_vertex(task->game_vertex_id());
        return;
    }

    // Навигация внутри уровня через level graph
    object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
    if (object().movement().accessible(task->level_vertex_id())) {
        object().movement().set_level_dest_vertex(task->level_vertex_id());
        Fvector pos = task->position();
        object().movement().set_desired_position(&pos);
        return;
    }

    // Если точка недоступна — идём к ближайшей доступной
    object().movement().set_nearest_accessible_position(
        task->position(), task->level_vertex_id());
}
```

`CALifeSmartTerrainTask` содержит:
- `game_vertex_id()` — вершина game graph (для межуровневой навигации)
- `level_vertex_id()` — вершина level graph (для навигации внутри уровня)
- `position()` — точная 3D-координата на уровне

---

### 15.8 Полная цепочка целеполагания

```
НПЦ создан → spawn_supplies() → m_smart_terrain_id = 0xffff (нет задачи)
│
▼
brain().update() каждый ALife-тик:
│
├── can_choose_alife_tasks() == true?
├── прошёл smart_terrain_choose_interval?
│
└── select_task():
    │
    ├── [перебор всех CSE_ALifeSmartZone]
    │   │
    │   ├── enabled(npc) == false? → skip
    │   │
    │   └── value = suitable(npc)     ← float, логика в Lua или C++ подклассе
    │       │
    │       │   Может учитывать:
    │       │   • ранг НПЦ (m_rank)
    │       │   • фракцию (community)
    │       │   • здоровье (fHealth)
    │       │   • расстояние до зоны (m_tGraphID)
    │       │   • текущую загруженность зоны
    │       │   • приоритет типа задачи (patrol > guard > rest)
    │       │
    │       └── if (value > best) → best_terrain = эта зона
    │
    └── m_smart_terrain_id = best_terrain.ID
        register_npc(npc) → зона сохраняет НПЦ у себя
        │
        ▼
    process_task() / CStalkerActionSmartTerrain::execute():
        task() → CALifeSmartTerrainTask (game_vertex + level_vertex + position)
        НПЦ навигирует к точке назначения
        по достижении: m_task_reached = true → выполнение задачи (патруль/охрана/etc.)
```

---

### 15.9 Итог: что определяет цель НПЦ

| Фактор | Где задаётся | Влияет на |
|---|---|---|
| Временной интервал поиска | `.ltx`: `smart_terrain_choose_interval` | Как часто НПЦ ищет новую цель |
| Разрешение поиска | `can_choose_alife_tasks` (bool-флаг) | Может ли НПЦ вообще выбирать цели |
| Фильтрация зон | `enabled(npc)` — виртуальный метод | Какие зоны попадают на рассмотрение |
| Оценка зон | `suitable(npc)` — виртуальный метод | Какая зона получит наивысший балл |
| Логика suitable() | Lua-скрипт или C++ подкласс | Реальные правила (ранг, фракция, дистанция, тип задачи) |
| Навигация к цели | `CALifeSmartTerrainTask` | Точная 3D-координата и путь к ней |

**Вывод:** «Случайности» в целеполагании нет. Каждая умная зона сама решает, кого принять и с каким баллом. Движок предоставляет инфраструктуру (жадный выбор максимума), а конкретная бизнес-логика (патруль важнее отдыха, сталкеры определённого ранга идут в определённые места) реализуется в `suitable()` через Lua-скрипты или C++-подклассы.

---

## 16. Отладка ALife

### 16.1 Сборка с отладкой

Все debug-инструменты спрятаны за препроцессорным флагом `#ifdef DEBUG`. Чтобы они были доступны, нужно **собрать проект в Debug-конфигурации** (Visual Studio: конфигурация `Debug`; препроцессорный символ `DEBUG`, определяется через `/D DEBUG` или `/DDEBUG` — стандартный MSVC-синтаксис). В Release-сборках консольные команды `ai_draw_*` и детальные лог-блоки отсутствуют.

Дополнительный флаг для GOAP-планировщика — аргумент командной строки **`-dbgact`**, включающий вывод каждого действия:
```
"DEBUG: Action [%s] initializing"
"DEBUG: Action [%s] executing"
```

---

### 16.2 Флаги отладки AI (`psAI_Flags`)

Все флаги определены в `src/xrGame/ai_debug.h` и управляются битовым полем `psAI_Flags`:

| Флаг | Маска | Назначение |
|---|---|---|
| `aiDebug` | `1<<0` | Общий AI-отладчик |
| `aiBrain` | `1<<1` | Принятие решений (Brain) |
| `aiMotion` | `1<<2` | Движение / анимации путей |
| `aiFrustum` | `1<<3` | Отладка усечённой пирамиды видимости |
| `aiFuncs` | `1<<4` | Вызовы оценочных функций |
| `aiALife` | `1<<5` | **ALife-симулятор** (основной флаг) |
| `aiGOAP` | `1<<7` | Планировщик GOAP |
| `aiCover` | `1<<8` | Система укрытий |
| `aiVision` | `1<<10` | Система зрения |
| `aiMonsterDebug` | `1<<11` | Отладка монстров |
| `aiSerialize` | `1<<14` | Сериализация состояния |
| `aiGOAPScript` | `1<<17` | GOAP через скрипт |
| `aiGOAPObject` | `1<<18` | GOAP конкретного объекта |
| `aiStalker` | `1<<19` | Сталкер-специфика |
| `aiDrawGameGraph` | `1<<20` | **Рисовать граф уровней** |
| `aiDrawGameGraphStalkers` | `1<<21` | **Позиции сталкеров на графе** |
| `aiDrawGameGraphObjects` | `1<<22` | Все ALife-объекты на графе |
| `aiDrawGameGraphRealPos` | `1<<28` | Реальные 3D-позиции (не мини-карта) |
| `aiDrawVisibilityRays` | `1<<26` | Лучи видимости |

---

### 16.3 Консольные команды

#### Основные ALife-команды

```
ai_dbg_alife 1         // включить ALife-отладку (флаг aiALife)
ai_dbg_brain 1         // отладка принятия решений
ai_dbg_goap 1          // вывод плана GOAP в лог
ai_dbg_goap_script 1   // GOAP через скрипты
ai_dbg_goap_object 1   // GOAP конкретного объекта
mt_alife 1             // параллельное выполнение ALife (многопоток)
```

#### Визуализация game graph (только Debug-сборка)

```
ai_draw_game_graph 1                      // показать граф уровней
ai_draw_game_graph_stalkers 1             // показать позиции сталкеров
ai_draw_game_graph_objects 1              // показать все ALife-объекты
ai_draw_game_graph_real_pos 1             // использовать реальные 3D-координаты
ai_draw_game_graph_current_level          // граф текущего уровня
ai_draw_game_graph_all                    // граф всех уровней
ai_draw_game_graph_level <имя_уровня>     // граф конкретного уровня
```

#### Прочие полезные AI-команды

```
ai_monster_info 1       // информация о ближайшем монстре
ai_dbg_node 1           // отладка узлов nav-графа
ai_use_smart_covers 1   // включить/выключить smart covers
```

---

### 16.4 Цветовое кодирование game graph

`CLevelGraph::render()` в `level_graph_debug.cpp` (весь файл — только `#ifdef DEBUG`) рисует:

| Цвет | RGB (десятичный) | Объект |
|---|---|---|
| Голубой | (0, 255, 255) | Обычные вершины графа |
| Пурпурный | (255, 0, 255) | Специальные вершины |
| Зелёный | (0, 255, 0) | Рёбра (связи между вершинами) |
| Красный | (255, 0, 0) | Позиции сталкеров |
| Жёлтый | (255, 255, 0) | Текстовые метки объектов |

---

### 16.5 Логи ALife в консоли

Даже в Release-сборке ряд событий выводится через `Msg()`:

| Событие | Сообщение |
|---|---|
| Создание игры | `* Creating new game...` / `* New game is successfully created!` |
| Сохранение | `* Game %s is successfully saved to file '%s'` |
| Загрузка | `* Game %s is successfully loaded from file '%s' (%.3fs)` |
| Спавн объекта | `[LSS] Spawning object [%s][%s][%d]` |
| Начало боя | `[LSS] %s started combat versus %s` |
| Выбор атаки | `[LSS] %s choosed to attack %s` ¹ |
| Отступление | `[LSS] %s choosed to retreat from %s` ¹ |
| Смерть | `[LSS] %s is dead` |
| Загрузка спавна | `* %d spawn points are successfully loaded` |
| Загрузка объектов | `* %d objects are successfully loaded` |
| Сохранение объектов | `* %d objects are successfully saved` |

Все `[LSS]`-сообщения — из `alife_interaction_manager.cpp` и `alife_combat_manager.cpp`, видны в игровой консоли и в `log.txt`.

> ¹ Опечатка в оригинальном исходном коде (`choosed` вместо `chose`) — строки воспроизведены точно.

---

### 16.6 GOAP-планировщик: отладка решений

При включённом `ai_dbg_goap 1` (и сборке с `#ifdef LOG_ACTION`):

```cpp
// action_planner_inline.h
Msg("%6d : Solution for object %s [%d vertices searched]",
    Device.dwTimeGlobal,
    object_name(),
    solver_algorithm().data_storage().get_visited_node_count());
// Для каждого действия в плане:
Msg("%s", action2string(solution()[i]));
```

Позволяет видеть **текущий план НПЦ** (список действий) и **стоимость поиска** (число просмотренных вершин) в реальном времени.

---

### 16.7 ПДА: что видно про НПЦ

**PDA-интерфейс** (`UIPdaWnd`) содержит вкладки:

| Вкладка | Содержимое |
|---|---|
| `eptTasks` | Задания игрока |
| `eptRanking` | Рейтинги персонажей/фракций |
| `eptLogs` | Новости / сообщения (обновляются через `UpdateNews()`) |

Встроенного **списка NPC-отрядов с позициями на карте** в базовом PDA нет. Карта доступна через `UIMapWnd` (отдельное окно), но она показывает метки заданий игрока, а не положение ALife-объектов.

Позиции сталкеров в мире доступны только через консольную команду `ai_draw_game_graph_stalkers 1` (debug-сборка) — рисует красные точки прямо в 3D-мире.

---

### 16.8 Настройка параметров ALife (ltx/конфиги)

Основные параметры ALife задаются в C++-коде через методы симулятора:

```cpp
alife().set_process_time(microseconds);  // время на обработку за кадр
alife().objects_per_update(n);           // объектов за один тик
alife().set_switch_factor(factor);       // коэффициент переключения онлайн/оффлайн
```

Найденный конфиг-файл `gamedata/configs/mod_system_spawn_antifreeze_ignore.ltx` управляет исключениями из анти-фриза спавна — он не влияет на основные параметры симуляции.

**Для изменения параметров в runtime** используйте Lua-скрипты через биндинги `CALifeSimulatorScript` (экспорт в Lua через `alife_simulator_script.cpp`).

---

### 16.9 Быстрый старт отладки ALife

```
# 1. Собрать в Debug-конфигурации (Visual Studio → Debug)

# 2. Запустить игру с флагом для GOAP:
game.exe -dbgact

# 3. В консоли игры (тильда ~):
ai_dbg_alife 1
ai_dbg_goap 1
ai_draw_game_graph 1
ai_draw_game_graph_stalkers 1
ai_draw_game_graph_objects 1

# 4. Смотреть log.txt на [LSS]-сообщения:
# [LSS] Spawning object ...
# [LSS] X started combat versus Y
# [LSS] X is dead
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
| `ai_stalker_alife.cpp` | `src/xrGame/` |
| `ai_stalker_fire.cpp` | `src/xrGame/` |
| `alife_human_object_handler.h/.cpp` | `src/xrGame/` |
| `stalker_alife_task_actions.h/.cpp` | `src/xrGame/` |
| `ai_debug.h` | `src/xrGame/` |
| `console_commands.cpp` | `src/xrGame/` |
| `level_graph_debug.cpp` | `src/xrGame/` |
| `alife_interaction_manager.cpp` | `src/xrGame/` |
| `action_planner_inline.h` | `src/xrGame/` |
| `stalker_property_evaluators.cpp` | `src/xrGame/` |
| `alife_monster_brain_inline.h` | `src/xrServerEntities/` |
| `xrServer_script_macroses.h` | `src/xrServerEntities/` |
| `xrServer_Objects_ALife_script3.cpp` | `src/xrServerEntities/` |

---

## 17. Как добавить новую механику и дебаг-меню

### 17.1 Общая архитектура скриптовой системы

Игра использует **Lua (через Luabind)** для расширения игровой логики без перекомпиляции C++. Скрипты хранятся в `gamedata/scripts/*.script`.

Движок предоставляет:
- `RegisterScriptCallback(name, fn)` — подписка на игровые события
- `SendScriptCallback(name, ...)` — генерация события
- `ImGui.*` — полный набор ImGui-виджетов
- `ImGui.Groups.*` — система регистрации дебаг-панелей в overlay

**Точка входа в скриптовую систему:** движок вызывает функции-диспетчеры в `level_input.on_key_press` (зарегистрированы движком, см. `Level_input.cpp:188`).

---

### 17.2 Как работает F7 и ImGui overlay

```
Пользователь нажимает F7
│
└── Level_input.cpp::IR_OnKeyboardPress()
    │
    └── if (_curr == kEDITOR) {
        │   // kEDITOR связан с F7 через xr_level_controller.cpp
        │
        ├── 1-е нажатие: Device.imgui().Show()       → показать overlay
        ├── 2-е нажатие: Device.imgui().EnableInput() → захватить ввод (мышь/клавиши)
        └── 3-е нажатие: Device.imgui().Show(false)  → скрыть overlay
    }
    │
    └── Lua-диспетчер: level_input.on_key_press(key, action, disabled)
        └── RegisterScriptCallback("on_key_press", fn) → Lua-обработчики
```

После 2-го нажатия F7 можно кликать по ImGui-меню.

---

### 17.3 Система ImGui.Groups

Файл `gamedata/scripts/_imgui_groups.script` реализует точки расширения overlay:

| Группа | Когда рендерится | Назначение |
|---|---|---|
| `Main` | Когда overlay видим | Основные виджеты |
| `MenuBar` | Верхняя строка меню overlay | Кнопки-меню |
| `Debug` | Подменю **Debug** в MenuBar | Инструменты отладки |
| `Mods` | Подменю **Mods** в MenuBar | UI модов |
| `Unique` | **Каждый кадр** (независимо от overlay) | Постоянные окна |

**Добавить пункт в меню Debug:**
```lua
ImGui.Groups.Debug.Widget(function()
    local clicked, value = ImGui.MenuItem("My Tool", nil, show_window)
    if clicked then show_window = value end
end)
```

**Добавить окно, которое рисуется всегда (когда открыто):**
```lua
ImGui.Groups.Unique.Widget(function()
    if not show_window then return end
    -- ImGui.Begin / ... / ImGui.End
end)
```

---

### 17.4 Ключевые Lua API для механики торговли НПЦ

Все методы доступны на объектах типа `game_object` (онлайн-объекты):

| Метод | Описание |
|---|---|
| `obj:money()` | Получить баланс (u32) |
| `obj:transfer_money(amount, target)` | Перевести деньги другому объекту |
| `obj:give_money(amount)` | Добавить деньги объекту |
| `obj:transfer_item(item, target)` | Передать предмет в инвентарь `target` |
| `obj:iterate_inventory(fn, owner)` | Итерация по инвентарю |
| `obj:inventory_for_each(fn)` | Для каждого предмета |
| `level.object_by_id(id)` | Получить онлайн-объект по ID |
| `alife():object(id)` | Получить server-entity по ID (оффлайн тоже) |

**Ограничение:** `transfer_item` и `money` работают только для объектов в **онлайн-зоне** (в радиусе загрузки от актора). В оффлайне для денег нужны кастомные server-entity биндинги.

---

### 17.5 Структура реализованной механики

Четыре файла реализуют полный стек «протокол → механика → тест → дебаг-панель»:

#### `gamedata/scripts/npc_trade.script` — базовый API обмена

```lua
npc_trade.get_balance(npc_id)                              → number
npc_trade.list_inventory(npc_id)                           → { {section, id}, ... }
npc_trade.sell_item(seller_id, buyer_id, item_id, price)   → ok, msg
npc_trade.demo_trade(npc_a_id, npc_b_id, price)            → ok, msg
```

Логика `sell_item`: проверить инвентарь → проверить баланс →
`buyer:transfer_money(price, seller)` → `seller:transfer_item(item, buyer)`.

#### `gamedata/scripts/npc_trade_negotiation.script` — FSM переговоров

Реализует протокол из 9 шагов для покупателя и 6 для продавца.

**Покупатель (Stalker_2):**
```
idle → checking_inventory → needs_weapon → found_seller
     → sent_request → waiting_response → approaching → arrived → done
```

**Продавец (Stalker_1):**
```
idle → received_request → reviewing → responded → waiting_buyer → sold
```

Ключевые правила:
- Продавец **всегда соглашается** (`ACCEPT_CHANCE = 100`) — надёжный режим тестирования; меняется в одной строке
- Покупатель вызывает `set_desired_position(seller:position())` и идёт к продавцу
- Переход «arrived» происходит по условию `distance_to <= 5m` или по таймауту (5 шагов)
- Финальный обмен через `npc_trade.sell_item()`

**Публичный API:**
```lua
npc_trade_negotiation.start(buyer_id, seller_id)  -- запустить сессию
npc_trade_negotiation.update()                    -- обновить FSM (вызывать из AddUniqueCall)
npc_trade_negotiation.get_sessions()              → table  -- все активные сессии
npc_trade_negotiation.buyer_state(session)        → string -- текущее состояние покупателя
npc_trade_negotiation.seller_state(session)       → string -- текущее состояние продавца
npc_trade_negotiation.clear()                     -- сбросить все сессии
```

#### `gamedata/scripts/debug_trade_scenario.script` — тест-сценарий

Спавнит двух нейтральных сталкеров-лонеров, выдаёт снаряжение и запускает переговоры.

Открыть: **F7 → F7 → Debug → NPC Trade Scenario**.

> **Это скрипт-спавн, а не ALife.**  
> `alife():create()` обходит симуляцию и создаёт объект напрямую.  
> ALife-спавн управляется движком через смарт-терреины и spawn-точки уровня.

Что происходит после нажатия «Запустить сценарий»:
1. `alife():create("stalker_loner", pos, lv, gv)` × 2 — продавец (+3,+2м) и покупатель (−3,−2м)
2. Через `setup_delay_ms` (2 сек, ожидание онлайн-перехода): `obj:set_character_community("stalker", 0, 0)` — принудительная установка фракции лонеров на обоих НПЦ
3. `alife():create(weapon, pos, lv, gv, seller_id)` × 2 + `buyer:give_money(500)`
4. `npc_trade_negotiation.start(buyer_id, seller_id)` — запуск FSM
5. `AddUniqueCall` → `npc_trade_negotiation.update()` каждые 2 секунды реального времени
6. Все события: `Msg()` в log.txt + ImGui-лог

Пример лога в log.txt:
```
[SCENARIO] Метод спавна: alife():create() — СКРИПТ-СПАВН (не ALife).
[SCENARIO] Секция НПЦ: [stalker_loner] (лонеры, нейтральны друг к другу)
[SCENARIO] Spawned [stalker_loner] id=1234 pos=(150.3, 0.0, 200.1)
[SCENARIO] Spawned [stalker_loner] id=1235 pos=(144.3, 0.0, 200.1)
[SCENARIO]   community → 'stalker' (loner) для npc 1234
[SCENARIO]   community → 'stalker' (loner) для npc 1235
[SCENARIO]   + item [wpn_ak74] id=1236 → npc 1234
[SCENARIO]   + item [wpn_ak74] id=1237 → npc 1234
[TRADE]    Stalker_1235: нет оружия! Ищу торговца поблизости...
[TRADE]    Stalker_1235: нашёл торговца Stalker_1234 (оружий: 2, дистанция: 6.0m)
[TRADE]    Stalker_1234: рассматриваю запрос... ПРИНЯТЬ (1/100 <= 100% шанс)
[TRADE]    Stalker_1235: приближаюсь к Stalker_1234... 3.2m
[TRADE]    [СДЕЛКА ЗАВЕРШЕНА] Stalker_1235 купил 'wpn_ak74' у Stalker_1234 за 300 руб.
[TRADE]      Баланс: покупатель=200 руб | продавец=300 руб
```

**Почему `stalker_loner`, а не `stalker_bandit`:**  
| Секция | Фракция | Отношение к лонерам |
|---|---|---|
| `stalker_loner` | `stalker` (loner) | Нейтральны → мирная торговля ✓ |
| `stalker_bandit` | `bandit` | Враждебны → немедленная атака ✗ |

Класс движка для обоих — `"stalker"` (`object_factory_register.cpp:258`).  
Полный список секций — в `gamedata/configs/creatures/` базовой игры (не в этом репо).

**Настройка** (в начале файла):
```lua
local CFG = {
    seller_section = "stalker_loner",   -- нейтральная фракция
    buyer_section  = "stalker_loner",
    weapon_section = "wpn_ak74",        -- подобрать под контент
    buyer_money    = 500,
    setup_delay_ms = 2000,              -- задержка перехода в онлайн
}
```

#### `gamedata/scripts/debug_npc_trade.script` — ручная дебаг-панель

Регистрируется под «NPC Trade» в меню Debug.  
Позволяет вручную указывать ID объектов и исполнять разовые сделки (без FSM).

---

### 17.6 Как добавить новую механику — чеклист

```
1. Создать gamedata/scripts/<механика>.script
   └── Объявить модуль: my_mechanic = {}
   └── Реализовать функции (используя game_object API)
   └── Зарегистрировать callback: RegisterScriptCallback("on_game_start", ...)

2. Создать gamedata/scripts/debug_<механика>.script
   └── Объявить local state = {} для состояния панели
   └── Зарегистрировать пункт меню:
       ImGui.Groups.Debug.Widget(function()
           if ImGui.MenuItem("My Tool", nil, show) then show = not show end
       end)
   └── Зарегистрировать рендер окна:
       ImGui.Groups.Unique.Widget(function()
           if not show then return end
           ImGui.Begin("My Tool")
           -- ImGui виджеты
           ImGui.End()
       end)

3. Открыть дебаг-панель в игре:
   F7 → F7 (capture input) → Debug → My Tool
```

---

### 17.7 Источники

| Файл | Роль |
|---|---|
| `src/xrGame/Level_input.cpp` | F7 → `kEDITOR` → ImGui toggle |
| `src/xrEngine/imgui_base.h` | `xr_imgui::ide` — движок overlay |
| `gamedata/scripts/_imgui_groups.script` | Система групп ImGui |
| `gamedata/scripts/imgui_helper.script` | Вспомогательные ImGui-функции |
| `src/xrGame/script_game_object_script3.cpp` | `transfer_item`, `transfer_money`, `money`, `iterate_inventory` |
| `src/xrGame/InventoryOwner_script.cpp` | `get_money`, `EnableTrade` |
| `src/xrGame/script_game_object_inventory_owner.cpp` | Реализация `TransferItem`, `TransferMoney` |
| `gamedata/scripts/npc_trade.script` | **Базовый API: передача предметов и денег** |
| `gamedata/scripts/npc_trade_negotiation.script` | **FSM переговоров: протокол покупатель↔продавец** |
| `gamedata/scripts/debug_trade_scenario.script` | **Тест-сценарий: спавн + снаряжение + ImGui-панель** |
| `gamedata/scripts/debug_npc_trade.script` | **Ручная дебаг-панель для разовых сделок** |

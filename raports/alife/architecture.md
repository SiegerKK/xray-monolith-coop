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

## 15. Отладка ALife

### 15.1 Сборка с отладкой

Все debug-инструменты спрятаны за препроцессорным флагом `#ifdef DEBUG`. Чтобы они были доступны, нужно **собрать проект в Debug-конфигурации** (Visual Studio: конфигурация `Debug`; препроцессорный символ `DEBUG`, определяется через `/D DEBUG` или `/DDEBUG` — стандартный MSVC-синтаксис). В Release-сборках консольные команды `ai_draw_*` и детальные лог-блоки отсутствуют.

Дополнительный флаг для GOAP-планировщика — аргумент командной строки **`-dbgact`**, включающий вывод каждого действия:
```
"DEBUG: Action [%s] initializing"
"DEBUG: Action [%s] executing"
```

---

### 15.2 Флаги отладки AI (`psAI_Flags`)

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

### 15.3 Консольные команды

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

### 15.4 Цветовое кодирование game graph

`CLevelGraph::render()` в `level_graph_debug.cpp` (весь файл — только `#ifdef DEBUG`) рисует:

| Цвет | RGB (десятичный) | Объект |
|---|---|---|
| Голубой | (0, 255, 255) | Обычные вершины графа |
| Пурпурный | (255, 0, 255) | Специальные вершины |
| Зелёный | (0, 255, 0) | Рёбра (связи между вершинами) |
| Красный | (255, 0, 0) | Позиции сталкеров |
| Жёлтый | (255, 255, 0) | Текстовые метки объектов |

---

### 15.5 Логи ALife в консоли

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

### 15.6 GOAP-планировщик: отладка решений

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

### 15.7 ПДА: что видно про НПЦ

**PDA-интерфейс** (`UIPdaWnd`) содержит вкладки:

| Вкладка | Содержимое |
|---|---|
| `eptTasks` | Задания игрока |
| `eptRanking` | Рейтинги персонажей/фракций |
| `eptLogs` | Новости / сообщения (обновляются через `UpdateNews()`) |

Встроенного **списка NPC-отрядов с позициями на карте** в базовом PDA нет. Карта доступна через `UIMapWnd` (отдельное окно), но она показывает метки заданий игрока, а не положение ALife-объектов.

Позиции сталкеров в мире доступны только через консольную команду `ai_draw_game_graph_stalkers 1` (debug-сборка) — рисует красные точки прямо в 3D-мире.

---

### 15.8 Настройка параметров ALife (ltx/конфиги)

Основные параметры ALife задаются в C++-коде через методы симулятора:

```cpp
alife().set_process_time(microseconds);  // время на обработку за кадр
alife().objects_per_update(n);           // объектов за один тик
alife().set_switch_factor(factor);       // коэффициент переключения онлайн/оффлайн
```

Найденный конфиг-файл `gamedata/configs/mod_system_spawn_antifreeze_ignore.ltx` управляет исключениями из анти-фриза спавна — он не влияет на основные параметры симуляции.

**Для изменения параметров в runtime** используйте Lua-скрипты через биндинги `CALifeSimulatorScript` (экспорт в Lua через `alife_simulator_script.cpp`).

---

### 15.9 Быстрый старт отладки ALife

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
| `ui/UIPdaWnd.h/.cpp` | `src/xrGame/` |

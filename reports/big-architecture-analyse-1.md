# Архитектурный анализ X-Ray Monolith (xray-monolith-coop)

> **Цель**: Предварительный анализ архитектуры проекта как основа для переноса одиночного режима S.T.A.L.K.E.R. Anomaly на кооперативные рельсы.

---

## 1. Общее описание проекта

**X-Ray Monolith** — это форк движка X-Ray 1.6 (оригинально использованного в S.T.A.L.K.E.R. Call of Pripyat), адаптированный для мода **S.T.A.L.K.E.R. Anomaly**. Проект основан на Call of Chernobyl (CoC) 1.4.22, расширен большим числом патчей от сообщества и нацелен на улучшение производительности, стабильности и возможностей моддинга.

Версия движка: **X-Ray Monolith v1.5.3**

### Ключевые особенности
- Поддержка MT-версии с агрессивной многопоточностью
- Расширенный Lua API (LuaJIT 2.x / lua 5.1) для скриптования
- DLTX — система патчинга LTX-конфигов без перезаписи
- DXML — система патчинга XML-файлов через Lua
- ImGui-интеграция для отладки и dev-инструментов
- Discord Rich Presence
- Поддержка ReShade без воздействия на UI
- HDR10 вывод (DX11)
- Shader Scopes, 3D ballistics, улучшенный OpenAL (EFX)

---

## 2. Структура репозитория

```
xray-monolith-coop/
├── src/                  — Исходный код движка (C++)
│   ├── xrCore/           — Базовое ядро: типы, математика, файловая система, память
│   ├── xrEngine/         — Движок: устройство, рендер-интерфейс, планировщик, ввод
│   ├── xrGame/           — Игровая логика: актор, ИИ, ALife, сеть, UI, сервер
│   ├── xrNetServer/      — Сетевой транспорт (DirectPlay 8)
│   ├── xrServerEntities/ — Серверные объекты, Lua-движок, фабрика объектов
│   ├── xrSound/          — Звуковой движок (OpenAL)
│   ├── xrPhysics/        — Физический движок (ODE)
│   ├── xrCDB/            — Коллизионная БД (OPCODE)
│   ├── xrParticles/      — Система частиц
│   ├── xrCPU_Pipe/       — CPU-скининг, SSE/AVX
│   ├── Layers/           — Абстракция рендера
│   │   ├── xrAPI/        — Абстракции API (DX9/DX10/DX11)
│   │   ├── xrRender/     — Общие рендер-интерфейсы и реализации
│   │   ├── xrRenderDX9/  — Статический рендер (R1)
│   │   ├── xrRenderDX10/ — DX10 рендер (R3)
│   │   ├── xrRenderPC_R1 — Полная реализация R1
│   │   ├── xrRenderPC_R2 — Реализация R2
│   │   ├── xrRenderPC_R3 — Реализация R3
│   │   └── xrRenderPC_R4 — Реализация R4 (DX11)
│   ├── Include/          — Публичные интерфейсы (IKinematics, IRenderable...)
│   └── 3rd party/        — Внешние библиотеки
├── gamedata/             — Игровые данные
│   ├── configs/          — LTX-конфиги
│   ├── scripts/          — Lua-скрипты
│   ├── shaders/          — Шейдеры
│   ├── levels/           — Уровни
│   ├── materials/        — Материалы
│   └── textures/         — Текстуры
├── sdk/                  — SDK редактора (Level Editor, etc.)
├── compressor/           — Утилиты сжатия (xrCompress)
└── reports/              — Документация
```

---

## 3. Основные модули движка (C++)

### 3.1 xrCore — Базовое ядро

**Назначение**: Фундаментальные типы, утилиты, математика, работа с файлами и памятью.

**Ключевые компоненты**:
- `xrMemory` — кастомный менеджер памяти с поддержкой пулов, выравнивания, отладки
- `xrString / shared_str` — интернированные строки с пулингом для снижения аллокаций
- `LocatorAPI (FS)` — виртуальная файловая система: монтирование архивов (`.db`), папок, автообнаружение файлов
- `Xr_ini` — парсер `.ltx`-конфигов (INI-подобный формат с наследованием секций)
- `Math (_vector3d, _matrix, _quaternion, _fbox...)` — полный набор 3D-математики
- `FTimer` — высокоточный таймер
- `xrDebug` — система ассертов и обработки исключений
- Компрессоры: LZO, PPMd, RT (несколько алгоритмов для сжатия сетевых пакетов и ресурсов)
- `NET_utils` — базовые сетевые утилиты
- `xrSyncronize` — примитивы синхронизации (Critical Sections)

**Зависимости**: нет внутренних зависимостей (низший уровень)

---

### 3.2 xrEngine — Движок

**Назначение**: Управление устройством, игровым циклом, планировщиком, вводом, базовыми объектами.

**Ключевые компоненты**:
- `CRenderDevice / Device` — центральный объект: управление D3D-устройством, свойства экрана, игровой цикл (фреймы, таймер, `pureFrame/pureRender` callbacks)
- `CEngine` — главный класс движка, содержит:
  - `CEngineAPI External` — загрузчик DLL (xrGame, рендер)
  - `CEventAPI Event` — система событий движка
  - `CSheduler Sheduler` — планировщик задач (`ISheduled` интерфейс)
- `IGame_Level` — интерфейс игрового уровня
- `IGame_Persistent` — интерфейс постоянных данных игры (главное меню, переходы между уровнями)
- `xrSheduler` — планировщик: регулярный вызов `ISheduled::shedule_Update()`
- `Xr_input` — ввод с клавиатуры и мыши (DirectInput/XInput)
- `CameraBase / CameraManager` — базовая камера и менеджер камер
- `Feel::Touch / Feel::Vision / Feel::Sound` — системы обнаружения (тактильные, зрительные, звуковые триггеры)
- `IRenderable` — интерфейс рендерируемых объектов
- `xr_object / xr_object_list` — базовые игровые объекты и их контейнер
- `GameFont / GameMtlLib / LightAnimLibrary` — шрифты, материалы, анимации света
- `Environment` — динамическая погода и небо
- `Rain / Thunderbolt` — эффекты дождя и грозы
- `PS_instance` — экземпляры систем частиц
- `imgui_base / imgui_helper` — интеграция Dear ImGui в движок (debug UI)
- `xrTheora_Stream/Surface` — воспроизведение Theora-видео
- `xrSASH` — SASH (утилита автотестирования)
- `FDemoRecord / FDemoPlay` — запись и воспроизведение демо-роликов

**Особенности архитектуры**:
- Движок реализован как набор DLL: `xrEngine.dll` + `xrGame.dll` + render DLL
- Фабрика объектов через `DLL_Pure` и `CLASS_ID`
- Чистый callback-интерфейс через `pureRender`, `pureFrame`, `pureMove` и т.д.

---

### 3.3 xrGame — Игровая логика

Это самый крупный модуль (~1000+ файлов). Содержит:

#### 3.3.1 Архитектура сервер/клиент

Движок использует классическую клиент-серверную модель даже в одиночной игре:

- **`xrServer`** (`xrServer.h`) — серверная часть игры:
  - Управляет сущностями (`xrS_entities`, `CSE_Abstract*`)
  - Обрабатывает подключение клиентов (`xrClientData`)
  - Рассылает апдейты (`xrServer_process_update.cpp`)
  - Для одиночной игры клиент и сервер живут в одном процессе
  - Содержит `CALifeSimulator* m_alife_simulator`

- **`CLevel`** (`Level.h`) — клиентская часть:
  - Наследует `IGame_Level` и `IPureClient`
  - Управляет всеми клиентскими объектами
  - Обрабатывает сетевые сообщения (`Level_network.cpp`)
  - Содержит пул спавна (`CClientSpawnManager`)
  - Управляет очередью апдейтов (`NET_Queue`)

- **`game_sv_GameState` / `game_cl_GameState`** — серверная и клиентская части состояния игры
  - Для одиночной игры: `game_sv_Single` / `game_cl_Single`
  - Для мультиплеера: `game_sv_mp` / `game_cl_mp` с подтипами (DM, TDM, ArtefactHunt, CTA)

#### 3.3.2 ALife — система жизни мира

**ALife** — уникальная система Stalker: симулятор всего живого в игровом мире вне зоны видимости.

- **`CALifeSimulator`** (`alife_simulator.h`) — центральный симулятор:
  - Наследует `CALifeUpdateManager` и `CALifeInteractionManager`
  - Управляет объектами на уровне всего мира (граф уровней `game_graph`)
  - Регулирует онлайн/офлайн переход объектов

- **Компоненты ALife**:
  - `alife_object_registry` — реестр всех ALife-объектов
  - `alife_graph_registry` — граф путей между уровнями
  - `alife_spawn_registry` — точки спавна
  - `alife_story_registry` — "storyline" объекты
  - `alife_switch_manager` — переключение объектов онлайн/офлайн
  - `alife_time_manager` — игровое время
  - `alife_surge_manager` — управление выбросами
  - `alife_schedule_registry` — расписание апдейтов
  - `alife_smart_terrain_registry` — реестр смарт-зон

#### 3.3.3 CActor — главный персонаж

`CActor` (`Actor.h`) — один из ключевых классов:
```
CActor : CEntityAlive, IInputReceiver, Feel::Touch, 
         CInventoryOwner, CPhraseDialogManager, CStepManager, Feel::Sound
```
- Обрабатывает ввод, движение, физику, инвентарь, диалоги, звук шагов
- Сетевой код вынесен в `Actor_Network.cpp`
- Поддерживает экспорт и импорт сетевого состояния

#### 3.3.4 ИИ (Artificial Intelligence)

Используется **планировщик на основе STRIPS-подобных операций** (Goal-Oriented Action Planning):

- `action_planner` — планировщик действий
- `property_evaluator` — оценщик свойств
- `problem_solver` — решатель задач

**Stalker AI** (`ai/stalker/`):
- `ai_stalker.cpp` — основной класс сталкера
- Планировщики: `stalker_combat_planner`, `stalker_danger_planner`, `stalker_death_planner`, `stalker_movement_manager_base` и т.д.
- `stalker_animation_manager` — менеджер анимаций с блендингом

**Monster AI** (`ai/monsters/`):
- Различные типы монстров с собственными FSM/планировщиками
- `ai_monster_squad` — групповое поведение монстров

**Память ИИ**:
- `visual_memory_manager` — визуальная память (что видел)
- `sound_memory_manager` — звуковая память (что слышал)
- `hit_memory_manager` — память о полученных хитах
- `enemy_manager` — управление врагами

#### 3.3.5 Оружие и предметы

Иерархия:
- `CInventoryItem` → `CHudItem` → `CWeapon` → `CWeaponMagazined` → конкретные виды оружия
- Полная система магазинов, улучшений (`inventory_upgrade`), подствольников

#### 3.3.6 Физика (в контексте xrGame)

- `CCharacterPhysicsSupport` — управление физикой персонажа
- `CPHMovementControl` — движение через физику
- `CPHCommander` — центральный управляющий физическими вызовами
- `PHShellCreator` — создание физических оболочек
- `PHDestroyable` — разрушаемые объекты

#### 3.3.7 UI

Расположен в `xrGame/ui/`:
- Полный набор компонентов (UIWindow, UIButton, UIListBox, UIScrollView, UIProgressBar...)
- `UIActorMenu` — главное меню актора (инвентарь, апгрейды, торговля)
- `UIGameSP` — игровой HUD для одиночной игры
- `UIGameMP` — игровой HUD для мультиплеера
- `UIPdaWnd` — КПК (карта, задания, контакты)
- XML-driven: размещение через XML-файлы в `gamedata/configs/ui/`

---

### 3.4 xrNetServer — Сетевой транспорт

**Назначение**: Низкоуровневый сетевой слой.

**Технология**: **Microsoft DirectPlay 8** (`<dplay/dplay8.h>`)  
- DirectPlay — устаревшее API DirectX для игровых сетей (peer-to-peer и клиент-сервер)
- Используется протокол UDP с гарантированной/негарантированной доставкой

**Ключевые компоненты**:
- `IPureServer` / `IPureClient` — интерфейсы сервера и клиента
- `IClient` / `xrClientData` — данные клиентского соединения
- `NET_Packet` — сетевой пакет (сериализация данных)
- `MultipacketSender` / `MultipacketReciever` — объединение маленьких пакетов в один
- `NET_Compressor` — сжатие сетевых пакетов (LZO)
- `NET_AuthCheck` — проверка аутентификации
- `ip_filter` — IP-фильтрация (бан-листы)

**Конфигурация портов**:
- LAN клиент: 1234, LAN сервер: 1235, Интернет: 1237-1238

---

### 3.5 xrServerEntities — Серверные объекты и Lua-движок

#### 3.5.1 Lua-скриптовый движок

- **Runtime**: LuaJIT 1.1.8 или Lua 5.1 (в зависимости от сборки)
- **Биндинг**: Luabind (форк с обновлениями)
- **`CScriptEngine`** (`script_engine.h`) — управляет всеми Lua-потоками
- **`CScriptStorage`** — хранилище состояния Lua
- **`CScriptProcess`** — отдельный Lua-процесс/корутина
- **`CScriptThread`** — Lua-поток

Экспорт в Lua через макросы `DECLARE_SCRIPT_REGISTER_FUNCTION`, `script_export_macroses.h`.

#### 3.5.2 Фабрика серверных объектов

- `object_factory` — реестр всех классов игровых объектов
- `xrServer_Objects_ALife.h/cpp` — серверные объекты ALife (NPC, предметы, зоны)
- `xrServer_Object_Base` — базовый серверный объект
- `CSE_Abstract` — абстрактная серверная сущность

---

### 3.6 xrSound — Звуковой движок

**Технология**: **OpenAL 1.23.1** с поддержкой **EFX** (Environmental Audio Extensions)

- `SoundRender_Core` — центральное ядро: управление источниками, кэш
- `SoundRender_CoreA` — OpenAL-реализация
- `SoundRender_Emitter` — эмиттер звука (источник в 3D пространстве)
- `SoundRender_Target` / `SoundRender_TargetA` — цель воспроизведения (OpenAL source)
- `SoundRender_Source` — хранение звуковых данных (OGG → PCM)
- `SoundRender_Environment` — акустические среды (реверберация, EFX-профили)
- Поддержка **эффекта Доплера**

---

### 3.7 xrPhysics — Физический движок

**Технология**: **ODE** (Open Dynamics Engine)

- `PHWorld` — физический мир (обёртка над dWorld/dSpace)
- `PHShell` — физическая оболочка объекта
- `PHElement` — физический элемент (тело + геометрия)
- `PHJoint` — физический шарнир
- `PHCharacter` / `PHActorCharacter` / `PHSimpleCharacter` — физика персонажей
- `PHCapture` — захват объектов руками
- `PHFracture` — разрушение объектов
- `PhysicsShellAnimator` — синхронизация физики с анимацией

---

### 3.8 xrCDB — Коллизионная база данных

**Технология**: **OPCODE** (Optimized Collision Detection library)

- `xrCDB` — база коллизий уровня (BSP/BVH-деревья)
- `ISpatial` — пространственный индекс (octree/quadtree для динамических объектов)
- `xrXRC` — XRay Collision (раypick, AABB, frustum и сферные запросы)
- `xr_area` — зона запросов (сферы, AABB, фрустумы, лучи)

---

### 3.9 Render Layer — Рендер-подсистема

Рендер реализован как набор DLL, загружаемых в runtime:

| Рендер | API | Описание |
|--------|-----|----------|
| R1 (xrRenderPC_R1) | Direct3D 9 | Статическое освещение |
| R2 (xrRenderPC_R2) | Direct3D 9 | Динамическое освещение (deferred) |
| R3 (xrRenderDX10/xrRenderPC_R3) | Direct3D 10 | DX10 deferred shading |
| R4 (xrRenderPC_R4) | Direct3D 11 | DX11, HDR10, вычислительные шейдеры |

**Интерфейсы** (в `Include/xrRender/`):
- `IRenderDeviceRender` — рендер устройство
- `IKinematics` / `IKinematicsAnimated` — анимированные модели
- `IRenderVisual` — рендерируемый визуал
- `ImGuiRender` — интерфейс ImGui-рендера

**Общие компоненты** (`Layers/xrRender/`):
- `dxImGuiRender` — ImGui рендер
- `dxApplicationRender`, `dxConsoleRender`, `dxDebugRender`, `dxEnvironmentRender` — специализированные рендеры
- Blenders (шейдеры): `Blender_BmmD`, `Blender_Model`, `Blender_tree`, `Blender_Particle` и т.д.

---

## 4. Третьесторонние библиотеки

| Библиотека | Версия/Описание | Применение |
|------------|-----------------|------------|
| **LuaJIT** | 1.1.8 или Lua 5.1 | Скриптовый движок |
| **Luabind** | Форк (обновлённый) | Биндинг C++ → Lua |
| **ODE** | Open Dynamics Engine | Физика (rigid bodies, joints) |
| **OPCODE** | Optimized Collision Detection | Коллизии |
| **OpenAL** | 1.23.1 + EFX | Позиционный 3D звук |
| **DirectPlay 8** | Microsoft DirectX | Сетевой транспорт |
| **Direct3D 9/10/11** | Microsoft DirectX | Рендеринг |
| **ImGui** | Dear ImGui | Debug UI / dev-инструменты |
| **Intel TBB** | Threading Building Blocks | Параллелизм (MT-версия) |
| **Discord Game SDK** | discord_game_sdk | Rich Presence |
| **ICU** | icuuc | Unicode/UTF-8 |
| **ReShade** | reshadecompat | Постобработка (без влияния на UI) |
| **LZO** | rt_lzo | Сжатие данных |
| **PPMd** | PPMd | Сжатие данных |
| **libjpeg** | libjpeg | Обработка JPEG |
| **cximage** | CxImage | Обработка изображений |
| **stackwalker** | StackWalker | Стек-трейс в логах |
| **optick** | optick-git | Профилирование |
| **robin_hood** | robin_hood | Хэш-таблицы (MT-версия) |
| **crypto** | crypto | Криптография (CDKey) |
| **nvapi** | NVAPI | NVIDIA API |
| **fastdelegate** | Don Clugston's FastDelegate | Быстрые делегаты |

---

## 5. Игровые данные (gamedata/)

### 5.1 Конфигурация (configs/)
- **LTX-файлы** — INI-подобные файлы с секциями и наследованием
- **DLTX** — система патчинга через `mod_*.ltx` файлы без перезаписи оригиналов
- `mod_system_*.ltx` — системные конфиги (HUD, базовые предметы, пути патрулирования)
- `patrol_paths.ltx` — пути патрулирования NPC (LTX-формат, новое расширение)

### 5.2 Скрипты (scripts/)
- **Lua 5.1** — основной язык скриптинга
- Ключевые скрипты:
  - `axr_main.script` — главный скрипт Anomaly
  - `bind_monster.script` — биндеры монстров
  - `dynamic_callbacks.lua` — динамические колбэки
  - `dxml_core.script` — ядро DXML системы
  - `class_registrator_modded_exes.script` — регистратор классов
  - `imgui_helper.script` / `imgui_lua_debug.script` — ImGui скрипты
  - `options_modded_exes_*.script` — настройки расширенных опций

### 5.3 Шейдеры (shaders/)
- HLSL-шейдеры для рендер-режимов R1–R4

---

## 6. Архитектура сетевой подсистемы (детально)

Понимание этой части критически важно для реализации кооперативного режима.

### 6.1 Топология

```
┌─────────────────────────────────────────────────────────┐
│                    Один процесс (Single Player)          │
│                                                          │
│  ┌──────────────┐        ┌──────────────────────────┐  │
│  │   xrServer    │◄──────►│       CLevel             │  │
│  │ (IPureServer) │  NET   │ (IPureClient)            │  │
│  │               │        │                          │  │
│  │  game_sv_Single │      │  game_cl_Single          │  │
│  │  CALifeSimulator│      │  CActor (local player)   │  │
│  └──────────────┘        └──────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

Для мультиплеера (DM/TDM):
```
┌──────────────────┐     DirectPlay 8 / UDP    ┌──────────────────────┐
│  SERVER PROCESS  │◄──────────────────────────►│  CLIENT PROCESS      │
│  xrServer        │                            │  CLevel (IPureClient)│
│  game_sv_mp      │                            │  game_cl_mp          │
└──────────────────┘                            └──────────────────────┘
```

### 6.2 Протокол обмена сообщениями

Основные типы сообщений (из `xrMessages.h`):
- `M_SPAWN` — спавн объекта
- `M_EVENT` — игровые события
- `M_UPDATE` — обновление состояния объекта
- `M_MOVE` — движение актора
- `M_CHAT` — чат
- `M_FILE_TRANSFER` — передача файлов (карты)

Все пакеты (`NET_Packet`) имеют:
- Тип сообщения (`u16`)
- Данные переменной длины
- Опционально: гарантированная доставка

### 6.3 Обработка объектов

Каждый объект имеет две половины:
- **Серверная (CSE_Abstract)**: позиция в ALife, настройки спавна, состояние
- **Клиентская (CGameObject)**: рендер, физика, ИИ, анимации

Переход онлайн/офлайн (`alife_switch_manager`):
- `online` — объект заспавнен на клиенте, получает update'ы
- `offline` — объект существует только в ALife-симуляторе

---

## 7. Архитектурные паттерны

### 7.1 DLL-архитектура

```
x_ray.exe
    └── xrEngine.dll
            ├── xrCore.dll
            ├── xrGame.dll
            │       ├── xrServerEntities.dll (Lua engine + server objects)
            │       ├── xrNetServer.dll
            │       ├── xrSound.dll
            │       ├── xrPhysics.dll
            │       └── xrCDB.dll
            └── xrRender_R*.dll (загружается динамически по выбору)
```

### 7.2 Factory Pattern

`object_factory` регистрирует все классы объектов через `CLASS_ID` (4-байтный идентификатор).
Создание объектов через `xrFactory_Create(CLASS_ID)`, уничтожение через `xrFactory_Destroy`.

### 7.3 Шедулер (Scheduler)

`CSheduler` регулярно вызывает `shedule_Update()` всех зарегистрированных `ISheduled` объектов.
Масштабируется через `shedule_Scale()` (1.0 = раз в кадр, 0.1 = реже).

### 7.4 Pure Callbacks

```cpp
class pureFrame  { virtual void OnFrame() = 0; };
class pureRender { virtual void OnRender() = 0; };
class pureMove   { virtual void OnMove()   = 0; };
```
Объекты регистрируются в `Device` и получают колбэки на каждый кадр/фрейм/тик.

### 7.5 GOAP (Goal-Oriented Action Planning) для ИИ

Каждый NPC содержит:
- `CALifeHumanBrain` — стратегическое поведение (где находиться в мире)
- `CStalkerPlanner` → набор под-планировщиков (combat, danger, death, search...)
- Каждый планировщик: набор `action_base` + `property_evaluator`

---

## 8. Критические точки для реализации кооператива

### 8.1 Что нужно изменить

**Главная проблема**: Одиночная игра (Single Player) использует `game_sv_Single`, который:
1. Содержит `CALifeSimulator` — симулятор, работающий только на хосте
2. Не поддерживает нескольких акторов (`CActor`)
3. Не синхронизирует состояние ALife между клиентами

**Критические компоненты для изменения**:

| Компонент | Проблема | Необходимое изменение |
|-----------|----------|-----------------------|
| `game_sv_Single` | Один актор | Поддержка нескольких акторов |
| `CALifeSimulator` | Только на хосте | Частичная синхронизация между клиентами |
| `CActor` (сеть) | `Actor_Network.cpp` предполагает один локальный актор | Поддержка удалённых акторов |
| `alife_switch_manager` | Онлайн/офлайн зависит от одного игрока | Расчёт по всем игрокам |
| `game_cl_Single` | Не поддерживает несколько клиентов | Новый `game_cl_coop` |
| Транспорт `DirectPlay 8` | Устаревший, только LAN/интернет через DirectX | Возможна замена на ENet/SteamNetworking |

### 8.2 Существующий мультиплеерный код

Движок уже содержит полную реализацию мультиплеера для PvP-режимов:
- `game_sv_mp` / `game_cl_mp` — базовые MP классы
- Система синхронизации движения, стрельбы, хитов
- `actor_mp_client` / `actor_mp_server` — акторы в мультиплеере

Это очень ценная основа: **кооперативный режим можно создать как производный от `game_sv_mp` / `game_cl_mp`**, добавив:
- Синхронизацию ALife
- Отключение PvP-хитов
- Общий прогресс квестов
- Синхронизацию инвентаря/торговли

### 8.3 Главные технические вызовы

1. **ALife-синхронизация**: `CALifeSimulator` работает только на сервере. Клиенты видят мир через стриминг объектов. Для коопа нужно, чтобы хост управлял ALife, а клиенты получали его состояние.

2. **Множество акторов**: `g_actor` — глобальный указатель на единственного актора. В коопе нужен список всех акторов + различение локального.

3. **Камера и ввод**: Каждый клиент должен иметь свою камеру и свой ввод, что уже реализовано в MP.

4. **Уровень загрузки**: Синхронизированная загрузка карты (`Level_network_map_sync.cpp`) уже есть в движке.

5. **Квесты и прогресс**: ALife не предназначен для многопользовательского доступа. Нужна система блокировок или репликации состояния квестов.

6. **Выброс (Surge)**: `alife_surge_manager` должен выполняться только на хосте и синхронизироваться.

7. **Сохранения**: `game_sv_Single` управляет сохранениями. В коопе нужна стратегия: сохранение только у хоста, или у каждого игрока.

---

## 9. Поток данных (Data Flow)

### 9.1 Загрузка уровня

```
x_ray.exe
  → CEngine::Initialize()
  → CRenderDevice (создание D3D устройства)
  → CEngineAPI::Initialize() (загрузка xrGame.dll)
  → IGame_Persistent::OnGameStart()
  → xrServer::Create() → game_sv_Single::Create() → CALifeSimulator()
  → CLevel::Load() 
    → Загрузка геометрии (level.geom, level.cform)
    → Создание объектов через CClientSpawnManager
    → Синхронизация сервер↔клиент
```

### 9.2 Игровой цикл (frame)

```
CRenderDevice::Run() [основной цикл]
  → Device.PreFrame() (обновление таймера)
  → CSheduler::Update() (плановые задачи)
    → CLevel::shedule_Update()
      → ClientReceive() (получение сетевых пакетов)
      → ProcessGameEvents() (обработка событий)
      → Objects.Update() (апдейт всех объектов)
        → CActor::UpdateCL()
        → AI-объекты UpdateCL()
  → CRenderDevice::Render() (рендер сцены)
  → Device.PostFrame()
```

### 9.3 Сетевой тик

```
Сервер:
  xrServer::Update()
    → game_sv_Single::Update()
      → CALifeSimulator::update() (ALife тик)
      → game_sv_Single::SendUpdates()
        → PerObject: CSE_Abstract::net_Export() → NET_Packet
        → IClient::SendPacket()

Клиент:
  CLevel::ClientReceive()
    → IPureClient::RecievePacket()
      → switch(msg_type):
        M_SPAWN → CClientSpawnManager::Process()
        M_UPDATE → CGameObject::net_Import()
        M_EVENT → game_cl_GameState::net_signal()
```

---

## 10. Lua-скриптинг в контексте кооперации

Большая часть игровой логики вынесена в Lua (`gamedata/scripts/`). Для кооперации это означает:

- **Серверная логика** в Lua (квесты, диалоги, торговля) работает только на хосте
- **Клиентские скрипты** (HUD, анимации, визуальные эффекты) работают у каждого
- DXML/DLTX гарантируют совместимость конфигов между клиентами

Система колбэков (`dynamic_callbacks.lua`) позволяет мода́м подписываться на события:
- `actor_on_update` — тик актора
- `npc_on_hit` — попадание в NPC  
- `on_game_load` — загрузка игры
- Эти колбэки нужно будет расширить для сетевых событий кооп-режима

---

## 11. Итоги и рекомендации

### 11.1 Сильные стороны архитектуры для коопа

1. ✅ **Уже есть клиент-серверная модель** — одиночная игра использует её внутренне
2. ✅ **Есть полноценный MP-код** (DM/TDM/ArtefactHunt) — хорошая база
3. ✅ **NET_Packet и DirectPlay** — рабочий сетевой транспорт
4. ✅ **actor_mp_client / actor_mp_server** — уже есть сетевые акторы
5. ✅ **Level_network_map_sync** — синхронизация карты уже реализована
6. ✅ **Lua-скриптинг** — логику можно изменять без перекомпиляции движка

### 11.2 Главные препятствия

1. ⚠️ **DirectPlay 8 устарел** — могут быть проблемы с NAT traversal и Windows 11+
2. ⚠️ **ALife — однопользовательский симулятор** — требует серьёзной работы
3. ⚠️ **g_actor — глобальный синглтон** — потребует рефакторинга
4. ⚠️ **Квесты/прогресс** — нет механизма синхронизации
5. ⚠️ **Физика ODE** — детерминирована только при одинаковых входных данных

### 11.3 Рекомендуемый путь

**Минимальный MVP кооп**:
1. Создать `game_sv_coop` на основе `game_sv_mp` + интеграция `CALifeSimulator`
2. Создать `game_cl_coop` на основе `game_cl_mp` + UI одиночной игры
3. Заспавнить второго актора через `game_sv_coop::OnCreate`
4. Ограничить ALife-апдейты только хостом
5. Синхронизировать переходы между уровнями

**Долгосрочно**:
- Рассмотреть замену DirectPlay на ENet или Steam Networking
- Синхронизация ALife через снапшоты состояния
- Система совместных сохранений
- Синхронизация Lua-состояния для критических квест-событий

---

*Документ составлен на основе анализа исходного кода xray-monolith-coop (коммит: март 2026)*

---

## 12. Детальный анализ компонентов, требующих изменений для кооператива

Этот раздел — результат глубокого изучения исходного кода. Каждый блок описывает конкретный компонент, текущее ограничение и точное место, которое нужно изменить.

---

### 12.1 Глобальный указатель `g_actor` — критический синглтон

**Файл**: `src/xrGame/Actor_Network.cpp`, строка 58  
**Проблема**: Единственный глобальный указатель на актора игрока.

```cpp
CActor* g_actor = NULL;

CActor* Actor() {
    VERIFY(g_actor);
    return (g_actor);
}
```

**Механизм присвоения** (строка 532):
```cpp
BOOL CActor::net_Spawn(CSE_Abstract* DC) {
    // ...
    if (OnServer()) {
        E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE); // В SP сервер всегда делает объект локальным
    }
    // Становится g_actor ТОЛЬКО если LOCAL + ASPLAYER
    if (TRUE == E->s_flags.test(M_SPAWN_OBJECT_LOCAL) && 
        TRUE == E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))
        g_actor = this;
```

`M_SPAWN_OBJECT_ASPLAYER` (флаг `1<<3`) выставляется сервером при создании персонажа игрока. В одиночной игре это единственный актор.

**В кооперативе**: Каждый клиент должен знать, какой актор — _его собственный_. `g_actor` должен быть заменён на:
```cpp
// Текущий локальный актор (принадлежащий этому клиенту)
CActor* g_actor = NULL;  // остаётся как "мой актор"

// Все акторы в мире (все игроки)
extern xr_vector<CActor*> g_all_actors;  // новый контейнер
```

**Где используется**: `g_actor` используется в 200+ местах в `xrGame`. Критические:
- `Actor_Feel.cpp:282` — только локальный актор получает тактильные ощущения
- `Actor_Movement.cpp:149` — только локальный актор управляется с клавиатуры
- `sight_manager_target.cpp:55` — AI сталкеров видит `g_actor` как цель
- `ActorNightVision.cpp:31,46,60` — приборы ночного видения
- `UIHudStatesWnd.cpp:579,603` — HUD-координаты

Большинство этих мест используют `g_actor` правильно — они хотят именно _свой_ актор. Но AI и некоторые серверные системы хотят знать о _всех_ игроках.

---

### 12.2 Тип игры и класс-фабрика

**Файл**: `src/xrServerEntities/game_base_space.h`  
**Текущие типы игр** (enum `EGameIDs` закомментирован, используется числами):
```cpp
eGameIDSingle              = u32(1) << 0,
eGameIDDeathmatch          = u32(1) << 1,
eGameIDTeamDeathmatch      = u32(1) << 2,
eGameIDArtefactHunt        = u32(1) << 3,
eGameIDCaptureTheArtefact  = u32(1) << 4,
```

**Файл**: `src/xrGame/game_base.cpp`, функция `getCLASS_ID()`  
Строковое имя типа игры → CLASS_ID серверного и клиентского классов:
```cpp
case eGameIDSingle:
    return (isServer) ? TEXT2CLSID("SV_SINGL") : TEXT2CLSID("CL_SINGL");
```

**Файл**: `src/xrServerEntities/object_factory_register.cpp`, строки 208–221  
Регистрация в фабрике:
```cpp
add<game_sv_Single>(CLSID_SV_GAME_SINGLE,      "game_sv_single");
add<game_cl_Single>(CLSID_CL_GAME_SINGLE,      "game_cl_single");
add<game_sv_Deathmatch>(CLSID_SV_GAME_DEATHMATCH, "game_sv_deathmatch");
add<game_cl_Deathmatch>(CLSID_CL_GAME_DEATHMATCH, "game_cl_deathmatch");
// ... и т.д.
```

**Файл**: `src/xrServerEntities/clsid_game.h`, строки 211–221  
CLSID-константы:
```cpp
#define CLSID_SV_GAME_SINGLE   MK_CLSID('S','V','_','S','I','N','G','L')
#define CLSID_CL_GAME_SINGLE   MK_CLSID('C','L','_','S','I','N','G','L')
```

**Для кооператива нужно добавить**:
1. `eGameIDCooperative = u32(1) << 5` в enum
2. `CLSID_SV_GAME_COOP = MK_CLSID('S','V','_','C','O','O','P',' ')` в `clsid_game.h`
3. `CLSID_CL_GAME_COOP = MK_CLSID('C','L','_','C','O','O','P',' ')` в `clsid_game.h`
4. Ветку `case eGameIDCooperative:` в `getCLASS_ID()`
5. `add<game_sv_Coop>()` и `add<game_cl_Coop>()` в `object_factory_register.cpp`

---

### 12.3 Создание сервера и запуск игры

**Файл**: `src/xrGame/Level_start.cpp`, функция `net_start1()`, строки ~115–120  
Тип сервера зависит от типа игры:
```cpp
if (!xr_strcmp(p.m_game_type, "single"))
    Server = xr_new<xrServer>();
else {
    g_allow_heap_min = false;
    Server = xr_new<xrGameSpyServer>(); // GameSpy — устаревшая система для MP
}
```

`xrGameSpyServer` — это сервер с поддержкой GameSpy-мастер-сервера для поиска игр. Для кооператива нужно использовать `xrServer` (как в SP), но с реальной сетевой работой (не `psNET_direct_connect`).

**Файл**: `src/xrNetServer/NET_Client.cpp` (и `NET_Shared.h`)  
Флаг `psNET_direct_connect` — если установлен, клиент и сервер работают в одном процессе без реального сетевого стека. Для кооператива этот флаг **не должен быть установлен** (нужна реальная сеть).

---

### 12.4 CALifeSimulator — центральное препятствие для кооператива

**Файл**: `src/xrGame/game_sv_single.cpp`  
```cpp
void game_sv_Single::Create(shared_str& options) {
    inherited::Create(options);
    if (strstr(*options, "/alife"))
        m_alife_simulator = xr_new<CALifeSimulator>(&server(), &options);
    switch_Phase(GAME_PHASE_INPROGRESS);
}
```

`CALifeSimulator` создаётся только на сервере (`game_sv_Single`). Клиенты вообще не имеют прямого доступа к объектам ALife.

#### 12.4.1 Онлайн/офлайн переключение (`CALifeSwitchManager`)

**Файл**: `src/xrGame/alife_switch_manager_inline.h`  
Дистанции переключения конфигурируются в `alife.ltx`:
```cpp
m_switch_distance = pSettings->r_float(section, "switch_distance"); // типично 150м
m_online_distance  = m_switch_distance * (1.f - m_switch_factor);   // ~120м
m_offline_distance = m_switch_distance * (1.f + m_switch_factor);   // ~180м
```

**Функции** (`src/xrGame/alife_switch_manager.cpp`):
- `try_switch_online(object)` — заспавнить объект на клиентах если игрок рядом
- `try_switch_offline(object)` — удалить объект с клиентов если игрок ушёл далеко
- `add_online(object)` — непосредственно вызывает `server().Process_spawn()` → отправляет `M_SPAWN` всем клиентам

**Проблема**: `update_switch()` в `CALifeUpdateManager::update()` вычисляет расстояния **только от позиции одного актора** (через `ai().level_graph()` и локальный граф). В кооперативе нужно вычислять **объединение зон онлайн-присутствия** для всех игроков.

**Конкретный код для изменения** (`src/xrGame/alife_switch_manager.cpp`):
```cpp
// ТЕКУЩИЙ КОД (псевдо):
void CALifeSwitchManager::update_switch() {
    // Перебирает объекты и проверяет расстояние до ОДНОГО игрока
    for (auto& obj : objects) {
        float dist = distance_to_actor(obj);
        if (dist < online_distance)  switch_online(obj);
        if (dist > offline_distance) switch_offline(obj);
    }
}

// НУЖНО ДЛЯ КООПА:
void CALifeSwitchManager::update_switch() {
    for (auto& obj : objects) {
        float min_dist = FLT_MAX;
        // Проверяем расстояние до КАЖДОГО игрока
        for (auto* actor : g_all_actors) {
            min_dist = min(min_dist, distance_to(obj, actor->Position()));
        }
        if (min_dist < online_distance)  switch_online(obj);
        if (min_dist > offline_distance) switch_offline(obj);
    }
}
```

#### 12.4.2 Смена уровня (`change_level`)

**Файл**: `src/xrGame/game_sv_single.cpp`
```cpp
bool game_sv_Single::change_level(NET_Packet& net_packet, ClientID sender) {
    return (alife().change_level(net_packet));
}
```

Один игрок нажимает на "Level Changer" → сервер вызывает `change_level`. В кооперативе нужен механизм:
1. Собрать голоса/согласие всех игроков (или только хост решает)
2. Дождаться, пока все клиенты готовы к смене уровня
3. Только потом выполнить переход

#### 12.4.3 Сохранение и загрузка (`CALifeStorageManager`)

**Файл**: `src/xrGame/alife_storage_manager.h`  
`save(LPCSTR save_name)` — сохраняет весь мир ALife в `.sav` файл.  
`load(LPCSTR save_name)` — загружает.

В кооперативе:
- Только хост должен выполнять `save()`
- При загрузке все клиенты должны использовать одно и то же сохранение (синхронизация через файловую систему или через сеть)
- Состояние персонажей клиентов (инвентарь, здоровье, прогресс) — отдельная задача

---

### 12.5 Синхронизация состояния актора в сети

#### 12.5.1 Флаги спавна актора

**Файл**: `src/xrServerEntities/xrMessages.h`, строки 272, 276  
```cpp
M_SPAWN_OBJECT_LOCAL   = (1 << 0), // Объект принадлежит/обновляется локально
M_SPAWN_OBJECT_ASPLAYER = (1 << 3), // Объект является управляемым игроком
```

В SP сервер выставляет `M_SPAWN_OBJECT_LOCAL` для актора принудительно:
```cpp
// Actor_Network.cpp:526
if (OnServer()) {
    E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
}
```

В MP (через `CActorMP`): сервер присваивает актор нужному клиенту, и только тот клиент получает `LOCAL`. Остальные видят актора как `Remote`.

Эта логика **уже правильная для кооперации** — достаточно создать актора для каждого клиента с нужными флагами.

#### 12.5.2 Что синхронизируется в MP акторе

**Файл**: `src/xrGame/actor_mp_state.h`  
```cpp
struct actor_mp_state {
    Fquaternion physics_quaternion;
    Fvector     physics_angular_velocity;
    Fvector     physics_linear_velocity;
    Fvector     physics_force;
    Fvector     physics_torque;
    Fvector     physics_position;
    Fvector     position;
    Fvector     logic_acceleration;
    float       model_yaw;
    float       camera_yaw;
    float       camera_pitch;
    float       camera_roll;
    u32         time;
    float       health;
    float       radiation;
    u32         inventory_active_slot  : 4;
    u32         body_state_flags       : 15;
    u32         physics_state_enabled  : 1;
};
```

Это **достаточный минимум** для отображения другого игрока. Для полного кооперативного опыта нужно дополнительно синхронизировать:
- Состояние инвентаря (что взял/положил)
- Текущее оружие и его состояние (патроны, режим огня)
- Состояние здоровья/кровотечения/радиации (для HUD других игроков)
- Взаимодействия с миром (открыл дверь, взял предмет)

---

### 12.6 ИИ Stalker — видение игроков в коопе

**Файл**: `src/xrGame/sight_manager_target.cpp`, строка 55
```cpp
if (g_actor == object)
    // особая логика для игрока
```

**Файл**: `src/xrGame/visual_memory_manager.cpp`  
Система памяти ИИ: сталкеры "видят" объекты через `Feel::Vision`. В SP только один актор является потенциальной целью из категории "игрок". В коопе нужно, чтобы ИИ замечал **всех** акторов игроков.

Это потребует изменений в:
- `CAIStalker::feel_vision_isRelevant()` — кто является объектом зрения
- `memory_manager` — регистрация всех игроков как потенциальных врагов/целей
- Системы поиска цели в `stalker_combat_actions.cpp`

---

### 12.7 IsServer/IsClient и разделение логики

**Файл**: `src/xrGame/Level.cpp`, строки 1754–1767  
```cpp
bool CLevel::IsServer() {
    if (!Server || IsDemoPlayStarted()) return false;
    return true; // Server != nullptr → это хост
}

bool CLevel::IsClient() {
    if (IsDemoPlayStarted()) return true;
    if (Server) return false; // Если есть сервер — это хост, не чистый клиент
    return true;
}
```

В SP: `IsServer() = true`, `IsClient() = false` (хост сам себе сервер).  
В MP-клиент: `IsServer() = false`, `IsClient() = true`.  
В MP-хост: `IsServer() = true`, `IsClient() = false`.

Для кооперативного хоста хочется `IsServer() = true && IsClient() = true` (хост и играет, и управляет ALife). Текущая логика делает это корректно — хост с `Server != nullptr` является и сервером, и может играть.

**Потенциальная проблема**: Некоторые проверки типа:
```cpp
if (GameID() == eGameIDSingle || !OnServer()) return;
```
(Actor_Network.cpp:1814) — логика подходит для SP и MP. Для кооператива нужно убедиться, что все такие проверки корректно обработаны при новом `eGameIDCooperative`.

---

### 12.8 Физический движок ODE — детерминизм

**Файл**: `src/xrPhysics/PHWorld.cpp`  
ODE **не гарантирует** идентичное поведение на разных машинах из-за:
- Различных режимов с плавающей точкой
- Разного порядка обработки тел

**Последствия для кооперации**:
- Полная авторитетная физика (как в классическом Source Engine) требует симуляции только на сервере и отправки результатов клиентам
- **Для MVP кооперации**: физика каждого актора вычисляется на его владеющем клиенте, результат отправляется на сервер и к другим клиентам (уже делается через `actor_mp_state.physics_*`)
- Физика окружения (бочки, трупы, двери) должна быть авторитетной на сервере

---

### 12.9 UI и HUD — разделение локальных и удалённых данных

**Файл**: `src/xrGame/ui/UIHudStatesWnd.cpp`  
```cpp
// Строка 579:
posf.set(Level().CurrentControlEntity()->Position());
// Строка 603:
Fvector P = Level().CurrentControlEntity()->Position();
```

`CurrentControlEntity()` — возвращает актора, которым управляет текущий клиент. Это **уже правильно** для кооператива — каждый клиент рисует HUD для своего персонажа.

**Нужно добавить**:
- Маркеры/иконки других игроков на карте и в мире
- Список игроков (здоровье, статус) в HUD
- Возможно, систему голосовых/текстовых команд

---

### 12.10 Lua-скриптинг — серверная vs клиентская логика

**Файл**: `gamedata/scripts/axr_main.script`  
Большинство скриптов Anomaly запускаются **только на хосте** в контексте SP. Для кооперации нужно разграничить:

| Тип скрипта | Где выполняется | Проблема в коопе |
|-------------|-----------------|------------------|
| ALife callbacks (`on_npc_death`) | Только хост | ✅ Нормально |
| Квест-скрипты (`task_manager`) | Только хост | ⚠️ Клиенты не знают о прогрессе |
| HUD-скрипты (`ui_hud`) | Только свой клиент | ✅ Нормально |
| Actor callbacks (`actor_on_hit`) | Только хост в SP | ⚠️ В коопе должен быть на клиенте-владельце |
| Динамические события (`dynamic_callbacks`) | Везде | ⚠️ Нужна синхронизация |

Нужен новый тип колбэка — `on_coop_event(event_type, data)` для передачи событий от хоста клиентам через `NET_Packet`.

---

## 13. Архитектурная концепция кооперативного режима

### 13.1 Принципы проектирования

1. **Хост — абсолютный авторитет**: ALife, квесты, сохранения — только у хоста.
2. **Клиент — представление**: Каждый клиент получает данные от хоста и управляет только своим актором.
3. **Минимум изменений**: Использовать существующую MP-инфраструктуру (`game_sv_mp` / `game_cl_mp`) как фундамент.
4. **Постепенное расширение**: MVP сначала, полная синхронизация потом.
5. **Lua-расширяемость**: Бизнес-логику кооп-сессии реализовывать через скрипты, не трогая движок без необходимости.

---

### 13.2 Предлагаемая архитектура: слои изменений

```
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 1: Игровой тип                          │
│  + eGameIDCooperative (game_base_space.h)                       │
│  + CLSID_SV_GAME_COOP / CLSID_CL_GAME_COOP (clsid_game.h)      │
│  + Регистрация в object_factory_register.cpp                    │
│  + Ветка в getCLASS_ID() (game_base.cpp)                        │
└─────────────────────────────────────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 2: Серверная логика                      │
│  game_sv_Coop : game_sv_mp (Хост+ALife управляет миром)         │
│  ├── m_alife_simulator (CALifeSimulator*) — как в SP            │
│  ├── Override: Create() → инициализация ALife                   │
│  ├── Override: OnCreate() → как в game_sv_Single                │
│  ├── Override: Update() → ALife тик + MP тик                   │
│  ├── Override: change_level() → ждём всех + ALife.change        │
│  ├── Override: save_game() → ALife.save + игроки                │
│  └── Override: load_game() → ALife.load + рассылка клиентам     │
└─────────────────────────────────────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 3: Клиентская логика                     │
│  game_cl_Coop : game_cl_mp (Клиент отображает мир)              │
│  ├── Override: createGameUI() → SP HUD (UIGameSP)               │
│  ├── Override: GetGameTime() → принимает от хоста               │
│  ├── Override: TranslateGameMessage() → кооп-события            │
│  └── Обработка новых пакетов: квест-события, ALife-нотификации  │
└─────────────────────────────────────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 4: ALife — мульти-игрок                  │
│  CALifeSwitchManager::update_switch()                           │
│  → Онлайн/офлайн зоны = объединение сфер всех игроков          │
└─────────────────────────────────────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 5: Актор                                 │
│  g_all_actors: xr_vector<CActor*> — реестр всех акторов         │
│  g_actor — остаётся как "мой локальный актор"                   │
│  CActor::net_Spawn() → добавляет в g_all_actors                 │
│  CActor::net_Destroy() → удаляет из g_all_actors                │
└─────────────────────────────────────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    СЛОЙ 6: Lua-скриптинг                        │
│  Новый колбэк: on_coop_player_connected(actor_id)              │
│  Новый колбэк: on_coop_player_disconnected(actor_id)           │
│  Новый колбэк: on_coop_level_change_requested(new_level)       │
│  Новые функции: get_coop_players(), coop_send_event(...)        │
└─────────────────────────────────────────────────────────────────┘
```

---

### 13.3 Классы и их иерархия

#### 13.3.1 `game_sv_Coop` — серверная логика кооп

```cpp
// src/xrGame/game_sv_coop.h
class game_sv_Coop : public game_sv_mp
{
    typedef game_sv_mp inherited;
private:
    CALifeSimulator* m_alife_simulator;   // Управляет миром (только хост)
    xr_vector<u16>  m_pending_level_change_acks; // Готовность игроков к смене уровня

public:
    virtual LPCSTR type_name() const { return "coop"; }
    virtual void   Create(shared_str& options);
    virtual ~game_sv_Coop();

    // ALife-делегирование (как в game_sv_Single)
    virtual void OnCreate(u16 id_who);
    virtual BOOL OnTouch(u16 eid_who, u16 eid_what, BOOL bForced = FALSE);
    virtual void OnDetach(u16 eid_who, u16 eid_what);

    // Время игры — из ALife-симулятора
    virtual ALife::_TIME_ID GetStartGameTime();
    virtual ALife::_TIME_ID GetGameTime();
    virtual float GetGameTimeFactor();
    virtual void  SetGameTimeFactor(const float fTimeFactor);

    // Смена уровня — синхронизация всех игроков
    virtual bool change_level(NET_Packet& net_packet, ClientID sender);
    // Сохранение/загрузка — через ALife (только хост сохраняет)
    virtual void save_game(NET_Packet& net_packet, ClientID sender);
    virtual bool load_game(NET_Packet& net_packet, ClientID sender);
    // Дистанция переключения ALife — по всем игрокам
    virtual void switch_distance(NET_Packet& net_packet, ClientID sender);

    // PvP выключен
    virtual BOOL CanHaveFriendlyFire() { return FALSE; }

    // Доступ к ALife для синхронизации
    IC CALifeSimulator& alife() { VERIFY(m_alife_simulator); return *m_alife_simulator; }

    // Новые кооп-методы:
    void broadcast_coop_event(u16 event_type, NET_Packet& data);
    void on_player_ready_for_level_change(ClientID sender);

protected:
    virtual void Update();
};
```

#### 13.3.2 `game_cl_Coop` — клиентская логика кооп

```cpp
// src/xrGame/game_cl_coop.h
class game_cl_Coop : public game_cl_mp
{
    typedef game_cl_mp inherited;
public:
    virtual LPCSTR type_name() const { return "coop"; }

    // HUD — как в SP (не MP с таблицей смертей)
    virtual CUIGameCustom* createGameUI();
    virtual char*          getTeamSection(int Team) { return ""; }

    // Время — принимаем от хоста через net_import_state
    virtual ALife::_TIME_ID GetStartGameTime();
    virtual ALife::_TIME_ID GetGameTime();
    virtual float           GetGameTimeFactor();
    virtual void            SetGameTimeFactor(const float fTimeFactor);

    // Обработка кооп-событий от сервера
    virtual void TranslateGameMessage(u32 msg, NET_Packet& P);

    // Синхронизация квестов, смены уровня и т.д.
    void on_coop_event(u16 event_type, NET_Packet& P);
    void on_level_change_requested(NET_Packet& P);
    void on_quest_update(NET_Packet& P);
};
```

---

### 13.4 Диаграмма топологии кооп-сессии

```
                        ХОСТ (Host)
┌────────────────────────────────────────────────────────────┐
│  xrServer                                                  │
│  ├── game_sv_Coop                                          │
│  │   ├── CALifeSimulator ──→ управляет миром              │
│  │   ├── Игрок A (CSE_ALifeCreatureActor) ←── локальный  │
│  │   ├── Игрок B (CSE_ActorMP)             ←── удалённый  │
│  │   └── NPC/предметы/зоны...                              │
│  └── xrClientData[A], xrClientData[B]                      │
│                                                            │
│  CLevel (IsServer=true, IsClient=false*)                   │
│  ├── game_cl_Coop (клиентская часть хоста)                 │
│  ├── CActor [Игрок A] ← g_actor (локальный)               │
│  └── CActorMP [Игрок B] ← удалённый, управляется клиентом │
└────────────────────────────────────────────────────────────┘
              ↕ NET_Packet по UDP (DirectPlay 8)
                        КЛИЕНТ (Client)
┌────────────────────────────────────────────────────────────┐
│  CLevel (IsServer=false, IsClient=true)                    │
│  ├── game_cl_Coop                                          │
│  ├── CActor [Игрок B] ← g_actor (локальный для B)         │
│  └── CActorMP [Игрок A] ← удалённый, управляется хостом   │
└────────────────────────────────────────────────────────────┘
```
**Примечание**: `IsClient()` возвращает `false` для процесса хоста (так как у него есть `Server != nullptr`), однако хост всё равно участвует в игре как игрок — его локальный актор управляется с клавиатуры, а `g_actor` указывает именно на него. `IsServer()` и `IsClient()` описывают роль _процесса_, а не игрового персонажа.

---

### 13.5 Синхронизация ALife — поэтапный план

#### Этап 1: Минимальный рабочий вариант (MVP)
- ALife работает только на хосте
- Объекты переходят онлайн/офлайн на основе **минимального расстояния до любого игрока**
- Клиенты получают объекты через стандартный `M_SPAWN` / `M_DESTROY`
- Никакой явной синхронизации состояния ALife на клиентах — они видят только то, что заспавнено

#### Этап 2: Синхронизация событий
- Новые типы пакетов для кооп-событий:
  ```
  M_COOP_QUEST_UPDATE     — обновление задания у клиентов
  M_COOP_LEVEL_CHANGE     — запрос смены уровня
  M_COOP_ALIFE_EVENT      — нотификация об ALife-событии (смерть NPC, изменение отношений)
  M_COOP_SAVE_REQUEST     — запрос сохранения
  M_COOP_SAVE_ACK         — подтверждение готовности к сохранению
  ```

#### Этап 3: Полная синхронизация (будущее)
- Периодические снапшоты ключевого состояния ALife (позиции групп, статус смарт-зон)
- Синхронизация состояния Lua-переменных для квест-системы
- Распределённое управление NPC-памятью

---

### 13.6 Реестр всех акторов (`g_all_actors`)

```cpp
// src/xrGame/Actor_Network.cpp
CActor* g_actor = NULL; // остаётся — "мой" актор

// НОВОЕ:
xr_vector<CActor*> g_all_actors; // все акторы всех игроков

// В CActor::net_Spawn():
// После установки g_actor (если это я) или просто:
g_all_actors.push_back(this);

// В CActor::net_Destroy():
g_all_actors.erase(
    std::find(g_all_actors.begin(), g_all_actors.end(), this)
);

// Вспомогательные функции для ALife:
CActor* GetNearestActor(const Fvector& pos);
float   GetMinDistanceToAnyActor(const Fvector& pos);
```

Это позволяет:
1. ALife-системе корректно вычислять зоны онлайн-присутствия
2. ИИ-сталкерам знать о всех игроках
3. Lua-скриптам перечислять всех игроков

---

### 13.7 Решение проблемы смены уровня

В SP один игрок нажимает Level Changer → немедленный переход. В коопе нужна синхронизация.

**Протокол смены уровня**:
```
Игрок A входит в Level Changer
    ↓
game_sv_Coop::change_level():
    1. Рассылает M_COOP_LEVEL_CHANGE всем клиентам (имя нового уровня)
    2. Запоминает sender в m_pending_level_change_acks
    3. Ждёт ACK от всех (или таймаут 30 сек)
    
Клиент B получает M_COOP_LEVEL_CHANGE:
    1. game_cl_Coop::on_level_change_requested() показывает уведомление
    2. Телепортирует персонажа к Level Changer или к спавн-точке группы
    3. Отправляет M_COOP_LEVEL_CHANGE_ACK

Хост получил все ACK:
    alife().change_level(net_packet) → стандартный переход
```

---

### 13.8 Система сохранений в коопе

**Проблема**: `CALifeStorageManager::save()` сохраняет весь мир, но не состояния отдельных игроков в кооперативе.

**Предлагаемое решение**:

```
Сохранение (только хост):
    1. alife().save("coop_save") → стандартный ALife .sav файл
    2. Для каждого игрока C: запрос состояния через M_COOP_PLAYER_SAVE_REQUEST
    3. Каждый игрок отвечает M_COOP_PLAYER_STATE_DATA (инвентарь, здоровье, позиция)
    4. Хост сохраняет coop_save_players.dat = массив состояний игроков

Загрузка (хост + клиенты):
    1. Хост: alife().load("coop_save") → восстанавливает мир
    2. Хост рассылает M_COOP_LOAD_COMPLETE + URL/путь к coop_save_players.dat
    3. Хост восстанавливает инвентарь/состояние каждого клиентского актора через ALife
    4. Клиенты получают свои акторы со своим инвентарём через M_UPDATE
```

---

### 13.9 Транспортный слой — оценка DirectPlay 8

**Текущее состояние**: DirectPlay 8 (`src/xrNetServer/NET_Shared.h`) — устаревшее Microsoft API.

**Проблемы**:
- Официально не поддерживается с Windows Vista
- NAT traversal не гарантирован (проблемы с соединением через интернет)
- Нет встроенной поддержки P2P relay серверов

**Варианты замены (если потребуется)**:

| Библиотека | Преимущества | Недостатки |
|------------|--------------|------------|
| **ENet** | Надёжный UDP, лёгкий, проверен в играх | Нет NAT traversal |
| **Steam Networking** (Steamworks) | NAT traversal, relay, бесплатно | Привязка к Steam |
| **GameNetworkingSockets** (Valve, open source) | Без Steam, NAT traversal | Тяжёлая зависимость |
| **RakNet / SLikeNet** | Полноценный SDK, NAT traversal | Большой, сложный |

**Рекомендация**: Для MVP использовать **существующий DirectPlay 8** (LAN и прямое IP-соединение работают). Заменить на **GameNetworkingSockets** в будущем для поддержки интернет-игры.

**Ключевое место замены**: `IPureClient` и `IPureServer` в `src/xrNetServer/` — они инкапсулируют DirectPlay. Замена сводится к переписыванию этих двух классов.

---

### 13.10 Дорожная карта реализации

#### Фаза 0: Подготовка (без изменений в C++)
- [ ] Тест существующего MP (DM) для понимания стабильности сети
- [ ] Документирование всех мест использования `g_actor`
- [ ] Документирование всех мест, где `IsGameTypeSingle()` влияет на поведение

#### Фаза 1: Новый игровой тип — каркас (минимальные C++ изменения)
- [ ] `eGameIDCooperative` в `game_base_space.h`
- [ ] `CLSID_SV_GAME_COOP` и `CLSID_CL_GAME_COOP` в `clsid_game.h`
- [ ] Пустые `game_sv_Coop : game_sv_Single` и `game_cl_Coop : game_cl_Single`
- [ ] Регистрация в `object_factory_register.cpp` и `game_base.cpp`
- [ ] Тест: запуск одиночной игры как "coop" через командную строку

#### Фаза 2: Два игрока, базовая синхронизация
- [ ] `g_all_actors` — реестр всех акторов
- [ ] Изменение `game_sv_Coop::Create()` — использует `CALifeSimulator` как SP
- [ ] Тест: хост и клиент в одном уровне, два актора двигаются
- [ ] Тест: ИИ NPC видит и реагирует на обоих игроков

#### Фаза 3: ALife для нескольких игроков
- [ ] `CALifeSwitchManager::update_switch()` — объединение зон по всем игрокам
- [ ] Тест: объекты появляются/исчезают при движении любого из игроков
- [ ] Смена уровня с протоколом синхронизации
- [ ] Тест: оба игрока переходят на новый уровень вместе

#### Фаза 4: UI и HUD
- [ ] `game_cl_Coop::createGameUI()` — использует `UIGameSP` + индикаторы игроков
- [ ] Маркеры игроков на мини-карте
- [ ] Статус здоровья/радиации других игроков

#### Фаза 5: Сохранения и Lua
- [ ] Система кооп-сохранений (хост + данные игроков)
- [ ] Новые Lua-колбэки для кооп-событий
- [ ] Синхронизация квест-прогресса через `M_COOP_QUEST_UPDATE`

#### Фаза 6: Polish и стабилизация
- [ ] Обработка отключения игрока
- [ ] Тест на сложных уровнях (Припять, Чернобыльская АЭС)
- [ ] Замена DirectPlay (опционально)
- [ ] Оптимизация сетевого трафика

---

### 13.11 Таблица рисков

| Риск | Вероятность | Влияние | Митигация |
|------|------------|---------|-----------|
| DirectPlay NAT проблемы | Высокая | Средняя | Начать с LAN/прямого IP; план замены на ENet |
| ALife краши при нескольких игроках | Высокая | Критическая | Тщательное тестирование; fallback на "хост решает всё" |
| Квест-скрипты не синхронизируются | Средняя | Высокая | Lua-шина событий; квесты только у хоста с нотификациями |
| ИИ атакует только одного игрока | Средняя | Средняя | Расширить список целей в stalker_sight/memory |
| Рассинхронизация физики | Низкая | Средняя | Авторитетная физика на хосте для мира |
| Производительность (2x NPC online) | Высокая | Высокая | Уменьшить switch_distance в коопе; MT-версия двигателя |
| Несовместимость SP сохранений | Средняя | Средняя | Отдельный формат кооп-сохранений |

---

*Документ обновлён: глубокий анализ компонентов и архитектурное предложение по кооперативному режиму (март 2026)*

---

## 14. Детальный анализ для Фазы 1: консольные команды `coop_host` и `coop_connect`

### 14.1 Как работает существующая система консольных команд (точные ссылки)

#### 14.1.1 Объявление команды — макросы

**Файл**: `src/xrEngine/xr_ioc_cmd.h`, строки 3–7
```cpp
#define CMD1(cls, p1)  { static cls x##cls(p1); Console->AddCommand(&x##cls); }
#define CMD2(cls, p1, p2) { ... }
// и т.д. до CMD4
```

Каждая команда — это C++-класс, унаследованный от `IConsole_Command` с методом `Execute(LPCSTR args)`.

#### 14.1.2 Регистрация команд в xrGame

**Файл**: `src/xrGame/console_commands.cpp`, функция `CCC_RegisterCommands()` (строка 2378)  
**Вызов**: `src/xrGame/xrGame.cpp`, строка 56 — вызывается при инициализации `xrGame.dll`.

Именно сюда нужно добавить регистрацию `CMD1(CCC_CoopHost, "coop_host")` и `CMD1(CCC_CoopConnect, "coop_connect")`.

#### 14.1.3 Существующая команда `start` как образец

**Файл**: `src/xrEngine/xr_ioc_cmd.cpp`, класс `CCC_Start` (строка 330)

Команда `start` принимает аргументы вида:
```
start server(<map>/<type>[/параметры...]) client(<host>[/name=...][/port=...])
```

Она вызывает:
```cpp
Engine.Event.Defer("KERNEL:start",
    u64(xr_strlen(op_server) ? xr_strdup(op_server) : 0),
    u64(xr_strdup(op_client)));
```

**Файл**: `src/xrEngine/x_ray.cpp`, обработчик `eStart` (строка ~1400)
```cpp
g_pGamePersistent->PreStart(op_server);                // парсит параметры
g_pGameLevel = NEW_INSTANCE(CLSID_GAME_LEVEL);         // создаёт уровень
g_pGamePersistent->Start(op_server);                   // применяет параметры
g_pGameLevel->net_Start(op_server, op_client);         // запускает сервер+клиент
```

---

### 14.2 Полная трассировка команды `start server(...) client(...)` по коду

```
Пользователь вводит в консоль:
  start server(l01_escape/single/alife/new) client(localhost/name=TestPlayer)
      ↓
CCC_Start::Execute(args)               [xr_ioc_cmd.cpp:373]
  parse(op_server, args, "server")     → "l01_escape/single/alife/new"
  parse(op_client, args, "client")     → "localhost/name=TestPlayer"
  Engine.Event.Defer("KERNEL:start", op_server_copy, op_client_copy)
      ↓
CApplication::OnEvent(E=eStart, P1=op_server, P2=op_client) [x_ray.cpp:~1395]
  g_pGamePersistent->PreStart(op_server)
      ↓
  IGame_Persistent::PreStart(op)       [IGame_Persistent.cpp:121]
    m_game_params.parse_cmd_line(op)   → m_game_type = "single",
                                          m_alife = "alife",
                                          m_new_or_load = "new"
      ↓
  g_pGameLevel->net_Start(op_server, op_client) [Level_start.cpp]
      ↓
  CLevel::net_start1()                 [Level_start.cpp:~103]
    if (!xr_strcmp(p.m_game_type, "single"))
        Server = xr_new<xrServer>()    ← loopback сервер
    else
        Server = xr_new<xrGameSpyServer>()
      ↓
  CLevel::net_start2()                 [Level_start.cpp]
    Server->Connect(m_caServerOptions, game_descr)
      ↓
  xrServer::Connect(session_name, ...)  [xrServer_Connect.cpp:38]
    CLASS_ID clsid = game_GameState::getCLASS_ID("single", true)
    game = NEW_INSTANCE(CLSID_SV_GAME_SINGLE)     ← game_sv_Single
    game->Create(options)
      ↓
  game_sv_Single::Create(options)      [game_sv_single.cpp]
    if (strstr(*options, "/alife"))
        m_alife_simulator = new CALifeSimulator(...)
      ↓
  CLevel::net_start_client*()          [Level_network_start_client.cpp]
    IPureClient::Connect(op_client, ...)
      ↓
  NET_Client::Connect(options, ...)    [NET_Client.cpp:~380]
    server_name = "localhost"           ← извлекается из op_client
    port = 1235                         ← START_PORT_LAN_SV
    DirectPlay8 connect to server_name:port
```

---

### 14.3 Формат строк `op_server` и `op_client`

#### `op_server` — строка параметров сервера

**Формат** (парсится через `IGame_Persistent::params::parse_cmd_line()`):
```
<map_name>/<game_type>/<alife_flag>/<new_or_load>[/ver=<version>][/estime=<time>]
```

Элементы разделены `/`, порядок важен:
- `m_params[0]` = имя уровня/сохранения (`m_game_or_spawn`)
- `m_params[1]` = тип игры (`m_game_type`) — `"single"`, `"deathmatch"`, ...
- `m_params[2]` = ALife-флаг (`m_alife`) — `"alife"` или пусто
- `m_params[3]` = режим загрузки (`m_new_or_load`) — `"new"` или `"load"`

**Примеры**:
```
l01_escape/single/alife/new      ← новая игра на уровне l01_escape
my_save/single/alife/load        ← загрузка сохранения my_save
zaton/deathmatch                 ← DM на zaton
```

#### `op_client` — строка параметров клиента

**Формат** (парсится в `NET_Client::Connect()`, `Level_start.cpp`):
```
<server_host>[/name=<player_name>][/port=<port>][/portcl=<client_port>][/psw=<password>]
```

**Примеры**:
```
localhost/name=TestPlayer        ← локальное подключение
192.168.1.100/name=Player1/port=1235
```

---

### 14.4 Подробный анализ того, что нужно изменить для `coop_host`

#### 14.4.1 Что делает `coop_host`

Команда `coop_host <save_name>` должна:
1. Запустить сервер с типом игры `"coop"` (не `"single"`, не `"deathmatch"`)
2. Загрузить указанное ALife-сохранение (или начать новую игру)
3. Подключить локальный клиент как хоста (`localhost`)
4. **Не использовать** `psNET_direct_connect = TRUE` (он отключает реальный сетевой стек)

Итоговый вызов должен выглядеть:
```cpp
Engine.Event.Defer("KERNEL:start",
    u64(xr_strdup("my_save/coop/alife/load")),    // op_server
    u64(xr_strdup("localhost/name=PlayerName")));  // op_client
```

Но перед этим нужно выставить `psNET_direct_connect = FALSE`, чтобы DirectPlay создал реальный сервер (не loopback).

#### 14.4.2 Файлы, которые нужно изменить

**1. `src/xrServerEntities/gametype_chooser.h` — добавить `eGameIDCooperative`**

```cpp
enum EGameIDs {
    eGameIDNoGame             = u32(0),
    eGameIDSingle             = u32(1) << 0,
    eGameIDDeathmatch         = u32(1) << 1,
    eGameIDTeamDeathmatch     = u32(1) << 2,
    eGameIDArtefactHunt       = u32(1) << 3,
    eGameIDCaptureTheArtefact = u32(1) << 4,
    eGameIDDominationZone     = u32(1) << 5,
    eGameIDTeamDominationZone = u32(1) << 6,
    eGameIDCooperative        = u32(1) << 7,  // ← НОВОЕ (следующий свободный бит после TeamDominationZone)
};
```

**2. `src/xrGame/GamePersistent.cpp` — добавить "coop" в `ParseStringToGameType()`** (строка 238)

```cpp
EGameIDs ParseStringToGameType(LPCSTR str) {
    if (!xr_strcmp(str, "single"))        return eGameIDSingle;
    if (!xr_strcmp(str, "coop") ||
        !xr_strcmp(str, "cooperative"))   return eGameIDCooperative;  // ← НОВОЕ
    // ...
}
```

**3. `src/xrGame/game_base.cpp` — добавить ветку в `getCLASS_ID()`** (строка ~248)

```cpp
case eGameIDCooperative:
    return (isServer) ? TEXT2CLSID("SV_COOP") : TEXT2CLSID("CL_COOP");
```

**4. `src/xrServerEntities/clsid_game.h` — добавить CLSID константы**

```cpp
#define CLSID_SV_GAME_COOP  MK_CLSID('S','V','_','C','O','O','P',' ')
#define CLSID_CL_GAME_COOP  MK_CLSID('C','L','_','C','O','O','P',' ')
```

**5. `src/xrGame/game_sv_coop.h` и `game_sv_coop.cpp` — НОВЫЙ ФАЙЛ** (заглушка для Фазы 1)

```cpp
// game_sv_coop.h — начальная заглушка
class game_sv_Coop : public game_sv_Single {
    typedef game_sv_Single inherited;
public:
    virtual LPCSTR type_name() const { return "coop"; }
    virtual void   Create(shared_str& options);
    // Фаза 1: только хост может подключиться
    // Позже: разрешить внешние клиенты
};
```

**6. `src/xrGame/game_cl_coop.h` и `game_cl_coop.cpp` — НОВЫЙ ФАЙЛ** (заглушка для Фазы 1)

```cpp
// game_cl_coop.h — начальная заглушка
class game_cl_Coop : public game_cl_Single {
    typedef game_cl_Single inherited;
public:
    virtual LPCSTR type_name() const { return "coop"; }
    // Фаза 1: использует SP UI без изменений
};
```

**7. `src/xrServerEntities/object_factory_register.cpp` — зарегистрировать классы** (строки ~208–221)

```cpp
add<game_sv_Coop>(CLSID_SV_GAME_COOP, "game_sv_coop");
add<game_cl_Coop>(CLSID_CL_GAME_COOP, "game_cl_coop");
```

**8. `src/xrGame/Level_start.cpp` — разрешить coop использовать `xrServer`** (строки ~115–120)

```cpp
if (!xr_strcmp(p.m_game_type, "single") ||
    !xr_strcmp(p.m_game_type, "coop"))    // ← НОВОЕ: coop использует базовый xrServer
    Server = xr_new<xrServer>();
else {
    g_allow_heap_min = false;
    Server = xr_new<xrGameSpyServer>();
}
```

**9. `src/xrGame/console_commands.cpp` — добавить `CCC_CoopHost` и `CCC_CoopConnect`**

---

#### 14.4.3 Ключевая проблема: `psNET_direct_connect`

**Файл**: `src/xrNetServer/NET_Shared.h`  
**Переменная**: `extern BOOL psNET_direct_connect;`

В одиночной игре:
```cpp
// NET_Client.cpp:~298
if (!psNET_direct_connect) {
    // Настраиваем реальный DirectPlay TCP/IP
} else {
    // Просто помечаем, что подключены
}
```

В SP `psNET_direct_connect = TRUE` — пропускается весь сетевой стек. Для кооперации его нужно держать `FALSE`.

**Проблема**: При `psNET_direct_connect = FALSE` и `server_name = "localhost"` DirectPlay создаёт реальный UDP-сокет и слушает на `START_PORT_LAN_SV = 1235`. Это работает для LAN, но через интернет нужны дополнительные меры (NAT traversal).

**Для Фазы 1 (тест)**: `psNET_direct_connect = FALSE` + `localhost` — работает без изменений.

---

### 14.5 Подробный анализ того, что нужно изменить для `coop_connect`

#### 14.5.1 Что делает `coop_connect`

Команда `coop_connect [<ip>]` должна:
1. Подключиться к существующему coop-серверу по IP
2. Запустить клиентскую часть без запуска сервера (нет `op_server`)
3. По умолчанию IP = `localhost` (для тестирования на одной машине)

Итоговый вызов:
```cpp
Engine.Event.Defer("KERNEL:start",
    u64(0),                                          // op_server = NULL → не запускать сервер
    u64(xr_strdup("192.168.1.100/name=Player2"))); // op_client
```

Но текущий обработчик в `x_ray.cpp` требует `op_server` для части логики. Нужно проверить это:

**Файл**: `src/xrEngine/x_ray.cpp`, строка ~1408:
```cpp
g_pGamePersistent->PreStart(op_server);  // если op_server == NULL, это проблема
```

**Файл**: `src/xrEngine/IGame_Persistent.cpp`:
```cpp
void IGame_Persistent::PreStart(LPCSTR op) {
    string256 prev_type;
    params new_game_params;
    xr_strcpy(prev_type, m_game_params.m_game_type);
    new_game_params.parse_cmd_line(op);  // если op == NULL → краш
```

**Вывод**: При подключении только как клиент (`op_server = NULL`) нужно либо:
- Передать пустую строку `""` вместо NULL и защитить `parse_cmd_line` от пустой строки
- Или передать минимальную строку типа `"/coop/alife"` без имени карты

**Существующее решение в MP**: В multiplayer-режиме клиент подключается к серверу, получает имя карты и загружает её сам. Переменная `m_caServerOptions` остаётся пустой, `m_caClientOptions` содержит IP. Этот путь корректно работает:

```cpp
// Level_start.cpp::net_start1()
if (m_caServerOptions.size()) {
    // ... запуск сервера
} else {
    g_allow_heap_min = false; // ← только это, если нет op_server
}
```

**Поэтому**: Команда `coop_connect` должна передавать только `op_client`, `op_server` = пустая строка.

#### 14.5.2 Файлы, которые нужно изменить для `coop_connect`

**1. `src/xrGame/console_commands.cpp` — добавить `CCC_CoopConnect`**

```cpp
// Ничего нового в движке не нужно! connect работает через стандартный механизм.
```

**2. Получение имени карты при подключении клиента**

Когда клиент подключается к MP-серверу, он не знает имени уровня заранее. Оно получается из `GameDescriptionData`, которая передаётся сервером при согласовании. Это уже работает в `Level_network_start_client.cpp`:

```cpp
// Level_network_start_client.cpp::net_start_client3()
if (psNET_direct_connect) {
    level_name = name().c_str();      // SP: уже знаем уровень
} else {
    level_name = get_net_DescriptionData().map_name;  // MP: получаем от сервера
    rescan_mp_archives();
}
```

**Проблема Фазы 1**: В текущем коде в MP-режиме ищутся MP-архивы (`$game_arch_mp$`). Для кооператива нужно убрать это ограничение или разрешить SP-карты в MP-режиме.

---

### 14.6 Полные спецификации реализации команд

#### 14.6.1 Команда `coop_host`

```
Синтаксис:
    coop_host [<save_name>]
    
Аргументы:
    save_name — имя .scop файла без расширения (опционально)
                если не указано — начинает новую игру с дефолтного уровня
                
Примеры:
    coop_host                    ← новая игра
    coop_host my_coop_save       ← загрузить сохранение
    
Что делает:
    1. Читает player_name из реестра или fallback на Core.UserName
    2. Определяет save_name и режим (new/load)
    3. Получает текущий/начальный уровень из ALife конфигурации
    4. Создаёт op_server и op_client строки
    5. Деферит KERNEL:start
    
Генерирует:
    op_server = "<save_or_level>/coop/alife/<new_or_load>"
    op_client = "localhost/name=<player_name>"
```

**Проблема**: Для "новой игры" в SP, `m_game_or_spawn` — это имя spawn-файла (обычно `"all"` или конкретная локация, конфигурируется в `alife.ltx`). Нужно прочитать это значение из конфига или использовать хардкодированный дефолт.

**Файл**: `src/xrGame/alife_update_manager.cpp`
```cpp
void CALifeUpdateManager::new_game(LPCSTR save_name) {
    // ...
    if (pSettings->line_exist("alife", "new_game_spawn"))
        xr_strcpy(save_name, pSettings->r_string("alife", "new_game_spawn"));
    else
        xr_strcpy(save_name, "all"); // дефолт
}
```

**Вывод**: Для `coop_host` без аргументов нужно использовать `"all"` (или читать из `alife.ltx`).

#### 14.6.2 Команда `coop_connect`

```
Синтаксис:
    coop_connect [<ip>]
    
Аргументы:
    ip — IP-адрес или hostname сервера (опционально, дефолт: localhost)
    
Примеры:
    coop_connect                     ← подключиться к localhost:1235
    coop_connect 192.168.1.100       ← подключиться к локальному хосту
    coop_connect my.server.com       ← подключиться к удалённому хосту
    
Что делает:
    1. Разбирает IP из аргументов (дефолт: "localhost")
    2. Читает player_name из реестра
    3. Деферит KERNEL:start с пустым op_server и нужным op_client
    
Генерирует:
    op_server = ""     ← не запускает сервер
    op_client = "<ip>/name=<player_name>/port=1235"
```

---

### 14.7 Код реализации команд (финальный вид)

Следующий код добавляется в `src/xrGame/console_commands.cpp`:

```cpp
// ============================================================
// coop_host — запуск кооп-сервера (добавить после строки ~933)
// ============================================================

#include "ui/UICDkey.h"       // GetPlayerName_FromRegistry

class CCC_CoopHost : public IConsole_Command
{
public:
    CCC_CoopHost(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        // Определяем режим: загрузить или новая игра
        bool has_save = args && args[0] != '\0';
        
        string_path save_name;
        if (has_save) {
            xr_strcpy(save_name, args);
            // Убираем пробелы в конце — O(n) за один проход
            int len = (int)xr_strlen(save_name);
            while (len > 0 && save_name[len - 1] == ' ')
                save_name[--len] = '\0';
        }

        // Валидация сохранения
        if (has_save) {
            if (!CSavedGameWrapper::saved_game_exist(save_name)) {
                Msg("! coop_host: save file '%s' not found", save_name);
                return;
            }
            if (!CSavedGameWrapper::valid_saved_game(save_name)) {
                Msg("! coop_host: save file '%s' is corrupted or version mismatch", save_name);
                return;
            }
        }

        // Имя игрока
        string64 player_name;
        GetPlayerName_FromRegistry(player_name, sizeof(player_name));
        if (!xr_strlen(player_name)) {
            xr_strcpy(player_name, xr_strlen(Core.UserName) ? Core.UserName : "CoopHost");
        }

        // Строка сервера: <уровень_или_сохранение>/coop/alife/<new|load>
        string512 op_server;
        if (has_save) {
            xr_sprintf(op_server, "%s/coop/alife/load", save_name);
        } else {
            // Читаем имя начального спавна из конфига
            LPCSTR spawn = pSettings->line_exist("alife", "new_game_spawn")
                ? pSettings->r_string("alife", "new_game_spawn")
                : "all";
            xr_sprintf(op_server, "%s/coop/alife/new", spawn);
        }

        // Строка клиента: всегда localhost для хоста
        string256 op_client;
        xr_sprintf(op_client, "localhost/name=%s", player_name);

        Msg("* coop_host: server='%s' client='%s'", op_server, op_client);

        if (g_pGameLevel)
            Engine.Event.Defer("KERNEL:disconnect");

        Engine.Event.Defer("KERNEL:start",
            u64(xr_strdup(op_server)),
            u64(xr_strdup(op_client)));
    }

    virtual void Info(TInfo& I)
    {
        xr_strcpy(I, "[save_name] — start coop server (new game or load save)");
    }

    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        get_files_list(tips, "$game_saves$", SAVE_EXTENSION);
    }
};

// ============================================================
// coop_connect — подключение к кооп-серверу
// ============================================================

class CCC_CoopConnect : public IConsole_Command
{
public:
    CCC_CoopConnect(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        // Адрес сервера (дефолт: localhost)
        string256 server_ip;
        if (args && args[0] != '\0') {
            xr_strcpy(server_ip, args);
            // Убираем пробелы в конце — O(n) за один проход
            int len = (int)xr_strlen(server_ip);
            while (len > 0 && server_ip[len - 1] == ' ')
                server_ip[--len] = '\0';
        } else {
            xr_strcpy(server_ip, "localhost");
        }

        // Имя игрока
        string64 player_name;
        GetPlayerName_FromRegistry(player_name, sizeof(player_name));
        if (!xr_strlen(player_name)) {
            xr_strcpy(player_name, xr_strlen(Core.UserName) ? Core.UserName : "CoopPlayer");
        }

        // Строка клиента
        string512 op_client;
        xr_sprintf(op_client, "%s/name=%s/port=%d",
            server_ip,
            player_name,
            START_PORT_LAN_SV);  // 1235

        Msg("* coop_connect: connecting to '%s' as '%s'", server_ip, player_name);

        if (g_pGameLevel)
            Engine.Event.Defer("KERNEL:disconnect");

        // op_server = xr_strdup("") — пустая строка вместо NULL (PreStart безопасно обработает)
        // Передача nullptr вызывает краш в IGame_Persistent::PreStart()
        Engine.Event.Defer("KERNEL:start",
            u64(xr_strdup("")),        // пустой op_server = нет сервера, только клиент
            u64(xr_strdup(op_client)));
    }

    virtual void Info(TInfo& I)
    {
        xr_strcpy(I, "[ip_address] — connect to coop server (default: localhost)");
    }
};
```

И в `CCC_RegisterCommands()` добавить строки:
```cpp
CMD1(CCC_CoopHost,    "coop_host");
CMD1(CCC_CoopConnect, "coop_connect");
```

---

### 14.8 Карта зависимостей: какие файлы нужно изменить

| № | Файл | Тип изменения | Что делать |
|---|------|---------------|------------|
| 1 | `src/xrServerEntities/gametype_chooser.h` | Добавить константу | `eGameIDCooperative = u32(1) << 7` |
| 2 | `src/xrGame/GamePersistent.cpp` | Добавить case | "coop" → `eGameIDCooperative` в `ParseStringToGameType()` |
| 3 | `src/xrGame/GamePersistent.cpp` | Добавить case | Строковое представление в `GameTypeToString()` |
| 4 | `src/xrGame/game_base.cpp` | Добавить case | `eGameIDCooperative` → `TEXT2CLSID("SV_COOP")` / `TEXT2CLSID("CL_COOP")` |
| 5 | `src/xrServerEntities/clsid_game.h` | Добавить defines | `CLSID_SV_GAME_COOP` и `CLSID_CL_GAME_COOP` |
| 6 | `src/xrGame/game_sv_coop.h` | Новый файл | Заголовок `game_sv_Coop : game_sv_Single` |
| 7 | `src/xrGame/game_sv_coop.cpp` | Новый файл | Реализация (Фаза 1 = thin wrapper) |
| 8 | `src/xrGame/game_cl_coop.h` | Новый файл | Заголовок `game_cl_Coop : game_cl_Single` |
| 9 | `src/xrGame/game_cl_coop.cpp` | Новый файл | Реализация (Фаза 1 = thin wrapper) |
| 10 | `src/xrServerEntities/object_factory_register.cpp` | Добавить регистрацию | `add<game_sv_Coop>(CLSID_SV_GAME_COOP, ...)` |
| 11 | `src/xrGame/Level_start.cpp` | Изменить условие | Разрешить "coop" использовать `xrServer` |
| 12 | `src/xrGame/console_commands.cpp` | Добавить 2 класса + 2 CMD1 | `CCC_CoopHost` и `CCC_CoopConnect` |

**Итого: 6 модифицированных файлов + 4 новых файла** — минимально необходимо для Фазы 1.

---

### 14.9 Потенциальные проблемы и способы их решения

#### 14.9.1 Проблема: `PreStart(NULL)` при `coop_connect`

**Симптом**: При `op_server = NULL` в `CApplication::OnEvent` → краш в `IGame_Persistent::PreStart(NULL)`

**Решение**: `CCC_CoopConnect` должен передавать `op_server = xr_strdup("")` вместо `0`:
```cpp
Engine.Event.Defer("KERNEL:start",
    u64(xr_strdup("")),    // пустая строка, не NULL
    u64(xr_strdup(op_client)));
```

В `IGame_Persistent::PreStart` пустая строка безопасна — `parse_cmd_line("")` просто ничего не заполняет.

#### 14.9.2 Проблема: карта не загружается при `coop_connect`

**Симптом**: Клиент не знает имя карты до подключения к серверу.

**Решение**: Это уже работает в MP-режиме! При `op_server = ""` срабатывает ветка без создания сервера, а имя карты получается через `get_net_DescriptionData().map_name` после согласования с сервером. Нужно только убедиться, что `rescan_mp_archives()` не ломает SP-архивы.

#### 14.9.3 Проблема: `IsGameTypeSingle()` в ~200 местах кода

**Файл**: `src/xrGame/Level.h`, строка 445:
```cpp
IC bool IsGameTypeSingle() { return (g_pGamePersistent->GameType() == eGameIDSingle); }
```

В режиме `eGameIDCooperative` это вернёт `FALSE`, что изменит поведение во многих местах. Часть из этих изменений желательна (AI будет работать как в MP), часть — нет (HUD переключится на MP-версию).

**Решение для Фазы 1**: Добавить вспомогательную функцию:
```cpp
IC bool IsGameTypeSingleOrCoop() {
    u32 t = g_pGamePersistent->GameType();
    return t == eGameIDSingle || t == eGameIDCooperative;
}
```
И постепенно заменять критичные `IsGameTypeSingle()` на `IsGameTypeSingleOrCoop()` там, где кооп должен вести себя как SP.

#### 14.9.4 Проблема: `NO_SINGLE` и `#ifdef` блоки

В движке есть `#ifndef NO_SINGLE` блоки, которые исключают SP-код в чистом MP-билде. Для кооператива всегда нужны эти блоки, поэтому `game_sv_Coop` должен компилироваться совместно с ALife-кодом (не в `BENCHMARK_BUILD`).

---

### 14.10 Зависимости от внешних систем

#### `psNET_direct_connect` — критически важный флаг

**Файл**: `src/xrNetServer/NET_Shared.h`  
**Значение в SP**: `TRUE` (loopback без реального сетевого стека)  
**Нужное значение для coop**: `FALSE`

Когда `psNET_direct_connect = FALSE` и `server_name = "localhost"`:
- DirectPlay создаёт UDP-сокет на порту 1235
- Клиент подключается к `localhost:1235`
- На одной машине работает без изменений

#### Порты DirectPlay 8

**Файл**: `src/xrNetServer/NET_Common.h`
```cpp
#define START_PORT_LAN_CL 1234   // клиентский порт
#define START_PORT_LAN_SV 1235   // серверный порт  ← нужно передавать клиенту
#define END_PORT_LAN      1236
```

В команде `coop_host` сервер будет слушать на 1235. Команда `coop_connect localhost` подключится к `localhost:1235`.

Для интернет-игры через NAT нужен либо port forwarding (1235 UDP), либо замена транспорта (ENet/GNS).

---

*Документ обновлён: детальный анализ для Фазы 1 — консольные команды coop_host и coop_connect (март 2026)*

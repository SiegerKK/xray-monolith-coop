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

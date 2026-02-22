# Архитектура проекта xray-monolith-coop

> Дата индексации: 2026-02-22  
> Цель: анализ кодовой базы для создания инструментария интеграции кооперативного режима в Anomaly 1.5.3

---

## 1. Общее описание

**xray-monolith-coop** — форк движка X-Ray (XE 1.6 / Call of Chernobyl base), адаптированный как «modded executables» для STALKER: Anomaly 1.5.x. Проект предоставляет модифицированный бинарник игры и набор скриптов/конфигов, расширяющих базовую функциональность Anomaly.

Технологический стек:
- **Язык C++17** (MSVC / Visual Studio 2022)
- **Lua / LuaJIT 2** — скриптовый язык игровой логики
- **DirectPlay 8** — транспортный уровень сети
- **DirectX 9/10/11** — рендеринг (R1/R2/R3/R4 рендер-бэкенды)
- **OpenAL (soft)** — звук
- **ODE** — физика тел
- **ImGui** — внутриигровой debug UI

---

## 2. Структура репозитория

```
xray-monolith-coop/
├── src/                        # Исходный код движка (C++)
│   ├── 3rd party/              # Сторонние библиотеки
│   ├── Include/                # Публичные заголовки (xrAPI, xrRender)
│   ├── Layers/                 # Рендер-слои (R1–R4, DX9/DX10)
│   ├── ReShadeCompat/          # Совместимость с ReShade
│   ├── xrCDB/                  # Система обнаружения столкновений
│   ├── xrCPU_Pipe/             # CPU-пайплайн (TBB)
│   ├── xrCore/                 # Ядро движка (типы, память, ФС, сеть)
│   ├── xrEngine/               # Движок (устройство, рендер, уровень)
│   ├── xrGame/                 # Игровая логика (основной DLL)
│   │   ├── ai/                 # AI компоненты
│   │   ├── gamespy/            # GameSpy CD-key проверка
│   │   ├── ik/                 # Инверсная кинематика
│   │   ├── ui/                 # Интерфейс (312 файлов)
│   │   └── vs2022/             # VS2022 проектные файлы
│   ├── xrNetServer/            # Сетевой транспорт (сервер/клиент)
│   ├── xrParticles/            # Система частиц
│   ├── xrPhysics/              # Физический движок (ODE)
│   ├── xrServerEntities/       # Серверные сущности ALife
│   ├── xrSound/                # Звуковая подсистема
│   └── xrXMLParser/            # XML парсер
├── gamedata/                   # Игровые данные (скрипты, конфиги, шейдеры)
│   ├── configs/                # LTX конфигурации
│   ├── levels/                 # Данные уровней (level.snd_env)
│   ├── materials/              # Материалы
│   ├── scripts/                # Lua-скрипты игровой логики
│   ├── shaders/                # HLSL шейдеры (r2, r3)
│   └── textures/               # Текстуры UI
├── sdk/                        # SDK библиотеки и бинарники
│   ├── binaries/               # DLL зависимости
│   └── include/                # SDK заголовки (DPlay, DXSDK, jpeg)
├── compressor/                 # Утилита сжатия gamedata
├── .github/workflows/          # CI (msbuild.yml)
├── README.md
├── DXML.md
└── License.txt
```

---

## 3. Модули движка (src/)

### 3.1 xrCore (~168 файлов)
Фундамент всего движка:
- Базовые типы (`xr_types.h`, `_types.h`)
- Менеджер памяти (`memory_manager.cpp`)
- Файловая система (`FileSystem.cpp`, `FS.cpp`)
- Строки (`string_utils.cpp`, `shared_str.h`)
- Сетевые утилиты (`net_utils.h` — `NET_Packet`, `ClientID`)
- Криптография, логирование, синхронизация
- Математика (`_matrix.h`, `_vector3d.h`, `_quaternion.h`)

### 3.2 xrEngine (~191 файлов)
Движок игрового мира:
- `IGame_Level` — абстракция игрового уровня
- `Device` — главный объект устройства (рендер, таймер, события)
- `Engine` — главный цикл приложения
- `CameraManager`, `CameraBase` — система камер
- `Environment` — погода, небо, освещение
- `IGame_Persistent` — постоянное состояние игры
- Demo-система (`FDemoPlay`, `FDemoRecord`)

### 3.3 xrGame (~1883 файлов) — **основной игровой DLL**
Самый большой модуль, содержит всю игровую логику:

**Ключевые подсистемы:**

| Группа | Файлы | Описание |
|--------|-------|---------|
| `Level_*` | `Level.cpp`, `Level_network*.cpp` | Игровой уровень + сетевой обмен |
| `xrServer*` | `xrServer.cpp`, `xrServer_*.cpp` | Серверная часть |
| `game_sv_*` | `game_sv_single.cpp`, `game_sv_mp.cpp` и др. | Серверная игровая логика |
| `game_cl_*` | `game_cl_base.cpp`, `game_cl_mp.cpp` и др. | Клиентская игровая логика |
| `Actor*` | `Actor.cpp`, `Actor_Network.cpp` и др. | Актор (игровой персонаж) |
| `ai/` | ~6 файлов | AI-подсистема |
| `ui/` | ~312 файлов | Игровой интерфейс |
| `ALife*` | Множество файлов | Система ALife (симуляция мира) |

**Режимы игры (game_sv):**
- `game_sv_single` — одиночная игра (SP + ALife)
- `game_sv_deathmatch` — Deathmatch (MP)
- `game_sv_teamdeathmatch` — Team Deathmatch (MP)
- `game_sv_artefacthunt` — Artefact Hunt (MP)
- `game_sv_capture_the_artefact` — Capture the Artefact (MP)

### 3.4 xrNetServer (~20 файлов)
Транспортный сетевой уровень:
- `NET_Server.h/cpp` — `IPureServer` (DirectPlay8 сервер)
- `NET_Client.h/cpp` — `IPureClient` (DirectPlay8 клиент)
- `NET_Common.h/cpp` — `MultipacketSender`, `MultipacketReciever`
- `NET_Compressor.h/cpp` — LZO сжатие пакетов
- `NET_Messages.h` — константы сетевых флагов (DPNSEND_*)
- `NET_AuthCheck.h/cpp` — проверка авторизации
- `ip_filter.h/cpp` — IP-фильтрация

### 3.5 xrServerEntities (~165 файлов)
Описание серверных сущностей (CSE_Abstract иерархия):
- `xrServer_Object_Base.h` — `CSE_Abstract` (базовый класс)
- `xrServer_Objects_ALife.h` — ALife-объекты
- `xrServer_Objects_ALife_Items.h` — предметы (оружие, броня и т.д.)
- `xrServer_Objects_ALife_Monsters.h` — монстры и NPC
- `xrMessages.h` — все константы сетевых сообщений
- `object_factory.cpp` — фабрика объектов
- `script_storage.cpp` — LuaJIT интеграция

### 3.6 xrPhysics (~130 файлов)
Физическая симуляция (ODE-based):
- Ragdoll, транспорт, разрушаемые объекты
- `IPHWorld` — интерфейс физического мира
- `PHNetState` — сетевое состояние физики

### 3.7 xrSound (~36 файлов)
Звуковая подсистема на базе OpenAL Soft:
- Окружающие звуки, 3D-позиционирование
- Эффекты (reverb, EAX)

### 3.8 Рендер-слои (Layers/)
- `xrRenderPC_R1` — DX8/SW рендер
- `xrRenderPC_R2` — DX9 рендер
- `xrRenderPC_R3` — DX10 рендер
- `xrRenderPC_R4` — DX11 рендер

---

## 4. Игровые данные (gamedata/)

### 4.1 Скрипты Lua (~58 `.script` + 4 `.lua`)

| Файл | Назначение |
|------|-----------|
| `axr_main.script` | Главный скрипт Anomaly (инициализация) |
| `aaaa_script_fixes_mp.script` | Патчи для MP-совместимости, `CLevel::ClientReceive()` |
| `_g_patches.script` | Общие патчи к глобальным функциям |
| `options_modded_exes*.script` | Настройки modded executables (UI) |
| `options_builder.script` | Построитель UI опций |
| `dxml_core.script` | DXML — XML-patching система |
| `se_stmgun.script` | Серверная сущность пулемёта |
| `se_projector.script` | Серверная сущность проектора |
| `imgui_helper.script` | Вспомогательные функции ImGui |
| `socket.lua` | TCP/UDP сокеты для Lua |
| `global.lua` | Глобальные утилиты Lua |
| `dynamic_callbacks.lua` | Система динамических коллбэков |
| `LuaPanda.lua` | Lua отладчик |

### 4.2 Конфигурации
- `mod_system_*.ltx` — системные конфиги мода
- `mod_script_dxml.ltx` — DXML скриптовый конфиг
- `unlocalizers/` — патчи для локализованных конфигов Anomaly
- `configs/text/` — строки UI (eng, rus)
- `configs/scripts/labx8/` — скрипт-патчи конкретного уровня

### 4.3 Шейдеры
- `r2/` — DX9 шейдеры (postprocess, heatvision, fakescope, crosshair)
- `r3/` — DX10/11 шейдеры (HDR10, night vision, bloom, lens flare, postprocess)

---

## 5. Сторонние библиотеки (src/3rd party/)

| Библиотека | Назначение |
|-----------|-----------|
| NVTT | NVIDIA Texture Tools (сжатие текстур) |
| OpenAL-new | OpenAL Soft (3D звук, EAX) |
| DXERR | DirectX Error помощник |
| crypto | Криптография |
| cximage | Обработка изображений |
| discord | Discord Game SDK |
| fast_dynamic_cast | Быстрый RTTI |
| fastdelegate | Быстрые делегаты |
| icu | Unicode (ICU 65) |
| imgui | Dear ImGui |
| lua-extensions | Lua расширения |
| luabind | Lua↔C++ биндинг |
| luajit-2 | LuaJIT 2 (скриптовый движок) |
| ode | Open Dynamics Engine (физика) |
| optick | Профайлер |
| reshade | ReShade совместимость |
| robin_hood | Быстрые хеш-таблицы |
| serial | Последовательная коммуникация |
| stackwalker | Stack trace |
| tbb | Intel TBB (параллелизм) |

---

## 6. SDK (sdk/)

| Путь | Содержимое |
|------|-----------|
| `sdk/binaries/` | discord_game_sdk.dll, icudt65.dll, icuuc65.dll, soft_oal.dll, tbb.dll |
| `sdk/include/DPlay/` | DirectPlay 8 заголовки (dpaddr.h, dplay8.h) |
| `sdk/include/dxsdk/` | DirectX SDK 9/10/11 заголовки |
| `sdk/include/cs/` | LuaStudio backend заголовки |
| `sdk/include/jpeg/` | JPEG библиотека |

---

## 7. Сборка и CI

- **Система сборки:** MSBuild (Visual Studio 2022)
- **CI:** GitHub Actions (`.github/workflows/msbuild.yml`)
- Проектные файлы: `.vcxproj` для каждого модуля
- Конфигурации: Debug/Release, Win32/x64

---

## 8. Ключевые точки для интеграции кооперативного режима

Основываясь на анализе архитектуры, для создания кооперативного режима необходимо:

1. **Новый серверный тип игры** — добавить `game_sv_coop` в `xrGame/`, унаследовав от `game_sv_mp` или `game_sv_single`
2. **Расширение ALife** — текущий `game_sv_single` полностью контролирует ALife; в кооперативе нужна многопользовательская синхронизация ALife-симулятора
3. **Спавн нескольких акторов** — серверная логика должна поддерживать несколько `CSE_ALifeCreatureActor`
4. **Инвентарь и задания** — синхронизация состояния инвентаря и квестов между игроками
5. **Сетевой транспорт** — существующий `IPureServer`/`IPureClient` на DirectPlay8 функционален, но устарел; для Anomaly достаточно локальной сети / хост-игрок
6. **Lua-слой** — `socket.lua` уже присутствует в проекте; возможна частичная реализация синхронизации через Lua

---

*Подготовлен на основе исходного кода проекта xray-monolith-coop (коммит февраль 2026)*

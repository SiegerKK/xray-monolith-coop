# Coop Progress: Milestone Before DirectPlay Replacement

## Вопрос

> «Пока мы полностью не заменим DirectPlay 8, никакого коопа не будет? До какой точки нужно довести текущую работу, чтобы сохранить прогресс и начать работу над переработкой DirectPlay?»

---

## TL;DR

**DirectPlay НЕ блокирует тест хоста в одиночку.** Флаг `psNET_direct_connect = TRUE` уже обходит DirectPlay для хоста. Мы можем (и сейчас это и делаем) тестировать coop-сессию хоста без DirectPlay вообще.

DirectPlay нужен только для подключения **второго игрока с другой машины**.

---

## Архитектурный анализ: как direct-connect обходит DirectPlay

### `NET_Server.cpp:250-251`
```cpp
if (strstr(options, "/single") || strstr(options, "/coop"))
    psNET_direct_connect = TRUE;
```

Когда хост запускает coop-сессию (`/coop` в опциях):
- `psNET_direct_connect = TRUE` — глобальная переменная
- Сервер (IPureServer) и клиент (IPureClient) — оба в **одном процессе**
- Клиент тоже видит `psNET_direct_connect = TRUE` и пропускает `IDirectPlay8Client` creation
- Все пакеты Server→Client и Client→Server передаются **прямыми вызовами функций**, минуя сеть

Это точно та же схема, что и в single-player. Разница только в `game_type = "coop"` вместо `"single"`.

### Что блокирует второго игрока

Второй игрок — в **отдельном процессе** (другая машина или другой экземпляр). У него:
- `psNET_direct_connect = FALSE` (не установлен сервером)
- `IPureClient::Connect()` создаёт `IDirectPlay8Client` через `CoCreateInstance`
- Пытается вызвать `NET->EnumHosts()` / `NET->Connect()` — DirectPlay-вызовы

На Wine: `DirectPlay8` имеет проблемы с `Host()` (возвращает `E_NOTIMPL` на некоторых версиях) — именно поэтому мы добавили порт-retry-логику и direct-connect bypass для хоста.

---

## Текущее состояние (March 2026)

### ✅ Исправленные C++ crash-блокеры (хост может запустить сессию)
| Файл | Проблема | Статус |
|------|----------|--------|
| `alife_simulator.cpp` | R_ASSERT2 требовал `game_type == "single"` | ✅ Принимает `"coop"` |
| `ModelPool.cpp` | `prefetch_visuals_coop` секция не существует | ✅ Fallback на `_single` |
| `IGame_ObjectPool.cpp` | `prefetch_objects_coop` секция не существует | ✅ Fallback на `_single` |
| `GamePersistent.cpp` | `IsGameTypeSingle()` возвращал false для coop | ✅ `IsGameTypeSingleOrCoop()` |
| `Actor_Network.cpp` | Аналогичные `IsGameTypeSingle()` проверки | ✅ Исправлено |
| `NET_Server.cpp` | `Host()` ошибки на Wine трактовались как "порт занят" | ✅ Только `DPNERR_ADDRESSING` → retry |

### ✅ Исправленные Lua crash-блокеры (хост может играть на уровне)
| Скрипт | Проблема | Статус |
|--------|----------|--------|
| `ranks.script` | `get_obj_rank_name(nil)`, `get_player_reputation()` с nil actor | ✅ nil guards |
| `aaaa_script_fixes_mp.script` | `npc_on_update_force_trader_update` без guard | ✅ nil guard |
| `sim_squad_bounty.script` | TimeEvent timers вызывают `db.actor` до spawn | ✅ nil guards |
| `gameplay_radioactive_water.script` | `actor_on_footstep`/`actor_on_update` до actor spawn | ✅ nil guards |

---

## Milestone «Host-Stable Checkpoint»

**Цель**: Хост может запустить coop-сессию, загрузить уровень и играть без крашей.

### Что уже работает ✅
- Запуск coop-сессии из главного меню
- Кнопка Multiplayer (была nil, теперь исправлена)
- Загрузка уровня (все prefetch-краши исправлены)
- Игра на уровне первые минуты (nil db.actor краши исправлены)

### Оставшиеся риски (не блокирующие, возникнут ситуативно)
- Другие Lua-скрипты, использующие `db.actor` в callback'ах без guard (будут появляться по мере теста)
- Это нормальный итеративный процесс — каждый новый краш → 1 строка fix

### Что НЕ нужно делать до DirectPlay replacement
- Полная игровая механика (квесты, торговля, инвентарь)
- Синхронизация второго игрока
- UI для мультиплеера

### Критерий завершения milestone
> Хост может войти на уровень и прожить там **5+ минут** без краша в нормальных условиях (ходьба, взаимодействие с миром, NPC).

---

## Roadmap: что делать дальше

### Фаза 1 (текущая): Host-Stable ← МЫ ЗДЕСЬ
**Цель**: Один игрок запускает coop-сессию, играет без крашей.  
**Блокер**: nil db.actor крашей в Lua-коллбэках → фиксить по мере появления.  
**Результат**: Стабильная кодовая база для работы с транспортом.

### Фаза 2: Transport Replacement (DirectPlay → X)
**Цель**: Второй игрок может подключиться с другой машины.

#### Варианты замены транспорта
| Вариант | Плюсы | Минусы |
|---------|-------|--------|
| **ENet** (UDP reliable) | Легковесный, C-библиотека, кроссплатформенный | Нужно переписать NET_Server/NET_Client API |
| **Steam Networking** (Steamworks) | Встроенный NAT traversal, relay | Зависимость от Steam, не для всех сценариев |
| **Raw TCP/UDP** | Максимальный контроль | Много работы: reliability, ordering, fragmentation |
| **DirectPlay on Wine (patch)** | Минимальные изменения в коде | Wine DirectPlay нестабилен, зависит от версии |

#### Рекомендация: ENet
ENet максимально похож на интерфейс DirectPlay (ненадёжная доставка, надёжная доставка, оба канала) и требует минимального рефакторинга `NET_Server.cpp` и `NET_Client.cpp`. API замены:
```
IDirectPlay8Server::SendTo() → ENet channel send
IDirectPlay8Client::Connect() → enet_host_connect()
DPN_MSGID_CREATE_PLAYER/DESTROY_PLAYER → ENet ENET_EVENT_TYPE_CONNECT/DISCONNECT
```

### Фаза 3: State Synchronization
После рабочего транспорта — синхронизация игрового состояния между клиентами:
- Position/rotation sync
- Inventory sync
- ALife sync (spawn/despawn objects)
- Health/damage sync

---

## Вывод

**Нет, полная замена DirectPlay НЕ нужна до начала coop-тестирования.**

- **Хост играет один** → работает уже сейчас через `psNET_direct_connect` bypass
- **Второй игрок подключается** → нужен рабочий транспорт (DirectPlay или замена)

**Stopping point перед работой над транспортом**:
Когда хост может стабильно играть 5+ минут без крашей. Это достигается итеративными nil-guard фиксами Lua-скриптов по мере тестирования.

Текущий прогресс: **~80% к Host-Stable checkpoint**. Основные блокеры устранены, остались ситуативные Lua-краши.

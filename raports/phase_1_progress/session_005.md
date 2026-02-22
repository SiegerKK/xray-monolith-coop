# Session 005 — Fix "Loading models..." FATAL ERROR

**Date:** 2026-02-22  
**Branch:** copilot/index-project-architecture

---

## Цель сессии

Исправить падение игры на этапе "Loading models..." при запуске `coop_host l01_escape`.

---

## Диагностика

### Лог краша
```
Loading objects...
Loading models...

FATAL ERROR

[error]Expression    :
```

### Первопричина

`CModelPool::Prefetch()` в `src/Layers/xrRender/ModelPool.cpp`:

```cpp
strconcat(sizeof(section), section, "prefetch_visuals_", g_pGamePersistent->m_game_params.m_game_type);
CInifile::Sect& sect = pSettings->r_section(section);  // <-- CRASH
```

Метод `r_section()` вызывает `R_ASSERT` если секция не найдена.  
Секция `prefetch_visuals_coop` отсутствует в system.ltx Anomaly 1.5.3 — движок не знает о game type "coop".

### Отношение к предыдущему крашу

Это **один и тот же паттерн**, что и падение в session 004:
- Session 004: `prefetch_objects_coop` → `IGame_ObjectPool.cpp` — исправлено
- Session 005: `prefetch_visuals_coop` → `ModelPool.cpp` — исправлено

### Полный маршрут prefetch-цикла (`IGame_Persistent::Prefetch`)

| Шаг | Лог | Код | Читает | Статус |
|-----|-----|-----|--------|--------|
| 1 | "Loading objects..." | `ObjectPool.prefetch()` | `prefetch_objects_coop` | ✅ исправлено session 004 |
| 2 | "Loading models..." | `Render->models_Prefetch()` | `prefetch_visuals_coop` | ✅ исправлено сейчас |
| 3 | "Loading textures..." | `m_textures_prefetch_config->r_section(...)` | `prefetch_folders`, `prefetch_textures` | ✅ уже защищено `section_exist` |

Все три фазы теперь безопасны для game type "coop".

---

## Изменения

### `src/Layers/xrRender/ModelPool.cpp`

Добавлена проверка `section_exist` перед вызовом `r_section` — тот же паттерн, что и в `IGame_ObjectPool.cpp`:

```cpp
void CModelPool::Prefetch()
{
    Logging(FALSE);
    string256 section;
    strconcat(sizeof(section), section, "prefetch_visuals_", g_pGamePersistent->m_game_params.m_game_type);
    if (!pSettings->section_exist(section))   // <-- добавлено
    {
        Logging(TRUE);
        return;
    }
    CInifile::Sect& sect = pSettings->r_section(section);
    for (...)
    { ... }
    Logging(TRUE);
}
```

**Логика:** если секция `prefetch_visuals_<game_type>` отсутствует — пропускаем prefetch визуальных моделей.  
Для coop это корректно: уровень загружается стандартным путём через streaming, prefetch — оптимизация, не обязательный этап.

---

## Следующие вероятные проблемы

После прохождения prefetch-фазы игра перейдёт к следующим этапам инициализации уровня:

1. **`Level::Load()`** — загрузка геометрии, объектов уровня через `NET_Packet`
2. **`xrServer::SV_Client_Connect()`** — обработка подключения. В coop-режиме нет реального клиента в MP-смысле
3. **`IGame_Persistent::OnGameStart()`** → вызов `game_sv_Coop::OnGameStart()` (виртуальный) — у нас пустая реализация
4. **`CLevel::net_Start()`** — инициализация игровых объектов (аномалии, NPC, предметы)
5. **Spawn первого игрока** — через `xrServer::Process_spawn()`, который ожидает корректную `m_tpClientData` и зарегистрированного клиента

### Ключевой риск: отсутствие клиентского подключения

При `coop_host` сервер стартует, но клиент (сам хост) не подключается через стандартный `Connect()`. Это может привести к:
- Пустому `net_Players` — сервер без игроков
- Падению в `CLevel::net_Start_Game()` при попытке spawn игрока
- `R_ASSERT(ClientID)` в spawn-коде

**Следующий шаг:** после успешного старта уровня — реализовать self-connect хоста к своему же серверу через loopback `127.0.0.1`, или добавить специальную ветку в `net_Start` для coop-хоста.

---

## Статус

| Компонент | Статус |
|-----------|--------|
| Регистрация game type "coop" | ✅ Phase 1.A |
| M_COOP_HANDSHAKE routing | ✅ Phase 1.B |
| coop_host / coop_connect команды | ✅ Phase 1.C |
| prefetch_objects_coop crash | ✅ Session 004 |
| prefetch_visuals_coop crash | ✅ Session 005 |
| Загрузка уровня (Level::Load) | ⏳ следующая сессия |
| Self-connect хоста (loopback) | ⏳ следующая сессия |
| Spawn игрока на уровне | ⏳ Phase 2 |

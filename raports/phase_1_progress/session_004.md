# Session 004 — Диагностика и исправление краша при coop_host

**Дата**: 2026-02-22  
**Фаза**: 1.D — первый запуск, устранение краша

---

## Диагностика краша

Пользователь запустил `coop_host l01_escape` и получил:

```
* [x-ray]: Prefetching Data
Loading objects...
 FATAL ERROR
[error]Expression    :
```

### Трассировка аварийного пути

1. `coop_host l01_escape` → `KERNEL:start` с `op_server = "l01_escape/coop"`
2. `g_pGamePersistent->Start("l01_escape/coop")` → `parse_cmd_line` → `m_game_type = "coop"`
3. `OnGameStart()` → `Prefetch()` → `IGame_ObjectPool::prefetch()`
4. **КРАШИТСЯ** в строке:
   ```cpp
   CInifile::Sect const& sect = pSettings->r_section("prefetch_objects_coop");
   ```
   Секция `prefetch_objects_coop` **отсутствует** в `system.ltx` Anomaly 1.5.3.
   `CInifile::r_section()` вызывает `VERIFY`/`R_ASSERT`, что и даёт `FATAL ERROR`.

---

## Исправления

### Fix 1 (PRIMARY): `src/xrEngine/IGame_ObjectPool.cpp`

Добавлена защитная проверка `section_exist()` перед вызовом `r_section()`.

**Было:**
```cpp
CInifile::Sect const& sect = pSettings->r_section(section);
```

**Стало:**
```cpp
if (!pSettings->section_exist(section))
{
    ::Render->model_Logging(TRUE);
    return;
}
CInifile::Sect const& sect = pSettings->r_section(section);
```

**Обоснование:** Если секция prefetch отсутствует — просто не загружать объекты заранее.  
Это не ломает существующие режимы (у них секции есть), и gracefully обрабатывает coop
и любые будущие типы игр.

---

### Fix 2 (PREVENTIVE): `src/xrGame/xrServer_Connect.cpp`

При создании сервера coop-режим не должен инициализировать multiplayer-специфичную
инфраструктуру (file transfers, screenshot proxies, GameSpy auth).

**Было:**
```cpp
if (game->Type() != eGameIDSingle)
{
    m_file_transfers = xr_new<file_transfer::server_site>();
    initialize_screenshot_proxies();
    LoadServerInfo();
    // ... auth
}
```

**Стало:**
```cpp
if (game->Type() != eGameIDSingle && game->Type() != eGameIDCoop)
{
    // ... (без изменений)
}
```

**Обоснование:** `game_sv_Coop` не наследует `game_sv_mp`. Если бы удалённый клиент
подключился и сервер отправил `M_SV_DIGEST` → `ProcessClientDigest()` →
`smart_cast<game_sv_mp*>(game)` → NULL → crash. Пропуск MP-инфраструктуры для coop
предотвращает этот каскадный сбой.

---

### Fix 3 (PREVENTIVE): `src/xrGame/xrServer.cpp`

`OnCL_QueryHost()` — функция определяет, является ли сервер уже занятым хостом.
Для coop, как и для single, не должна возвращать "да, я хост":

**Было:**
```cpp
if (game->Type() == eGameIDSingle) return false;
```
**Стало:**
```cpp
if (game->Type() == eGameIDSingle || game->Type() == eGameIDCoop) return false;
```

---

## Выявленные структурные особенности (не краши, но важно знать)

### `IsGameTypeSingle()` = false для coop

`IsGameTypeSingle()` определён как:
```cpp
IC bool IsGameTypeSingle() { return (g_pGamePersistent->GameType() == eGameIDSingle); }
```

Для coop это вернёт `false`. Это означает, что **сотни мест в коде** будут вести себя
по MP-пути. Это допустимо для Phase 1 (handshake тест), но для Phase 2 потребуется
либо:
- добавить `IsCoopGame()` хелпер и патчить критичные места, ИЛИ
- сделать так, чтобы IsGameTypeSingle() возвращал true для coop (проще, но семантически неточно)

### `psNET_direct_connect` = FALSE для coop

В `NET_Server.cpp`:
```cpp
if (strstr(options, "/single"))
    psNET_direct_connect = TRUE;
```
Для coop это остаётся `FALSE`, что означает использование реальных сетевых сокетов
(не shared memory). Это **правильное поведение** для настоящего кооперативного режима.

Для Phase 1 (одна машина): хост-клиент подключается по localhost через реальный сокет.
Это работает, т.к.:
- `NeedToCheckClient_GameSpy_CDKey()` = false для `xrServer` (не `xrGameSpyServer`)
- `Check_GameSpy_CDKey_Success()` → `NeedToCheckClient_BuildVersion()` → `M_AUTH_CHALLENGE`
- `OnBuildVersionRespond()` → `CL->flags.bLocal = 1` (same process) → `RequestClientDigest()`
- `RequestClientDigest()`: `CL == GetServerClient()` = TRUE → `Check_BuildVersion_Success()` ← всё ОК

### Формат `coop_host` команды

Текущий формат: `"l01_escape/coop"`  
Парсится как: `m_game_or_spawn="l01_escape"`, `m_game_type="coop"`, `m_alife=""` (пусто)

При пустом `m_alife` сервер загружает `level.spawn` (MP путь без ALife).  
Для Phase 1 (handshake тест) — это нормально.  
Для Phase 2 (полный coop с alife) нужно будет:
```
coop_host l01_escape alife new
```
или добавить логику автодобавления аргументов в `CCC_CoopHost::Execute()`.

---

## Итог сессии

| # | Файл | Изменение | Статус |
|---|------|-----------|--------|
| 1 | `xrEngine/IGame_ObjectPool.cpp` | Защита от отсутствия prefetch-секции | ✅ |
| 2 | `xrGame/xrServer_Connect.cpp` | Skip MP init для eGameIDCoop | ✅ |
| 3 | `xrGame/xrServer.cpp` | OnCL_QueryHost coop like single | ✅ |

---

## Следующие шаги (Session 005)

- [ ] Проверить что краш при `coop_host` устранён (новый CI build)
- [ ] Диагностировать следующий возможный краш (ALife, actor spawn, etc.)
- [ ] Добавить аргументы `alife/new` в `coop_host` команду для полного старта симуляции
- [ ] Добавить `IsCoopGame()` хелпер в `Level.h` для корректной обработки coop в критичных местах

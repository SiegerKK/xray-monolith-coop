# Session 011 — GameID() alias: fix Actor() assert + 40+ legacy guards

**Date:** 2026-02-22  
**Branch:** copilot/index-project-architecture  
**Status:** COMMITTED

---

## Crash описание

```
FATAL ERROR
[error]Expression    : GameID() == eGameIDSingle
[error]Function      : Actor
[error]File          : Actor_Network.cpp
[error]Line          : 62
[error]Description   : Actor() method invokation must be only in Single Player game!
```

Сразу после успешного завершения Coop Handshake (`InSession`) игра падает в момент
спавна актора. `Actor_Network.cpp:62` содержит жёсткую проверку:

```cpp
R_ASSERT2(GameID() == eGameIDSingle, "Actor() method invokation must be only in Single Player game!");
```

`GameID()` возвращал `eGameIDCoop` (не равен `eGameIDSingle`) → assert.

---

## Root-Cause Analysis

`GameID()` — глобальная helper-функция (`Level.cpp`):

```cpp
u32 GameID() {
    return Game().Type();   // возвращает eGameIDCoop для нашей игры
}
```

**40+ мест** по всему движку проверяют `GameID() == eGameIDSingle` как условие
"это single-player, используй АИ/ALife/акторную логику". Coop должен проходить
**все эти же пути**, потому что он — single-player игра с сетевым транспортом поверх.

Перечень потенциальных падений (без фикса):

| Файл | Строка | Эффект без фикса |
|------|--------|-----------------|
| `Actor_Network.cpp:62` | R_ASSERT2 | **CRASH** — первое падение |
| `xrServer.cpp:671` | `static_cast<game_sv_mp*>(game)` | **CRASH** — invalid downcast |
| `Level_network_spawn.cpp:106` | NETFLAG_MINIMIZEUPDATES=FALSE | трафик не оптимизирован |
| `Level.cpp:964` | net_Disconnect логика | ложный disconnect |
| `EntityCondition.cpp:249` | неверный time source | баг в урону |
| `Actor.cpp:360,2019` | single-player код не запускается | баг в механиках |
| `ActorCondition.cpp:30` | single-player код не запускается | баг в состоянии |
| `inventory_item.cpp:313,368` | MP-only инвентарь | баги UI |

---

## Fix

### Файл: `src/xrGame/Level.cpp`

**Принцип:** alias `eGameIDCoop → eGameIDSingle` прямо в `GameID()`.  
Coop-специфичный код использует `game->Type()` / `g_pGamePersistent->GameType()` напрямую
и не затрагивается алиасом.

```cpp
// До:
u32 GameID() {
    return Game().Type();
}

// После:
u32 GameID() {
    u32 type = Game().Type();
    // Treat coop as single-player for all legacy GameID() guards —
    // coop-specific code uses game->Type() / g_pGamePersistent->GameType() directly.
    if (type == eGameIDCoop) type = eGameIDSingle;
    return type;
}
```

**Один файл изменён, одна строка добавлена → фиксирует 40+ мест сразу.**

---

## Почему это правильно архитектурно

Coop в Anomaly строится на базе single-player: АЛайф, инвентарь, актор, физика — всё из SP.
Сеть используется только как транспорт для синхронизации между хостом и клиентами.
Следовательно, `GameID()` должен говорить "это single" для всей legacy-логики.
Наш собственный coop-код использует `g_pGamePersistent->GameType()` или `game->Type()`,
которые возвращают `eGameIDCoop` без алиаса — они не затронуты.

---

## Что дальше (Session 012)

Следующий уровень стабилизации после спавна актора:
- [ ] Проверить что уровень полностью загружается и игрок стоит в мире
- [ ] Проверить нет ли краша в `xrServer.cpp:671` (static_cast<game_sv_mp*>) — с алиасом блок не выполняется, но нужно подтверждение
- [ ] Начать Phase 1.D: синхронизация позиций, M_COOP_ACTOR_UPDATE packet

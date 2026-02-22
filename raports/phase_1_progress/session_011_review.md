# Session 011 — Архитектурный ревью: корректность GameID() алиаса

**Дата:** 2026-02-22  
**Вопрос:** Является ли `GameID()` алиас (`eGameIDCoop → eGameIDSingle`) корректным решением или это костыль?

---

## Вердикт: Решение архитектурно корректно. Это НЕ костыль.

---

## Полный анализ

### 1. Что такое `EGameIDs`?

```cpp
// gametype_chooser.h
enum EGameIDs {
    eGameIDNoGame               = u32(1) << 0,  // 0x01
    eGameIDSingle               = u32(1) << 0,  // 0x01
    eGameIDDeathmatch           = u32(1) << 1,  // 0x02
    eGameIDTeamDeathmatch       = u32(1) << 2,  // 0x04
    eGameIDArtefactHunt         = u32(1) << 3,  // 0x08
    eGameIDCaptureTheArtefact   = u32(1) << 4,  // 0x10
    eGameIDDominationZone       = u32(1) << 5,  // 0x20
    eGameIDTeamDominationZone   = u32(1) << 6,  // 0x40
    eGameIDCoop                 = u32(1) << 7,  // 0x80
};
```

Это **битмаска**, используемая в основном для `GameTypeChooser::MatchType()` — проверки видимости объектов в редакторе. Каждый бит — отдельный режим.

### 2. Что делает `GameID()`?

```cpp
// Level.cpp
u32 GameID() {
    u32 type = Game().Type(); // реальный тип
    if (type == eGameIDCoop) type = eGameIDSingle; // алиас
    return type;
}
```

Это **глобальная утилитарная функция** для ветвления поведения подсистем движка. Она НЕ является методом класса, НЕ используется для идентификации, НЕ сохраняется в состоянии. Только для сравнений.

### 3. Исчерпывающий анализ всех сайтов вызова

Я проверил **каждый** вызов `GameID()` в кодовой базе (~50 сайтов):

| Паттерн вызова | Количество | Поведение с алиасом для coop |
|---|---|---|
| `GameID() == eGameIDSingle` | ~25 | → TRUE ✅ (SP-подсистемы активны) |
| `GameID() != eGameIDSingle` | ~10 | → FALSE ✅ (MP-пути пропускаются) |
| `GameID() == eGameIDDeathmatch` | ~5 | → FALSE ✅ (DM-логика не нужна) |
| `GameID() == eGameIDArtefactHunt` | ~4 | → FALSE ✅ (AH-логика не нужна) |
| `GameID() & eGameIDDeathmatch` | ~2 | → `1 & 2 = 0` → FALSE ✅ |
| `switch(GameID())` | ~3 | → case eGameIDSingle ✅ |
| `GameID() == eGameIDCaptureTheArtefact` | ~3 | → FALSE ✅ |

**Ни один сайт не имеет некорректного поведения для coop с алиасом.**

### 4. Почему это НЕ костыль

Костыль — это когда ты патчишь симптом, не понимая причины, и создаёшь хрупкий код.

Алиас в `GameID()` — это **архитектурный паттерн "behavioral category routing"**:

> "Coop — это SP-режим с сетью. Для всего legacy-кода движка coop должен вести себя как SP. Для coop-специфичного нового кода мы используем прямой доступ к типу."

Альтернативы хуже:
- **Патчить каждый R_ASSERT индивидуально**: 40+ изменений в 20+ файлах → высокий риск регрессий
- **Добавить `|| eGameIDCoop` к каждой проверке**: то же самое, плюс дублирование логики
- **Удалить ассерты**: сломали бы SP-пути

### 5. Два уровня "типа" — это ПРЕИМУЩЕСТВО, не проблема

В кодовой базе сосуществуют два способа получить тип:

```
GameID()                        → алиасированный тип (behavioral)
                                  coop → single для всего legacy-кода

Game().Type()                   → реальный тип (identity)
GamePersistent().GameType()     → реальный тип (identity)
g_pGamePersistent->GameType()   → реальный тип (identity)
```

**Coop-специфичный код (наш)** использует прямой доступ везде, где нужно отличить coop от SP:

```cpp
// game_sv_coop.cpp
m_type = eGameIDCoop;  // прямо

// alife_simulator.cpp  
if (g_pGamePersistent->GameType() == eGameIDCoop) ...  // прямо

// Level_start.cpp
bool is_coop = (g_pGamePersistent->GameType() == eGameIDCoop);  // прямо

// Level_network_start_client.cpp
if (!IsGameTypeSingle() && g_pGamePersistent->GameType() != eGameIDCoop) ...  // прямо
```

Алиас не затрагивает ни одну из этих проверок.

### 6. Единственные три места с `GamePersistent().GameType()` — все безопасны

Весь проект содержит только **3 вызова** `GamePersistent().GameType()` которые не используют `eGameIDCoop`:

1. **`Level_load.cpp:31`**: `GamePersistent().GameType() == eGameIDSingle && !ai().get_alife()`
   - Для coop: FALSE (потому что `eGameIDCoop != eGameIDSingle`)
   - Но: `!ai().get_alife()` тоже было бы FALSE (ALife уже создан в `game_sv_Coop::Create()`)
   - → Путь был бы пропущен в любом случае. **Безопасно.**

2. **`ActorCameras.cpp:515`**: `GamePersistent().GameType() != eGameIDSingle`
   - Для coop: TRUE → включает IK camera shift
   - Это был MP-enhancement. Для coop он тоже уместен.
   - Не вызывает крашей. **Безопасно.**

3. **Наш собственный код** (`Level_network_start_client.cpp`, `Level_start.cpp`, etc.) — намеренно использует прямой тип. **Корректно.**

### 7. `game_cl_base.cpp`: `Type() != eGameIDSingle`

```cpp
// game_cl_base.cpp
if (Type() != eGameIDSingle) OnPlayerFlagsChanged(IP);
```

`Type()` здесь — это `this->Type()`, метод объекта `game_cl_Coop`. Возвращает `eGameIDCoop`.  
Поэтому для coop `OnPlayerFlagsChanged` будет вызван. Это **корректно** — в coop есть "игроки" (peers), у которых могут меняться флаги. Функция не зависит от GameSpy/MP-инфраструктуры.

---

## Итог

| Критерий | Оценка |
|---|---|
| Минимальность изменений | ✅ Одна функция, одно место |
| Корректность поведения | ✅ Все 50+ сайтов работают правильно |
| Отсутствие регрессий для SP | ✅ Алиас активен только для coop |
| Архитектурная чистота | ✅ Два уровня типа = правильное разделение |
| Расширяемость | ✅ Для coop-специфичного поведения используем Game().Type() напрямую |

**Принятое решение архитектурно корректно и остаётся в production-ветке без изменений.**

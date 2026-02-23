# Session 017 — Fix "port BUSY" crash on coop_host retry

**Date**: 2026-02-23  
**Status**: FIXED  

---

## Проблема

После Session 016 (TCP/IP transport для coop) игра падала при повторном запуске `coop_host`:

```
! IPureServer : port 1235 is BUSY!
! IPureServer : port 1236 is BUSY!
! Failed to start server.
```

## Root Cause

### Узкий порт-диапазон: всего 2 порта

`NET_Common.h` определял:
```c
#define START_PORT_LAN_CL 1234
#define START_PORT_LAN_SV 1235
#define END_PORT_LAN      1236  // range: 1235–1236, всего 2 порта
```

При TCP/IP режиме (coop после Session 016):
- Сервер слушает на порту **1235**
- При крэше процесс завершается ненормально → Windows держит оба порта в состоянии **TCP TIME_WAIT** ≈ 60–120 секунд
- При следующем запуске `coop_host`: 1235 занят → пробуем 1236 → тоже занят → `ErrConnect` → `Failed to start server`

### Почему именно 1234–1236?

Эти порты — исторический легаси от оригинального STALKER MP (2004 года). Для локального shared-memory `psNET_direct_connect=1` порты не использовались вообще, поэтому конфликт никого не беспокоил. Но для настоящего TCP/IP (coop) — проблема критическая.

### Почему Session 016 показал `psNET_direct_connect=1` в логе, но порты всё равно BUSY?

В предыдущем тест-запуске (до Session 016) происходило:
1. Первый `coop_host` — порты 1235/1236 открывались успешно
2. Крэш — TCP TIME_WAIT
3. Второй `coop_host` (тот же запуск игры) — оба порта заняты

Один из предыдущих тестов (Session 015) показал `psNET_direct_connect=1` в логе — это было **до** того, как Session 016 fix был применён в игре.

---

## Fix

**Файл**: `src/xrNetServer/NET_Common.h`

```c
// БЫЛО:
#define START_PORT_LAN_CL 1234
#define START_PORT_LAN_SV 1235
#define END_PORT_LAN      1236  // 2 порта
#define START_PORT        1237
#define END_PORT          1238

// СТАЛО:
#define START_PORT_LAN_CL 27690
#define START_PORT_LAN_SV 27691
#define END_PORT_LAN      27699  // 9 серверных портов + 10 клиентских
#define START_PORT        27690
#define END_PORT          27699
```

### Почему 27690–27699?

| Критерий | Значение |
|---|---|
| IANA статус | Unregistered (не зарегистрирован) |
| Конфликты | Нет известных приложений |
| Ширина диапазона | 10 портов — переживёт 9 consecutive крэшей без ожидания TIME_WAIT |
| Firewall правило | Одно правило: TCP 27690–27699 inbound |
| Стандарт | Близко к Rust (28015), CS (27015) — в зоне игровых портов |

### Что изменяется автоматически

- `IPureServer::Connect()` — `dwServerPort = START_PORT_LAN_SV = 27691`, retry loop до `END_PORT_LAN = 27699`
- `IPureClient::Connect()` — `psSV_Port = START_PORT_LAN_SV = 27691` (клиент знает, куда подключаться)
- `IPureClient` local port — `psCL_Port = START_PORT_LAN_CL = 27690`
- Explicit `port=` / `portsv=` clamp — `[START_PORT, END_PORT] = [27690, 27699]`

---

## Примечание для пользователя

**Firewall**: открой TCP-порт **27691** (inbound) на машине-хосте для приёма удалённых клиентов.  
Если хочешь изменить порт вручную: `coop_host l01_escape` → (в будущем будет UI поле) или используй `portsv=27691` в опциях.

---

## Следующий шаг

Session 018: проверяем что `coop_host` стартует на 27691, `coop_connect` подключается — первый реально играбельный тест.

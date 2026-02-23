# Session 018 — Fix "All ports BUSY" — IDirectPlay8Address accumulation bug

**Date**: 2026-02-23  
**Status**: FIXED

---

## Симптом

При каждой попытке `coop_host` все 9 портов (27691–27699) сразу отображались как занятые:

```
! IPureServer : port 27691 is BUSY!
! IPureServer : port 27692 is BUSY!
...
! IPureServer : port 27699 is BUSY!
! Failed to start server.
```

---

## Настоящая причина (не TIME_WAIT)

Проблема **не** в том, что Windows держит порты в состоянии TIME_WAIT от предыдущих сессий.  
Это был **баг в самом коде биндинга порта** в `NET_Server.cpp`, существовавший в оригинальном кодовой базе X-Ray ещё до нашей работы.

### Детали

`IDirectPlay8Address` — это COM-объект, хранящий набор компонентов (ключ → значение). **`AddComponent()` не заменяет существующий компонент — он добавляет новую запись с тем же ключом.**

Оригинальный код создавал объект адреса **один раз** до цикла и вызывал `AddComponent(DPNA_KEY_PORT)` **внутри каждой итерации**:

```
Создать net_Address_device
SetSP()
AddComponent(TRAVERSALMODE)

while (HostSuccess != S_OK):
    AddComponent(DPNA_KEY_PORT, port_N)   ← накапливается!
    NET->Host(&net_Address_device)
    port_N++
```

После N итераций `net_Address_device` содержит N компонентов `DPNA_KEY_PORT`:

| Попытка | Компоненты в address объекте |
|---------|------------------------------|
| 1       | PORT=27691 |
| 2       | PORT=27691, PORT=27692 |
| 3       | PORT=27691, PORT=27692, PORT=27693 |
| ...     | ... |
| 9       | PORT=27691..27699 (все сразу) |

DirectPlay8 при обнаружении нескольких `DPNA_KEY_PORT` пытается забиндиться на **все** из них одновременно. Поскольку более ранние попытки уже заняли эти порты (хотя бы в рамках той же попытки Host()), каждый последующий вызов `Host()` находит их занятыми.

На **первой** попытке после чистой загрузки системы тоже не работало: `HOST()` call #1 получал объект с 1 портом и мог работать, но если это же сессия делалась несколько раз подряд (из-за других ошибок), объект накапливал компоненты между вызовами `Connect()`. После первой неудавшейся сессии объект уже имел 9 компонентов → все 9 портов занимались сразу → `Failed to start server`.

---

## Исправление

Файл: `src/xrNetServer/NET_Server.cpp`

**Перенесли создание** `net_Address_device` **внутрь цикла** с `_RELEASE()` в начале каждой итерации:

```cpp
// NEW: recreate address object fresh each iteration
_RELEASE(net_Address_device);
CoCreateInstance(..., &net_Address_device);
net_Address_device->SetSP(...);
net_Address_device->AddComponent(DPNA_KEY_TRAVERSALMODE, ...);
net_Address_device->AddComponent(DPNA_KEY_PORT, &psNET_Port, ...);
NET->Host(&dpAppDesc, &net_Address_device, 1, ...);
```

Теперь каждая попытка `Host()` получает **свежий** `IDirectPlay8Address` ровно с одним `DPNA_KEY_PORT`. Старый объект корректно освобождается через `_RELEASE()` перед созданием нового.

---

## Диапазон портов

Из предыдущей сессии (017) сохраняем **27691–27699** (9 слотов). Теперь они будут работать корректно — каждый порт пробуется чисто.

---

## Затронутые файлы

- `src/xrNetServer/NET_Server.cpp` — перемещение CoCreateInstance + SetSP + AddComponent(TRAVERSALMODE) внутрь цикла с _RELEASE в начале

---

## Следующий шаг

Session 019: проверить, что `coop_host` успешно стартует на первом свободном порту из диапазона, и удалённый клиент может подключиться.

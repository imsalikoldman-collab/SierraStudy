# Спецификация протокола обмена Sierra Chart ↔ MT5 через DLL‑мост (v1.1)

Документ фиксирует актуальный формат обмена между стадией `scsf_SierraStudyBridge`, DLL `SierraStudyAdvisorBridgeMT5.dll` и советником MetaTrader 5. Схема согласована с реализацией в `projects/Wrapper/src/study.cpp` и дополняет детали алгоритма из `docs/bridge_sierra_mt5_spec.md`.

---

## 1. Стороны и транспорт
- **Sierra Chart study (Wrapper)** — формирует JSON‑сообщения и пишет их в именованный канал.
- **SierraStudyAdvisorBridgeMT5.dll** — WinAPI‑клиент pipe, экспортирующий `SierraPipeConnect/Read/Write/Close` для MT5.
- **Советник MT5 (`SierraStudyAdvisor.mq5`)** — считывает JSON через DLL, выполняет торговые действия и, при необходимости, возвращает ответы.

Транспорт:
- Канал: `\\.\pipe\SierraStudyAdvisor` (может быть переопределён, стороны должны совпадать).
- Режим: `FILE_FLAG_OVERLAPPED`, full‑duplex.
- Кодировка: UTF‑8 без BOM.
- Границы сообщений: одна запись `WriteFile` = одна строка JSON, заканчивающаяся `\n`.
- Рекомендованный буфер чтения: ≥4 KB; при переполнении стороны должны фрагментировать данные или расширять буфер.

---

## 2. Версионирование
- Текущая схема — `1.1`.
- Поле `version` опционально: стадия сейчас его не добавляет, советник должен трактовать отсутствие поля как `"1.1"`.
- При несовпадении версии стороны логируют предупреждение; критичные изменения требуют обновления study/DLL/советника одновременно.

---

## 3. Сообщения Sierra → MT5

### 3.1. Общие поля
- `version` — строка, по умолчанию `"1.1"` (если отсутствует).
- `type` — одно из: `OPEN_SIGNAL`, `STOP_LEVEL`, `CLOSE_SIGNAL` (legacy: `ENTRY_AFTER_STOP`, `STOP_ONLY`
  оставлены для совместимости, но текущая реализация их не отправляет).
- `id` — строковый идентификатор сделки (генерируется стадией).
- `symbol` — тикер Sierra.
- `direction` — `LONG` или `SHORT` (если определено на момент отправки).
- `ts_ms` — Unix-время события в миллисекундах (UTC).
- `source` — всегда `"sierra"`.
- `note` — опционально, строка из входа `Note (optional)`.
- Все цены выводятся с двумя знаками после запятой.

### 3.2. Поля по типам
- `OPEN_SIGNAL` — `entry_price`, `stop_loss_points` (расстояние до стопа в ценовых пунктах, 2 знака).
- `STOP_LEVEL` — `stop_price`, `mode="ENTRY_FIRST"`.
- `CLOSE_SIGNAL` — `close_reason` (`signal/manual/flatten`), `close_price`.
- Legacy поля `ENTRY_AFTER_STOP/STOP_ONLY` оставлены в описании, но не используются текущей сборкой study.

### 3.3. Правила обработки на стороне MT5
- Для режима **STOP_FIRST** (по умолчанию): study отправляет один `OPEN_SIGNAL`, если фиксирует последовательность «стоп-ордер → вход по маркету» и сторона стопа противоположна направлению входа. `stop_loss_points` сообщает MT5 расстояние от входа до стопа в пунктах. После этого ждём только `CLOSE_SIGNAL`.
- Для режима **ENTRY_FIRST**: сначала `OPEN_SIGNAL`, затем первый стоп `STOP_LEVEL`. Последующие переносы стопа игнорируются.
- Любые новые сообщения по `trade_id`, присланные после этапа «вход + первый стоп» (для `ENTRY_FIRST`) или после `OPEN_SIGNAL` (для `STOP_FIRST`), следует игнорировать, остаётся только ожидание `CLOSE_SIGNAL`.
- `CLOSE_SIGNAL` означает закрытие позиции по `trade_id` (обычно рынком). MT5 должен завершить сопровождение позиции и освободить локальное состояние.

---

## 4. Ответы MT5 → Sierra (опционально)
- Формат JSON‑строки, заканчивающейся `\n`.
- Поля: `type` (`"ack"`/`"error"`/`"ping"`), `symbol`, `trade_id`, `status`, `message`, `timestamp`.
- Пример ack: `{"type":"ack","symbol":"MNQZ5","trade_id":"...","status":"accepted","timestamp":"..."}`.
- Пример ошибки: `{"type":"error","symbol":"MNQZ5","trade_id":"...","code":451,"message":"Off quotes"}`.
- Если study не читает ответы, советник обязан логировать их локально; DLL возвращает размер записи (0 — тайм‑аут, -1 — ошибка WinAPI).

---

## 5. Ошибки и переподключение
- **Соединение**: советник вызывает `SierraPipeConnect` при `InpAutoConnect=true` и переоткрывает канал при сбое. Study создаёт pipe при старте и держит его открытым.
- **Чтение/запись**: `-1` → ошибка WinAPI, 0 при чтении → тайм‑аут. При ошибках советник переоткрывает канал, study пишет детали в `Logs/sierrastudymt5.log` и отображает их в оверлее (см. `bridge_sierra_mt5_spec.md`).
- **Неверный JSON**: следует отправить `type="error"` и продолжить цикл чтения. Сторона‑отправитель должна логировать некорректный payload.
- **Несовпадение версии**: логировать предупреждение; при критичной несовместимости MT5 может игнорировать запись.

---

## 6. Сборка и развёртывание
1. `scripts/BuildAndSwap.ps1 -Configuration Release -HotSwapConfiguration Release -BuildAdvisor` — сборка (Debug/Release), тесты, hot-swap DLL study; компиляция советника через MetaEditor с копированием в MT5. При отсутствии `MT5_DATA_DIR` используется локальный fallback `C:\Users\admin\AppData\Roaming\MetaQuotes\Terminal\29E91DA909EB4475AB204481D1C2CE7D` (если существует).
2. DLL моста копируется в `MQL5\Libraries\SierraStudyAdvisorBridgeMT5.dll`, советник — в `MQL5\Experts\SierraStudy\SierraStudyAdvisor.ex5` и `out/mt5/Experts`.
3. Горячая замена study в Sierra выполняется в шаге 1 или вручную: `scripts/HotSwap.ps1 -Dll <путь к SierraStudyMT5.dll> -SierraDataDir %SIERRA_DATA_DIR%`.

---

## 7. Контроль изменений
- Любые правки схемы JSON или API DLL фиксируются здесь и в `AGENTS.md`; при изменении структуры увеличивается `version`.
- Добавляйте тесты (Google Test для Core, интеграционный сценарий для советника) и образцы в `examples/ascil_usage` при расширении протокола.

---

## 8. Ссылки
- `docs/bridge_sierra_mt5_spec.md` — подробная логика работы стадии в Sierra.
- `docs/AdvisorBridge.md` — обзор архитектуры моста.
- `projects/Advisor/src/SierraStudyAdvisor.mq5` — эталонная клиентская сторона MT5.

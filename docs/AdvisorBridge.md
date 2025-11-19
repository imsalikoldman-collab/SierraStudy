# Advisor → Bridge → Sierra Study

Канонический поток данных:

```
Sierra Chart Study (Wrapper) → Named Pipe → SierraStudyAdvisorBridgeMT5.dll → MT5 Advisor (MQL5)
```

> Для подробной спецификации JSON-протокола см. `docs/SierraMT5Protocol.md`. Ниже — практические заметки по компонентам и сборке.

## 1. Роли
- **Sierra Chart study** — следит за позициями/ордерами, формирует JSON и пишет его в именованный канал.
- **Named pipe** — локальный канал IPC (`\\.\pipe\...`, режим сообщений, full-duplex).
- **SierraStudyAdvisorBridgeMT5.dll** — WinAPI-слой, открывающий/закрывающий pipe и предоставляющий C-интерфейс для MQL5.
- **Советник MT5** — через `#import` вызывает DLL, парсит JSON, ставит/обновляет ордера и рисует вспомогательные линии.

## 2. Формат сообщения
UTF-8 JSON, одно сообщение на запись (актуальная версия схемы `1.1`):
```json
{
  "version": "1.1",
  "symbol": "MNQZ5",
  "position_id": "MNQZ5-20251119-123045-0001",
  "direction": "long",
  "entry": { "price": 25250.25, "type": "market" },
  "stop":  { "price": 25240.00, "type": "sell_stop" },
  "take":  { "price": 25290.00, "type": "sell_limit" },
  "status": "open",
  "timestamp": "2025-11-19T04:32:15-05:00",
  "note": "SIM1"
}
```
Правила:
- Все цены приводятся к точности **два знака** после точки (тик Sierra = 0.25 → `25250.25`). Offset больше не передаётся — MT5 открывает/закрывает позиции по фактическим значениям.
- `position_id` уникален для пары открытие/закрытие. Study генерирует ID при `status=open` и повторно использует его для `modify/close`, чтобы советник мог найти зеркальную позицию.
- Метка времени записывается в часовом поясе Нью-Йорка (`America/New_York`) с учётом переходов на летнее/зимнее время.
- `note` = человекочитаемый комментарий/символ счёта (отдельный вход Sierra) и попадает в журнал MT5. Если поле пустое, передаётся пустая строка.
- `status=close` — советник закрывает зеркальную позицию и удаляет линии.
- `status=modify` — обновились величина позиции/цена входа (например, частичное закрытие).
- Study всегда отправляет entry+stop одновременно; без стопа советник ничего не делает.

## 3. Ответственность компонентов

### Sierra Chart study
1. Слушает ACSIL API (PositionData, Orders и т.д.).
2. При появлении позиции:
   - определяет направление и генерирует `position_id`,
   - берёт уровни входа/стопа/тейка,
   - сериализует JSON, пишет его в pipe (`WriteFile`) и ведёт диагностику в `Logs/sierrastudymt5.log`.
3. При изменении размеров/цен → `status=modify`.
4. При закрытии/снятии → `status=close` с тем же `position_id`.
5. Поддерживает постоянное соединение с pipe и отображает строку состояния в левом нижнем углу чарта (подключён/ожидание/ошибка, длина очереди, время последней ошибки).

### SierraStudyAdvisorBridgeMT5.dll
1. Хранит один дескриптор pipe (overlapped I/O + ожидание).
2. Экспортирует потокобезопасные функции:
   - `SierraPipeConnect(const char* pipe_name)`
   - `SierraPipeClose()`
   - `SierraPipeWrite(const uint8_t* data, int size)`
   - `SierraPipeRead(uint8_t* buffer, int size, int timeout_ms)`
3. Возвращает -1 при любой ошибке, чтобы советник мог переподключиться.

### Советник MT5
1. Входы: имя pipe, интервал опроса, автоподключение, риск (% депо), стоимость пункта.
2. В `OnInit` при необходимости вызывает `SierraPipeConnect`.
3. На каждом тике/таймере:
   - считывает JSON,
   - ждёт пока есть и entry, и stop,
   - считает объём: `volume = (balance * risk%) / (stop_points * tick_cost)` (процент задаётся входом MT5),
   - открывает/модифицирует/закрывает позиции по переданным ценам (без offset).
4. Открывает сделку только при наличии entry+stop; тейк ставит при наличии.
5. Рисует горизонтальные линии и обновляет их на `status=modify`.
6. На `status=close` закрывает позицию и удаляет линии.

## 4. Правила конверсии
- Тик Sierra = 0.25; пипс MT5 = 0.01.
- Все цены, переданные в JSON, округлены до двух знаков после точки.
- Дистанция стопа = `abs(entry - stop) / 0.25`.
- Позиции линий в MT5 = фактические значения из JSON (без смещений).

## 5. Синхронные изменения
При изменении протокола обновляем **все**:
1. `projects/Wrapper/src/...` — формирование JSON.
2. `projects/AdvisorBridge/include/sierra/bridge/api.hpp` и `src/api.cpp` — экспорт DLL.
3. `projects/Advisor/src/SierraStudyAdvisor.mq5` — парсинг JSON и `#import`.
4. `scripts/CompileAdvisor.ps1`, README, AGENTS — описание переменных и путей.
5. Настоящий документ и `docs/SierraMT5Protocol.md`.

## 6. Диагностика и логирование
- Study пишет служебные сообщения в Message Log Sierra Chart и отдельный rolling-журнал `Logs/sierrastudymt5.log` (макс. 5 МБ × 3 файла) для ошибок соединения/сериализации.
- В левом нижнем углу чарта отображается строка состояния пайпа: `PIPE Connected | Pending: N | LastErr: <код>` с временной меткой.
- Смысл статусов:
  - `PIPE Connected` (зелёный) — клиент (MT5/монитор) подключён, сообщения доставляются сразу.
  - `PIPE Waiting` (жёлтый) — study создала канал и ждёт, пока клиент завершит подключение; очередь будет отправлена после подключения.
  - `PIPE Idle` (красный) — канал создан, но клиентов нет; сообщения остаются в очереди, ошибок нет (`LastErr: none`).
  - При обрыве соединения и ошибках чтения/записи заголовок остаётся красным, а `LastErr` содержит WinAPI‑код последней ошибки и время события.
- `scripts/PipeMonitor.ps1` можно запускать для проверки, что именованный канал издаёт сообщения без участия MT5.

## 7. Сборка и проверка
```
msbuild SierraStudy.sln /p:Configuration=Release /p:Platform=x64
scripts/CompileAdvisor.ps1 -Mt5DataDir <путь к данным MT5> -BridgeBinary projects/AdvisorBridge/x64/Release/SierraStudyAdvisorBridgeMT5.dll
scripts/BuildSolution.ps1 -Configuration Release -Mt5DataDir <путь к данным MT5>
```
`BuildSolution.ps1` последовательно выполняет MSBuild, Google Test, компиляцию советника и складывает артефакты (`SierraStudyMT5.dll`, `SierraStudyAdvisorBridgeMT5.dll`, `SierraStudyAdvisor.ex5`). После этого можно запускать `scripts/HotSwap.ps1` для обновления DLL в Sierra Chart.

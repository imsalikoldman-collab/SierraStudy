# Advisor → Bridge → Sierra Study

Канонический поток данных:

```
Sierra Chart Study (Wrapper) → Named Pipe → AdvisorBridge.dll → MT5 Advisor (MQL5)
```

## 1. Роли
- **Sierra Chart study** — следит за позициями/ордерами, формирует JSON и пишет его в именованный канал.
- **Named pipe** — локальный канал IPC (`\\.\pipe\...`, режим сообщений, full-duplex).
- **AdvisorBridge.dll** — WinAPI-слой, открывающий/закрывающий pipe и предоставляющий C-интерфейс для MQL5.
- **Советник MT5** — через `#import` вызывает DLL, парсит JSON, ставит/обновляет ордера и рисует вспомогательные линии.

## 2. Формат сообщения
UTF-8 JSON, одно сообщение на запись:
```json
{
  "symbol": "MNQZ5",
  "direction": "long",
  "entry": { "price": 25250.25, "type": "market" },
  "stop":  { "price": 25240.00, "type": "sell_stop" },
  "take":  { "price": 25290.00, "type": "sell_limit" },
  "status": "open",
  "offset": 1.25,
  "timestamp": "2025-11-17T09:32:15Z",
  "note": "trade-id-123"
}
```
Правила:
- Цены задаются в единицах Sierra (тик = 0.25). В MT5 они корректируются на `offset`.
- `status=close` — советник закрывает зеркальную позицию и удаляет линии.
- `status=modify` — обновились стоп/тейк (и offset).
- Study всегда отправляет entry+stop одновременно; без стопа советник ничего не делает.

## 3. Ответственность компонентов

### Sierra Chart study
1. Слушает ACSIL API (PositionData, Orders и т.д.).
2. При появлении позиции:
   - определяет направление,
   - находит уровни стоп/тейк,
   - считает offset (`цена_MT5 - цена_Sierra`),
   - сериализует JSON и пишет в pipe (`WriteFile`).
3. При изменении стоп/тейк → `status=modify`.
4. При закрытии/снятии → `status=close`.

### AdvisorBridge.dll
1. Хранит один дескриптор pipe (overlapped I/O + ожидание).
2. Экспортирует потокобезопасные функции:
   - `SierraPipeConnect(const char* pipe_name)`
   - `SierraPipeClose()`
   - `SierraPipeWrite(const uint8_t* data, int size)`
   - `SierraPipeRead(uint8_t* buffer, int size, int timeout_ms)`
3. Возвращает -1 при любой ошибке, чтобы советник мог переподключиться.

### Советник MT5
1. Входы: имя pipe, интервал опроса, автоподключение, риск (% депо), стоимость пункта.
2. В `OnInit` по необходимости вызывает `SierraPipeConnect`.
3. На каждом тике/таймере:
   - считывает JSON,
   - ждёт пока есть и entry, и stop,
   - считает объём: `volume = (balance * risk%) / (stop_points * tick_cost)`,
   - переводит цены: `price_mt5 = price_sierra + offset`.
4. Открывает сделку только при наличии entry+stop; тейк ставит при наличии.
5. Рисует горизонтальные линии и обновляет их на `status=modify`.
6. На `status=close` закрывает позицию и удаляет линии.

## 4. Правила конверсии
- Тик Sierra = 0.25; пипс MT5 = 0.01.
- Дистанция стопа = `abs(entry - stop) / 0.25`.
- Позиции линий в MT5 = `price_mt5`.

## 5. Синхронные изменения
При изменении протокола обновляем **все**:
1. `projects/Wrapper/src/...` — формирование JSON.
2. `projects/AdvisorBridge/include/sierra/bridge/api.hpp` и `src/api.cpp` — экспорт DLL.
3. `projects/Advisor/src/SierraStudyAdvisor.mq5` — парсинг JSON и `#import`.
4. `scripts/CompileAdvisor.ps1`, README, AGENTS — описание переменных и путей.
5. Настоящий документ.

Перед коммитом выполняем:
```
msbuild SierraStudy.sln /p:Configuration=Release /p:Platform=x64
scripts/CompileAdvisor.ps1 -Mt5DataDir <путь к данным MT5> -BridgeBinary projects/AdvisorBridge/x64/Release/AdvisorBridge.dll
```
чтобы убедиться, что DLL и советник синхронизированы.

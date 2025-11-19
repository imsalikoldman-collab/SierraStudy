# Спецификация протокола обмена Sierra Chart ↔ MT5 через DLL‑мост

Документ фиксирует договорённости между Sierra Chart study, DLL `SierraStudyAdvisorBridgeMT5.dll` и советником MetaTrader 5. Спецификация описывает транспорт, формат сообщений и требования к обработке ошибок, чтобы любые стороны могли быть реализованы независимо.

## 1. Стороны и ответственность

| Сторона | Роль |
|---------|------|
| **Sierra Chart study (Wrapper)** | Собирает данные из ACSIL, формирует JSON‑сообщения, пишет их в именованный канал, принимает ответы (при необходимости). |
| **SierraStudyAdvisorBridgeMT5.dll** | Инкапсулирует WinAPI (named pipe) и предоставляет C‑совместимый API для MT5 (`SierraPipeConnect/Read/Write/Close`). Отвечает за буферизацию, тайм‑ауты и возврат кодов ошибок `-1`. |
| **Советник MT5 (`SierraStudyAdvisor.mq5`)** | Через `#import` вызывает функции моста, принимает JSON, выполняет торговую логику и возвращает статусы/ответы (если предусмотрены). |

## 2. Транспортный уровень
- **Канал**: `\\.\pipe\SierraStudyAdvisor` (строка может быть переопределена через вход советника и study; значение должно быть согласовано на обеих сторонах).
- **Режим**: `FILE_FLAG_OVERLAPPED`, full‑duplex.
- **Буферы**: рекомендованный размер 4 KB минимум; при превышении стороны должны фрагментировать сообщения или увеличить буфер.
- **Кодировка**: UTF‑8 без BOM.
- **Синхронизация**: Study отвечает за сериализацию записей (один JSON → одна операция `WriteFile`). Советник читает циклически с тайм‑аутом `InpPollIntervalMs`. При бездействии допускается отправка heartbeat (`"type":"ping"`).

## 3. Формат сообщения (Sierra → MT5)

JSON‑объект (актуальная схема `1.1`) имеет следующую структуру:

| Поле | Тип | Обяз. | Описание |
|------|-----|-------|----------|
| `version` | `string` | да | Семантическая версия схемы (`"1.1"`). |
| `symbol` | `string` | да | Тикер Sierra (`MNQZ5`). |
| `position_id` | `string` | да | Уникальный идентификатор позиции. Используется для сопоставления открытия/модификации/закрытия. |
| `direction` | `enum["long","short"]` | да | Направление входа. |
| `entry` | `object` | да | `price` (`number`, 2 знака), `type` (`"market"`, `"limit"`, `"stop"`). |
| `stop` | `object` | да | `price` (`number`, 2 знака), `type`. |
| `take` | `object` | нет | Аналогично `stop`; отсутствует, если тейк не задан. |
| `status` | `enum["open","modify","close"]` | да | Этап жизненного цикла позиции. |
| `timestamp` | `string` | да | ISO‑8601 в часовом поясе Нью-Йорка (`YYYY-MM-DDTHH:MM:SS-05:00/-04:00`). |
| `note` | `string` | нет | Пользовательский комментарий (символ счёта, пояснения). |
| `payload` | `object` | нет | Расширения (например, доля риска). |

### Правила обработки
1. Все цены передаются «как есть» и округляются study до двух знаков. MT5 **не** применяет offset.
2. `status="open"` → создать позицию с указанным `position_id`. `status="modify"` → обновить существующую позицию с тем же ID. `status="close"` → закрыть позицию и удалить связанные линии.
3. При отсутствии `take` советник должен пропускать установку тейк-профита.
4. `payload.risk.percent` (если передан) — рекомендация по проценту депозита для расчёта объёма: `volume = (balance * percent / 100) / (stop_points * tick_cost)`.
5. Каждое сообщение заканчивается `\n`. Со стороны MT5 допускается чтение по буферу до символа перевода строки.

## 4. Ответы MT5 → Sierra (опционально)
- Формат также JSON (`type`, `symbol`, `status`, `position_id`, `message`). Предусматривается для ack или ошибок. Если study пока не читает ответы, советник просто логирует их.
- Рекомендуемое подтверждение: `{"type":"ack","symbol":"MNQZ5","position_id":"...","status":"accepted","timestamp":"...","note":"order-id"}`.
- Ошибки: `{"type":"error","symbol":"MNQZ5","position_id":"...","code":451,"message":"Off quotes"}`. DLL возвращает размер записи; 0 → тайм‑аут.

## 5. Обработка ошибок и повторов
1. **Соединение**: советник пытается `SierraPipeConnect` при `InpAutoConnect=true`. Повтор каждые `InpPollIntervalMs` до успеха. Study после старта сразу создаёт именованный канал с открытой DACL и держит его постоянно открытым.
2. **Чтение**: код `-1` → ошибка WinAPI. Советник закрывает и переоткрывает канал; study фиксирует ошибку в `Logs/sierrastudymt5.log` и отображает её в строке статуса.
3. **Формат**: невалидный JSON → ответственность советника. Рекомендуется оборачивать парсер в `try` и отдавать `type="error"` c описанием.
4. **Версионирование**: несовпадение `version` → стороны логируют предупреждение. При критичном расхождении MT5 может игнорировать запись до обновления study/DLL.

## 6. API DLL (резюме)

```cpp
int  SierraPipeConnect(const char* pipe_name);   // 1 при успехе, -1 при ошибке
void SierraPipeClose();
int  SierraPipeWrite(const uint8_t* data, int size);      // количество записанных байт или -1
int  SierraPipeRead(uint8_t* buffer, int size, int timeout_ms); // байты или 0 при тайм‑ауте
```

DLL должна логировать в `Logs/SierraStudy.log` (через study) только при необходимости, чтобы не блокировать горячие циклы.

## 7. Сборка и развёртывание
1. `scripts/BuildSolution.ps1 -Configuration Release -Mt5DataDir <путь>` — единый сценарий, собирающий `SierraStudyMT5.dll`, `SierraStudyAdvisorBridgeMT5.dll`, прогоняющий тесты и компилирующий `SierraStudyAdvisor.ex5`.
2. DLL моста копируется в `MQL5\Libraries\SierraStudyAdvisorBridgeMT5.dll`, советник — в `MQL5\Experts\SierraStudy\SierraStudyAdvisor.ex5` и `out/mt5/Experts` для версионирования.
3. Перед hot-swap в Sierra Chart запустить `scripts/HotSwap.ps1 -Dll <путь к SierraStudyMT5.dll> -SierraDataDir %SIERRA_DATA_DIR%` или использовать `BuildAndSwap.ps1`.

## 8. Контроль изменений
- Любые изменения схемы JSON или API DLL фиксируются в этом файле и в `AGENTS.md`. Номер версии следует повышать (`version` в payload).
- При обновлении структуры нужно добавлять соответствующие тесты (Google Test для Core, интеграционный тест для советника) и пример в `examples/ascil_usage`.

## 9. Ссылки
- `docs/AdvisorBridge.md` — обзор архитектуры моста.
- `scripts/CompileAdvisor.ps1`, `scripts/BuildSolution.ps1` — автоматизация сборок.
- `projects/Advisor/src/SierraStudyAdvisor.mq5` — эталонная реализация клиентской стороны MT5.

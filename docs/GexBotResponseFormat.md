# Формат ответа GexBot API

## Основные поля
- `timestamp` (`number`) — Unix‑время генерации данных.
- `ticker` (`string`) — тикер запрошенного актива.
- `spot` (`number`) — текущая спот‑цена базового актива.
- `min_dte` (`number`) — DTE ближайшей экспирации.
- `sec_min_dte` (`number`) — DTE следующей экспирации.

## Ключевые уровни
- `major_positive` (`number`) — страйк с максимальной положительной экспозицией указанного грека.
- `major_negative` (`number`) — страйк с максимальной отрицательной экспозицией указанного грека.
- `major_long_gamma` (`number`) — страйк с максимальной длинной клиентской гамма‑экспозицией.
- `major_short_gamma` (`number`) — страйк с максимальной короткой клиентской гамма‑экспозицией.

## mini_contracts (array)
Массив элементов формата:
```
[strike, call_ivol, put_ivol, specified_greek, priors]
```
- `strike` (`number`) — цена страйка.
- `call_ivol` (`number`) — implied volatility колл, % годовых.
- `put_ivol` (`number`) — implied volatility пут, % годовых.
- `specified_greek` (`number`) — значение выбранного грека по страйку (единицы зависят от грека: gamma, delta, vanna, charm и т.д.). Знак: + длинные клиенты (дилеры короткие), − короткие клиенты.
- `priors` (`array`) — исторические значения `specified_greek` по lookback-интервалам:
  - index 0: текущее значение
  - 1: 1 мин назад
  - 2: 5 мин назад
  - 3: 10 мин назад
  - 4: 15 мин назад
  - 5: 30 мин назад

## Примечания
- Все поля приходят в JSON; парсим как YAML‑подмножество через RapidYAML.
- В отрисовке на графике отображаем основные поля, ключевые уровни и первые 3 элемента `mini_contracts`; полный ответ сохраняем в блоке `Raw:` для отладки.

## Как хранится в коде (Wrapper/study.cpp)
- `key_levels` — структура с полями `major_positive`, `major_negative`, `major_long_gamma`, `major_short_gamma` (double, NaN если отсутствуют).
- `mini_contracts` — `std::vector` элементов `{ strike, specified_greek }`, заполняется **всеми** элементами массива `mini_contracts` и сортируется по `strike` для дальнейшего отображения/расчётов. По умолчанию на график выводятся только первые 5 строк для компактности.
- Сервисные поля состояния: `last_poll_time`, `poll_interval`, `last_text` управляют расписанием опросов и текстом между запросами.

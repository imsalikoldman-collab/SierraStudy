# Спецификация **YML v1.5-min-obj**

Минимальный формат для планов по **NQ** и **ES**. Кастом-стади читает файл и рисует ровно две зоны (long/short) при наличии, одну flip-зону и вертикальный маркер времени генерации плана.

---

## 1) Что нового в v1.5
- Обязательное поле корня **`generated_at`** — *единственная* временная метка в файле.
- Зоны описываются отдельными объектами `zone_long` и `zone_short` — по одному на тикер, оба опциональны.
- В каждой зоне обязательно текстовое поле **`label`**.
- Диапазон инвалидации называется **`invalidation`**.
- Flip-зона по-прежнему одна (`flip`) или отсутствует.

---

## 2) Структура данных (миникарта)

### EBNF
```
Root ::= {
  v: "1.5-min-obj",
  generated_at: datetime,
  NQ: Instrument,
  ES: Instrument
}

Instrument ::= {
  zone_long?: Zone,
  zone_short?: Zone,
  flip?: Flip
}

Zone ::= {
  label: string,
  range: [number, number],
  sl: number,
  tp1: number,
  tp2: number,
  invalidation?: [number, number]
}

Flip ::= {
  range: [number, number],
  label: string
}
```

### Таблица полей (сводно)
| Узел        | Поле           | Тип                   | Обяз. | Комментарий |
|-------------|----------------|-----------------------|:----:|-------------|
| Root        | `v`            | string                | ✓    | Ровно `"1.5-min-obj"` |
| Root        | `generated_at` | datetime (ISO-8601)   | ✓    | Единственная метка времени |
| Root        | `NQ`, `ES`     | Instrument            | ✓    | Две секции по тикерам |
| Instrument  | `zone_long`    | `Zone`                | —    | Наиболее актуальная LONG-зона (0..1) |
| Instrument  | `zone_short`   | `Zone`                | —    | SHORT-зона (0..1) |
| Instrument  | `flip`         | `Flip`                | —    | Единственный flip-диапазон |
| Zone        | `label`        | string                | ✓    | Текст подсказки/названия зоны |
| Zone        | `range`        | `[number, number]`    | ✓    | Диапазон цены (`low < high`) |
| Zone        | `sl`           | number                | ✓    | Стоп-лосс |
| Zone        | `tp1`,`tp2`    | number                | ✓    | Тейки |
| Zone        | `invalidation` | `[number, number]`    | —    | Диапазон инвалидации |
| Flip        | `range`        | `[number, number]`    | ✓    | Диапазон flip-зоны |
| Flip        | `label`        | string                | ✓    | Подпись справа |

---

## 3) Правила и валидация
- `v == "1.5-min-obj"`, `generated_at` обязателен и указывается в формате ISO-8601 c TZ.
- В каждой секции допускаются максимум одна `zone_long` и одна `zone_short`. Любая из них может отсутствовать.
- Поле `label` обязательно для каждой зоны; пустые значения запрещены.
- Диапазоны (`range`, `invalidation`, `flip.range`) задаются как `[low, high]`, где `low < high`.
- `sl`, `tp1`, `tp2` должны присутствовать в каждой зоне.
- Неизвестные поля игнорируются.

**Идентичность зоны:** определяется парой (тип зоны, диапазон). При изменении `range` старая зона удаляется и создаётся новая; при изменении остальных полей бэкенд обновляет существующую.

---

## 4) Правила YAML (строгий профиль)
- YAML 1.2, UTF-8, отступ **2 пробела**, без табов и якорей (`&`, `*`, `<<`).
- Числа — десятичные; строки с спецсимволами (`<`, `>`, `:` и т.п.) берите в кавычки.
- Ключи только ASCII `[A-Za-z0-9._-]`.

---

## 5) Контракт отображения (study)
- `zone_long` и `zone_short` → полупрозрачные прямоугольники на диапазоне `range`, тянущиеся по времени вправо; цвет зависит от типа (buy — зелёный, sell — красный).
- `sl`, `tp1`, `tp2` → горизонтальные лучи (`DRAWING_HORIZONTAL_RAY`) с началом в `generated_at` и обязательной подписью «Имя + значение».
- `invalidation` → дополнительный прямоугольник поверх основной зоны с пониженной прозрачностью.
- `flip` → один нейтральный прямоугольник + подпись `label` справа по центру.
- `generated_at` → вертикальная линия (маркер) на указанном времени, подпись вида `Plan generated yyyy-mm-dd hh:mm America/New_York`.
- Слои (снизу → вверх): `invalidation` → `zone` → `flip` → `lines` → `labels` → `generated_at`.
- Цены округляются к `TickSize`. Тикер чарта сопоставляется по префиксу (NQ↔NQ/MNQ, ES↔ES/MES).

---

## 6) Канонический пример
```yaml
v: "1.5-min-obj"
generated_at: "2025-10-24T06:45:00Z"

NQ:
  zone_long:
    label: "NQ long 24500-24900"
    range: [24500.0, 24900.0]
    sl: 24400.0
    tp1: 25000.0
    tp2: 25100.0
    invalidation: [24300.0, 24400.0]
  zone_short:
    label: "NQ short 25250-25320"
    range: [25250.0, 25320.0]
    sl: 25360.0
    tp1: 25210.0
    tp2: 25150.0
  flip:
    range: [24700.0, 24800.0]
    label: "Bias change"

ES:
  zone_long: null
  zone_short:
    label: "ES short 4870-4885"
    range: [4870.0, 4885.0]
    sl: 4892.0
    tp1: 4860.0
    tp2: 4850.0
```

---

## 7) Пустой шаблон
```yaml
v: "1.5-min-obj"
generated_at: "YYYY-MM-DDThh:mm:ss±hh:mm"

NQ: { zone_long: null, zone_short: null, flip: null }
ES: { zone_long: null, zone_short: null, flip: null }
```

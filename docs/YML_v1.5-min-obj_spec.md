# Спецификация **YML v1.5-min-obj**

Минимальный человекочитаемый формат для планов по **NQ** и **ES**. Этот файл читает кастом‑study (ACSIL) и однозначно рисует зоны, уровни, единственную flip‑зону и вертикальную метку времени генерации плана.

---

## 1) Что нового в v1.5
- Добавлено **обязательное** поле корня **`generated_at`** — *единственная* временная метка в файле.  
- Study рисует **вертикальную линию** (маркер) на этом времени с подписью вида *“Plan generated …”*.
- Остальное соответствует v1.4: `zones` (buy/sell), `flip` (ровно один или отсутствует).

---

## 2) Структура данных (миникарта)

### EBNF
```
Root ::= {
  v: "1.5-min-obj",
  generated_at: datetime,     // ISO-8601 с часовым поясом
  NQ: Instrument,
  ES: Instrument
}

Instrument ::= {
  zones: sequence(Zone),
  flip?: Flip                 // 0..1
}

Zone ::= {
  direction: "buy" | "sell",
  range: [number, number],    // low < high
  sl: number,
  tp1: number,
  tp2: number,
  invalid?: [number, number],
  notes?: {                   // подписи (опц.)
    range?, sl?, tp1?, tp2?, invalid?: string
  }
}

Flip ::= {
  range: [number, number],    // low < high
  label: string               // подпись справа
}
```

### Таблица полей (сводно)
| Узел        | Поле           | Тип                   | Обяз. | Комментарий |
|-------------|----------------|-----------------------|:----:|-------------|
| Root        | `v`            | string                | ✓    | Ровно `"1.5-min-obj"` |
| Root        | `generated_at` | datetime (ISO-8601)   | ✓    | Единственная метка времени в файле |
| Root        | `NQ`, `ES`     | Instrument            | ✓    | Две секции по инструментам |
| Instrument  | `zones`        | `Zone[]`              | ✓    | Список зон (0..N) |
| Instrument  | `flip`         | `Flip`                | —    | Единственный объект или отсутствует |
| Zone        | `direction`    | `"buy"` \| `"sell"`   | ✓    | Направление зоны |
| Zone        | `range`        | `[number, number]`    | ✓    | Диапазон цены (`low < high`) |
| Zone        | `sl`           | number                | ✓    | Стоп‑лосс |
| Zone        | `tp1`,`tp2`    | number                | ✓    | Тейки |
| Zone        | `invalid`      | `[number, number]`    | —    | Диапазон инвалидации |
| Zone        | `notes`        | map<string,string>    | —    | Подписи: `range/sl/tp1/tp2/invalid` |
| Flip        | `range`        | `[number, number]`    | ✓    | Диапазон flip‑зоны |
| Flip        | `label`        | string                | ✓    | Текст справа |

---

## 3) Правила и валидация
- `v` обязательно и равно `"1.5-min-obj"`.
- `generated_at` обязателен, **только** в корне; формат ISO‑8601 с TZ (пример: `"2025-10-23T08:45:00Z"`).
- В секциях `NQ` и `ES`:
  - `zones` — список объектов `Zone` (≥ 0).
  - `flip` — **необязателен**, но если есть — **ровно один**.
- Обязательные поля `Zone`: `direction`, `range`, `sl`, `tp1`, `tp2`. При отсутствии любого — зона пропускается.
- Диапазоны всегда `low < high`. `direction` ∈ {`buy`, `sell`}. `notes` — опциональные строки по ключам `range/sl/tp1/tp2/invalid`.
- Неизвестные поля игнорируются.

**Идентичность зоны (без ID):** ключ = `(direction, range.low, range.high)`  
— Изменился `range` → **новая** зона (старую удалить, новую создать).  
— `range` прежний, изменились `sl/tp*/invalid/notes` → зона **обновляется**.

**Рекомендации (нестрого):**  
- Для `buy`: `sl ≤ range.low`, `tp1 ≥ range.high`, `tp2 ≥ tp1`.  
- Для `sell`: `sl ≥ range.high`, `tp1 ≤ range.low`, `tp2 ≤ tp1`.

---

## 4) Правила YAML (строгий профиль)
- YAML 1.2, UTF‑8, отступ **2 пробела**, без табов/якорей (`&`, `*`, `<<`).  
- Числа — десятичные; строки, содержащие `<`, `>`, `:`, оборачивать в кавычки.  
- Ключи без пробелов: `[A-Za-z0-9._-]`.

---

## 5) Контракт отображения (study)
- **`zone.range`** → прямоугольник на ценовой шкале, тянется по времени. Buy — зелёный, Sell — красный (полупрозрачные).
- **`sl`, `tp1`, `tp2`** → горизонтальные линии; подписи справа по уровню.
- **`invalid`** → дополнительный прямоугольник низкой прозрачности (обычно красный).
- **`flip`** → один нейтральный прямоугольник (например, жёлтый) + **`label`** справа по центру диапазона.
- **`generated_at`** → вертикальная линия (маркер) на указанном времени, подпись вида `Plan generated yyyy-mm-dd hh:mm TZ`.
- Слои (снизу → вверх): `invalid` → `zone` → `flip` → `lines (sl/tp)` → `labels` → `generated_at` (поверх).  
- Округление цен к `TickSize`. Сопоставление секций с символами чарта — по префиксам (NQ→NQ/MNQ, ES→ES/MES).

**ACSIL-подсказка (маркер времени через `UseTool`):**
```cpp
s_UseTool Tool; Tool.Clear();
Tool.ChartNumber = sc.ChartNumber;
Tool.DrawingType = DRAWING_VERTICALLINE;
Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
Tool.BeginDateTime = GeneratedAtSCDateTime;   // конвертируйте ISO-8601 → SCDateTime
Tool.LineWidth   = 2;
Tool.Color       = RGB(255, 204, 0);          // пример цвета
Tool.Text        = "Plan generated 2025-10-23 08:45Z";
int& vlineLN = sc.GetPersistentInt(10001);    // храните LineNumber
Tool.LineNumber  = vlineLN;
sc.UseTool(Tool);
vlineLN = Tool.LineNumber;
```

---

## 6) Канонический пример
```yaml
v: "1.5-min-obj"
generated_at: "2025-10-23T08:45:00Z"

NQ:
  zones:
    - direction: buy
      range: [17120.0, 17170.0]
      sl: 17110.0
      tp1: 17190.0
      tp2: 17230.0
      invalid: [17100.0, 17110.0]
      notes:
        range: "NQ buy zone 17120–17170"
        sl:    "Stop-loss 17110"
        tp1:   "TP1 17190"
        tp2:   "TP2 17230"
        invalid: "Invalidation 17100–17110"
    - direction: sell
      range: [17280.0, 17320.0]
      sl: 17330.0
      tp1: 17240.0
      tp2: 17200.0
      invalid: [17320.0, 17340.0]
      notes:
        range: "NQ sell zone 17280–17320"
        sl:    "Stop-loss 17330"
        tp1:   "TP1 17240"
        tp2:   "TP2 17200"
        invalid: "Invalidation 17320–17340"

  flip:
    range: [17175.0, 17185.0]
    label: "Flip: bias turns short below 17175"

ES:
  zones:
    - direction: buy
      range: [4850.0, 4862.0]
      sl: 4849.0
      tp1: 4868.0
      tp2: 4878.0
      invalid: [4846.0, 4849.5]
      notes:
        range: "ES buy zone 4850–4862"
        sl:    "Stop-loss 4849.0"
        tp1:   "TP1 4868.0"
        tp2:   "TP2 4878.0"
        invalid: "Invalidation 4846.0–4849.5"
    - direction: sell
      range: [4888.0, 4896.0]
      sl: 4898.0
      tp1: 4880.0
      tp2: 4872.0
      invalid: [4896.0, 4902.0]
      notes:
        range: "ES sell zone 4888–4896"
        sl:    "Stop-loss 4898.0"
        tp1:   "TP1 4880.0"
        tp2:   "TP2 4872.0"
        invalid: "Invalidation 4896.0–4902.0"

  flip:
    range: [4864.0, 4869.0]
    label: "Flip: intraday short bias below 4864"
```

---

## 7) Пустой шаблон
```yaml
v: "1.5-min-obj"
generated_at: "YYYY-MM-DDThh:mm:ss±hh:mm"

NQ: { zones: [], flip: null }
ES: { zones: [], flip: null }
```

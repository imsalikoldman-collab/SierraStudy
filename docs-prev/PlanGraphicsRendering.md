# Графическая отрисовка плана

*Версия:* 1.0  
*Дата:* 2025‑10‑30  

Документ фиксирует текущую последовательность создания графических объектов Sierra Chart для визуализации торгового плана. При изменении логики обязательно обновляйте ссылки на исходный код, чтобы заметки оставались актуальными.

## Основные этапы

1. **Очистка предыдущих объектов**  
   Используется `DeleteExistingDrawings`, которая проходит по `line_numbers` и удаляет рисунки через `sc.DeleteACSChartDrawing` (`projects/Wrapper/src/supportFunction.cpp:189`). Это гарантирует отсутствие дубликатов при каждом обновлении плана.

2. **Нормализация координат**  
   Размер тикa (`sc.TickSize`) и функции `round_to_tick`/`format_price` приводят все цены к сетке графика (`projects/Wrapper/src/supportFunction.cpp:279`). Горизонтальные координаты вычисляются из `start_time`/`end_time`, определяемых в `RenderPlanGraphics` (`projects/Wrapper/src/study.cpp:462`, `projects/Wrapper/src/study.cpp:470`).

3. **Прямоугольники зон**  
   Для каждой `zone_long`/`zone_short` создаётся `DRAWING_RECTANGLEHIGHLIGHT` с заполнением по диапазону цен, а подпись зоны добавляется в список `LabelInfo` (`projects/Wrapper/src/supportFunction.cpp:371`, `projects/Wrapper/src/supportFunction.cpp:393`).

4. **Flip-зона**  
   При наличии `instrument.flip` формируется аналогичный прямоугольник и текстовая подпись на правом краю (`projects/Wrapper/src/supportFunction.cpp:405`, `projects/Wrapper/src/supportFunction.cpp:425`).

5. **Линии уровней SL/TP**  
   Лямбда `add_level` строит горизонтальные лучи (`DRAWING_HORIZONTAL_RAY`) и сопутствующие подписи (`SL`, `TP1`, `TP2`) (`projects/Wrapper/src/supportFunction.cpp:438`, `projects/Wrapper/src/supportFunction.cpp:454`).

6. **Метка времени генерации**  
   Структура `LabelInfo` с флажком `use_relative_vertical` размещает текстовую подпись на 98 % высоты окна, а вертикальная линия (`DRAWING_VERTICALLINE`) фиксирует момент генерации (`projects/Wrapper/src/supportFunction.cpp:472`, `projects/Wrapper/src/supportFunction.cpp:508`).

## Координаты текстов уровней

- **Время:** `LabelInfo::date_time` приравнивается к `end_time`, поэтому подписи оказываются справа от активного диапазона (`projects/Wrapper/src/supportFunction.cpp:455`).
- **Цена:** значения округляются через `round_to_tick` и пишутся в `LabelInfo::price`, после чего попадают в `BeginValue` объекта `DRAWING_TEXT` (`projects/Wrapper/src/supportFunction.cpp:439`, `projects/Wrapper/src/supportFunction.cpp:499`).
- **Относительный режим:** единственная метка, использующая относительные проценты по вертикали, — подпись даты генерации (`projects/Wrapper/src/supportFunction.cpp:474`).

## Связанные компоненты

- `RenderInstrumentPlanGraphics` — основной конвейер построения графики (`projects/Wrapper/src/supportFunction.cpp:268`).
- `RenderPlanGraphics` — вычисляет границы по времени и вызывает отрисовщик (`projects/Wrapper/src/study.cpp:446`).
- `ConvertIso8601ToSCDateTime` — переводит ISO‑8601 в координаты Sierra Chart (`projects/Wrapper/src/supportFunction.cpp:201`).

## Требования к сопровождению

- При изменении типов фигур, стратегии вычисления координат или оформления текстовых подписи обновляйте соответствующие разделы документа.
- Проверяйте, что ссылки на исходный код указывают на актуальные строки после правок.
- Если появляются новые элементы графики (например, дополнительные уровни), описывайте их в разделе «Основные этапы» и добавляйте ссылки в «Связанные компоненты».

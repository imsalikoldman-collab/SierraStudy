# Практические заметки по работе с Sierra Chart (ACSIL)

Этот документ фиксирует проверенные приёмы, которые помогли избежать типичных ошибок при работе с временными координатами и графическими объектами в Sierra Chart.

---

## 1. Создание временных меток `SCDateTime`
- В ACSIL **нет** метода `sc.DateTimeToSCDateTime`. Используйте объект `SCDateTime` и его методы.
- Стандартный способ задать конкретный момент времени:
  ```cpp
  SCDateTime ts;
  ts.SetDateTimeYMDHMS(2025, 10, 26, 18, 0, 0);  // ГГГГ, ММ, ДД, часы, минуты, секунды
  ```
- Когда нужно обработать строку ISO-8601 (например, из YAML), сначала парсим её вручную, после чего вызываем `SetDateTimeYMDHMS`. В нашем коде это делает `ConvertIso8601ToSCDateTime`.

## 2. Горизонтальная линия (сплошной сегмент)
```cpp
s_UseTool tool;
tool.Clear();
tool.ChartNumber = sc.ChartNumber;
tool.DrawingType = DRAWING_LINE;
tool.AddMethod = UTAM_ADD_OR_ADJUST;
tool.AddAsUserDrawnDrawing = 0;
tool.LineStyle = LINESTYLE_SOLID;
tool.LineWidth = 2;
tool.Color = RGB(255, 0, 0);

SCDateTime start_ts; start_ts.SetDateTimeYMDHMS(2025, 10, 26, 18, 0, 0);
SCDateTime end_ts;   end_ts.SetDateTimeYMDHMS(2025, 10, 28, 18, 0, 0);
tool.BeginDateTime = start_ts;
tool.EndDateTime   = end_ts;
tool.BeginValue = tool.EndValue = 25700.0f;

sc.UseTool(tool);
```
- Если линия уже существует и хочется обновлять её, перед вызовом `UseTool` достаточно выставить `tool.LineNumber = existing_line_number`.
- После получения `LineNumber` сохраните его в persistent‑слоте и удалите через `sc.DeleteACSChartDrawing`, когда линия больше не нужна.

## 3. Горизонтальный луч (extends вправо)
```cpp
s_UseTool ray;
ray.Clear();
ray.ChartNumber = sc.ChartNumber;
ray.DrawingType = DRAWING_HORIZONTAL_RAY;
ray.LineStyle = LINESTYLE_DASH;
ray.LineWidth = 1;
ray.ExtendRight = 1;
ray.BeginDateTime = start_ts;
ray.BeginValue = 25700.0f;
ray.EndDateTime = start_ts + (1.0 / (24.0 * 60.0 * 60.0));  // небольшое смещение на 1 секунду
ray.EndValue = ray.BeginValue;
sc.UseTool(ray);
```
- Небольшой шаг по времени нужен, чтобы ACSIL принял начальную координату; луч всё равно будет тянуться вправо благодаря `ExtendRight = 1`.

## 4. Обновление и очистка
- Все номера линий/лучей обязательно складывайте в `std::vector<int>` и очищайте через `sc.DeleteACSChartDrawing`.
- При завершении study (`sc.LastCallToFunction`) удалите все созданные объекты, иначе они останутся на графике.

## 5. Отладочный пример
Функция `RenderStandaloneDebugLine` (см. `projects/Wrapper/src/supportFunction.cpp`) демонстрирует полный цикл:
1. Формирование времён через `SetDateTimeYMDHMS`.
2. Построение линии `DRAWING_LINE`.
3. Сохранение `LineNumber` в persistent-памяти и удаление при выгрузке.

Используйте её как шаблон для быстрого создания тестовых объектов.

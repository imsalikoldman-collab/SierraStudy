#pragma once

#include "SierraChart.h"

#include <string>
#include <vector>

#include "sierra/core/plan.hpp"

namespace sierra::acsil {

/**
 * @brief Пишет информацию о загрузке DLL в Message Log Sierra Chart.
 * @param sc Интерфейс study, предоставляемый Sierra Chart.
 * @return Ничего не возвращает.
 * @note Вызывайте один раз при инициализации, чтобы избежать повторов в логе.
 * @warning Не используйте во время SetDefaults — лог ещё недоступен.
 */
void LogDllStartup(SCStudyInterfaceRef sc);

/**
 * @brief Конвертирует строку ISO-8601 в представление времени Sierra Chart.
 * @param iso8601 Строка вида YYYY-MM-DDThh:mm:ssZ.
 * @param fallback Значение, которое вернётся при ошибке парсинга.
 * @return Значение SCDateTime (количество дней с 1899-12-30).
 * @note Конвертация выполняется в UTC, с учётом суточной доли.
 * @warning При невозможности разбора возвращается fallback без выброса исключений.
 */
SCDateTime ConvertIso8601ToSCDateTime(const std::string& iso8601, SCDateTime fallback);

/**
 * @brief Создаёт графические объекты, представляющие зоны плана и уровни.
 * @param sc Интерфейс study для доступа к графику.
 * @param instrument План конкретного инструмента, загруженный из YAML.
 * @param start_time Время начала отображения (обычно время генерации плана).
 * @param end_time Время окончания (правый край графика).
 * @param line_numbers Контейнер с номерами созданных объектов (обновляется функцией).
 * @param generated_at_iso8601 Строковое представление метки времени генерации плана.
 * @note Перед построением функция удаляет объекты, перечисленные в line_numbers.
 * @warning Убедитесь, что line_numbers принадлежит текущему инстансу study.
 */
void RenderInstrumentPlanGraphics(SCStudyGraphRef sc,
                                  const sierra::core::InstrumentPlan& instrument,
                                  SCDateTime start_time,
                                  SCDateTime end_time,
                                  std::vector<int>& line_numbers,
                                  const std::string& generated_at_iso8601);

}  // namespace sierra::acsil

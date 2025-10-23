#pragma once

#include "SierraChart.h"

namespace sierra::acsil {

/**
 * @brief Фиксирует в Message Log факт загрузки DLL SierraStudy.
 * @param sc Интерфейс исследования, предоставляемый Sierra Chart.
 * @return void Функция не возвращает значение.
 * @note Сообщение выводится только один раз за сессию, чтобы избежать повторов.
 * @warning Вызывайте функцию из кода Sierra Chart (например, в обработке SetDefaults).
 */
void LogDllStartup(SCStudyInterfaceRef sc);

}  // namespace sierra::acsil

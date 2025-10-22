#pragma once

#include "SierraChart.h"

namespace sierra::acsil {

/**
 * @brief Выводит в Message Log информацию о запуске DLL.
 * @param sc Контекст исследования, предоставленный Sierra Chart.
 * @return void.
 * @note Функция записывает пустую строку, разделитель и сообщение о загрузке DLL.
 * @warning Предполагается, что вызывающий код обеспечивает корректную последовательность вызовов (например, в SetDefaults).
 */
void LogDllStartup(SCStudyInterfaceRef sc);

}  // namespace sierra::acsil

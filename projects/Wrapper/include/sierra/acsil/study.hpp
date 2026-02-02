#pragma once

#pragma warning(push, 0)
#include "SierraChart.h"
#pragma warning(pop)

/// @brief Заглушка точки входа (SCSFExport) для пользовательского исследования.
/// @param sc Интерфейс ACSIL, предоставляемый Sierra Chart при каждом вызове.
/// @return void.
/// @note Декларация нужна, чтобы её могли видеть study.cpp и потенциальные будущие модули обёртки.
/// @warning Имя и сигнатура должны совпадать с реализацией; логика пока отсутствует.
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc);

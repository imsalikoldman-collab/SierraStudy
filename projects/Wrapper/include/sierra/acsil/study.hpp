#pragma once

#pragma warning(push, 0)
#include "SierraChart.h"
#pragma warning(pop)

/// @brief Точка входа (SCSFExport) исследования GexBot Poller.
/// @param sc Интерфейс ACSIL, предоставляемый Sierra Chart при каждом вызове.
/// @return void.
/// @note Декларация нужна, чтобы её могли видеть study.cpp и потенциальные будущие модули обёртки.
/// @warning Имя и сигнатура должны совпадать с реализацией; любое расхождение приведёт к отсутствию study в списке.
SCSFExport scsf_SierraStudyGexBotPoller(SCStudyGraphRef sc);

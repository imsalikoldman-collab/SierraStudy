#pragma once

#include "SierraChart.h"

/// @brief Точка входа для исследования, которую ищет Sierra Chart при загрузке DLL.
/// @param sc Интерфейс ACSIL, предоставляемый Sierra Chart при каждом вызове.
/// @return void.
/// @note Декларацию выносим в заголовок, чтобы её могли видеть study.cpp и потенциальные другие модули обёртки.
/// @warning Убедитесь, что сигнатура и имя полностью совпадают с экспортом в реализации.
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc);

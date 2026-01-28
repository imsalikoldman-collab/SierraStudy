#pragma once

#include "SierraChart.h"

/// @brief Точка входа для исследования, которую ищет Sierra Chart при загрузке DLL.
/// @param sc Интерфейс ACSIL, предоставляемый Sierra Chart при каждом вызове.
/// @return void.
/// @note Декларацию выносим в заголовок, чтобы её могли видеть study.cpp и потенциальные другие модули обёртки.
/// @warning Убедитесь, что сигнатура и имя полностью совпадают с экспортом в реализации.
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc);

/// @brief Study «Gexbot Gamma Levels» — запрашивает gamma-профиль через HTTP и рисует уровни.
/// @param sc Контекст Sierra Chart для текущего исследования.
/// @return void.
/// @note Реализация расположена в gexbot_gamma_levels.cpp, экспорт имя должно совпадать.
/// @warning Требуется валидный API key и поддерживаемый тикер чарта (ES/MES → SPX, NQ/MNQ → NDX).
SCSFExport scsf_GexbotGammaLevels(SCStudyGraphRef sc);

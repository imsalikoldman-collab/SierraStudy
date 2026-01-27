#pragma once

#include "SierraChart.h"

/**
 * @brief ACSIL-обёртка для построения уровней SpotGamma на графике Sierra Chart.
 * @param sc Контекст исследования, предоставляемый Sierra Chart.
 * @return void Функция не возвращает значение.
 * @note Поддерживает парсинг одной строки SpotGamma, выбор секции по символу ES/NQ (и микро ES/NQ), отрисовку горизонтальных линий и легенды.
 * @warning Требуется корректно заполненная строка SpotGamma согласно SG-ROW-FMT-001; при ошибках данные не выводятся, линии очищаются.
 */
SCSFExport scsf_SpotGammaLevels(SCStudyGraphRef sc);

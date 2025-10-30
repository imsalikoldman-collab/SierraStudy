#pragma once

#include <string>

#include "sierra/core/plan.hpp"

namespace sierra::core {

/**
 * @brief Формирует текстовую таблицу с данными торгового плана для отображения в ACSIL.
 * @param plan Торговый план после успешного парсинга YAML.
 * @param symbol_filter Символ чарта (для выборки соответствующего тикера).
 * @return std::string Готовый многострочный текст таблицы.
 * @note Таблица включает заголовок, дату/время генерации (America/New_York) и строки по тикеру.
 * @warning Текст рассчитан на моноширинный шрифт; используйте его при выводе в ACSIL.
 */
std::string FormatPlanAsTable(const StudyPlan& plan, const std::string& symbol_filter);

}  // namespace sierra::core


#pragma once

#include <string>

#include "sierra/core/plan.hpp"

namespace sierra::core {

/**
 * @brief Формирует текстовую таблицу с данными торгового плана для отображения в ACSIL.
 * @param plan Торговый план после успешного парсинга YAML.
 * @return std::string Готовый многострочный текст таблицы.
 * @note Таблица включает заголовок, время генерации и строки по тикерам с зонами и flip.
 * @warning Текст рассчитан на моноширинный шрифт; используйте его при выводе в ACSIL.
 */
std::string FormatPlanAsTable(const StudyPlan& plan);

}  // namespace sierra::core


#pragma once

#include <filesystem>

#include "sierra/core/plan.hpp"

namespace sierra::core {

/**
 * @brief Загружает торговый план из YAML-файла по спецификации v1.5-min-obj.
 * @param path Путь к файлу плана.
 * @return PlanLoadResult Итог загрузки: заполненный план или описание ошибки.
 * @note Функция не выбрасывает исключения — ошибки возвращаются в error_message.
 * @warning При невозможности чтения или несоответствии схемы plan будет пустым.
 */
PlanLoadResult LoadStudyPlanFromFile(const std::filesystem::path& path);

}  // namespace sierra::core


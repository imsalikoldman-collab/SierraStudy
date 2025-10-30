#include "sierra/core/yaml_plan_loader.hpp"

#include <gtest/gtest.h>

#include <filesystem>

/**
 * @brief Проверяет корректную загрузку реального YAML-плана.
 * @note Использует файл из test_files для интеграционного сценария.
 * @warning Пути рассчитываются относительно рабочего каталога тестов.
 */
TEST(YamlPlanLoaderTest, LoadsValidPlan) {
  const auto path = std::filesystem::current_path() / "test_files" / "nq_intraday_flip_example.yaml";
  const auto result = sierra::core::LoadStudyPlanFromFile(path);
  ASSERT_TRUE(result.success()) << result.error_message;

  const auto& plan = result.plan;
  EXPECT_EQ(plan.version, "1.5-min-obj");
  EXPECT_EQ(plan.generated_at_iso8601, "2025-10-24T06:45:00Z");
  ASSERT_EQ(plan.instruments.size(), 2u);
  const auto nq_it = plan.instruments.find("NQ");
  ASSERT_NE(nq_it, plan.instruments.end());
  const auto& nq = nq_it->second;
  ASSERT_TRUE(nq.zone_long.has_value());
  const auto& zone = nq.zone_long.value();
  EXPECT_EQ(zone.direction, sierra::core::ZoneDirection::kBuy);
  EXPECT_EQ(zone.label, "NQ long 24500-24900");
  EXPECT_DOUBLE_EQ(zone.range.low, 24500.0);
  ASSERT_TRUE(zone.invalidation.has_value());
  EXPECT_DOUBLE_EQ(zone.invalidation->low, 24300.0);
  EXPECT_TRUE(nq.flip.has_value());
  EXPECT_DOUBLE_EQ(nq.flip->range.low, 24700.0);
  EXPECT_EQ(nq.flip->label, "Bias change");

  const auto es_it = plan.instruments.find("ES");
  ASSERT_NE(es_it, plan.instruments.end());
  EXPECT_FALSE(es_it->second.zone_long.has_value());
  EXPECT_FALSE(es_it->second.zone_short.has_value());
  EXPECT_FALSE(es_it->second.flip.has_value());
}

/**
 * @brief Проверяет реакцию загрузчика на отсутствующий файл.
 * @note Должна вернуться осмысленная ошибка без исключений.
 * @warning Путь формируется заведомо несуществующий.
 */
TEST(YamlPlanLoaderTest, ReportsMissingFile) {
  const auto path = std::filesystem::current_path() / "test_files" / "missing_plan.yaml";
  const auto result = sierra::core::LoadStudyPlanFromFile(path);
  EXPECT_FALSE(result.success());
  EXPECT_NE(result.error_message.find("не найден"), std::string::npos);
}

/**
 * @brief Проверяет обработку некорректной структуры YAML.
 * @note Отсутствует обязательное поле label у zone_long.
 * @warning Ошибка должна содержать идентификатор проблемного инструмента.
 */
TEST(YamlPlanLoaderTest, ReportsInvalidStructure) {
  const auto path = std::filesystem::current_path() / "projects" / "Tests" / "data" /
                    "invalid_plan_missing_label.yaml";
  const auto result = sierra::core::LoadStudyPlanFromFile(path);
  EXPECT_FALSE(result.success());
  EXPECT_NE(result.error_message.find("label"), std::string::npos);
}

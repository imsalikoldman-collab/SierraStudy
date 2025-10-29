#include "sierra/core/plan_formatter.hpp"

#include <gtest/gtest.h>

/**
 * @brief Проверяет формирование таблицы с зонами и flip.
 * @param  test  Контекст Google Test.
 * @return void  Результат проверки.
 * @note Используем фиксированные значения, чтобы проверить формат.
 * @warning При изменении формата таблицы обновите этот тест.
 */
TEST(PlanFormatterTest, BuildsTableWithZones) {
  sierra::core::StudyPlan plan{};
  plan.version = "1.5-min-obj";
  plan.generated_at_iso8601 = "2025-10-24T06:45:00Z";

  sierra::core::InstrumentPlan instrument{};
  sierra::core::Zone zone{};
  zone.direction = sierra::core::ZoneDirection::kBuy;
  zone.range = {24500.0, 24900.0};
  zone.sl = 24400.0;
  zone.tp1 = 25000.0;
  zone.tp2 = 25100.0;
  instrument.zones.push_back(zone);

  sierra::core::FlipZone flip{};
  flip.range = {24700.0, 24800.0};
  flip.label = "Flip zone";
  instrument.flip = flip;

  plan.instruments.emplace("NQ", instrument);

  const auto table = sierra::core::FormatPlanAsTable(plan);

  EXPECT_NE(table.find("Plan version: 1.5-min-obj"), std::string::npos);
  EXPECT_NE(table.find("Generated at: 2025-10-24T06:45:00Z"), std::string::npos);
  EXPECT_NE(table.find("Ticker: NQ"), std::string::npos);
  EXPECT_NE(table.find("Zone 1 BUY 24500.00-24900.00 | SL 24400.00 | TP1 25000.00 | TP2 25100.00"),
            std::string::npos);
  EXPECT_NE(table.find("Flip: 24700.00-24800.00"), std::string::npos);
}

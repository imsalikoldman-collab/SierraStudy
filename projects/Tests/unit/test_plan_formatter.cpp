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
  sierra::core::Zone buy_zone{};
  buy_zone.direction = sierra::core::ZoneDirection::kBuy;
  buy_zone.label = "NQ long 24500-24900";
  buy_zone.range = {24500.0, 24900.0};
  buy_zone.sl = 24400.0;
  buy_zone.tp1 = 25000.0;
  buy_zone.tp2 = 25100.0;
  buy_zone.invalidation = sierra::core::PriceRange{24350.0, 24380.0};
  instrument.zone_long = buy_zone;

  sierra::core::Zone sell_zone{};
  sell_zone.direction = sierra::core::ZoneDirection::kSell;
  sell_zone.label = "NQ short 25250-25320";
  sell_zone.range = {25250.0, 25320.0};
  sell_zone.sl = 25360.0;
  sell_zone.tp1 = 25210.0;
  sell_zone.tp2 = 25150.0;
  instrument.zone_short = sell_zone;

  sierra::core::FlipZone flip{};
  flip.range = {24700.0, 24800.0};
  flip.label = "Bias change";
  instrument.flip = flip;

  plan.instruments.emplace("NQ", instrument);

  const auto table = sierra::core::FormatPlanAsTable(plan, "NQZ5");

  EXPECT_NE(table.find("Plan version: 1.5-min-obj"), std::string::npos);
  EXPECT_NE(table.find("Generated date (NY): 2025-10-24"), std::string::npos);
  EXPECT_NE(table.find("Generated time (NY): 02:45:00 America/New_York"), std::string::npos);
  EXPECT_NE(table.find("Ticker: NQ"), std::string::npos);
  EXPECT_NE(
      table.find("  Long: NQ long 24500-24900 | Range 24500.00-24900.00 | SL 24400.00 | TP1 25000.00 | TP2 25100.00"),
      std::string::npos);
  EXPECT_NE(table.find("    Invalidation: 24350.00-24380.00"), std::string::npos);
  EXPECT_NE(
      table.find("  Short: NQ short 25250-25320 | Range 25250.00-25320.00 | SL 25360.00 | TP1 25210.00 | TP2 25150.00"),
      std::string::npos);
  EXPECT_NE(table.find("  Flip: Bias change | Range 24700.00-24800.00"), std::string::npos);
}

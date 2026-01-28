/**
 * @brief Тесты парсера Gexbot Options Profile Greeks и маппинга тикеров.
 * @note Проверяем фильтрацию положительной гаммы, сортировку и обрезание top-N.
 * @warning Тесты используют минимальный JSON; они валятся при изменении контракта парсера.
 */

#include "sierra/core/gexbot_gamma.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using sierra::core::GammaLevel;
using sierra::core::GammaResponse;
using sierra::core::MapChartSymbolToGexbotTicker;
using sierra::core::ParseGexbotGammaResponse;

const char* kSampleJson = R"json(
{
  "timestamp": 1700000000,
  "ticker": "SPX",
  "spot": 4800.5,
  "mini_contracts": [
    [4800, null, null, -12.0, [1,2]],
    [4810, 0.1, 0.2, 15.5, [0]],
    [4820, 0.1, 0.2, 5.0, [0], null, null],
    [4830, 0.1, 0.2, null, null],
    [4840, 0.1, 0.2, 25.0]
  ]
}
)json";

TEST(GexbotTickerMap, MapsKnownFutures) {
  EXPECT_EQ(MapChartSymbolToGexbotTicker("ESZ24"), "SPX");
  EXPECT_EQ(MapChartSymbolToGexbotTicker("mesm25"), "SPX");
  EXPECT_EQ(MapChartSymbolToGexbotTicker("NQH25"), "NDX");
  EXPECT_EQ(MapChartSymbolToGexbotTicker("mnqz24"), "NDX");
}

TEST(GexbotTickerMap, RejectsUnknown) {
  EXPECT_TRUE(MapChartSymbolToGexbotTicker("CLZ24").empty());
  EXPECT_TRUE(MapChartSymbolToGexbotTicker("AAPL").empty());
}

TEST(GexbotParser, FiltersPositiveGammaAndSorts) {
  GammaResponse response = ParseGexbotGammaResponse(kSampleJson, /*max_levels=*/2);

  EXPECT_EQ(response.timestamp, 1700000000);
  EXPECT_EQ(response.ticker, "SPX");
  EXPECT_DOUBLE_EQ(response.spot, 4800.5);

  ASSERT_EQ(response.levels.size(), 2u);
  EXPECT_DOUBLE_EQ(response.levels[0].strike, 4840.0);
  EXPECT_DOUBLE_EQ(response.levels[0].specified_greek, 25.0);
  EXPECT_DOUBLE_EQ(response.levels[1].strike, 4810.0);
  EXPECT_DOUBLE_EQ(response.levels[1].specified_greek, 15.5);
}

TEST(GexbotParser, ThrowsOnMissingMiniContracts) {
  const char* broken_json = R"({"timestamp":1,"ticker":"SPX","spot":10.0})";
  EXPECT_THROW(ParseGexbotGammaResponse(broken_json, 5), std::runtime_error);
}

}  // namespace

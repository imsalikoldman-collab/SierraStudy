/**
 * @brief Тесты парсера SpotGamma и выбора секции по символу.
 * @note Покрываем требования SG-STUDY-TASK-001 / SG-ROW-FMT-001: парсинг, fallback, игнор нулей, устойчивость к обрезке.
 */
#include "sierra/core/spotgamma.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

std::string sample_row() {
  return "$ES1!, ES1!, 7000, 6800, 6925, 7000, 6900, 6950, 6000, 6999, 6798, 7102, 7047, 0.006, 0.0148, 6875,"
         "$NQ1!, NQ1!, 18000, 17000, 17500, 18050, 17800, 17600, 17400, 17300, 17200, 17100, 17050, 0.004, 0.009, 17550";
}

TEST(SpotGammaParseTest, ParsesTwoSections) {
  const auto sections = sierra::core::parse_spotgamma_row(sample_row());
  ASSERT_EQ(sections.size(), 2u);

  const auto& es = sections[0];
  EXPECT_EQ(es.chart_symbol, "ES1!");
  ASSERT_TRUE(es.levels.call_wall.has_value());
  EXPECT_DOUBLE_EQ(*es.levels.call_wall, 7000.0);
  EXPECT_DOUBLE_EQ(*es.levels.zero_gamma, 6875.0);

  const auto& nq = sections[1];
  EXPECT_EQ(nq.chart_symbol, "NQ1!");
  ASSERT_TRUE(nq.levels.large_gamma_1.has_value());
  EXPECT_DOUBLE_EQ(*nq.levels.large_gamma_1, 18050.0);
  ASSERT_TRUE(nq.levels.implied_move_1d.has_value());
  EXPECT_DOUBLE_EQ(*nq.levels.implied_move_1d, 0.004);
}

TEST(SpotGammaParseTest, SkipsSectionWithInvalidNumber) {
  const std::string row =
      "$ES1!, ES1!, 7000, 6800, 6925, 7000, 6900, 6950, 6000, 6999, 6798, 7102, 7047, OOPS, 0.0148, 6875";
  const auto sections = sierra::core::parse_spotgamma_row(row);
  EXPECT_TRUE(sections.empty());
}

TEST(SpotGammaParseTest, IgnoresTrailingIncompleteTokens) {
  const std::string row =
      "$ES1!, ES1!, 7000, 6800, 6925, 7000, 6900, 6950, 6000, 6999, 6798, 7102, 7047, 0.006, 0.0148, 6875, EXTRA, 1, 2";
  const auto sections = sierra::core::parse_spotgamma_row(row);
  ASSERT_EQ(sections.size(), 1u);
  EXPECT_EQ(sections[0].chart_symbol, "ES1!");
}

TEST(SpotGammaParseTest, ZeroValuesAreTreatedAsAbsent) {
  const std::string row =
      "$ES1!, ES1!, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0";
  const auto sections = sierra::core::parse_spotgamma_row(row);
  ASSERT_EQ(sections.size(), 1u);
  const auto& levels = sections[0].levels;
  EXPECT_FALSE(levels.call_wall.has_value());
  EXPECT_FALSE(levels.put_wall.has_value());
  EXPECT_FALSE(levels.zero_gamma.has_value());
  EXPECT_TRUE(levels.implied_move_1d.has_value());
  EXPECT_DOUBLE_EQ(*levels.implied_move_1d, 0.0);
}

TEST(SpotGammaSelectTest, ExactMatchPreferred) {
  const auto sections = sierra::core::parse_spotgamma_row(sample_row());
  const auto selected = sierra::core::select_spotgamma_section(sections, "NQ1!");
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->chart_symbol, "NQ1!");
}

TEST(SpotGammaSelectTest, FuturesFallbackToContinuous) {
  const auto sections = sierra::core::parse_spotgamma_row(sample_row());
  const auto selected = sierra::core::select_spotgamma_section(sections, "ESH26");
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->chart_symbol, "ES1!");
}

TEST(SpotGammaSelectTest, ReturnsNulloptWhenMissing) {
  const auto sections = sierra::core::parse_spotgamma_row(sample_row());
  const auto selected = sierra::core::select_spotgamma_section(sections, "YMZ26");
  EXPECT_FALSE(selected.has_value());
}

}  // namespace

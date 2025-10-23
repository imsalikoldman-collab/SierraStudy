/**
 * @brief Набор модульных тестов для функции вычисления скользящего среднего.
 * @note Проверяем как корректные расчёты, так и реакцию на некорректный период.
 * @warning Тесты предполагают, что реализация возвращает NaN до накопления периода и бросает исключение при периоде 0.
 */
#include "sierra/core/moving_average.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

TEST(MovingAverageTest, ProducesNaNUntilEnoughSamples) {
  const std::vector<double> input{1.0, 2.0, 3.0, 4.0, 5.0};
  const auto output = sierra::core::moving_average(input, 3);

  ASSERT_EQ(output.size(), input.size());
  EXPECT_TRUE(std::isnan(output[0]));
  EXPECT_TRUE(std::isnan(output[1]));
  EXPECT_DOUBLE_EQ(output[2], 2.0);
  EXPECT_DOUBLE_EQ(output[3], 3.0);
  EXPECT_DOUBLE_EQ(output[4], 4.0);
}

TEST(MovingAverageTest, ThrowsOnZeroPeriod) {
  const std::vector<double> input{1.0, 2.0};
  EXPECT_THROW(sierra::core::moving_average(input, 0), std::invalid_argument);
}

}  // namespace

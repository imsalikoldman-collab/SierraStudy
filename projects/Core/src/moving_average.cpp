#include "sierra/core/moving_average.hpp"

#include <limits>
#include <stdexcept>

namespace sierra::core {

std::vector<double> moving_average(const std::vector<double>& input, std::size_t period) {
  if (period == 0) {
    throw std::invalid_argument("moving_average period must be greater than zero");
  }

  const std::size_t size = input.size();
  std::vector<double> output(size, std::numeric_limits<double>::quiet_NaN());
  if (size == 0) {
    return output;
  }

  double running_sum = 0.0;
  for (std::size_t i = 0; i < size; ++i) {
    running_sum += input[i];
    if (i >= period) {
      running_sum -= input[i - period];
    }
    if (i + 1 >= period) {
      output[i] = running_sum / static_cast<double>(period);
    }
  }

  return output;
}

}  // namespace sierra::core

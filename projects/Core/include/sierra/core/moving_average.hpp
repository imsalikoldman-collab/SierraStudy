#pragma once

#include <cstddef>
#include <vector>

namespace sierra::core {

// Computes a simple moving average over the input series.
// Values that do not have enough history are set to NaN.
std::vector<double> moving_average(const std::vector<double>& input, std::size_t period);

}  // namespace sierra::core

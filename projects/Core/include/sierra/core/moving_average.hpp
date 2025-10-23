#pragma once

#include <cstddef>
#include <vector>

namespace sierra::core {

/// @brief Вычисляет простое скользящее среднее по входному ряду.
/// @param input Набор значений, по которому строим среднее.
/// @param period Размер окна; должен быть положительным.
/// @return Вектор того же размера, где элементы до накопления истории заполнены NaN.
/// @note Значения, для которых истории недостаточно, возвращаются как `NaN`.
/// @warning Если передать период 0, будет выброшено исключение `std::invalid_argument`.
std::vector<double> moving_average(const std::vector<double>& input, std::size_t period);

}  // namespace sierra::core

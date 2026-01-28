#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sierra::core {

/**
 * @brief Представление уровня положительной гаммы из ответа Gexbot.
 * @param strike Ценовой уровень (strike).
 * @param specified_greek Значение выбранного gamma-профиля для данного страйка (только положительные значения).
 */
struct GammaLevel {
  double strike{};
  double specified_greek{};
};

/**
 * @brief Нормализованный ответ Gexbot для gamma-профилей.
 * @param timestamp Unix-время (в секундах), когда данные были сгенерированы на стороне сервиса.
 * @param ticker Тикер в ответе (например, SPX/NDX).
 * @param spot Спот/референсная цена базового актива.
 * @param levels Отфильтрованные уровни с положительной гаммой, отсортированные по убыванию specified_greek.
 */
struct GammaResponse {
  std::int64_t timestamp{};
  std::string ticker;
  double spot{};
  std::vector<GammaLevel> levels;
};

/**
 * @brief Конвертирует тикер графика Sierra Chart в тикер API Gexbot.
 * @param chart_symbol Символ графика (например, ESZ24, MNQH25).
 * @return Строка тикера Gexbot (SPX/NDX) или пустая строка, если символ не поддержан.
 * @note Маппинг: ES или MES → SPX; NQ или MNQ → NDX.
 */
std::string MapChartSymbolToGexbotTicker(const std::string& chart_symbol);

/**
 * @brief Парсит JSON-ответ Gexbot Options Profile Greeks и отбирает положительные gamma-уровни.
 * @param json_body Тело ответа HTTP в формате JSON.
 * @param max_levels Максимальное число линий для визуализации (обрезает top-N по убыванию specified_greek).
 * @return Нормализованный ответ с отсортированными уровнями.
 * @note В выборку попадают только строки mini_contracts, где specified_greek > 0 и не null.
 * @warning При некорректном JSON или отсутствии ключевых полей выбрасывает std::runtime_error.
 */
GammaResponse ParseGexbotGammaResponse(const std::string& json_body, std::size_t max_levels);

}  // namespace sierra::core

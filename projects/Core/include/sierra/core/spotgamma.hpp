#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sierra::core {

/**
 * @brief Набор уровней SpotGamma для одного инструмента.
 * @note Значения, равные нулю в исходной строке, считаются отсутствующими и представлены как std::nullopt.
 * @warning Для полей Implied Move нулевое значение сохраняется, так как это доля/процент, а не ценовой уровень.
 */
struct SpotGammaLevels {
  std::optional<double> call_wall;
  std::optional<double> put_wall;
  std::optional<double> volatility_trigger;
  std::optional<double> large_gamma_1;
  std::optional<double> large_gamma_2;
  std::optional<double> large_gamma_3;
  std::optional<double> large_gamma_4;
  std::optional<double> combo_1;
  std::optional<double> combo_2;
  std::optional<double> combo_3;
  std::optional<double> combo_4;
  std::optional<double> implied_move_1d;
  std::optional<double> implied_move_5d;
  std::optional<double> zero_gamma;
};

/**
 * @brief Представление секции SpotGamma (16 токенов) после парсинга строки.
 * @note Включает идентификаторы инструмента и набор уровней.
 */
struct SpotGammaSection {
  std::string source_symbol;  ///< Символ из источника (часто с '$').
  std::string chart_symbol;   ///< Символ для сравнения с sc.Symbol.
  SpotGammaLevels levels;     ///< Набор уровней и метрик.
};

/**
 * @brief Парсит строку SpotGamma в набор секций.
 * @param row Текстовая строка, содержащая одну или несколько секций, разделённых запятыми.
 * @return Вектор успешно распознанных секций. Неверные или неполные секции пропускаются.
 * @note Разбиение строго по 16 токенов: 2 строковых + 14 числовых. Хвост <16 токенов игнорируется.
 * @warning При ошибке парсинга любого числового поля секция пропускается целиком; это корректное и ожидаемое поведение.
 */
std::vector<SpotGammaSection> parse_spotgamma_row(const std::string& row);

/**
 * @brief Выбирает секцию, соответствующую символу графика, с поддержкой fallback.
 * @param sections Секции, полученные после парсинга строки.
 * @param chart_symbol Символ текущего графика Sierra Chart (например, "ESH26" или "ES1!").
 * @return Секция, если найдена, иначе std::nullopt.
 * @note Приоритет: 1) точное совпадение chart_symbol; 2) fallback по префиксу:
 *       ES/MES → ищем ES1!/MES1! или первую секцию с префиксом ES/MES;
 *       NQ/MNQ → аналогично NQ1!/MNQ1! или первая с префиксом NQ/MNQ.
 * @warning Для символов вне набора ES/NQ (и микроверсий) возвращается std::nullopt без исключений — обёртка должна очистить вывод.
 */
std::optional<SpotGammaSection> select_spotgamma_section(
    const std::vector<SpotGammaSection>& sections,
    const std::string& chart_symbol);

/**
 * @brief Возвращает список уровней с подписью Level ID в фиксированном порядке.
 * @param levels Набор уровней конкретной секции.
 * @return Вектор пар (метка, значение) в порядке, описанном в спецификации SG-ROW-FMT-001.
 * @note Удобен для вывода легенды и отрисовки линий.
 * @warning Пары со значением std::nullopt обозначают отсутствие уровня и не должны рисоваться на графике.
 */
std::vector<std::pair<std::string, std::optional<double>>> enumerate_levels(const SpotGammaLevels& levels);

}  // namespace sierra::core

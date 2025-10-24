#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sierra::core {

/**
 * @brief Представляет диапазон значений, описывающий ценовой интервал.
 * @note Интервалы задаются как [low, high] с предположением low < high.
 */
struct PriceRange {
  double low{};
  double high{};
};

/**
 * @brief Возможные направления торговых зон.
 * @warning Следующие значения соответствуют ключам YAML: "buy" и "sell".
 */
enum class ZoneDirection { kBuy, kSell };

/**
 * @brief Описывает торговую зону из плана.
 * @param direction Направление зоны (покупка/продажа).
 * @param range Основной рабочий диапазон цен.
 * @param sl Уровень стоп-лосса.
 * @param tp1 Первый тейк-профит.
 * @param tp2 Второй тейк-профит.
 * @param invalid Дополнительный диапазон инвалидации, если указан.
 * @param notes Свободные текстовые пометки по ключам (range/sl/tp1/tp2/invalid).
 */
struct Zone {
  ZoneDirection direction{};
  PriceRange range{};
  double sl{};
  double tp1{};
  double tp2{};
  std::optional<PriceRange> invalid;
  std::unordered_map<std::string, std::string> notes;
};

/**
 * @brief Описывает flip-зону с диапазоном и подписью.
 * @param range Диапазон цен, где меняется сценарий.
 * @param label Подпись для отображения на графике.
 */
struct FlipZone {
  PriceRange range{};
  std::string label;
};

/**
 * @brief План для конкретного тикера.
 * @param zones Набор торговых зон.
 * @param flip Опциональный flip-блок.
 */
struct InstrumentPlan {
  std::vector<Zone> zones;
  std::optional<FlipZone> flip;
};

/**
 * @brief Полный торговый план, загруженный из YAML.
 * @param version Версия спецификации (например, "1.5-min-obj").
 * @param generated_at_iso8601 Строка времени генерации плана в формате ISO-8601.
 * @param instruments Набор планов по тикерам (NQ, ES и т.п.).
 */
struct StudyPlan {
  std::string version;
  std::string generated_at_iso8601;
  std::unordered_map<std::string, InstrumentPlan> instruments;
};

/**
 * @brief Результат загрузки плана из файла.
 * @param plan Загруженный план, если чтение прошло успешно.
 * @param error_message Описание ошибки при неудаче.
 * @note Метод success() сообщает об успешной загрузке.
 */
struct PlanLoadResult {
  StudyPlan plan;
  std::string error_message;

  [[nodiscard]] bool success() const { return error_message.empty(); }
};

}  // namespace sierra::core


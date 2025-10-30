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
 * @brief Торговая зона, импортированная из YAML плана.
 * @param direction Тип зоны: покупка (`zone_long`) или продажа (`zone_short`).
 * @param label Обязательное текстовое описание, выводимое внутри зоны.
 * @param range Основной диапазон цен [low, high], в котором действует зона.
 * @param sl Уровень стоп-лосса.
 * @param tp1 Первый тейк-профит.
 * @param tp2 Второй тейк-профит.
 * @param invalidation Необязательный диапазон инвалидации.
 * @note Структура не содержит произвольных заметок: всё, что нужно для отображения, перечислено явно.
 * @warning При отсутствии `label` или диапазона `range` парсер отклоняет зону.
 */
struct Zone {
  ZoneDirection direction{};
  std::string label;
  PriceRange range{};
  double sl{};
  double tp1{};
  double tp2{};
  std::optional<PriceRange> invalidation;
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
 * @brief План для одного тикера.
 * @param zone_long Опциональная покупочная зона `zone_long`.
 * @param zone_short Опциональная продажная зона `zone_short`.
 * @param flip Необязательная flip-зона для смены сценария.
 * @note На тикер приходится максимум одна зона каждого типа.
 * @warning Оба поля `zone_long` и `zone_short` могут быть пустыми одновременно.
 */
struct InstrumentPlan {
  std::optional<Zone> zone_long;
  std::optional<Zone> zone_short;
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


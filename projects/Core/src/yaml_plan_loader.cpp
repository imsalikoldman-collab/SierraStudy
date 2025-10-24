#include "sierra/core/yaml_plan_loader.hpp"

#include <c4/charconv.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sierra::core {
namespace {

std::string ToString(ryml::ConstNodeRef node) {
  const auto value = node.val();
  return std::string(value.str, value.len);
}

bool ParseDouble(ryml::ConstNodeRef node, double* out, std::string* error) {
  const auto value = node.val();
  if (value.empty()) {
    *error = "Пустое значение числа в YAML";
    return false;
  }
  double parsed{};
  if (!c4::from_chars(value, &parsed)) {
    *error = "Не удалось преобразовать \"" + std::string(value.str, value.len) + "\" в число";
    return false;
  }
  *out = parsed;
  return true;
}

bool ParsePriceRangeNode(ryml::ConstNodeRef node, PriceRange* out, std::string* error) {
  if (!node.is_seq() || node.num_children() != 2u) {
    *error = "Поле диапазона должно быть YAML-последовательностью из двух чисел";
    return false;
  }
  double low{};
  double high{};
  if (!ParseDouble(node.child(0), &low, error)) {
    return false;
  }
  if (!ParseDouble(node.child(1), &high, error)) {
    return false;
  }
  out->low = low;
  out->high = high;
  return true;
}

bool ParseDirection(ryml::ConstNodeRef node, ZoneDirection* out, std::string* error) {
  const auto text = ToString(node);
  if (text == "buy") {
    *out = ZoneDirection::kBuy;
    return true;
  }
  if (text == "sell") {
    *out = ZoneDirection::kSell;
    return true;
  }
  *error = "Поле direction должно принимать значения \"buy\" или \"sell\"";
  return false;
}

bool ParseNotes(ryml::ConstNodeRef node, std::unordered_map<std::string, std::string>* out) {
  if (!node.is_map()) {
    return false;
  }
  out->clear();
  for (const auto& child : node.children()) {
    const auto key = std::string(child.key().str, child.key().len);
    out->emplace(key, std::string(child.val().str, child.val().len));
  }
  return true;
}

bool ParseZone(ryml::ConstNodeRef node, Zone* out, std::string* error) {
  if (!node.is_map()) {
    *error = "Элемент зоны должен быть YAML-картой";
    return false;
  }

  const auto direction_node = node.find_child("direction");
  if (!direction_node.valid()) {
    *error = "Отсутствует обязательное поле direction";
    return false;
  }
  if (!ParseDirection(direction_node, &out->direction, error)) {
    return false;
  }

  const auto range_node = node.find_child("range");
  if (!range_node.valid()) {
    *error = "Отсутствует обязательное поле range";
    return false;
  }
  if (!ParsePriceRangeNode(range_node, &out->range, error)) {
    return false;
  }

  const auto sl_node = node.find_child("sl");
  const auto tp1_node = node.find_child("tp1");
  const auto tp2_node = node.find_child("tp2");
  if (!sl_node.valid() || !tp1_node.valid() || !tp2_node.valid()) {
    *error = "Поля sl, tp1 и tp2 обязательны для каждой зоны";
    return false;
  }
  if (!ParseDouble(sl_node, &out->sl, error)) {
    return false;
  }
  if (!ParseDouble(tp1_node, &out->tp1, error)) {
    return false;
  }
  if (!ParseDouble(tp2_node, &out->tp2, error)) {
    return false;
  }

  out->invalid.reset();
  const auto invalid_node = node.find_child("invalid");
  if (invalid_node.valid()) {
    PriceRange invalid{};
    if (!ParsePriceRangeNode(invalid_node, &invalid, error)) {
      return false;
    }
    out->invalid = invalid;
  }

  out->notes.clear();
  const auto notes_node = node.find_child("notes");
  if (notes_node.valid()) {
    ParseNotes(notes_node, &out->notes);
  }

  return true;
}

bool ParseFlip(ryml::ConstNodeRef node, std::optional<FlipZone>* out, std::string* error) {
  if (!node.valid() || (node.has_val() && node.val_is_null())) {
    out->reset();
    return true;
  }
  if (!node.is_map()) {
    *error = "Блок flip должен быть YAML-картой";
    return false;
  }

  FlipZone flip{};
  const auto range_node = node.find_child("range");
  const auto label_node = node.find_child("label");
  if (!range_node.valid() || !label_node.valid()) {
    *error = "Flip требует поля range и label";
    return false;
  }
  if (!ParsePriceRangeNode(range_node, &flip.range, error)) {
    return false;
  }
  flip.label = ToString(label_node);
  *out = std::move(flip);
  return true;
}

bool ParseInstrument(ryml::ConstNodeRef node, InstrumentPlan* out, std::string* error) {
  if (!node.is_map()) {
    *error = "Инструмент должен быть YAML-картой";
    return false;
  }

  out->zones.clear();
  if (auto zones_node = node.find_child("zones"); zones_node.valid()) {
    if (!(zones_node.is_val() && zones_node.val_is_null())) {
      if (!zones_node.is_seq()) {
        *error = "Поле zones должно быть последовательностью";
        return false;
      }
      out->zones.reserve(static_cast<std::size_t>(zones_node.num_children()));
      for (const auto& zone_node : zones_node.children()) {
        Zone zone{};
        if (!ParseZone(zone_node, &zone, error)) {
          return false;
        }
        out->zones.push_back(std::move(zone));
      }
    }
  }

  const auto flip_node = node.find_child("flip");
  if (!ParseFlip(flip_node, &out->flip, error)) {
    return false;
  }

  return true;
}

}  // namespace

PlanLoadResult LoadStudyPlanFromFile(const std::filesystem::path& path) {
  PlanLoadResult result{};

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    result.error_message = "Файл плана не найден: " + path.string();
    return result;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    result.error_message = "Не удалось открыть файл плана: " + path.string();
    return result;
  }

  std::stringstream buffer;
  buffer << stream.rdbuf();
  const std::string content = buffer.str();
  if (content.empty()) {
    result.error_message = "Файл плана пуст: " + path.string();
    return result;
  }

  std::vector<char> yaml_buffer(content.begin(), content.end());
  yaml_buffer.push_back('\0');

  ryml::Tree tree;
  try {
    tree = ryml::parse_in_place(ryml::substr(yaml_buffer.data(), yaml_buffer.size() - 1));
  } catch (const std::exception& ex) {
    result.error_message = "Ошибка парсинга YAML: " + std::string(ex.what());
    return result;
  }

  const auto root = tree.rootref();
  if (!root.valid()) {
    result.error_message = "Корневой документ YAML невалиден";
    return result;
  }

  const auto version_node = root.find_child("v");
  const auto generated_node = root.find_child("generated_at");
  if (!version_node.valid() || !generated_node.valid()) {
    result.error_message = "Отсутствуют обязательные поля v или generated_at";
    return result;
  }

  StudyPlan plan{};
  plan.version = ToString(version_node);
  plan.generated_at_iso8601 = ToString(generated_node);

  for (const auto& child : root.children()) {
    if (!child.has_key()) {
      continue;
    }
    const auto key_view = child.key();
    const std::string key(key_view.str, key_view.len);
    if (key == "v" || key == "generated_at") {
      continue;
    }
    InstrumentPlan instrument{};
    if (!ParseInstrument(child, &instrument, &result.error_message)) {
      result.error_message = "Инструмент \"" + key + "\": " + result.error_message;
      return result;
    }
    plan.instruments.emplace(key, std::move(instrument));
  }

  result.plan = std::move(plan);
  result.error_message.clear();
  return result;
}

}  // namespace sierra::core

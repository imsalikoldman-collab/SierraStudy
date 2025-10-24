#include <gtest/gtest.h>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include <stdexcept>

/**
 * @brief Преобразует скаляр YAML в число двойной точности.
 * @param node Узел-скаляр, содержащий строку с числом.
 * @return double Результат преобразования в формате double.
 * @note Использует c4::from_chars, поэтому парсинг гарантированно locale-нейтрален.
 * @warning При пустом значении или некорректном формате выбрасывает std::invalid_argument.
 */
static double NodeToDouble(const ryml::ConstNodeRef& node) {
  const auto scalar = node.val();
  if (scalar.empty()) {
    throw std::invalid_argument("YAML scalar is empty");
  }

  double result{};
  if (!c4::from_chars(scalar, &result)) {
    throw std::invalid_argument("Failed to parse YAML scalar as double");
  }

  return result;
}

/**
 * @brief Проверяет, что rapidyaml корректно парсит отображение ключ-значение.
 * @param Нет параметров.
 * @return void Функция-тест не возвращает значение.
 * @note Используем arena-парсинг, чтобы строки оставались валидными на протяжении проверки.
 * @warning Тест чувствителен к регистру ключей и значений.
 */
TEST(RapidYamlSmokeTest, ParsesScalarMapping) {
  const auto yaml = ryml::to_csubstr("key: value");
  ryml::Tree tree = ryml::parse_in_arena(yaml);

  const auto root = tree.rootref();
  ASSERT_EQ(root.num_children(), 1u);

  const auto child = root.first_child();
  ASSERT_TRUE(child.valid());
  EXPECT_EQ(child.key(), ryml::to_csubstr("key"));
  EXPECT_EQ(child.val(), ryml::to_csubstr("value"));
}

/**
 * @brief Проверяет разбор полного документа YML v1.5-min-obj с зонами и блоком flip.
 * @param Нет параметров.
 * @return void Функция-тест не возвращает значение.
 * @note Образец основан на спецификации docs/YML_v1.5-min-obj_spec.md.
 * @warning Ожидания чувствительны к числовым значениям и структуре документа.
 */
TEST(RapidYamlSpecTest, ParsesRootObjectWithZonesAndFlip) {
  constexpr const char* kYaml = R"(v: 1.5-min-obj
generated_at: 2025-10-23T08:45:00Z
NQ:
  zones:
    - direction: buy
      range: [15420.25, 15435.00]
      sl: 15410.00
      tp1: 15455.00
      tp2: 15480.00
      notes:
        range: "Основной диапазон NQ"
    - direction: sell
      range: [15550.00, 15570.50]
      sl: 15580.00
      tp1: 15520.00
      tp2: 15495.00
  flip:
    range: [15500.00, 15520.00]
    label: "NQ Flip"
ES:
  zones:
    - direction: buy
      range: [4300.00, 4308.00]
      sl: 4295.00
      tp1: 4315.00
      tp2: 4325.00
)";

  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(kYaml));
  const auto root = tree.rootref();
  ASSERT_TRUE(root.valid());
  ASSERT_EQ(root.num_children(), 4u);

  EXPECT_EQ(root["v"].val(), ryml::to_csubstr("1.5-min-obj"));
  EXPECT_EQ(root["generated_at"].val(), ryml::to_csubstr("2025-10-23T08:45:00Z"));

  ASSERT_TRUE(root.has_child("NQ"));
  const auto nq = root["NQ"];
  ASSERT_TRUE(nq.has_child("zones"));
  const auto nqZones = nq["zones"];
  ASSERT_EQ(nqZones.num_children(), 2u);

  const auto nqZone0 = nqZones.child(0);
  EXPECT_EQ(nqZone0["direction"].val(), ryml::to_csubstr("buy"));
  const auto nqRange0 = nqZone0["range"];
  ASSERT_EQ(nqRange0.num_children(), 2u);
  EXPECT_NEAR(NodeToDouble(nqRange0.child(0)), 15420.25, 1e-6);
  EXPECT_NEAR(NodeToDouble(nqRange0.child(1)), 15435.00, 1e-6);

  const auto nqFlip = nq["flip"];
  ASSERT_TRUE(nqFlip.valid());
  EXPECT_EQ(nqFlip["label"].val(), ryml::to_csubstr("NQ Flip"));

  ASSERT_TRUE(root.has_child("ES"));
  const auto es = root["ES"];
  const auto esZone0 = es["zones"].child(0);
  EXPECT_EQ(esZone0["direction"].val(), ryml::to_csubstr("buy"));
  EXPECT_NEAR(NodeToDouble(esZone0["tp2"]), 4325.00, 1e-6);
}

/**
 * @brief Проверяет обработку необязательных полей `invalid` и `notes` в зонах.
 * @param Нет параметров.
 * @return void Функция-тест не возвращает значение.
 * @note Демонстрирует корректную работу с отсутствующими ключами и проверяет числовые диапазоны.
 * @warning При изменении формата чисел потребуется скорректировать допуски в проверках.
 */
TEST(RapidYamlSpecTest, HandlesOptionalInvalidRangesAndNotes) {
  constexpr const char* kYaml = R"(zones:
  - direction: sell
    range: [100.0, 120.5]
    sl: 125.0
    tp1: 95.0
    tp2: 90.0
    invalid: [118.0, 119.0]
    notes:
      invalid: "пересмотр 119.0"
  - direction: buy
    range: [80.0, 92.0]
    sl: 75.0
    tp1: 100.0
    tp2: 110.0
)";

  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(kYaml));
  const auto root = tree.rootref();
  ASSERT_TRUE(root.valid());
  const auto zones = root["zones"];
  ASSERT_EQ(zones.num_children(), 2u);

  const auto sellZone = zones.child(0);
  ASSERT_TRUE(sellZone.has_child("invalid"));
  const auto invalidRange = sellZone["invalid"];
  ASSERT_EQ(invalidRange.num_children(), 2u);
  EXPECT_NEAR(NodeToDouble(invalidRange.child(0)), 118.0, 1e-6);
  EXPECT_NEAR(NodeToDouble(invalidRange.child(1)), 119.0, 1e-6);
  ASSERT_TRUE(sellZone.has_child("notes"));
  EXPECT_EQ(sellZone["notes"]["invalid"].val(), ryml::to_csubstr("пересмотр 119.0"));

  const auto buyZone = zones.child(1);
  EXPECT_FALSE(buyZone.has_child("invalid"));
  EXPECT_FALSE(buyZone.has_child("notes"));
  const auto buyRange = buyZone["range"];
  EXPECT_NEAR(NodeToDouble(buyRange.child(0)), 80.0, 1e-6);
  EXPECT_NEAR(NodeToDouble(buyRange.child(1)), 92.0, 1e-6);
}


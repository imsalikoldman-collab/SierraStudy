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
  ASSERT_TRUE(child.readable());
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
  zone_long:
    label: "NQ buy zone"
    range: [15420.25, 15435.00]
    sl: 15410.00
    tp1: 15455.00
    tp2: 15480.00
    invalidation: [15418.00, 15419.00]
  zone_short:
    label: "NQ sell zone"
    range: [15550.00, 15570.50]
    sl: 15580.00
    tp1: 15520.00
    tp2: 15495.00
  flip:
    range: [15500.00, 15520.00]
    label: "Bias change"
ES:
  zone_long: null
  zone_short:
    label: "ES short zone"
    range: [4300.00, 4308.00]
    sl: 4295.00
    tp1: 4315.00
    tp2: 4325.00
)";

  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(kYaml));
  const auto root = tree.rootref();
  ASSERT_TRUE(root.readable());
  ASSERT_EQ(root.num_children(), 4u);

  EXPECT_EQ(root["v"].val(), ryml::to_csubstr("1.5-min-obj"));
  EXPECT_EQ(root["generated_at"].val(), ryml::to_csubstr("2025-10-23T08:45:00Z"));

  ASSERT_TRUE(root.has_child("NQ"));
  const auto nq = root["NQ"];
  ASSERT_TRUE(nq.has_child("zone_long"));
  const auto nqZoneLong = nq["zone_long"];
  EXPECT_EQ(nqZoneLong["label"].val(), ryml::to_csubstr("NQ buy zone"));
  const auto nqRangeLong = nqZoneLong["range"];
  ASSERT_EQ(nqRangeLong.num_children(), 2u);
  EXPECT_NEAR(NodeToDouble(nqRangeLong.child(0)), 15420.25, 1e-6);
  EXPECT_NEAR(NodeToDouble(nqRangeLong.child(1)), 15435.00, 1e-6);
  ASSERT_TRUE(nqZoneLong.has_child("invalidation"));
  const auto invalidation = nqZoneLong["invalidation"];
  EXPECT_NEAR(NodeToDouble(invalidation.child(0)), 15418.00, 1e-6);

  ASSERT_TRUE(nq.has_child("zone_short"));
  const auto nqZoneShort = nq["zone_short"];
  EXPECT_EQ(nqZoneShort["label"].val(), ryml::to_csubstr("NQ sell zone"));
  EXPECT_FALSE(nqZoneShort.has_child("invalidation"));

  const auto nqFlip = nq["flip"];
  ASSERT_TRUE(!nqFlip.is_seed());
  EXPECT_EQ(nqFlip["label"].val(), ryml::to_csubstr("Bias change"));

  ASSERT_TRUE(root.has_child("ES"));
  const auto es = root["ES"];
  ASSERT_TRUE(es.has_child("zone_short"));
  const auto esShort = es["zone_short"];
  EXPECT_EQ(esShort["label"].val(), ryml::to_csubstr("ES short zone"));
  EXPECT_NEAR(NodeToDouble(esShort["tp2"]), 4325.00, 1e-6);
}

/**
 * @brief Проверяет обработку необязательных полей `invalid` и `notes` в зонах.
 * @param Нет параметров.
 * @return void Функция-тест не возвращает значение.
 * @note Демонстрирует корректную работу с отсутствующими ключами и проверяет числовые диапазоны.
 * @warning При изменении формата чисел потребуется скорректировать допуски в проверках.
 */
TEST(RapidYamlSpecTest, HandlesOptionalInvalidation) {
  constexpr const char* kYaml = R"(zone_long:
  label: "Sell control"
  range: [100.0, 120.5]
  sl: 125.0
  tp1: 95.0
  tp2: 90.0
  invalidation: [118.0, 119.0]
zone_short:
  label: "Buy setup"
  range: [80.0, 92.0]
  sl: 75.0
  tp1: 100.0
  tp2: 110.0
)";

  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(kYaml));
  const auto root = tree.rootref();
  ASSERT_TRUE(root.readable());

  const auto zoneLong = root["zone_long"];
  ASSERT_TRUE(zoneLong.has_child("invalidation"));
  const auto invalidation = zoneLong["invalidation"];
  ASSERT_EQ(invalidation.num_children(), 2u);
  EXPECT_NEAR(NodeToDouble(invalidation.child(0)), 118.0, 1e-6);
  EXPECT_NEAR(NodeToDouble(invalidation.child(1)), 119.0, 1e-6);

  const auto zoneShort = root["zone_short"];
  EXPECT_FALSE(zoneShort.has_child("invalidation"));
  const auto rangeShort = zoneShort["range"];
  EXPECT_NEAR(NodeToDouble(rangeShort.child(0)), 80.0, 1e-6);
  EXPECT_NEAR(NodeToDouble(rangeShort.child(1)), 92.0, 1e-6);
}

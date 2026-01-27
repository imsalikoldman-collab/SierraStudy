#include "sierra/core/spotgamma.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

bool is_zero(double value) {
  return std::abs(value) < 1e-12;
}

std::string trim_copy(const std::string& input) {
  const auto first = std::find_if_not(input.begin(), input.end(),
                                      [](unsigned char ch) { return std::isspace(ch); });
  const auto last = std::find_if_not(input.rbegin(), input.rend(),
                                     [](unsigned char ch) { return std::isspace(ch); }).base();
  if (first >= last) {
    return std::string();
  }
  return std::string(first, last);
}

bool parse_double(const std::string& token, double& out) {
  try {
    size_t idx = 0;
    out = std::stod(token, &idx);
    return idx == token.size();
  } catch (...) {
    return false;
  }
}

std::vector<std::string> split_tokens(const std::string& row) {
  std::vector<std::string> tokens;
  std::string current;
  for (char ch : row) {
    if (ch == ',') {
      tokens.push_back(trim_copy(current));
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  tokens.push_back(trim_copy(current));
  return tokens;
}

bool has_prefix(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), value.begin(),
                    [](char a, char b) { return a == b; });
}

}  // namespace

namespace sierra::core {

std::vector<SpotGammaSection> parse_spotgamma_row(const std::string& row) {
  constexpr std::size_t kTokensPerSection = 16;
  constexpr std::size_t kNumbersPerSection = 14;
  constexpr std::size_t kNumbersOffset = 2;

  std::vector<SpotGammaSection> sections;
  const auto tokens = split_tokens(row);
  if (tokens.size() < kTokensPerSection) {
    return sections;
  }

  for (std::size_t i = 0; i + kTokensPerSection <= tokens.size(); i += kTokensPerSection) {
    SpotGammaSection section;
    section.source_symbol = tokens[i];
    section.chart_symbol = tokens[i + 1];

    double numbers[kNumbersPerSection]{};
    bool invalid = false;
    for (std::size_t n = 0; n < kNumbersPerSection; ++n) {
      if (!parse_double(tokens[i + kNumbersOffset + n], numbers[n])) {
        invalid = true;
        break;
      }
    }
    if (invalid) {
      continue;
    }

    section.levels.call_wall = is_zero(numbers[0]) ? std::nullopt : std::make_optional(numbers[0]);
    section.levels.put_wall = is_zero(numbers[1]) ? std::nullopt : std::make_optional(numbers[1]);
    section.levels.volatility_trigger =
        is_zero(numbers[2]) ? std::nullopt : std::make_optional(numbers[2]);
    section.levels.large_gamma_1 = is_zero(numbers[3]) ? std::nullopt : std::make_optional(numbers[3]);
    section.levels.large_gamma_2 = is_zero(numbers[4]) ? std::nullopt : std::make_optional(numbers[4]);
    section.levels.large_gamma_3 = is_zero(numbers[5]) ? std::nullopt : std::make_optional(numbers[5]);
    section.levels.large_gamma_4 = is_zero(numbers[6]) ? std::nullopt : std::make_optional(numbers[6]);
    section.levels.combo_1 = is_zero(numbers[7]) ? std::nullopt : std::make_optional(numbers[7]);
    section.levels.combo_2 = is_zero(numbers[8]) ? std::nullopt : std::make_optional(numbers[8]);
    section.levels.combo_3 = is_zero(numbers[9]) ? std::nullopt : std::make_optional(numbers[9]);
    section.levels.combo_4 = is_zero(numbers[10]) ? std::nullopt : std::make_optional(numbers[10]);

    // Implied move — не уровень цены; сохраняем даже ноль.
    section.levels.implied_move_1d = numbers[11];
    section.levels.implied_move_5d = numbers[12];

    section.levels.zero_gamma = is_zero(numbers[13]) ? std::nullopt : std::make_optional(numbers[13]);

    sections.push_back(std::move(section));
  }

  return sections;
}

std::optional<SpotGammaSection> select_spotgamma_section(
    const std::vector<SpotGammaSection>& sections,
    const std::string& chart_symbol) {
  if (sections.empty()) {
    return std::nullopt;
  }

  // 1) точное совпадение
  const auto exact = std::find_if(sections.begin(), sections.end(),
                                  [&chart_symbol](const SpotGammaSection& sec) {
                                    return sec.chart_symbol == chart_symbol;
                                  });
  if (exact != sections.end()) {
    return *exact;
  }

  const auto select_by_prefix = [&sections](const std::string& preferred,
                                            const std::string& fallback_prefix)
      -> std::optional<SpotGammaSection> {
    if (!preferred.empty()) {
      const auto found = std::find_if(sections.begin(), sections.end(),
                                      [&preferred](const SpotGammaSection& sec) {
                                        return sec.chart_symbol == preferred;
                                      });
      if (found != sections.end()) {
        return *found;
      }
    }

    const auto prefix_it = std::find_if(sections.begin(), sections.end(),
                                        [&fallback_prefix](const SpotGammaSection& sec) {
                                          return has_prefix(sec.chart_symbol, fallback_prefix);
                                        });
    if (prefix_it != sections.end()) {
      return *prefix_it;
    }
    return std::nullopt;
  };

  // 2) fallback по префиксам
  if (has_prefix(chart_symbol, "ES")) {
    if (auto s = select_by_prefix("ES1!", "ES")) return s;
  } else if (has_prefix(chart_symbol, "NQ")) {
    if (auto s = select_by_prefix("NQ1!", "NQ")) return s;
  } else if (has_prefix(chart_symbol, "MES")) {
    if (auto s = select_by_prefix("MES1!", "MES")) return s;
  } else if (has_prefix(chart_symbol, "MNQ")) {
    if (auto s = select_by_prefix("MNQ1!", "MNQ")) return s;
  }

  return std::nullopt;
}

std::vector<std::pair<std::string, std::optional<double>>> enumerate_levels(
    const SpotGammaLevels& levels) {
  return {
      {"Call Wall", levels.call_wall},
      {"Put Wall", levels.put_wall},
      {"Volatility Trigger", levels.volatility_trigger},
      {"Zero Gamma", levels.zero_gamma},
      {"Large Gamma 1", levels.large_gamma_1},
      {"Large Gamma 2", levels.large_gamma_2},
      {"Large Gamma 3", levels.large_gamma_3},
      {"Large Gamma 4", levels.large_gamma_4},
      {"Combo 1", levels.combo_1},
      {"Combo 2", levels.combo_2},
      {"Combo 3", levels.combo_3},
      {"Combo 4", levels.combo_4},
      {"Implied Move 1D", levels.implied_move_1d},
      {"Implied Move 5D", levels.implied_move_5d},
  };
}

}  // namespace sierra::core

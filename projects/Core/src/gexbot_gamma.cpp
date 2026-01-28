#include "sierra/core/gexbot_gamma.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sierra::core {

namespace {

/// @brief Возвращает копию строки в верхнем регистре.
std::string ToUpper(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return out;
}

/// @brief Пропускает пробелы в строке, начиная с позиции pos.
void SkipWhitespace(const std::string& s, std::size_t& pos) {
  while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
    ++pos;
  }
}

/// @brief Пытается извлечь целое число (int64) из строки, начиная с позиции pos.
bool ParseInt64(const std::string& s, std::size_t pos, std::int64_t& out, std::size_t& end_pos) {
  errno = 0;
  char* ep = nullptr;
  const char* start = s.c_str() + pos;
  long long value = std::strtoll(start, &ep, 10);
  if (ep == start || errno == ERANGE) {
    return false;
  }
  out = static_cast<std::int64_t>(value);
  end_pos = static_cast<std::size_t>(ep - s.c_str());
  return true;
}

/// @brief Пытается извлечь число с плавающей точкой из строки, начиная с позиции pos.
bool ParseDouble(const std::string& s, std::size_t pos, double& out, std::size_t& end_pos) {
  errno = 0;
  char* ep = nullptr;
  const char* start = s.c_str() + pos;
  double value = std::strtod(start, &ep);
  if (ep == start || errno == ERANGE) {
    return false;
  }
  out = value;
  end_pos = static_cast<std::size_t>(ep - s.c_str());
  return true;
}

/// @brief Возвращает позицию первого вхождения ключа "key": в JSON и позицию после двоеточия.
bool FindKeyColon(const std::string& json, std::string_view key, std::size_t& value_pos) {
  const std::string pattern = "\"" + std::string(key) + "\"";
  const std::size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) {
    return false;
  }
  std::size_t colon = json.find(':', key_pos + pattern.size());
  if (colon == std::string::npos) {
    return false;
  }
  value_pos = colon + 1;
  SkipWhitespace(json, value_pos);
  return true;
}

/// @brief Пропускает JSON-массив/объект целиком, возвращает индекс его закрывающей скобки.
std::size_t SkipBracketed(const std::string& s, std::size_t start) {
  const char open = s[start];
  const char close = (open == '[') ? ']' : '}';
  int depth = 1;
  bool in_string = false;
  for (std::size_t i = start + 1; i < s.size(); ++i) {
    const char c = s[i];
    if (in_string) {
      if (c == '\\') {
        ++i;
        continue;
      }
      if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == open) {
      ++depth;
    } else if (c == close) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  throw std::runtime_error("Unbalanced JSON brackets");
}

/// @brief Парсит массив mini_contracts, возвращает только уровни с specified_greek > 0.
std::vector<GammaLevel> ParseMiniContracts(const std::string& json, std::size_t start_pos, std::size_t max_levels) {
  std::vector<GammaLevel> levels;
  if (json[start_pos] != '[') {
    throw std::runtime_error("mini_contracts is not an array");
  }

  const std::size_t end_outer = SkipBracketed(json, start_pos);
  std::size_t pos = start_pos + 1;
  while (pos < end_outer) {
    SkipWhitespace(json, pos);
    if (pos >= end_outer) break;
    if (json[pos] == ',') {
      ++pos;
      continue;
    }
    if (json[pos] != '[') {
      // пропускаем неожиданный токен
      ++pos;
      continue;
    }

    const std::size_t row_end = SkipBracketed(json, pos);
    std::size_t cursor = pos + 1;
    int field_index = 0;
    double strike = std::numeric_limits<double>::quiet_NaN();
    double specified = std::numeric_limits<double>::quiet_NaN();

    while (cursor < row_end) {
      SkipWhitespace(json, cursor);
      if (cursor >= row_end || json[cursor] == ']') {
        break;
      }
      if (json[cursor] == ',') {
        ++cursor;
        continue;
      }

      // При встрече вложенного массива (например, priors) просто пропускаем его как одно поле.
      if (json[cursor] == '[' || json[cursor] == '{') {
        const std::size_t nested_end = SkipBracketed(json, cursor);
        ++field_index;
        cursor = nested_end + 1;
        continue;
      }

      // null
      if (json.compare(cursor, 4, "null") == 0 || json.compare(cursor, 4, "NULL") == 0) {
        ++field_index;
        cursor += 4;
        continue;
      }

      // number
      double value = 0.0;
      std::size_t after_number = cursor;
      if (!ParseDouble(json, cursor, value, after_number)) {
        // не смогли разобрать число — прерываем строку
        break;
      }

      if (field_index == 0) {
        strike = value;
      } else if (field_index == 3) {
        specified = value;
      }

      ++field_index;
      cursor = after_number;
    }

    if (std::isfinite(strike) && std::isfinite(specified) && specified > 0.0) {
      levels.push_back({strike, specified});
    }

    pos = row_end + 1;
  }

  std::sort(levels.begin(), levels.end(),
            [](const GammaLevel& a, const GammaLevel& b) { return a.specified_greek > b.specified_greek; });

  if (levels.size() > max_levels) {
    levels.resize(max_levels);
  }

  return levels;
}

}  // namespace

std::string MapChartSymbolToGexbotTicker(const std::string& chart_symbol) {
  const std::string upper = ToUpper(chart_symbol);
  if (upper.rfind("ES", 0) == 0 || upper.rfind("MES", 0) == 0) {
    return "SPX";
  }
  if (upper.rfind("NQ", 0) == 0 || upper.rfind("MNQ", 0) == 0) {
    return "NDX";
  }
  return {};
}

GammaResponse ParseGexbotGammaResponse(const std::string& json_body, std::size_t max_levels) {
  if (json_body.empty()) {
    throw std::runtime_error("Empty HTTP response");
  }

  std::size_t value_pos = 0;
  std::int64_t timestamp = 0;
  if (!FindKeyColon(json_body, "timestamp", value_pos) ||
      !ParseInt64(json_body, value_pos, timestamp, value_pos)) {
    throw std::runtime_error("timestamp not found or invalid");
  }

  if (!FindKeyColon(json_body, "ticker", value_pos) || json_body[value_pos] != '"') {
    throw std::runtime_error("ticker not found or invalid");
  }
  std::size_t closing_quote = json_body.find('"', value_pos + 1);
  if (closing_quote == std::string::npos) {
    throw std::runtime_error("ticker closing quote not found");
  }
  const std::string ticker = json_body.substr(value_pos + 1, closing_quote - value_pos - 1);

  double spot = 0.0;
  if (!FindKeyColon(json_body, "spot", value_pos) || !ParseDouble(json_body, value_pos, spot, value_pos)) {
    throw std::runtime_error("spot not found or invalid");
  }

  if (!FindKeyColon(json_body, "mini_contracts", value_pos)) {
    throw std::runtime_error("mini_contracts not found");
  }

  const std::vector<GammaLevel> levels = ParseMiniContracts(json_body, value_pos, max_levels);

  GammaResponse result;
  result.timestamp = timestamp;
  result.ticker = ticker;
  result.spot = spot;
  result.levels = levels;
  return result;
}

}  // namespace sierra::core

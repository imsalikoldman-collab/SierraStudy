#include "sierra/core/plan_formatter.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>

namespace sierra::core {
namespace {

struct NyTimestamp {
  std::string date;
  std::string time;
};

#if defined(_WIN32)
std::time_t MakeTimeUtc(std::tm tm) { return _mkgmtime64(&tm); }
bool ToUtcTime(std::time_t value, std::tm* out) { return gmtime_s(out, &value) == 0; }
#else
std::time_t MakeTimeUtc(std::tm tm) { return timegm(&tm); }
bool ToUtcTime(std::time_t value, std::tm* out) { return gmtime_r(&value, out) != nullptr; }
#endif

std::time_t FindNthSundayUtc(int year, int month_zero_based, int nth) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month_zero_based;
  tm.tm_mday = 1;
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;

  std::time_t t = MakeTimeUtc(tm);
  std::tm utc_tm{};
  if (!ToUtcTime(t, &utc_tm)) {
    return t;
  }

  const int days_to_first_sunday = (7 - utc_tm.tm_wday) % 7;
  t += static_cast<std::time_t>(days_to_first_sunday) * 24 * 60 * 60;
  t += static_cast<std::time_t>(nth - 1) * 7 * 24 * 60 * 60;
  return t;
}

std::time_t EasternDstStartUtc(int year) {
  return FindNthSundayUtc(year, 2, 2) + 7 * 60 * 60;
}

std::time_t EasternDstEndUtc(int year) {
  return FindNthSundayUtc(year, 10, 1) + 6 * 60 * 60;
}

NyTimestamp FormatIsoToNy(const std::string& iso8601) {
  NyTimestamp result{"-", "-"};

  std::string value = iso8601;
  if (!value.empty() && (value.back() == 'Z' || value.back() == 'z')) {
    value.pop_back();
  }

  std::tm tm_components{};
  tm_components.tm_isdst = -1;
  std::istringstream stream(value);
  stream >> std::get_time(&tm_components, "%Y-%m-%dT%H:%M:%S");
  if (stream.fail()) {
    result.date = iso8601;
    return result;
  }

  std::time_t utc_time = MakeTimeUtc(tm_components);
  if (utc_time == static_cast<std::time_t>(-1)) {
    result.date = iso8601;
    return result;
  }

  std::tm utc_tm{};
  if (!ToUtcTime(utc_time, &utc_tm)) {
    result.date = iso8601;
    return result;
  }

  const int year = utc_tm.tm_year + 1900;
  const std::time_t dst_start = EasternDstStartUtc(year);
  const std::time_t dst_end = EasternDstEndUtc(year);
  const bool is_dst = utc_time >= dst_start && utc_time < dst_end;
  const int offset_hours = is_dst ? -4 : -5;
  const std::time_t ny_time = utc_time + offset_hours * 60 * 60;

  std::tm ny_tm{};
  if (!ToUtcTime(ny_time, &ny_tm)) {
    result.date = iso8601;
    return result;
  }

  std::ostringstream date_stream;
  date_stream << std::setfill('0') << std::setw(4) << (ny_tm.tm_year + 1900) << "-"
              << std::setw(2) << (ny_tm.tm_mon + 1) << "-" << std::setw(2) << ny_tm.tm_mday;
  std::ostringstream time_stream;
  time_stream << std::setfill('0') << std::setw(2) << ny_tm.tm_hour << ":" << std::setw(2)
              << ny_tm.tm_min << ":" << std::setw(2) << ny_tm.tm_sec << " America/New_York";

  result.date = date_stream.str();
  result.time = time_stream.str();
  return result;
}

std::string ToUpperASCII(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

void AppendZone(std::ostringstream& oss, const char* title, const Zone& zone) {
  oss << "  " << title << ": " << zone.label << " | Range " << std::fixed << std::setprecision(2)
      << zone.range.low << "-" << zone.range.high << " | SL " << zone.sl << " | TP1 "
      << zone.tp1 << " | TP2 " << zone.tp2 << "\n";
  if (zone.invalidation.has_value()) {
    oss << "    Invalidation: " << zone.invalidation->low << "-" << zone.invalidation->high
        << "\n";
  }
}

const InstrumentPlan* FindInstrument(const StudyPlan& plan,
                                     const std::string& symbol_filter,
                                     std::string* matched_ticker) {
  if (plan.instruments.empty()) {
    return nullptr;
  }

  if (symbol_filter.empty()) {
    const auto& first = *plan.instruments.begin();
    *matched_ticker = first.first;
    return &first.second;
  }

  const std::string filter_upper = ToUpperASCII(symbol_filter);
  const InstrumentPlan* best_match = nullptr;
  std::size_t best_length = 0;
  for (const auto& [ticker, instrument] : plan.instruments) {
    const std::string ticker_upper = ToUpperASCII(ticker);
    if (!ticker_upper.empty() && filter_upper.find(ticker_upper) != std::string::npos &&
        ticker_upper.length() > best_length) {
      best_match = &instrument;
      best_length = ticker_upper.length();
      *matched_ticker = ticker;
    }
  }
  return best_match;
}

}  // namespace

std::string FormatPlanAsTable(const StudyPlan& plan, const std::string& symbol_filter) {
  std::ostringstream oss;
  const NyTimestamp timestamp = FormatIsoToNy(plan.generated_at_iso8601);

  std::string ticker;
  const InstrumentPlan* instrument = FindInstrument(plan, symbol_filter, &ticker);
  if (instrument == nullptr) {
    ticker = symbol_filter.empty() ? "-" : symbol_filter;
  }

  oss << "Plan version: " << plan.version << "\n";
  oss << "Generated date (NY): " << timestamp.date << "\n";
  oss << "Generated time (NY): " << timestamp.time << "\n";
  oss << "Ticker: " << ticker << "\n";

  if (instrument == nullptr) {
    oss << "  (no zones)\n";
    return oss.str();
  }

  bool has_output = false;
  if (instrument->zone_long.has_value()) {
    AppendZone(oss, "Long", instrument->zone_long.value());
    has_output = true;
  }
  if (instrument->zone_short.has_value()) {
    AppendZone(oss, "Short", instrument->zone_short.value());
    has_output = true;
  }
  if (instrument->flip.has_value()) {
    oss << "  Flip: " << instrument->flip->label << " | Range " << std::fixed
        << std::setprecision(2) << instrument->flip->range.low << "-"
        << instrument->flip->range.high << "\n";
    has_output = true;
  }

  if (!has_output) {
    oss << "  (no zones)\n";
  }

  return oss.str();
}

}  // namespace sierra::core

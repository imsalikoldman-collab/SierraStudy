#include "sierra/core/plan_formatter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sierra::core {

namespace {

std::string ToString(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << value;
  return oss.str();
}

}  // namespace

std::string FormatPlanAsTable(const StudyPlan& plan) {
  std::vector<std::pair<std::string, InstrumentPlan>> instruments(plan.instruments.begin(),
                                                                  plan.instruments.end());
  std::sort(instruments.begin(), instruments.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::ostringstream oss;
  bool first_block = true;

  for (const auto& [ticker, instrument] : instruments) {
    if (!first_block) {
      oss << "\n";
    }
    first_block = false;

    oss << "Plan version: " << plan.version << "\n";
    oss << "Generated at: " << plan.generated_at_iso8601 << "\n";
    oss << "Ticker: " << ticker << "\n";

    if (instrument.zones.empty() && !instrument.flip.has_value()) {
      oss << "  (no zones)\n";
      continue;
    }

    std::size_t zone_index = 1;
    for (const auto& zone : instrument.zones) {
      const std::string direction = zone.direction == ZoneDirection::kBuy ? "BUY" : "SELL";
      oss << "  Zone " << zone_index << " " << direction << " " << ToString(zone.range.low) << "-"
          << ToString(zone.range.high) << " | SL " << ToString(zone.sl)
          << " | TP1 " << ToString(zone.tp1) << " | TP2 " << ToString(zone.tp2) << "\n";
      if (zone.invalid.has_value()) {
        oss << "    Invalid: " << ToString(zone.invalid->low) << "-" << ToString(zone.invalid->high)
            << "\n";
      }
      ++zone_index;
    }

    if (instrument.flip.has_value()) {
      oss << "  Flip: " << ToString(instrument.flip->range.low) << "-"
          << ToString(instrument.flip->range.high) << "\n";
    }
  }

  if (first_block) {
    // Нет инструментов — выводим только заголовки плана.
    oss << "Plan version: " << plan.version << "\n";
    oss << "Generated at: " << plan.generated_at_iso8601 << "\n";
    oss << "Ticker: -\n";
    oss << "  (no zones)\n";
  }

  return oss.str();
}

}  // namespace sierra::core

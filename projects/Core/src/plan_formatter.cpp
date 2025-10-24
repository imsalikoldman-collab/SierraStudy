#include "sierra/core/plan_formatter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sierra::core {

namespace {

struct TableRow {
  std::string ticker;
  std::string direction;
  std::string range;
  std::string sl;
  std::string tp1;
  std::string tp2;
  std::string flip;
};

std::string ToString(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << value;
  return oss.str();
}

void AppendZoneRows(const std::string& ticker, const InstrumentPlan& instrument, std::vector<TableRow>* rows) {
  for (const auto& zone : instrument.zones) {
    TableRow row{};
    row.ticker = ticker;
    row.direction = zone.direction == ZoneDirection::kBuy ? "BUY" : "SELL";
    row.range = ToString(zone.range.low) + "-" + ToString(zone.range.high);
    row.sl = ToString(zone.sl);
    row.tp1 = ToString(zone.tp1);
    row.tp2 = ToString(zone.tp2);
    if (instrument.flip) {
      row.flip = ToString(instrument.flip->range.low) + "-" + ToString(instrument.flip->range.high);
    } else {
      row.flip = "-";
    }
    rows->push_back(std::move(row));
  }
  if (instrument.zones.empty() && instrument.flip) {
    TableRow row{};
    row.ticker = ticker;
    row.direction = "-";
    row.range = "-";
    row.sl = "-";
    row.tp1 = "-";
    row.tp2 = "-";
    row.flip = ToString(instrument.flip->range.low) + "-" + ToString(instrument.flip->range.high);
    rows->push_back(std::move(row));
  }
}

std::string BuildTable(const StudyPlan& plan, const std::vector<TableRow>& rows) {
  const std::string header = "Ticker | Dir | Range            |   SL   |   TP1  |   TP2  | Flip";
  const std::string separator = "-------+-----+-----------------+--------+--------+--------+----------------";

  std::ostringstream oss;
  oss << "Plan version: " << plan.version << "\n";
  oss << "Generated at: " << plan.generated_at_iso8601 << "\n";
  oss << header << "\n";
  oss << separator << "\n";
  if (rows.empty()) {
    oss << "  (no zones)" << "\n";
  } else {
    for (const auto& row : rows) {
      oss << std::left << std::setw(6) << row.ticker << " | ";
      oss << std::setw(3) << row.direction << " | ";
      oss << std::setw(15) << row.range << " | ";
      oss << std::setw(6) << row.sl << " | ";
      oss << std::setw(6) << row.tp1 << " | ";
      oss << std::setw(6) << row.tp2 << " | ";
      oss << row.flip << "\n";
    }
  }
  return oss.str();
}

}  // namespace

std::string FormatPlanAsTable(const StudyPlan& plan) {
  std::vector<std::pair<std::string, InstrumentPlan>> instruments(plan.instruments.begin(),
                                                                  plan.instruments.end());
  std::sort(instruments.begin(), instruments.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::vector<TableRow> rows;
  rows.reserve(16);
  for (const auto& [ticker, instrument] : instruments) {
    AppendZoneRows(ticker, instrument, &rows);
  }

  return BuildTable(plan, rows);
}

}  // namespace sierra::core


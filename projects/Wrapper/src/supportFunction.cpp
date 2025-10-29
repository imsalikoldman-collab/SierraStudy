#include "sierra/acsil/supportFunction.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <string>
#include <sstream>

namespace sierra::acsil {
namespace {

constexpr double kSecondsPerDay = 24.0 * 60.0 * 60.0;

void DeleteExistingDrawings(SCStudyGraphRef sc, std::vector<int>& line_numbers) {
  for (const int line_number : line_numbers) {
    if (line_number != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line_number);
    }
  }
  line_numbers.clear();
}

COLORREF GetZoneColor(core::ZoneDirection direction) {
  return direction == core::ZoneDirection::kBuy ? RGB(0, 128, 0) : RGB(178, 34, 34);
}

}  // namespace

void LogDllStartup(SCStudyInterfaceRef sc) {
  static bool logged = false;
  if (logged) {
    return;
  }

  sc.AddMessageToLog("                                      ", 0);
  sc.AddMessageToLog("--------------------------------------", 0);
  sc.AddMessageToLog("          SierraStudy DLL loaded      ", 0);
  logged = true;
}

SCDateTime ConvertIso8601ToSCDateTime(const std::string& iso8601, SCDateTime fallback) {
  std::string value = iso8601;
  if (!value.empty() && (value.back() == 'Z' || value.back() == 'z')) {
    value.pop_back();
  }

  std::tm tm_components{};
  tm_components.tm_isdst = -1;
  std::istringstream stream(value);
  stream >> std::get_time(&tm_components, "%Y-%m-%dT%H:%M:%S");
  if (stream.fail()) {
    return fallback;
  }

  const __time64_t parsed_seconds = _mkgmtime64(&tm_components);
  if (parsed_seconds == -1) {
    return fallback;
  }

  static const __time64_t base_seconds = []() {
    std::tm base{};
    base.tm_year = 1899 - 1900;
    base.tm_mon = 11;
    base.tm_mday = 30;
    base.tm_isdst = -1;
    return _mkgmtime64(&base);
  }();

  const double seconds_delta = static_cast<double>(parsed_seconds - base_seconds);
  return seconds_delta / kSecondsPerDay;
}

void RenderInstrumentPlanGraphics(SCStudyGraphRef sc,
                                  const core::InstrumentPlan& instrument,
                                  SCDateTime start_time,
                                  SCDateTime end_time,
                                  std::vector<int>& line_numbers) {
  DeleteExistingDrawings(sc, line_numbers);

  if (instrument.zones.empty() && !instrument.flip.has_value()) {
    return;
  }

  const auto push_tool = [&](s_UseTool& tool) {
    tool.ChartNumber = sc.ChartNumber;
    tool.AddMethod = UTAM_ADD_OR_ADJUST;
    tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(tool);
    line_numbers.push_back(tool.LineNumber);
  };

  const auto add_horizontal_line = [&](double price,
                                       COLORREF color,
                                       int width,
                                       SubgraphLineStyles style) {
    s_UseTool line_tool;
    line_tool.Clear();
    line_tool.DrawingType = DRAWING_HORIZONTALLINE;
    line_tool.BeginDateTime = start_time;
    line_tool.EndDateTime = end_time;
    line_tool.BeginValue = static_cast<float>(price);
    line_tool.EndValue = static_cast<float>(price);
    line_tool.Color = color;
    line_tool.LineWidth = static_cast<uint16_t>(width);
    line_tool.LineStyle = style;
    line_tool.ExtendRight = 1;
    push_tool(line_tool);
  };

  const auto add_zone_rectangle = [&](const core::Zone& zone, COLORREF color) {
    s_UseTool rect_tool;
    rect_tool.Clear();
    rect_tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
    rect_tool.BeginDateTime = start_time;
    rect_tool.EndDateTime = end_time;
    rect_tool.BeginValue = static_cast<float>(zone.range.high);
    rect_tool.EndValue = static_cast<float>(zone.range.low);
    rect_tool.Color = color;
    rect_tool.SecondaryColor = color;
    rect_tool.TransparencyLevel = 70;
    rect_tool.LineWidth = 1;
    rect_tool.ExtendRight = 1;
    push_tool(rect_tool);

    add_horizontal_line(zone.sl, RGB(220, 20, 60), 2, LINESTYLE_SOLID);
    add_horizontal_line(zone.tp1, RGB(255, 215, 0), 2, LINESTYLE_DOT);
    add_horizontal_line(zone.tp2, RGB(0, 191, 255), 2, LINESTYLE_DASH);

    if (zone.invalid.has_value()) {
      add_horizontal_line(zone.invalid->low, RGB(255, 140, 0), 1, LINESTYLE_DASHDOT);
      add_horizontal_line(zone.invalid->high, RGB(255, 140, 0), 1, LINESTYLE_DASHDOT);
    }
  };

  for (const auto& zone : instrument.zones) {
    add_zone_rectangle(zone, GetZoneColor(zone.direction));
  }

  if (instrument.flip.has_value()) {
    const auto& flip = instrument.flip.value();
    s_UseTool flip_rect;
    flip_rect.Clear();
    flip_rect.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
    flip_rect.BeginDateTime = start_time;
    flip_rect.EndDateTime = end_time;
    flip_rect.BeginValue = static_cast<float>(flip.range.high);
    flip_rect.EndValue = static_cast<float>(flip.range.low);
    flip_rect.Color = RGB(138, 43, 226);
    flip_rect.SecondaryColor = RGB(138, 43, 226);
    flip_rect.TransparencyLevel = 50;
    flip_rect.LineWidth = 1;
    flip_rect.ExtendRight = 1;
    push_tool(flip_rect);
  }
}

}  // namespace sierra::acsil

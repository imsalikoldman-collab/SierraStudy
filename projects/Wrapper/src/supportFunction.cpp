#include "sierra/acsil/supportFunction.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace sierra::acsil {
namespace {

constexpr double kSecondsPerDay = 24.0 * 60.0 * 60.0;
constexpr SubgraphLineStyles kLevelLineStyle = LINESTYLE_DASH;
constexpr uint16_t kLevelLineWidth = 1;
constexpr const char* kLevelFontFace = "Consolas";
constexpr int kLevelFontSize = 8;
constexpr bool kLevelFontBold = false;
constexpr UINT kLevelTextAlignment = DT_CENTER | DT_BOTTOM;

/**
 * @brief Удаляет ранее созданные графические объекты из текущего графика.
 * @param sc Ссылочный интерфейс Sierra Chart, предоставляющий доступ к графику.
 * @param line_numbers Контейнер с идентификаторами объектов, очищается после удаления.
 * @return Ничего не возвращает.
 * @note Использует встроенную функцию Sierra Chart для адресного удаления объектов.
 * @warning Перед вызовом убедитесь, что номера относятся к текущему ChartNumber.
 */
void DeleteExistingDrawings(SCStudyGraphRef sc, std::vector<int>& line_numbers) {
  for (const int line_number : line_numbers) {
    if (line_number != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line_number);
    }
  }
  line_numbers.clear();
}

/**
 * @brief Возвращает цвет графического диапазона в зависимости от направления зоны.
 * @param direction Направление зоны в бизнес-логике Core.
 * @return COLORREF с RGB-цветом для отображения зоны.
 * @note Цвета подбирались для визуального различия покупки и продажи.
 * @warning При расширении перечня направлений потребуется обновление функции.
 */
COLORREF GetZoneColor(core::ZoneDirection direction) {
  return direction == core::ZoneDirection::kBuy ? RGB(0, 128, 0) : RGB(178, 34, 34);
}

}  // namespace

/**
 * @brief Выводит одноразовое сообщение о загрузке DLL в системный лог Sierra Chart.
 * @param sc Интерфейс Sierra Chart, предоставляющий методы логирования.
 * @return Ничего не возвращает.
 * @note Функция защищена от повторных вызовов благодаря статическому флагу.
 * @warning Не размещайте вызов в блоке SetDefaults — лог недоступен на этой фазе.
 */
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

/**
 * @brief Конвертирует строку в формате ISO-8601 в значение SCDateTime.
 * @param iso8601 Временная метка в формате YYYY-MM-DDThh:mm:ssZ (с необязательной Z).
 * @param fallback Значение, которое будет использовано при ошибке разбора.
 * @return Значение SCDateTime, совместимое с Sierra Chart.
 * @note Использует std::get_time и _mkgmtime64 для преобразования в UTC.
 * @warning При ошибках парсинга функция молча возвращает fallback.
 */
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

/**
 * @brief Строит графические объекты для отображения торгового плана на графике Sierra Chart.
 * @param sc Ссылочный интерфейс Sierra Chart для управления графическими объектами.
 * @param instrument Структура плана инструмента из ядра Core.
 * @param start_time Время начала отображения графического диапазона.
 * @param end_time Время окончания отображения (обычно правый край чарта).
 * @param line_numbers Контейнер с идентификаторами объектов; обновляется функцией.
 * @return Ничего не возвращает.
 * @note Предварительно удаляет все объекты, указанные в line_numbers, чтобы избежать дубликатов.
 * @warning Перед вызовом сохраните line_numbers в persistent-хранилище текущего study.
 */
void RenderInstrumentPlanGraphics(SCStudyGraphRef sc,
                                  const core::InstrumentPlan& instrument,
                                  SCDateTime start_time,
                                  SCDateTime end_time,
                                  std::vector<int>& line_numbers,
                                  const std::string& generated_at_iso8601) {
  DeleteExistingDrawings(sc, line_numbers);

  const double tick_size = sc.TickSize > 0.0 ? sc.TickSize : 0.25;

  const auto round_to_tick = [&](double value) {
    if (tick_size <= 0.0) {
      return value;
    }
    return std::round(value / tick_size) * tick_size;
  };

  const auto format_price = [&](double value) {
    const double adjusted = round_to_tick(value);
    SCString formatted = sc.FormatGraphValue(adjusted, sc.GetValueFormat());
    return std::string(formatted.GetChars());
  };

  const auto init_tool = [&]() {
    s_UseTool tool;
    tool.Clear();
    tool.ChartNumber = sc.ChartNumber;
    tool.Region = sc.GraphRegion;
    tool.AddMethod = UTAM_ADD_OR_ADJUST;
    tool.AddAsUserDrawnDrawing = 0;
    return tool;
  };

  const auto register_tool = [&](s_UseTool& tool) {
    sc.UseTool(tool);
    line_numbers.push_back(tool.LineNumber);
  };

  struct LabelInfo {
    bool use_relative_vertical = false;
    SCDateTime date_time{};
    float price = 0.0f;
    float relative_y = 0.0f;
    std::string text;
    UINT alignment = DT_LEFT | DT_VCENTER;
    COLORREF color = RGB(255, 165, 0);
    int font_size = 8;
    bool bold = false;
    bool multi_line = false;
  };

  const std::size_t zone_count = (instrument.zone_long.has_value() ? 1u : 0u) +
                                 (instrument.zone_short.has_value() ? 1u : 0u);

  std::vector<const core::Zone*> zones;
  zones.reserve(zone_count);
  if (instrument.zone_long.has_value()) {
    zones.push_back(&instrument.zone_long.value());
  }
  if (instrument.zone_short.has_value()) {
    zones.push_back(&instrument.zone_short.value());
  }

  std::vector<LabelInfo> labels;
  labels.reserve(zone_count * 4 + 6);

  const double start_time_value = start_time.GetAsDouble();
  const double end_time_value = end_time.GetAsDouble();
  const double zone_center_value = start_time_value + (end_time_value - start_time_value) * 0.5;
  const SCDateTime zone_center_time = zone_center_value;

  const COLORREF invalid_color = RGB(105, 105, 105);
  const COLORREF flip_color = RGB(138, 43, 226);
  const COLORREF flip_text_color = RGB(255, 255, 255);

  // Invalidation overlays (lowest z-order).
  for (const auto* zone : zones) {
    if (!zone->invalidation.has_value()) {
      continue;
    }

    const double high = round_to_tick(zone->invalidation->high);
    const double low = round_to_tick(zone->invalidation->low);
    if (high <= low) {
      continue;
    }

    auto rect = init_tool();
    rect.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
    rect.BeginDateTime = start_time;
    rect.EndDateTime = end_time;
    rect.BeginValue = static_cast<float>(high);
    rect.EndValue = static_cast<float>(low);
    rect.Color = invalid_color;
    rect.SecondaryColor = invalid_color;
    rect.TransparencyLevel = 80;
    rect.LineWidth = 1;
    rect.ExtendRight = 1;
    rect.ExtendLeft = 0;
    register_tool(rect);
  }

  // Primary zone rectangles with captions.
  for (const auto* zone : zones) {
    const double high = round_to_tick(zone->range.high);
    const double low = round_to_tick(zone->range.low);
    if (high <= low) {
      continue;
    }

    auto rect = init_tool();
    rect.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
    rect.BeginDateTime = start_time;
    rect.EndDateTime = end_time;
    rect.BeginValue = static_cast<float>(high);
    rect.EndValue = static_cast<float>(low);
    rect.Color = GetZoneColor(zone->direction);
    rect.SecondaryColor = rect.Color;
    rect.TransparencyLevel = 70;
    rect.LineWidth = 1;
    rect.ExtendRight = 1;
    rect.ExtendLeft = 0;
    register_tool(rect);

    LabelInfo label;
    label.date_time = zone_center_time;
    label.price = static_cast<float>((high + low) * 0.5);
    label.text = zone->label;
    label.alignment = DT_CENTER | DT_VCENTER;
    label.color = RGB(255, 255, 255);
    label.font_size = 9;
    label.bold = true;
    label.multi_line = true;
    labels.push_back(std::move(label));
  }

  // Flip zone (optional).
  if (instrument.flip.has_value()) {
    const auto& flip = instrument.flip.value();
    const double high = round_to_tick(flip.range.high);
    const double low = round_to_tick(flip.range.low);
    if (high > low) {
      auto rect = init_tool();
      rect.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
      rect.BeginDateTime = start_time;
      rect.EndDateTime = end_time;
      rect.BeginValue = static_cast<float>(high);
      rect.EndValue = static_cast<float>(low);
      rect.Color = flip_color;
      rect.SecondaryColor = flip_color;
      rect.TransparencyLevel = 50;
      rect.LineWidth = 1;
      rect.ExtendRight = 1;
      rect.ExtendLeft = 0;
      register_tool(rect);

      LabelInfo label;
      label.date_time = end_time;
      label.price = static_cast<float>((high + low) * 0.5);
      label.text = flip.label;
      label.alignment = DT_RIGHT | DT_VCENTER;
      label.color = flip_text_color;
      label.font_size = 9;
      label.bold = true;
      label.multi_line = true;
      labels.push_back(std::move(label));
    }
  }

  const auto add_level = [&](const core::Zone& zone, double price, COLORREF color, const char* caption) {
    const double adjusted = round_to_tick(price);

    auto line_tool = init_tool();
    line_tool.DrawingType = DRAWING_HORIZONTAL_RAY;
    line_tool.BeginDateTime = start_time;
    line_tool.EndDateTime = start_time + (1.0 / (24.0 * 60.0 * 60.0));
    line_tool.BeginValue = static_cast<float>(adjusted);
    line_tool.EndValue = static_cast<float>(adjusted);
    line_tool.Color = color;
    line_tool.LineWidth = kLevelLineWidth;
    line_tool.LineStyle = kLevelLineStyle;
    line_tool.ExtendRight = 1;
    line_tool.ExtendLeft = 0;
    register_tool(line_tool);

    LabelInfo label;
    label.date_time = end_time;
    label.price = static_cast<float>(adjusted);
    label.alignment = kLevelTextAlignment;
    label.color = color;
    label.font_size = kLevelFontSize;
    label.bold = kLevelFontBold;
    label.text = std::string(caption) + ": " + format_price(adjusted);
    labels.push_back(std::move(label));
  };

  for (const auto* zone : zones) {
    add_level(*zone, zone->sl, RGB(220, 20, 60), "SL");
    add_level(*zone, zone->tp1, RGB(50, 205, 50), "TP1");
    add_level(*zone, zone->tp2, RGB(0, 128, 0), "TP2");
  }

  // Marker label (rendered after other labels, before vertical line).
  LabelInfo marker_label;
  marker_label.date_time = start_time;
  marker_label.use_relative_vertical = true;
  marker_label.relative_y = 98.0f;
  marker_label.text = "Plan generated " + generated_at_iso8601;
  marker_label.alignment = DT_LEFT | DT_TOP;
  marker_label.color = RGB(255, 204, 0);
  marker_label.font_size = 9;
  marker_label.bold = true;
  labels.push_back(std::move(marker_label));

  for (const auto& label : labels) {
    auto text_tool = init_tool();
    text_tool.DrawingType = DRAWING_TEXT;
    text_tool.Color = label.color;
    text_tool.FontFace = kLevelFontFace;
    text_tool.FontSize = label.font_size;
    text_tool.FontBold = label.bold ? 1 : 0;
    text_tool.TextAlignment = label.alignment;
    text_tool.MultiLineLabel = label.multi_line ? 1 : 0;

    text_tool.BeginDateTime = label.date_time;
    text_tool.EndDateTime = label.date_time;

    if (label.use_relative_vertical) {
      text_tool.UseRelativeVerticalValues = 1;
      text_tool.BeginValue = label.relative_y;
    } else {
      text_tool.BeginValue = label.price;
    }

    text_tool.Text = label.text.c_str();
    text_tool.EndValue = text_tool.BeginValue;
    register_tool(text_tool);
  }  auto marker = init_tool();
  marker.DrawingType = DRAWING_VERTICALLINE;
  marker.BeginDateTime = start_time;
  marker.EndDateTime = start_time;
  marker.Color = RGB(255, 204, 0);
  marker.LineWidth = 2;
  register_tool(marker);
}

}  // namespace sierra::acsil


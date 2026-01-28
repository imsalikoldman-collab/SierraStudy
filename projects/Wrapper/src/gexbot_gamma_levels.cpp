#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/gexbot_gamma.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if __has_include(<plog/Log.h>)
#define SIERRA_GEXBOT_HAS_PLOG 1
#include <filesystem>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>
#else
#define SIERRA_GEXBOT_HAS_PLOG 0
#endif

namespace {

using sierra::core::GammaLevel;
using sierra::core::GammaResponse;
using sierra::core::MapChartSymbolToGexbotTicker;
using sierra::core::ParseGexbotGammaResponse;

enum RequestState { kIdle = 0, kRequestSent = 1 };

constexpr int kPersistLoggingFlag = 2;
constexpr int kPersistRequestState = 10;
constexpr int kPersistLastRequestTime = 11;
constexpr int kPersistLastResponseTimestamp = 12;
constexpr int kPersistLastLogTime = 13;
constexpr int kPersistDrawnLinesPtr = 20;
constexpr int kPersistStatusLineId = 21;
constexpr int kPersistCachedLevelsPtr = 22;
constexpr int kPersistCachedTimestamp = 23;
constexpr int kPersistLastRequestSentTime = 24;

constexpr int kLineIdBase = 200000;
constexpr int kTextIdOffset = 800000;
constexpr int kStatusLineNumber = 910000;

constexpr double kMinLogIntervalSeconds = 30.0;

/// @brief Безопасно преобразует SCString в std::string.
std::string ToStd(const SCString& s) {
  return std::string(s.GetChars());
}

/// @brief Преобразует значение в компактную строку (K/M) с двумя знаками после запятой.
std::string FormatExposure(double value) {
  std::ostringstream oss;
  const double abs_val = std::fabs(value);
  if (abs_val >= 1'000'000.0) {
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << (value / 1'000'000.0) << "M";
  } else if (abs_val >= 1'000.0) {
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << (value / 1'000.0) << "K";
  } else {
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << value;
  }
  return oss.str();
}

/// @brief Маскирует значение API key в URL для безопасного логирования.
std::string MaskApiKey(const std::string& url) {
  const std::string key_param = "key=";
  const std::size_t pos = url.find(key_param);
  if (pos == std::string::npos) {
    return url;
  }
  std::size_t value_pos = pos + key_param.size();
  std::size_t end = url.find_first_of("&", value_pos);
  std::string masked = url;
  masked.replace(value_pos, (end == std::string::npos ? masked.size() : end) - value_pos, "******");
  return masked;
}

/// @brief Создаёт или возвращает вектор идентификаторов линий, сохранённый в persistent.
std::vector<int>* GetOrCreateDrawnLinesCache(SCStudyGraphRef sc) {
  auto* ptr = reinterpret_cast<std::vector<int>*>(sc.GetPersistentPointer(kPersistDrawnLinesPtr));
  if (ptr == nullptr) {
    ptr = new std::vector<int>();
    sc.SetPersistentPointer(kPersistDrawnLinesPtr, ptr);
  }
  return ptr;
}

/// @brief Возвращает (создаёт) кэш последних уровней.
std::vector<GammaLevel>* GetOrCreateLevelsCache(SCStudyGraphRef sc) {
  auto* ptr = reinterpret_cast<std::vector<GammaLevel>*>(sc.GetPersistentPointer(kPersistCachedLevelsPtr));
  if (ptr == nullptr) {
    ptr = new std::vector<GammaLevel>();
    sc.SetPersistentPointer(kPersistCachedLevelsPtr, ptr);
  }
  return ptr;
}

/// @brief Запрещает частый спам в лог, возвращает true если можно логировать.
bool ShouldLog(SCStudyGraphRef sc, double now_seconds) {
  double& last_log = sc.GetPersistentDouble(kPersistLastLogTime);
  if (now_seconds - last_log < kMinLogIntervalSeconds) {
    return false;
  }
  last_log = now_seconds;
  return true;
}

#if SIERRA_GEXBOT_HAS_PLOG
void EnsureLogging(SCStudyGraphRef sc) {
  if (sc.Index != 0) {
    return;
  }

  const int initialized = sc.GetPersistentInt(kPersistLoggingFlag);
  if (initialized == 1 || initialized == -1) {
    return;
  }

  try {
    std::filesystem::create_directories("Logs");
    plog::init(plog::info, "Logs/SierraStudy.log", 5 * 1024 * 1024, 3);
    sc.SetPersistentInt(kPersistLoggingFlag, 1);
    PLOG_INFO << "Gexbot gamma study logging initialized";
  } catch (...) {
    sc.SetPersistentInt(kPersistLoggingFlag, -1);
  }
}
#else
void EnsureLogging(SCStudyGraphRef) {}
#endif

/// @brief Вычисляет уникальный LineNumber для отрезка уровня.
int MakeLineId(double strike, double tick_size) {
  if (tick_size <= 0.0) {
    tick_size = 0.25;  // разумная защита
  }
  const long long bucket = static_cast<long long>(std::llround(strike / tick_size));
  return kLineIdBase + static_cast<int>(bucket % 300000);
}

/// @brief Удаляет все ранее созданные линии/подписи.
void ClearAllDrawings(SCStudyGraphRef sc) {
  auto* cache = GetOrCreateDrawnLinesCache(sc);
  for (int id : *cache) {
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
  }
  cache->clear();
}

/// @brief Рисует статус индикатора в левом нижнем углу.
void DrawStatusIndicator(SCStudyGraphRef sc, const char* text, COLORREF color, int anchor_index) {
  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.LineNumber = kStatusLineNumber;
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.DrawingType = DRAWING_TEXT;
  tool.UseRelativeVerticalValues = 1;   // вертикаль фиксированно от нижнего края
  tool.BeginIndex = anchor_index;        // левый край видимой области
  tool.BeginValue = 2;                  // 2% от нижнего края окна
  tool.Color = color;
  tool.FontSize = 10;
  tool.Region = 0;
  tool.Text = text;
  tool.AddAsUserDrawnDrawing = 1;
  sc.UseTool(tool);
}

/// @brief Рисует уровень и подпись (если включена).
void DrawLevel(SCStudyGraphRef sc,
               int begin_index,
               int end_index,
               double strike,
               const GammaLevel& level,
               int line_style,
               int line_width,
               COLORREF color,
               bool text_enabled,
               int text_font_size,
               int text_offset_ticks,
               std::vector<int>& out_ids) {
  const double tick = sc.TickSize > 0.0 ? sc.TickSize : 0.25;
  const int line_id = MakeLineId(strike, tick);

  s_UseTool line;
  line.Clear();
  line.ChartNumber = sc.ChartNumber;
  line.LineNumber = line_id;
  line.AddMethod = UTAM_ADD_OR_ADJUST;
  line.DrawingType = DRAWING_LINE;
  line.BeginIndex = begin_index;
  line.EndIndex = end_index;
  line.BeginValue = strike;
  line.EndValue = strike;
  line.LineStyle = static_cast<SubgraphLineStyles>(line_style);
  line.LineWidth = line_width;
  line.Color = color;
  line.Region = 0;
  sc.UseTool(line);
  out_ids.push_back(line_id);

  if (!text_enabled) {
    return;
  }

  s_UseTool text;
  text.Clear();
  text.ChartNumber = sc.ChartNumber;
  text.LineNumber = line_id + kTextIdOffset;
  text.AddMethod = UTAM_ADD_OR_ADJUST;
  text.DrawingType = DRAWING_TEXT;
  text.BeginIndex = end_index;
  text.BeginValue = strike + static_cast<double>(text_offset_ticks) * tick;
  text.Text = ("G: " + FormatExposure(level.specified_greek)).c_str();
  text.Color = color;
  text.FontSize = text_font_size;
  text.Region = 0;
  sc.UseTool(text);
  out_ids.push_back(text.LineNumber);
}

/// @brief Конвертирует кастомный список Sierra в enum LineStyle.
int LineStyleFromInput(const SCInputRef& input) {
  switch (input.GetIndex()) {
    case 1:
      return LINESTYLE_DASH;
    case 2:
      return LINESTYLE_DOT;
    case 3:
      return LINESTYLE_DASHDOT;
    case 4:
      return LINESTYLE_DASHDOTDOT;
    case 5:
      return LINESTYLE_ALTERNATE;
    case 0:
    default:
      return LINESTYLE_SOLID;
  }
}

void RenderLevels(SCStudyGraphRef sc,
                  const std::vector<GammaLevel>& levels,
                  int line_begin,
                  int last_index,
                  int line_style,
                  int line_width,
                  COLORREF line_color,
                  bool text_enabled,
                  int text_font,
                  int text_offset_ticks,
                  std::vector<int>& out_ids) {
  for (const auto& level : levels) {
    DrawLevel(sc, line_begin, last_index, level.strike, level, line_style, line_width, line_color, text_enabled,
              text_font, text_offset_ticks, out_ids);
  }
}

}  // namespace

/**
 * @brief ACSIL-study «Gexbot Gamma Levels» — получает gamma-профиль через HTTP и рисует положительные уровни.
 * @param sc Контекст Sierra Chart.
 * @return void.
 * @note Логика: state-machine HTTP → парсинг JSON в Core → отрисовка отрезков и подписей справа на графике.
 * @warning Требуется задать API Key и поддерживаемый тикер графика: ES/MES → ES_SPX; NQ/MNQ → NQ_NDX (другие сервер не принимает). Частота запросов 5–30 сек.
 */
SCSFExport scsf_GexbotGammaLevels(SCStudyGraphRef sc) {
  sierra::acsil::LogDllStartup(sc);

  SCInputRef inputApiKey = sc.Input[0];
  SCInputRef inputGammaType = sc.Input[1];
  SCInputRef inputUpdateSec = sc.Input[2];
  SCInputRef inputRequestEnabled = sc.Input[3];
  SCInputRef inputLineStyle = sc.Input[4];
  SCInputRef inputLineWidth = sc.Input[5];
  SCInputRef inputLineColor = sc.Input[6];
  SCInputRef inputLineLength = sc.Input[7];
  SCInputRef inputRightWidth = sc.Input[8];
  SCInputRef inputTextEnabled = sc.Input[9];
  SCInputRef inputTextFont = sc.Input[10];
  SCInputRef inputTextOffset = sc.Input[11];
  SCInputRef inputMaxLevels = sc.Input[12];
  SCInputRef inputDebug = sc.Input[13];

  if (sc.SetDefaults) {
    sc.GraphName = "Gexbot Gamma Levels (State)";
    sc.StudyDescription = "Отрисовка уровней положительной гаммы из Gexbot Options Profile Greeks.";
    sc.GraphRegion = 0;
    sc.AutoLoop = 0;
    sc.FreeDLL = 1;

    inputApiKey.Name = "API Key";
    inputApiKey.SetString("");

    inputGammaType.Name = "Gamma Type";
    inputGammaType.SetCustomInputStrings("gamma_zero;gamma_one");
    inputGammaType.SetCustomInputIndex(0);

    inputUpdateSec.Name = "Update Period (sec)";
    inputUpdateSec.SetInt(10);
    inputUpdateSec.SetIntLimits(5, 30);

    inputRequestEnabled.Name = "Request Enabled";
    inputRequestEnabled.SetYesNo(true);

    inputLineStyle.Name = "Line Style";
    inputLineStyle.SetCustomInputStrings("Solid;Dash;Dot;DashDot;DashDotDot;Alternate");
    inputLineStyle.SetCustomInputIndex(0);

    inputLineWidth.Name = "Line Width (px)";
    inputLineWidth.SetInt(2);
    inputLineWidth.SetIntLimits(1, 6);

    inputLineColor.Name = "Line Color";
    inputLineColor.SetColor(RGB(0, 180, 90));

    inputLineLength.Name = "Line Length (bars)";
    inputLineLength.SetInt(60);
    inputLineLength.SetIntLimits(10, 200);

    inputRightWidth.Name = "Right Region Width (bars)";
    inputRightWidth.SetInt(250);
    inputRightWidth.SetIntLimits(100, 500);

    inputTextEnabled.Name = "Text Enabled";
    inputTextEnabled.SetYesNo(true);

    inputTextFont.Name = "Text Font Size";
    inputTextFont.SetInt(9);
    inputTextFont.SetIntLimits(6, 16);

    inputTextOffset.Name = "Text Offset (ticks)";
    inputTextOffset.SetInt(2);
    inputTextOffset.SetIntLimits(0, 20);

    inputMaxLevels.Name = "Max Levels";
    inputMaxLevels.SetInt(80);
    inputMaxLevels.SetIntLimits(1, 200);

    inputDebug.Name = "Debug Logging";
    inputDebug.SetYesNo(false);

    return;
  }

  EnsureLogging(sc);

  const double now_seconds = sc.CurrentSystemDateTime.GetAsDouble() * SECONDS_PER_DAY;
  const bool request_enabled = inputRequestEnabled.GetYesNo();

  if (sc.LastCallToFunction) {
    ClearAllDrawings(sc);
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, kStatusLineNumber);
    return;
  }

  auto* drawn_ids = GetOrCreateDrawnLinesCache(sc);

  const std::string api_key = ToStd(inputApiKey.GetString());
  if (api_key.empty()) {
    if (request_enabled && ShouldLog(sc, now_seconds)) {
      sc.AddMessageToLog("Gexbot gamma: API key не задан, запросы остановлены", 1);
    }
    ClearAllDrawings(sc);
    sc.GetPersistentInt(kPersistRequestState) = kIdle;
    return;
  }

  const std::string chart_symbol = ToStd(sc.Symbol);
  const std::string ticker = MapChartSymbolToGexbotTicker(chart_symbol);
  if (ticker.empty()) {
    if (request_enabled && ShouldLog(sc, now_seconds)) {
      SCString msg;
      msg.Format("Gexbot gamma: неподдерживаемый символ '%s'", sc.Symbol.GetChars());
      sc.AddMessageToLog(msg, 1);
    }
    ClearAllDrawings(sc);
    sc.GetPersistentInt(kPersistRequestState) = kIdle;
    return;
  }

  const int gamma_index = inputGammaType.GetIndex();
  const std::string gamma_type = gamma_index == 1 ? "gamma_one" : "gamma_zero";

  const int update_period = std::clamp(inputUpdateSec.GetInt(), 5, 30);
  const int line_length = std::clamp(inputLineLength.GetInt(), 10, 200);
  const int right_width = std::clamp(inputRightWidth.GetInt(), 100, 500);
  const int max_levels = std::clamp(inputMaxLevels.GetInt(), 1, 200);
  const int line_style = LineStyleFromInput(inputLineStyle);
  const int line_width = std::clamp(inputLineWidth.GetInt(), 1, 6);
  const COLORREF line_color = inputLineColor.GetColor();
  const bool text_enabled = inputTextEnabled.GetYesNo();
  const int text_font = std::clamp(inputTextFont.GetInt(), 6, 16);
  const int text_offset_ticks = std::clamp(inputTextOffset.GetInt(), 0, 20);
  const bool debug = inputDebug.GetYesNo();

  int& request_state = sc.GetPersistentInt(kPersistRequestState);
  double& last_request_time = sc.GetPersistentDouble(kPersistLastRequestTime);
  double& last_timestamp = sc.GetPersistentDouble(kPersistLastResponseTimestamp);
  auto* cached_levels = GetOrCreateLevelsCache(sc);
  double& cached_timestamp = sc.GetPersistentDouble(kPersistCachedTimestamp);
  double& last_request_sent_time = sc.GetPersistentDouble(kPersistLastRequestSentTime);

  const double seconds_since_last_request = now_seconds - last_request_time;

  const int last_index = sc.ArraySize - 1;
  const int right_begin = std::max(0, last_index - right_width);
  auto show_status = [&](const char* msg, COLORREF color) {
    DrawStatusIndicator(sc, msg, color, right_begin);
  };

  if (!request_enabled) {
    ClearAllDrawings(sc);
    request_state = kIdle;
    show_status("● нет запроса", RGB(220, 20, 60));
    return;
  }

  // Отправка запроса
  if (request_state == kIdle && seconds_since_last_request >= static_cast<double>(update_period)) {
    SCString url;
    url.Format("https://api.gexbot.com/%s/state/%s?key=%s", ticker.c_str(), gamma_type.c_str(), api_key.c_str());
    const std::string url_masked = MaskApiKey(ToStd(url));

    // сбрасываем старый буфер ответа перед новым запросом
    sc.HTTPResponse = "";

    if (!sc.MakeHTTPRequest(url)) {
      if (ShouldLog(sc, now_seconds)) {
        sc.AddMessageToLog("Gexbot gamma: не удалось отправить HTTP запрос", 1);
      }
      last_request_time = now_seconds;
      show_status("● нет запроса", RGB(220, 20, 60));
    } else {
      request_state = kRequestSent;
      last_request_time = now_seconds;
      last_request_sent_time = now_seconds;
      show_status("● нет ответа", RGB(255, 215, 0));
      if (ShouldLog(sc, now_seconds)) {
        SCString msg;
        msg.Format("Gexbot gamma: запрос отправлен %s", url_masked.c_str());
        sc.AddMessageToLog(msg, 0);
      }
    }
  }

  if (request_state != kRequestSent) {
    if (!cached_levels->empty()) {
      std::vector<int> ids;
      ids.reserve(cached_levels->size() * 2);
      const int line_begin = std::max(right_begin, last_index - line_length);
      RenderLevels(sc, *cached_levels, line_begin, last_index, line_style, line_width, line_color, text_enabled,
                   text_font, text_offset_ticks, ids);
      *drawn_ids = std::move(ids);
      show_status("● ОК", RGB(0, 180, 90));
    } else if (last_timestamp > 0.0) {
      show_status("● ОК", RGB(0, 180, 90));
    } else {
      show_status("● нет запроса", RGB(220, 20, 60));
    }
    return;
  }

  if (sc.HTTPResponse.GetLength() == 0) {
    // Если истёк период опроса и ответа нет — считаем тайм‑аутом и сбрасываем запрос.
    const double wait_time = now_seconds - last_request_sent_time;
    if (wait_time >= static_cast<double>(update_period)) {
      if (ShouldLog(sc, now_seconds)) {
        SCString msg;
        msg.Format("Gexbot gamma: нет ответа (ожидали %.1f сек)", wait_time);
        sc.AddMessageToLog(msg, 1);
      }
      request_state = kIdle;
    }
    show_status("● нет ответа", RGB(255, 215, 0));
    return;  // ответ ещё не готов или тайм‑аут
  }

  const std::string response = ToStd(sc.HTTPResponse);
  sc.HTTPResponse = "";
  request_state = kIdle;

  if (response == "HTTP_REQUEST_ERROR" || response == "ERROR" || response == "HTTP_REQUEST_TIMEOUT") {
    if (ShouldLog(sc, now_seconds)) {
      sc.AddMessageToLog("Gexbot gamma: ошибка HTTP ответа", 1);
    }
    show_status("● нет ответа", RGB(255, 215, 0));
    return;
  }

  if (ShouldLog(sc, now_seconds)) {
    const std::string prefix = response.substr(0, 50);
    SCString msg;
    msg.Format("Gexbot gamma: ответ получен, bytes=%d, prefix='%s'", static_cast<int>(response.size()),
               prefix.c_str());
    sc.AddMessageToLog(msg, 0);
  }

  GammaResponse parsed{};
  try {
    parsed = ParseGexbotGammaResponse(response, static_cast<std::size_t>(max_levels));
  } catch (const std::exception& ex) {
    if (ShouldLog(sc, now_seconds)) {
      SCString msg;
      msg.Format("Gexbot gamma: ошибка парсинга JSON (%s)", ex.what());
      sc.AddMessageToLog(msg, 1);
    }
    show_status("● нет ответа", RGB(255, 215, 0));
    return;
  }

  if (parsed.levels.empty()) {
    ClearAllDrawings(sc);
    cached_levels->clear();
    cached_timestamp = 0.0;
    if (ShouldLog(sc, now_seconds)) {
      sc.AddMessageToLog("Gexbot gamma: данных нет (нет положительной гаммы)", 0);
    }
    last_timestamp = static_cast<double>(parsed.timestamp);
    show_status("● ОК", RGB(0, 180, 90));
    return;
  }

  if (static_cast<std::int64_t>(last_timestamp) == parsed.timestamp) {
    return;  // данные не изменились
  }

  const int line_begin = std::max(right_begin, last_index - line_length);

  std::vector<int> new_ids;
  new_ids.reserve(parsed.levels.size() * 2);
  RenderLevels(sc, parsed.levels, line_begin, last_index, line_style, line_width, line_color, text_enabled, text_font,
               text_offset_ticks, new_ids);

  // Удаляем устаревшие линии
  for (int id : *drawn_ids) {
    if (std::find(new_ids.begin(), new_ids.end(), id) == new_ids.end()) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
    }
  }

  *drawn_ids = std::move(new_ids);
  last_timestamp = static_cast<double>(parsed.timestamp);
  *cached_levels = parsed.levels;
  cached_timestamp = last_timestamp;
  if (ShouldLog(sc, now_seconds)) {
    SCString msg;
    msg.Format("Gexbot gamma: OK timestamp=%lld levels=%d", static_cast<long long>(parsed.timestamp),
               static_cast<int>(parsed.levels.size()));
    sc.AddMessageToLog(msg, 0);
    // Логируем первые 50 символов ответа для трассировки без избыточности.
    const std::string prefix = response.substr(0, 50);
    SCString raw;
    raw = prefix.c_str();
    sc.AddMessageToLog(raw, 0);
  }
  show_status("● ОК", RGB(0, 180, 90));

  if (debug && ShouldLog(sc, now_seconds)) {
    SCString msg;
    msg.Format("Gexbot gamma: обновлено %d уровней, timestamp=%lld", static_cast<int>(parsed.levels.size()),
               static_cast<long long>(parsed.timestamp));
    sc.AddMessageToLog(msg, 0);
  }
}

#include "sierra/acsil/study.hpp"

#include <curl/curl.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/// \brief Пользовательские исследования Sierra Chart <Add Custom Study>.
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistState = 1;
constexpr double kSecondsPerDay = 24.0 * 60.0 * 60.0;
constexpr double kDefaultPollInterval = 30.0;
constexpr double kMinPollInterval = 10.0;
constexpr double kMaxPollInterval = 100.0;

/**
 * @brief Сохраняет данные опроса GexBot в persistent-памяти.
 * @note Данные живут между вызовами study до выгрузки DLL.
 * @warning Удаляется при sc.LastCallToFunction.
 */
struct GexState {
  double last_poll_time = 0.0;
  double poll_interval = kDefaultPollInterval;
  std::string last_text;
  // Структурированные данные для последующего отображения/сортировки.
  struct KeyLevels {
    double major_positive = std::numeric_limits<double>::quiet_NaN();
    double major_negative = std::numeric_limits<double>::quiet_NaN();
    double major_long_gamma = std::numeric_limits<double>::quiet_NaN();
    double major_short_gamma = std::numeric_limits<double>::quiet_NaN();
  } key_levels;
  struct MiniContract {
    double strike = std::numeric_limits<double>::quiet_NaN();
    double specified_greek = std::numeric_limits<double>::quiet_NaN();
  };
  std::vector<MiniContract> mini_contracts;
  // Состояние панельного отображения греков.
  double last_visible_low = std::numeric_limits<double>::quiet_NaN();
  double last_visible_high = std::numeric_limits<double>::quiet_NaN();
  std::vector<int> panel_line_numbers;
  int line_major_long = 0;
  int line_major_short = 0;
};

/**
 * @brief Обработчик записи для libcurl (складывает ответ в std::string).
 * @param ptr Буфер libcurl.
 * @param size Размер элемента.
 * @param nmemb Количество элементов.
 * @param userdata Указатель на std::string для накопления.
 * @return Количество записанных байт.
 * @note Возвращение меньшего числа приведёт к ошибке curl.
 * @warning Userdata должен указывать на валидную std::string.
 */
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* buffer = static_cast<std::string*>(userdata);
  const size_t total = size * nmemb;
  buffer->append(ptr, total);
  return total;
}

/**
 * @brief Приводит строку к верхнему регистру ASCII.
 * @param value Исходная строка.
 * @return Строка в верхнем регистре.
 * @note Используется для нормализации символа графика.
 * @warning Работает корректно только с ASCII-символами.
 */
std::string ToUpperAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

/**
 * @brief Маппинг символа графика в тикер GexBot.
 * @param symbol Символ из `sc.Symbol`.
 * @return Тикер GexBot или пустая строка, если символ не поддержан.
 * @note Поддерживаются ES/MES → SPX_ES и NQ/MNQ → NQ_NDX.
 * @warning Для прочих символов возвращается пустая строка — опрос не выполняется.
 */
std::string MapSymbolToTicker(const SCString& symbol) {
  const std::string upper = ToUpperAscii(symbol.GetChars());
  if (upper.find("MES") != std::string::npos || upper.find(" ES") != std::string::npos ||
      upper.rfind("ES", 0) == 0) {
    return "ES_SPX";
  }
  if (upper.find("MNQ") != std::string::npos || upper.find(" NQ") != std::string::npos ||
      upper.rfind("NQ", 0) == 0) {
    return "NQ_NDX";
  }
  return {};
}

/**
 * @brief Возвращает разницу времени в секундах между now и last.
 * @param now Текущее время (SCDateTime как double).
 * @param last Предыдущее время (SCDateTime как double).
 * @return Интервал в секундах; бесконечность, если last == 0.
 * @note Переводит дни Sierra Chart в секунды.
 * @warning При неверных данных может вернуть NaN или inf.
 */
double SecondsSince(double now, double last) {
  if (last == 0.0) return std::numeric_limits<double>::infinity();
  return (now - last) * kSecondsPerDay;
}

/**
 * @brief Безопасно преобразует строку в double, возвращает NaN при ошибке.
 * @param text Строка с числом.
 * @return double значение или NaN.
 */
double SafeToDouble(const std::string& text) {
  try {
    size_t idx = 0;
    const double v = std::stod(text, &idx);
    if (idx == text.size()) return v;
  } catch (...) {
  }
  return std::numeric_limits<double>::quiet_NaN();
}

std::string FormatDouble(double v, int precision = 4) {
  if (std::isnan(v)) return "NaN";
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << v;
  return oss.str();
}

/**
 * @brief Безопасно извлекает число с плавающей точкой из узла RapidYAML.
 * @param node Узел-скаляр JSON/YAML, содержащий число.
 * @param out Указатель для записи результата.
 * @return true если парсинг прошёл успешно, иначе false.
 * @note Возвращает false для пустых, null-значений и нечисловых скаляров.
 * @warning Не бросает исключения — проверяйте возвращаемое значение.
 */
bool TryParseDouble(ryml::ConstNodeRef node, double* out) {
  if (!node.readable() || !node.has_val() || node.val_is_null()) {
    return false;
  }

  double value{};
  if (!c4::from_chars(node.val(), &value)) {
    return false;
  }

  *out = value;
  return true;
}

/**
 * @brief Читает необязательное числовое поле из JSON-объекта.
 * @param parent Родительский узел (классический объект JSON).
 * @param key Имя искомого поля.
 * @return Значение double или NaN, если поле отсутствует либо не число.
 * @note Упрощает обработку необязательных ключей API без выброса исключений.
 * @warning Не различает отсутствующее поле и некорректное число — в обоих случаях возвращает NaN.
 */
double ReadOptionalDouble(const ryml::ConstNodeRef& parent, const char* key) {
  double value{};
  const auto node = parent.find_child(key);
  if (TryParseDouble(node, &value)) {
    return value;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

/**
 * @brief Парсит массив mini_contracts из JSON-ответа GexBot через RapidYAML.
 * @param root Корневой узел JSON-документа.
 * @param error_out Строка для диагностики; заполняется при критической ошибке структуры.
 * @return Отсортированный по strike вектор mini_contracts.
 * @note Отсутствующие или частично некорректные элементы пропускаются, чтобы не прерывать опрос.
 * @warning При полностью неверной структуре блока возвращается пустой вектор и сообщение об ошибке.
 */
std::vector<GexState::MiniContract> ParseMiniContracts(const ryml::ConstNodeRef& root,
                                                       std::string& error_out) {
  std::vector<GexState::MiniContract> result;

  const auto node = root.find_child("mini_contracts");
  if (!node.readable()) {
    return result;  // поле необязательно, просто вернём пустой список
  }

  if (!node.is_seq()) {
    error_out = "parse error: mini_contracts must be array";
    return {};
  }

  for (const auto& child : node.children()) {
    if (!child.is_seq() || child.num_children() < 4u) {
      continue;  // пропускаем некорректный элемент, не рвём весь запрос
    }

    double strike{};
    double greek{};
    if (!TryParseDouble(child.child(0), &strike)) {
      continue;
    }
    if (!TryParseDouble(child.child(3), &greek)) {
      continue;
    }

    if (greek > 0.0) {
      result.push_back(GexState::MiniContract{strike, greek});
    }
  }

  std::sort(result.begin(), result.end(),
            [](const GexState::MiniContract& a, const GexState::MiniContract& b) { return a.strike < b.strike; });
  return result;
}

/**
 * @brief Удаляет ранее созданные линии панели из графика.
 * @param sc Контекст Sierra Chart.
 * @param line_numbers Номера линий, подлежащие удалению.
 * @note Позволяет избежать захламления графика объектами UseTool.
 * @warning Перед новым рендером всегда очищайте старые линии.
 */
void ClearGreekPanelLines(SCStudyGraphRef sc, std::vector<int>& line_numbers) {
  for (const int line : line_numbers) {
    if (line != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line);
    }
  }
  line_numbers.clear();
}

/**
 * @brief Описывает уровень для панельной отрисовки.
 */
struct GreekPanelLevel {
  double strike{};
  double greek{};
};

/**
 * @brief Формирует уникальный набор уровней (max greek на strike) в пределах видимого диапазона.
 * @param contracts Вектор mini_contracts из состояния.
 * @param visible_low Нижняя граница видимого ценового диапазона.
 * @param visible_high Верхняя граница видимого ценового диапазона.
 * @return Отсортированный по strike вектор уровней (все положительные внутри диапазона).
 * @note Дубликаты объединяются по максимуму положительного значения.
 */
std::vector<GreekPanelLevel> BuildVisiblePositiveLevels(const std::vector<GexState::MiniContract>& contracts,
                                                        double visible_low,
                                                        double visible_high) {
  constexpr double kEps = 1e-9;
  double low = std::min(visible_low, visible_high);
  double high = std::max(visible_low, visible_high);
  if (std::abs(high - low) < kEps) {
    high = low + 1.0;
  }
  std::map<double, double> best_by_strike;
  for (const auto& mc : contracts) {
    if (std::isnan(mc.strike) || std::isnan(mc.specified_greek)) {
      continue;
    }
    if (mc.specified_greek <= 0.0) {
      continue;
    }
    if (mc.strike < low - kEps || mc.strike > high + kEps) {
      continue;
    }
    auto it = best_by_strike.lower_bound(mc.strike);
    bool merged = false;
    if (it != best_by_strike.end() && std::abs(it->first - mc.strike) < kEps) {
      it->second = std::max(it->second, mc.specified_greek);
      merged = true;
    } else if (it != best_by_strike.begin()) {
      auto prev = std::prev(it);
      if (std::abs(prev->first - mc.strike) < kEps) {
        prev->second = std::max(prev->second, mc.specified_greek);
        merged = true;
      }
    }
    if (!merged) {
      best_by_strike.emplace(mc.strike, mc.specified_greek);
    }
  }

  std::vector<GreekPanelLevel> levels;
  levels.reserve(best_by_strike.size());
  for (const auto& [strike, greek] : best_by_strike) {
    levels.push_back(GreekPanelLevel{strike, greek});
  }
  return levels;
}

/**
 * @brief Преобразует индекс выбора стиля в ACSIL LineStyle.
 * @param style_index 0=Solid, 1=Dashed, 2=Dotted, 3=DashDot.
 * @return Константа LINESTYLE_* для UseTool.
 */
int ToLineStyle(int style_index) {
  switch (style_index) {
    case 1:
      return LINESTYLE_DASH;
    case 2:
      return LINESTYLE_DOT;
    case 3:
      return LINESTYLE_DASHDOT;
    default:
      return LINESTYLE_SOLID;
  }
}

/**
 * @brief Вычисляет количество баров, соответствующее заданной длине в пикселях.
 * @param sc Контекст Sierra Chart.
 * @param length_px Требуемая длина в пикселях.
 * @return Целое количество баров (минимум 1).
 * @note Использует усреднённый пиксельный шаг между первым и последним видимым баром.
 * @warning При нулевой ширине возвращает 1, чтобы предотвратить нулевую длину линий.
 */
int PixelsToBars(SCStudyGraphRef sc, double length_px) {
  const int first = sc.IndexOfFirstVisibleBar;
  const int last = sc.IndexOfLastVisibleBar;
  if (last <= first) {
    return 1;
  }
  const int x_first = sc.BarIndexToXPixelCoordinate(first);
  const int x_last = sc.BarIndexToXPixelCoordinate(last);
  const double px_per_bar = static_cast<double>(x_last - x_first) / static_cast<double>(last - first);
  if (px_per_bar <= 0.0) {
    return 1;
  }
  const double bars = length_px / px_per_bar;
  return std::max(1, static_cast<int>(std::round(bars)));
}

/**
 * @brief Отрисовывает панель уровней specified_greek справа от графика.
 * @param sc Контекст ACSIL.
 * @param state Persistent-состояние GexBot опросчика.
 * @param display_mode 1 = Positive Only; 2 = All (заглушка).
 * @param panel_width_px Ширина панели в пикселях.
 * @param panel_offset_px Отступ панели от правой границы графика (пиксели).
 * @param line_color Цвет линий.
 * @param line_width Толщина линий.
 * @param auto_scale_on Флаг автонормализации (на текущем этапе всегда true).
 * @note Логика следует docs/tech_task_gexbot_greek_panel.md (Mode 1).
 * @warning Mode 2 реализован как заглушка без отрисовки.
 */
void RenderGreekPanel(SCStudyGraphRef sc,
                      GexState& state,
                      int display_mode,
                      int panel_width_px,
                      int panel_offset_px,
                      COLORREF line_color,
                      int line_width,
                      bool auto_scale_on,
                      bool draw_underneath,
                      bool debug_log) {
  // Заглушка для Mode 2 — просто очищаем старые линии.
  if (display_mode == 2) {
    ClearGreekPanelLines(sc, state.panel_line_numbers);
    return;
  }

  if (panel_width_px <= 0 || line_width <= 0 || state.mini_contracts.empty()) {
    ClearGreekPanelLines(sc, state.panel_line_numbers);
    return;
  }

  const double visible_low = sc.ScaleRangeBottom;
  const double visible_high = sc.ScaleRangeTop;

  // Подсчёт всех позитивных уровней для fallback.
  int total_positive = 0;
  for (const auto& mc : state.mini_contracts) {
    if (!std::isnan(mc.specified_greek) && mc.specified_greek > 0.0) {
      ++total_positive;
    }
  }

  const auto levels = BuildVisiblePositiveLevels(state.mini_contracts, visible_low, visible_high);
  std::vector<GreekPanelLevel> levels_to_draw = levels;
  if (levels_to_draw.empty() && total_positive > 0) {
    // Если все уровни вне диапазона — рисуем все позитивные.
    for (const auto& mc : state.mini_contracts) {
      if (!std::isnan(mc.specified_greek) && mc.specified_greek > 0.0) {
        levels_to_draw.push_back(GreekPanelLevel{mc.strike, mc.specified_greek});
      }
    }
  }
  if (levels_to_draw.empty()) {
    ClearGreekPanelLines(sc, state.panel_line_numbers);
    state.last_visible_low = visible_low;
    state.last_visible_high = visible_high;
    if (debug_log) {
      sc.AddMessageToLog("GexPanel: no positive levels", 0);
    }
    return;
  }

  double max_greek = 0.0;
  for (const auto& level : levels_to_draw) {
    max_greek = std::max(max_greek, level.greek);
  }
  if (max_greek <= 0.0 || !auto_scale_on) {
    ClearGreekPanelLines(sc, state.panel_line_numbers);
    state.last_visible_low = visible_low;
    state.last_visible_high = visible_high;
    return;
  }

  const int right_bar = sc.IndexOfLastVisibleBar;
  const int left_bar = sc.IndexOfFirstVisibleBar;
  if (right_bar < 0 || left_bar < 0 || sc.ArraySize <= 0) {
    ClearGreekPanelLines(sc, state.panel_line_numbers);
    return;
  }

  const int x_first = sc.BarIndexToXPixelCoordinate(left_bar);
  const int x_last = sc.BarIndexToXPixelCoordinate(right_bar);
  const int width_px = std::max(1, x_last - x_first);
  const int visible_bars = std::max(1, right_bar - left_bar);
  const double px_per_bar = static_cast<double>(width_px) / static_cast<double>(visible_bars);

  ClearGreekPanelLines(sc, state.panel_line_numbers);

  int drawn = 0;
  int failed = 0;
  for (const auto& level : levels_to_draw) {
    const double length_px = panel_width_px * (level.greek / max_greek);
    const double clamped_length_px = std::max(4.0, length_px);  // не даём линиям исчезнуть при очень малых значениях
    const int length_bars = std::max(1, static_cast<int>(std::round(clamped_length_px / px_per_bar)));
    const int offset_bars = std::max(0, static_cast<int>(std::round(panel_offset_px / px_per_bar)));
    const int end_bar = std::max(left_bar, right_bar - offset_bars);
    const int begin_bar = std::max(left_bar, end_bar - length_bars);

    s_UseTool tool;
    tool.Clear();
    tool.ChartNumber = sc.ChartNumber;
    tool.DrawingType = DRAWING_LINE;
    tool.BeginValue = static_cast<float>(level.strike);
    tool.EndValue = static_cast<float>(level.strike);
    tool.BeginIndex = begin_bar;
    tool.EndIndex = end_bar;
    tool.Region = sc.GraphRegion;
    tool.Color = line_color;
    tool.LineWidth = static_cast<uint16_t>(line_width);
    tool.LineStyle = LINESTYLE_SOLID;
    tool.AddMethod = UTAM_ADD_OR_ADJUST;
    tool.AddAsUserDrawnDrawing = 0;
    tool.HideDrawing = 0;
    tool.AllowSaveToChartbook = 1;
    tool.UseRelativeVerticalValues = 0;  // абсолютные цены
    tool.DrawUnderneathMainGraph = draw_underneath ? 1 : 0;
    tool.ExtendLeft = 0;
    tool.ExtendRight = 0;
    tool.DrawWithinRegion = 1;

    const int rc = sc.UseTool(tool);
    if (rc > 0) {
      state.panel_line_numbers.push_back(tool.LineNumber);
      ++drawn;
    } else {
      ++failed;
      if (debug_log) {
        sc.AddMessageToLog("GexPanel: UseTool failed to draw line", 0);
      }
    }
  }

  state.last_visible_low = visible_low;
  state.last_visible_high = visible_high;

  if (debug_log) {
    std::ostringstream dbg;
    dbg << "GexPanel: drawn=" << drawn << " failed=" << failed << " max=" << max_greek
        << " levels=" << levels_to_draw.size()
        << " first=" << (levels_to_draw.empty() ? 0.0 : levels_to_draw.front().strike)
        << " last=" << (levels_to_draw.empty() ? 0.0 : levels_to_draw.back().strike) << " region=" << sc.GraphRegion
        << " width_px=" << panel_width_px << " offset_px=" << panel_offset_px
        << " vis_px=" << width_px << " vis_bars=" << visible_bars;
    sc.AddMessageToLog(dbg.str().c_str(), 0);
  }
}

/**
 * @brief Рисует горизонтальные ключевые уровни major_long_gamma / major_short_gamma.
 * @param sc Контекст ACSIL.
 * @param state Persistent-состояние.
 * @param enabled Включить/выключить линию.
 * @param price Цена уровня.
 * @param color Цвет линии.
 * @param width Толщина линии.
 * @param style Индекс стиля (0=Solid,1=Dash,2=Dot,3=DashDot).
 * @param draw_underneath true — под графиком; false — над графиком.
 * @param line_number Сохранённый номер линии для переиспользования.
 */
void RenderKeyLevel(SCStudyGraphRef sc,
                    GexState& state,
                    bool enabled,
                    double price,
                    COLORREF color,
                    int width,
                    int style,
                    bool draw_underneath,
                    int& line_number) {
  (void)state;
  if (!enabled || std::isnan(price)) {
    if (line_number != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line_number);
      line_number = 0;
    }
    return;
  }

  const int left_bar = sc.IndexOfFirstVisibleBar;
  const int right_bar = sc.IndexOfLastVisibleBar;
  if (left_bar < 0 || right_bar < 0) {
    return;
  }

  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.DrawingType = DRAWING_HORIZONTALLINE;
  tool.BeginValue = static_cast<float>(price);
  tool.EndValue = tool.BeginValue;
  tool.BeginIndex = left_bar;
  tool.EndIndex = right_bar;
  tool.Region = sc.GraphRegion;
  tool.Color = color;
  tool.LineWidth = static_cast<uint16_t>(std::max(1, width));
  tool.LineStyle = static_cast<SubgraphLineStyles>(ToLineStyle(style));
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.AddAsUserDrawnDrawing = 0;
  tool.DrawUnderneathMainGraph = draw_underneath ? 1 : 0;
  tool.UseRelativeVerticalValues = 0;
  tool.AllowSaveToChartbook = 1;
  tool.HideDrawing = 0;
  tool.ExtendLeft = 1;
  tool.ExtendRight = 1;
  if (line_number != 0) {
    tool.LineNumber = line_number;
  }
  sc.UseTool(tool);
  line_number = tool.LineNumber;
}

// Формирует список мини-контрактов в один столбец (сортировка по убыванию strike).
std::string FormatMiniContractsColumns(const std::vector<GexState::MiniContract>& data) {
  if (data.empty()) return std::string("  (empty)");

  std::vector<GexState::MiniContract> desc = data;
  std::sort(desc.begin(), desc.end(),
            [](const GexState::MiniContract& a, const GexState::MiniContract& b) { return a.strike > b.strike; });

  std::ostringstream out;
  for (size_t idx = 0; idx < desc.size(); ++idx) {
    const auto& mc = desc[idx];
    out << std::setw(3) << (idx + 1) << " strike=" << std::setw(8) << FormatDouble(mc.strike, 2)
        << " greek=" << std::setw(10) << FormatDouble(mc.specified_greek, 2) << "\n";
  }
  return out.str();
}

/**
 * @brief Выполняет HTTP GET к API GexBot и возвращает готовый текст.
 * @param ticker Тикер GexBot.
 * @param greek Имя грека (delta/gamma/vega/theta/rho).
 * @param api_key Ключ API.
 * @param error_out Сюда пишется текст ошибки при неудаче.
 * @param state_out Заполняет структурированные данные (ключевые уровни, mini_contracts).
 * @return Многострочный текст результата или пустая строка при ошибке.
 * @note Парсинг JSON выполняется через RapidYAML (JSON-подмножество YAML).
 * @warning Требуется сетевое подключение и валидный ключ.
 */
std::string FetchGexbotState(const std::string& ticker,
                             const std::string& greek,
                             const std::string& api_key,
                             std::string& error_out,
                             GexState& state_out) {
  error_out.clear();
  if (ticker.empty()) {
    error_out = "Ticker mapping failed";
    return {};
  }
  if (api_key.empty()) {
    error_out = "API key is empty";
    return {};
  }
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    error_out = "curl init failed";
    return {};
  }

  std::string response;
  std::ostringstream url;
  url << "https://api.gexbot.com/" << ticker << "/state/" << greek << "?key=" << api_key;
  const std::string request_url = url.str();

  curl_easy_setopt(curl, CURLOPT_URL, request_url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "SierraStudy/1.0");

  const CURLcode rc = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    error_out = std::string("curl error: ") + curl_easy_strerror(rc);
    return {};
  }
  if (http_code >= 400) {
    std::ostringstream oss;
    oss << "Request: " << request_url << "\nHTTP " << http_code << " from GexBot";
    if (!response.empty()) {
      const std::string body_preview = response.substr(0, 512);
      oss << "; body: " << body_preview;
      if (response.size() > body_preview.size()) {
        oss << " ...";
      }
    }
    error_out = oss.str();
    return {};
  }

  // Парсинг JSON как YAML-подмножества.
  try {
    // Очистить старые структурированные данные.
    state_out.key_levels = {};
    state_out.mini_contracts.clear();

    ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(response));
    const auto root = tree.rootref();
    if (!root.is_map()) {
      error_out = "parse error: root JSON is not an object";
      return {};
    }

    std::ostringstream out;

    out << "Request: " << request_url;
    out << "\nTicker: " << ticker << "\nGreek: " << greek;

    // Key levels (short and clear)
    out << "\n\nKey Levels:";
    state_out.key_levels.major_positive = ReadOptionalDouble(root, "major_positive");
    state_out.key_levels.major_negative = ReadOptionalDouble(root, "major_negative");
    state_out.key_levels.major_long_gamma = ReadOptionalDouble(root, "major_long_gamma");
    state_out.key_levels.major_short_gamma = ReadOptionalDouble(root, "major_short_gamma");
    if (!std::isnan(state_out.key_levels.major_positive))
      out << "\n  major_positive: " << FormatDouble(state_out.key_levels.major_positive, 2);
    if (!std::isnan(state_out.key_levels.major_negative))
      out << "\n  major_negative: " << FormatDouble(state_out.key_levels.major_negative, 2);
    if (!std::isnan(state_out.key_levels.major_long_gamma))
      out << "\n  major_long_gamma: " << FormatDouble(state_out.key_levels.major_long_gamma, 2);
    if (!std::isnan(state_out.key_levels.major_short_gamma))
      out << "\n  major_short_gamma: " << FormatDouble(state_out.key_levels.major_short_gamma, 2);

    // Mini contracts: show all, columns of 25 rows, sorted by strike desc
    state_out.mini_contracts = ParseMiniContracts(root, error_out);
    if (!error_out.empty()) {
      return {};
    }
    out << "\n\nMini Contracts (" << state_out.mini_contracts.size()
        << " with positive greek, single column, sorted desc by strike):\n";
    out << FormatMiniContractsColumns(state_out.mini_contracts);

    return out.str();
  } catch (const std::exception& ex) {
    error_out = std::string("parse error: ") + ex.what();
    return {};
  } catch (...) {
    error_out = "parse error: unknown";
    return {};
  }
}

/**
 * @brief Отрисовывает текстовое состояние в левом верхнем углу графика.
 * @param sc Контекст ACSIL.
 * @param line_number Идентификатор уже созданного текста (для обновления).
 * @param text Многострочный текст.
 * @return void.
 * @note Использует DRAWING_STATIONARY_TEXT.
 * @warning Цвет/шрифт заданы жёстко; при необходимости вынесите в Inputs.
 */
void RenderStatusText(SCStudyGraphRef sc, int& line_number, const std::string& text) {
  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.DrawingType = DRAWING_STATIONARY_TEXT;
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.Color = RGB(0, 200, 120);
  tool.FontFace = "Consolas";
  tool.FontSize = 9;
  tool.FontBold = 0;
  tool.MultiLineLabel = 1;
  tool.TextAlignment = DT_LEFT | DT_TOP;
  tool.AddAsUserDrawnDrawing = 0;
  tool.UseRelativeVerticalValues = 1;
  tool.BeginDateTime = 1;
  tool.BeginValue = 100.0f;
  tool.EndValue = tool.BeginValue;
  tool.TransparencyLevel = 0;

  tool.Text = text.c_str();
  if (line_number != 0) {
    tool.LineNumber = line_number;
  }
  sc.UseTool(tool);
  line_number = tool.LineNumber;
}

}  // namespace

/**
 * @brief Опрос API GexBot по выбранному греку с периодом 10–100 сек.
 * @param sc Контекст ACSIL, предоставляемый Sierra Chart.
 * @return void.
 * @note Использует libcurl (GET) и RapidYAML для парсинга JSON-ответа. Результат выводится текстом на график.
 * @warning Требуются корректные Inputs: API Key, Greek, период опроса. Тикер маппится из символа графика (ES/MES→SPX_ES, NQ/MNQ→NQ_NDX).
 */
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc) {
  static bool curl_global_init_done = false;
  if (!curl_global_init_done) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_global_init_done = true;
  }

  SCInputRef apiKeyInput = sc.Input[0];
  SCInputRef greekInput = sc.Input[1];
  SCInputRef pollIntervalInput = sc.Input[2];
  SCInputRef displayModeInput = sc.Input[3];
  SCInputRef panelWidthInput = sc.Input[4];
  SCInputRef panelOffsetInput = sc.Input[5];
  SCInputRef lineColorInput = sc.Input[6];
  SCInputRef lineWidthInput = sc.Input[7];
  SCInputRef drawLayerInput = sc.Input[8];
  SCInputRef diagnosticsInput = sc.Input[9];
  SCInputRef shortGammaEnabledInput = sc.Input[10];
  SCInputRef shortGammaColorInput = sc.Input[11];
  SCInputRef shortGammaWidthInput = sc.Input[12];
  SCInputRef shortGammaStyleInput = sc.Input[13];
  SCInputRef longGammaEnabledInput = sc.Input[14];
  SCInputRef longGammaColorInput = sc.Input[15];
  SCInputRef longGammaWidthInput = sc.Input[16];
  SCInputRef longGammaStyleInput = sc.Input[17];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - GexBot Poller";
    sc.StudyDescription = "Polls GexBot API for option greeks and shows JSON response.";
    sc.AutoLoop = 1;
    sc.FreeDLL = 1;
    sc.GraphRegion = 0;
    sc.UpdateAlways = 1;

    sc.Subgraph[0].Name = "Unused";
    sc.Subgraph[0].DrawStyle = DRAWSTYLE_IGNORE;
    sc.Subgraph[0].DrawZeros = false;

    apiKeyInput.Name = "GexBot API Key";
    apiKeyInput.SetString("IZiEb6yDrgxE");

    greekInput.Name = "Greek";
    greekInput.SetCustomInputStrings(
        "delta_zero;gamma_zero;delta_one;gamma_one;charm_zero;vanna_zero;charm_one;vanna_one");
    greekInput.SetCustomInputIndex(1);  // gamma_zero

    pollIntervalInput.Name = "Poll Interval (seconds)";
    pollIntervalInput.SetFloat(static_cast<float>(kDefaultPollInterval));
    pollIntervalInput.SetFloatLimits(static_cast<float>(kMinPollInterval),
                                     static_cast<float>(kMaxPollInterval));

    displayModeInput.Name = "Display Mode";
    displayModeInput.SetCustomInputStrings("Positive Only;Negative & Positive");
    displayModeInput.SetCustomInputIndex(0);

    panelWidthInput.Name = "Panel Width (px)";
    panelWidthInput.SetInt(400);
    panelWidthInput.SetIntLimits(10, 400);

    panelOffsetInput.Name = "Panel Right Offset (px)";
    panelOffsetInput.SetInt(10);
    panelOffsetInput.SetIntLimits(0, 200);

    lineColorInput.Name = "Line Color";
    lineColorInput.SetColor(RGB(0, 255, 255));

    lineWidthInput.Name = "Line Width";
    lineWidthInput.SetInt(1);
    lineWidthInput.SetIntLimits(1, 6);

    drawLayerInput.Name = "Draw Layer";
    drawLayerInput.SetCustomInputStrings("Background;Foreground");
    drawLayerInput.SetCustomInputIndex(0);  // Background по умолчанию

    diagnosticsInput.Name = "Show Diagnostics";
    diagnosticsInput.SetYesNo(false);

    shortGammaEnabledInput.Name = "Show Short Gamma";
    shortGammaEnabledInput.SetYesNo(true);
    shortGammaColorInput.Name = "Short Gamma Color";
    shortGammaColorInput.SetColor(RGB(165, 105, 220));  // фиолетовый из макета
    shortGammaWidthInput.Name = "Short Gamma Width";
    shortGammaWidthInput.SetInt(1);
    shortGammaWidthInput.SetIntLimits(1, 6);
    shortGammaStyleInput.Name = "Short Gamma Style";
    shortGammaStyleInput.SetCustomInputStrings("Solid;Dashed;Dotted;Dash-Dot");
    shortGammaStyleInput.SetCustomInputIndex(1);  // dashed

    longGammaEnabledInput.Name = "Show Long Gamma";
    longGammaEnabledInput.SetYesNo(true);
    longGammaColorInput.Name = "Long Gamma Color";
    longGammaColorInput.SetColor(RGB(0, 200, 200));  // бирюзовый из макета
    longGammaWidthInput.Name = "Long Gamma Width";
    longGammaWidthInput.SetInt(1);
    longGammaWidthInput.SetIntLimits(1, 6);
    longGammaStyleInput.Name = "Long Gamma Style";
    longGammaStyleInput.SetCustomInputStrings("Solid;Dashed;Dotted;Dash-Dot");
    longGammaStyleInput.SetCustomInputIndex(1);  // dashed
    return;
  }

  auto* state = static_cast<GexState*>(sc.GetPersistentPointer(kPersistState));
  if (state == nullptr) {
    state = new GexState{};
    sc.SetPersistentPointer(kPersistState, state);
  }

  if (sc.LastCallToFunction) {
    sc.SetPersistentPointer(kPersistState, nullptr);
    delete state;
    return;
  }

  state->poll_interval =
      std::clamp(static_cast<double>(pollIntervalInput.GetFloat()), kMinPollInterval, kMaxPollInterval);

  std::string ticker = MapSymbolToTicker(sc.Symbol);
  const int greek_index = greekInput.GetIndex();
  std::string greek = "delta_zero";
  if (greek_index >= 0) {
    const SCString g = greekInput.GetSelectedCustomString();
    if (g.GetLength() > 0) greek = g.GetChars();
  }
  const SCString key_input = apiKeyInput.GetString();
  const std::string api_key = key_input.GetChars();

  const double now = sc.CurrentSystemDateTime.GetAsDouble();
  if (SecondsSince(now, state->last_poll_time) >= state->poll_interval) {
    state->last_poll_time = now;
    std::string error;
    const std::string text = FetchGexbotState(ticker, greek, api_key, error, *state);
    if (!error.empty()) {
      state->last_text = "GexBot error: " + error;
      sc.AddMessageToLog(state->last_text.c_str(), 1);
    } else if (!text.empty()) {
      state->last_text = text;
    }
  }

  if (state->last_text.empty()) {
    state->last_text = "Waiting for GexBot data...";
  }

  // Отрисовка панели уровней specified_greek.
  const int display_mode = std::max<int>(1, displayModeInput.GetIndex() + 1);
  const int panel_width_px = panelWidthInput.GetInt();
  const int panel_offset_px = panelOffsetInput.GetInt();
  const COLORREF line_color = lineColorInput.GetColor();
  const int line_width = lineWidthInput.GetInt();
  const int draw_layer_index = drawLayerInput.GetIndex();
  const bool draw_underneath = (draw_layer_index == 0);  // Background -> under graph
  const bool show_diagnostics = diagnosticsInput.GetYesNo();
  const bool show_short_gamma = shortGammaEnabledInput.GetYesNo();
  const COLORREF short_gamma_color = shortGammaColorInput.GetColor();
  const int short_gamma_width = shortGammaWidthInput.GetInt();
  const int short_gamma_style = shortGammaStyleInput.GetIndex();

  const bool show_long_gamma = longGammaEnabledInput.GetYesNo();
  const COLORREF long_gamma_color = longGammaColorInput.GetColor();
  const int long_gamma_width = longGammaWidthInput.GetInt();
  const int long_gamma_style = longGammaStyleInput.GetIndex();
  const bool auto_scale_on = true;  // всегда включено
  const bool debug_log = false;     // логи отключены
  RenderGreekPanel(sc, *state, display_mode, panel_width_px, panel_offset_px, line_color, line_width, auto_scale_on,
                   draw_underneath, debug_log);

  // Ключевые уровни gamma
  RenderKeyLevel(sc, *state, show_short_gamma, state->key_levels.major_short_gamma, short_gamma_color,
                 short_gamma_width, short_gamma_style, draw_underneath, state->line_major_short);
  RenderKeyLevel(sc, *state, show_long_gamma, state->key_levels.major_long_gamma, long_gamma_color, long_gamma_width,
                 long_gamma_style, draw_underneath, state->line_major_long);

  // Диагностический текст
  static int line_number = 0;
  if (show_diagnostics) {
    RenderStatusText(sc, line_number, state->last_text);
  } else if (line_number != 0) {
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line_number);
    line_number = 0;
  }
}

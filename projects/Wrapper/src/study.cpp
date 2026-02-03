#include "sierra/acsil/study.hpp"

#include <curl/curl.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
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

    result.push_back(GexState::MiniContract{strike, greek});
  }

  std::sort(result.begin(), result.end(),
            [](const GexState::MiniContract& a, const GexState::MiniContract& b) { return a.strike < b.strike; });
  return result;
}

// Формирует таблицу мини-контрактов в колонках по 25 строк, сортировка по убыванию strike.
std::string FormatMiniContractsColumns(const std::vector<GexState::MiniContract>& data) {
  if (data.empty()) return std::string("  (empty)");
  constexpr size_t kRows = 25;
  const size_t cols = (data.size() + kRows - 1) / kRows;

  std::vector<GexState::MiniContract> desc = data;
  std::sort(desc.begin(), desc.end(),
            [](const GexState::MiniContract& a, const GexState::MiniContract& b) { return a.strike > b.strike; });

  std::ostringstream out;
  for (size_t row = 0; row < kRows; ++row) {
    bool any = false;
    for (size_t col = 0; col < cols; ++col) {
      const size_t idx = row + col * kRows;
      if (idx >= desc.size()) continue;
      const auto& mc = desc[idx];
      out << std::setw(3) << (idx + 1) << " strike=" << std::setw(8) << FormatDouble(mc.strike, 2)
          << " greek=" << std::setw(8) << FormatDouble(mc.specified_greek, 2) << "   ";
      any = true;
    }
    if (any) out << "\n";
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
    out << "\n\nMini Contracts (" << state_out.mini_contracts.size() << ", sorted desc by strike, columns of 25):\n";
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

  static int line_number = 0;
  RenderStatusText(sc, line_number, state->last_text);
}

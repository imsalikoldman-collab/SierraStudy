#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"
#include "sierra/core/plan.hpp"
#include "sierra/core/plan_formatter.hpp"
#include "sierra/core/yaml_plan_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if __has_include(<plog/Log.h>)
#define SIERRA_STUDY_HAS_PLOG 1
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>
#else
#define SIERRA_STUDY_HAS_PLOG 0
#endif

/// \brief Пользовательские исследования Sierra Chart <Add Custom Study>.
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistLogging = 1;
constexpr int kPersistPlanState = 2;
constexpr int kPersistDebugLine = 3;
constexpr double kDefaultPlanCheckIntervalSeconds = 15.0;
constexpr double kMinPlanCheckIntervalSeconds = 1.0;
constexpr double kSecondsPerDay = 24.0 * 60.0 * 60.0;

/**
 * @brief Хранит кеш плана и состояние слежения за YAML-файлом.
 * @note Используется через persistent-указатель Sierra Chart.
 * @warning Объект живёт до выгрузки study, освобождать через ReleasePlanState.
 */
struct PlanWatcherState {
  std::filesystem::file_time_type last_write{};
  bool has_last_write = false;
  bool file_available = false;
  bool status_known = false;
  bool directory_reported_empty = false;
  bool file_name_reported_empty = false;
  double last_check_timestamp = 0.0;
  double last_interval_seconds = kDefaultPlanCheckIntervalSeconds;
  std::string last_directory_key;
  std::string last_file_name_key;
  std::shared_ptr<sierra::core::StudyPlan> plan;
  std::string rendered_text;
  bool dirty = false;
  int table_line_number = 0;
  std::vector<int> plan_drawing_line_numbers;
  bool graphics_dirty = false;
  SCDateTime plan_start_time{};
};

#if SIERRA_STUDY_HAS_PLOG
/// @brief Инициализирует plog при первом запуске study.
/// @param sc Контекст Sierra Chart, предоставляющий persistent-хранилище.
/// @return void.
/// @note Создаёт каталог Logs и включает кольцевой лог-файл SierraStudy.log.
/// @warning При исключении выставляет persistant-флаг -1, чтобы избежать повторов.
void EnsureLogging(SCStudyGraphRef sc) {
  if (sc.Index != 0) {
    return;
  }

  const int initialized = sc.GetPersistentInt(kPersistLogging);
  if (initialized == 1 || initialized == -1) {
    return;
  }

  try {
    std::filesystem::create_directories("Logs");
    plog::init(plog::info, "Logs/SierraStudy.log", 5 * 1024 * 1024, 3);
    sc.SetPersistentInt(kPersistLogging, 1);
    PLOG_INFO << "SierraStudy logging initialized";
  } catch (...) {
    sc.SetPersistentInt(kPersistLogging, -1);
  }
}
#else
void EnsureLogging(SCStudyGraphRef) {}
#endif

/**
 * @brief Пишет информационное сообщение в лог Sierra Chart и plog.
 * @param sc Контекст study для обращения к AddMessageToLog.
 * @param message Текст сообщения.
 * @return void.
 * @note Используется для отчётов о нормальном выполнении операций.
 * @warning Сообщение отображается в Message Log; избегайте частых повторов.
 */
void LogInfo(SCStudyGraphRef sc, const std::string& message) {
  sc.AddMessageToLog(message.c_str(), 0);
#if SIERRA_STUDY_HAS_PLOG
  PLOG_INFO << message;
#endif
}

/**
 * @brief Пишет сообщение об ошибке в лог Sierra Chart и plog.
 * @param sc Контекст study для вывода.
 * @param message Текст ошибки.
 * @return void.
 * @note Уровень ошибки в Sierra Chart помечается флагом 1.
 * @warning Не забывайте понятные формулировки для трейдера.
 */
void LogError(SCStudyGraphRef sc, const std::string& message) {
  sc.AddMessageToLog(message.c_str(), 1);
#if SIERRA_STUDY_HAS_PLOG
  PLOG_ERROR << message;
#endif
}

/**
 * @brief Возвращает persistent-состояние наблюдателя, создавая при необходимости.
 * @param sc Контекст study.
 * @return PlanWatcherState* Указатель на состояние.
 * @note Вызывается перед обращением к наблюдателю; память освобождается ReleasePlanState.
 * @warning Не храните возвращённый указатель вне функции между вызовами без проверки.
 */
std::string ToUpperASCII(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::string NormalizeDirectoryKey(const std::filesystem::path& directory) {
  if (directory.empty()) {
    return {};
  }

  std::filesystem::path normalized = directory;
  try {
    normalized = normalized.lexically_normal();
  } catch (...) {
    // Ignore normalization failures; fall back to original.
  }

  std::string generic = normalized.generic_string();
  while (!generic.empty() && generic.back() == '/') {
    generic.pop_back();
  }
  std::transform(generic.begin(), generic.end(), generic.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return generic;
}

std::string NormalizeFileNameKey(std::string file_name) {
  std::transform(file_name.begin(), file_name.end(), file_name.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return file_name;
}

void ClearPlanDrawings(SCStudyGraphRef sc, PlanWatcherState& state) {
  for (const int line_number : state.plan_drawing_line_numbers) {
    if (line_number != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, line_number);
    }
  }
  state.plan_drawing_line_numbers.clear();
}

PlanWatcherState* AcquirePlanState(SCStudyGraphRef sc) {
  auto* state = static_cast<PlanWatcherState*>(sc.GetPersistentPointer(kPersistPlanState));
  if (state == nullptr) {
    state = new PlanWatcherState{};
    state->rendered_text = "[plan]\nAwaiting plan data...";
    state->dirty = true;
    sc.SetPersistentPointer(kPersistPlanState, state);
  }
  return state;
}

/**
 * @brief Освобождает persistent-состояние наблюдателя.
 * @param sc Контекст study.
 * @return void.
 * @note Вызвать при выгрузке DLL (sc.LastCallToFunction).
 * @warning Без вызова возникнет утечка памяти.
 */
void ReleasePlanState(SCStudyGraphRef sc) {
  auto* state = static_cast<PlanWatcherState*>(sc.GetPersistentPointer(kPersistPlanState));
  if (state != nullptr) {
    ClearPlanDrawings(sc, *state);
    sc.SetPersistentPointer(kPersistPlanState, nullptr);
    delete state;
  }
}


/**
 * @brief Мониторит YAML-план: валидирует входные параметры, отслеживает изменения и инициирует парсинг.
 * @param sc Контекст Sierra Chart для логирования.
 * @param state Persistent-состояние слежения.
 * @param plan_directory Каталог, в котором ожидается файл плана.
 * @param file_name Имя YAML-файла плана.
 * @param poll_interval_seconds Период опроса файла в секундах.
 * @return void.
 * @note Предотвращает лишние обращения к диску благодаря хранению времени последнего опроса.
 * @warning При ошибках не удаляет прошлую валидную копию плана, чтобы графика оставалась актуальной.
 */
void UpdatePlanWatcher(SCStudyGraphRef sc,
                       PlanWatcherState* state,
                       const std::filesystem::path& plan_directory,
                       const std::string& file_name,
                       double poll_interval_seconds) {
  if (state == nullptr) {
    return;
  }

  const auto mark_plan_unavailable = [&](const std::string& text) {
    state->plan.reset();
    state->graphics_dirty = true;
    if (state->rendered_text != text) {
      state->rendered_text = text;
      state->dirty = true;
    }
  };

  const double interval = std::isfinite(poll_interval_seconds)
                              ? (std::max)(poll_interval_seconds, kMinPlanCheckIntervalSeconds)
                              : kDefaultPlanCheckIntervalSeconds;
  if (state->last_interval_seconds != interval) {
    state->last_interval_seconds = interval;
    state->last_check_timestamp = 0.0;
  }

  const std::string directory_key = NormalizeDirectoryKey(plan_directory);
  const std::string file_key = NormalizeFileNameKey(file_name);
  const bool directory_changed = directory_key != state->last_directory_key;
  const bool file_changed = file_key != state->last_file_name_key;
  if (directory_changed || file_changed) {
    state->last_directory_key = directory_key;
    state->last_file_name_key = file_key;
    state->has_last_write = false;
    state->file_available = false;
    state->status_known = false;
    state->last_check_timestamp = 0.0;
    state->plan_start_time = 0.0;
    state->graphics_dirty = true;
    state->plan.reset();
    state->rendered_text = "[plan]\nAwaiting plan data...";
    state->dirty = true;
  }

  if (plan_directory.empty()) {
    if (!state->directory_reported_empty) {
      LogError(sc, "[plan] Plan directory input is empty.");
      state->directory_reported_empty = true;
    }
    mark_plan_unavailable("[plan]\nPlan directory is not set.");
    return;
  }
  state->directory_reported_empty = false;

  if (file_name.empty()) {
    if (!state->file_name_reported_empty) {
      LogError(sc, "[plan] Plan file name input is empty.");
      state->file_name_reported_empty = true;
    }
    mark_plan_unavailable("[plan]\nPlan file name is not set.");
    return;
  }
  state->file_name_reported_empty = false;

  std::filesystem::path plan_path = plan_directory / file_name;

  const double now = sc.CurrentSystemDateTime.GetAsDouble();
  if (state->last_check_timestamp != 0.0) {
    const double seconds_since_last = (now - state->last_check_timestamp) * kSecondsPerDay;
    if (seconds_since_last < interval) {
      return;
    }
  }
  state->last_check_timestamp = now;

  std::error_code ec;
  const bool exists = std::filesystem::exists(plan_path, ec);
  if (ec) {
    LogError(sc, "[plan] Failed to query plan file: " + ec.message());
    return;
  }

  if (!exists) {
    if (!state->status_known || state->file_available) {
      LogError(sc, "[plan] Plan file not found: " + plan_path.string());
    }
    state->status_known = true;
    state->file_available = false;
    state->has_last_write = false;
    mark_plan_unavailable("[plan]\nPlan file is unavailable.");
    return;
  }

  if (!state->status_known || !state->file_available || directory_changed || file_changed) {
    LogInfo(sc, "[plan] Plan file detected: " + plan_path.string());
  }
  state->status_known = true;
  state->file_available = true;

  const auto current_write = std::filesystem::last_write_time(plan_path, ec);
  if (ec) {
    LogError(sc, "[plan] Failed to query last write time: " + ec.message());
    return;
  }

  if (state->has_last_write && current_write == state->last_write) {
    return;
  }

  state->last_write = current_write;
  state->has_last_write = true;

  LogInfo(sc, "[plan] Detected plan update: " + plan_path.string());

  const auto load_result = sierra::core::LoadStudyPlanFromFile(plan_path);
  if (!load_result.success()) {
    LogError(sc, "[plan] File read: ERROR - " + load_result.error_message);
    LogError(sc, "[plan] YAML parsed: ERROR");
    return;
  }

  state->plan = std::make_shared<sierra::core::StudyPlan>(load_result.plan);
  state->graphics_dirty = true;

  SCDateTime fallback_time = 0.0;
  if (sc.ArraySize > 0) {
    fallback_time = sc.BaseDateTimeIn[sc.ArraySize - 1];
  }
  if (fallback_time == 0.0) {
    fallback_time = now;
  }
  state->plan_start_time =
      sierra::acsil::ConvertIso8601ToSCDateTime(state->plan->generated_at_iso8601, fallback_time);

  std::ostringstream read_msg;
  const auto file_size = std::filesystem::file_size(plan_path, ec);
  if (ec) {
    read_msg << "[plan] File read: OK (size unknown)";
  } else {
    read_msg << "[plan] File read: OK (" << file_size << " bytes)";
  }
  LogInfo(sc, read_msg.str());

  std::size_t total_zones = 0;
  std::size_t total_flips = 0;
  for (const auto& [ticker, instrument] : state->plan->instruments) {
    if (instrument.zone_long.has_value()) ++total_zones;
    if (instrument.zone_short.has_value()) ++total_zones;
    if (instrument.flip.has_value()) {
      ++total_flips;
    }
  }

  std::ostringstream parsed_msg;
  parsed_msg << "[plan] YAML parsed: OK (version=" << state->plan->version << ", generated_at="
             << state->plan->generated_at_iso8601 << ", instruments=" << state->plan->instruments.size()
             << ", zones=" << total_zones << ", flips=" << total_flips << ")";
  const std::string new_text =
      sierra::core::FormatPlanAsTable(*state->plan, std::string(sc.Symbol.GetChars()));
  if (state->rendered_text != new_text) {
    state->rendered_text = new_text;
    state->dirty = true;
  }

  LogInfo(sc, parsed_msg.str());
}

void RenderPlanTable(SCStudyGraphRef sc, PlanWatcherState& state) {
  if (!state.dirty) {
    return;
  }

  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.DrawingType = DRAWING_STATIONARY_TEXT;
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.Color = RGB(255, 165, 0);
  tool.FontFace = "Consolas";
  tool.FontSize = 8;
  tool.FontBold = 0;
  tool.MultiLineLabel = 1;
  tool.TextAlignment = DT_LEFT | DT_TOP;
  tool.TransparencyLevel = 0;
  tool.AddAsUserDrawnDrawing = 0;
  tool.UseRelativeVerticalValues = 1;
  tool.BeginDateTime = 2.0;
  tool.BeginValue = 97.0f;
  tool.EndValue = tool.BeginValue;

  if (state.table_line_number != 0) {
    tool.LineNumber = state.table_line_number;
  }

  if (state.rendered_text.empty()) {
    tool.Text = "";
  } else {
    tool.Text = state.rendered_text.c_str();
  }

  sc.UseTool(tool);
  state.table_line_number = tool.LineNumber;
  state.dirty = false;
}

const sierra::core::InstrumentPlan* FindInstrumentPlanForSymbol(SCStudyGraphRef sc,
                                                                const sierra::core::StudyPlan& plan) {
  if (plan.instruments.empty()) {
    return nullptr;
  }

  const std::string symbol_upper = ToUpperASCII(sc.Symbol.GetChars());
  const sierra::core::InstrumentPlan* fallback = nullptr;
  const sierra::core::InstrumentPlan* best_match = nullptr;
  std::size_t best_match_length = 0;

  for (const auto& [ticker, instrument] : plan.instruments) {
    if (fallback == nullptr) {
      fallback = &instrument;
    }
    const std::string ticker_upper = ToUpperASCII(ticker);
    if (!ticker_upper.empty() && symbol_upper.find(ticker_upper) != std::string::npos &&
        ticker_upper.length() > best_match_length) {
      best_match_length = ticker_upper.length();
      best_match = &instrument;
    }
  }

  return best_match != nullptr ? best_match : fallback;
}

void RenderPlanGraphics(SCStudyGraphRef sc, PlanWatcherState& state) {
  if (!state.graphics_dirty) {
    return;
  }

  if (state.plan == nullptr || sc.ArraySize <= 0) {
    ClearPlanDrawings(sc, state);
    state.graphics_dirty = false;
    return;
  }

  const auto* instrument = FindInstrumentPlanForSymbol(sc, *state.plan);
  if (instrument == nullptr) {
    ClearPlanDrawings(sc, state);
    state.graphics_dirty = false;
    return;
  }

  ClearPlanDrawings(sc, state);

  SCDateTime start_time = state.plan_start_time;
  if (start_time == 0.0) {
    start_time = sc.BaseDateTimeIn[0];
  }
  if (start_time == 0.0) {
    start_time = sc.BaseDateTimeIn[sc.ArraySize - 1];
  }

  SCDateTime end_time = sc.BaseDateTimeIn[sc.ArraySize - 1] + 1.0;
  if (end_time <= start_time) {
    end_time = start_time + 1.0;
  }

  sierra::acsil::RenderInstrumentPlanGraphics(sc, *instrument, start_time, end_time,
                                              state.plan_drawing_line_numbers,
                                              state.plan->generated_at_iso8601);
  state.graphics_dirty = false;
}

}  // namespace

/// @brief Примерная обёртка ACSIL, вызывающая ядро и обслуживающая YAML-план.
/// @param sc Контекст Sierra Chart для текущего study.
/// @return void.
/// @note В SetDefaults задаём параметры отображения и входы, далее выполняем расчёт SMA и мониторинг плана.
/// @warning Требуются корректные переменные окружения `SIERRA_SDK_DIR` и `SIERRA_DATA_DIR` для успешной загрузки DLL.
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc) {
  sierra::acsil::LogDllStartup(sc);
  SCSubgraphRef ma = sc.Subgraph[0];
  SCInputRef periodInput = sc.Input[0];
  SCInputRef planDirectoryInput = sc.Input[1];
  SCInputRef planFileNameInput = sc.Input[2];
  SCInputRef planPollIntervalInput = sc.Input[3];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - Moving Average";
    sc.StudyDescription = "Example ACSIL study wrapping the core moving average and YAML plan monitoring.";
    sc.AutoLoop = 1;
    sc.FreeDLL = 1;
    sc.GraphRegion = 0;
    sc.UpdateAlways = 1;

    ma.Name = "Moving Average";
    ma.DrawStyle = DRAWSTYLE_LINE;
    ma.PrimaryColor = RGB(0, 128, 255);
    ma.LineWidth = 2;
    ma.DrawZeros = false;

    periodInput.Name = "Period";
    periodInput.SetInt(20);
    periodInput.SetIntLimits(1, 500);

    planDirectoryInput.Name = "Plan Directory";
    planDirectoryInput.SetString("");

    planFileNameInput.Name = "Plan File Name";
    planFileNameInput.SetString("nq_intraday_flip_example.yaml");

    planPollIntervalInput.Name = "Plan Poll Interval (seconds)";
    planPollIntervalInput.SetFloat(static_cast<float>(kDefaultPlanCheckIntervalSeconds));
    planPollIntervalInput.SetFloatLimits(static_cast<float>(kMinPlanCheckIntervalSeconds), 600.0f);

    sc.DataStartIndex = periodInput.GetInt() - 1;
    return;
  }

  if (sc.LastCallToFunction) {
    if (auto* state = static_cast<PlanWatcherState*>(sc.GetPersistentPointer(kPersistPlanState))) {
      state->rendered_text.clear();
      state->dirty = true;
      RenderPlanTable(sc, *state);
      state->graphics_dirty = true;
      RenderPlanGraphics(sc, *state);
    }
    const int debug_line = sc.GetPersistentInt(kPersistDebugLine);
    if (debug_line != 0) {
      sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, debug_line);
      sc.SetPersistentInt(kPersistDebugLine, 0);
    }
    ReleasePlanState(sc);
    return;
  }

  EnsureLogging(sc);

  auto* state = AcquirePlanState(sc);

  std::filesystem::path plan_directory;
  const SCString directory_input = planDirectoryInput.GetString();
  if (directory_input.GetLength() > 0) {
    plan_directory = std::filesystem::path(directory_input.GetChars());
  }

  std::string plan_file_name;
  const SCString file_input = planFileNameInput.GetString();
  if (file_input.GetLength() > 0) {
    plan_file_name = file_input.GetChars();
  }

  const double poll_interval = static_cast<double>(planPollIntervalInput.GetFloat());

  UpdatePlanWatcher(sc, state, plan_directory, plan_file_name, poll_interval);
  RenderPlanTable(sc, *state);
  RenderPlanGraphics(sc, *state);
  int debug_line_number = sc.GetPersistentInt(kPersistDebugLine);
  debug_line_number = sierra::acsil::RenderStandaloneDebugLine(sc, debug_line_number);
  sc.SetPersistentInt(kPersistDebugLine, debug_line_number);

  const int period = (std::max)(1, periodInput.GetInt());
  sc.DataStartIndex = period - 1;

  const int length = sc.ArraySize;
  if (length <= 0) {
    ma[sc.Index] = std::numeric_limits<float>::quiet_NaN();
    return;
  }

  std::vector<double> closes(static_cast<std::size_t>(sc.Index + 1));
  for (int i = 0; i <= sc.Index; ++i) {
    closes[static_cast<std::size_t>(i)] = sc.Close[i];
  }

  const auto averages =
      sierra::core::moving_average(closes, static_cast<std::size_t>(period));
  const double value = averages.back();
  ma[sc.Index] = std::isnan(value) ? std::numeric_limits<float>::quiet_NaN()
                                   : static_cast<float>(value);
}


















#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"
#include "sierra/core/plan.hpp"
#include "sierra/core/plan_formatter.hpp"
#include "sierra/core/yaml_plan_loader.hpp"

#include <algorithm>
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
constexpr double kPlanCheckIntervalSeconds = 1.0;
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
  bool path_reported_empty = false;
  double last_check_timestamp = 0.0;
  std::shared_ptr<sierra::core::StudyPlan> plan;
  std::string rendered_text;
  bool dirty = false;
  int table_line_number = 0;
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
PlanWatcherState* AcquirePlanState(SCStudyGraphRef sc) {
  auto* state = static_cast<PlanWatcherState*>(sc.GetPersistentPointer(kPersistPlanState));
  if (state == nullptr) {
    state = new PlanWatcherState{};
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
    sc.SetPersistentPointer(kPersistPlanState, nullptr);
    delete state;
  }
}


/**
 * @brief Проверяет YAML-файл на наличие/изменение, выполняет чтение и парсинг.
 * @param sc Контекст study для логирования.
 * @param state Состояние наблюдателя (persistent).
 * @param plan_path Путь к YAML-файлу плана.
 * @return void.
 * @note Интервал проверки ограничен одной секундой с помощью SCDateTime.
 * @warning При ошибках логирует подробности и не затирает предыдущий валидный план.
 */
void UpdatePlanWatcher(SCStudyGraphRef sc, PlanWatcherState* state, const std::filesystem::path& plan_path) {
  if (state == nullptr) {
    return;
  }

  if (plan_path.empty()) {
    if (!state->path_reported_empty) {
      LogError(sc, "[plan] Путь к YAML не задан");
      state->path_reported_empty = true;
    }
    return;
  }

  const double now = sc.CurrentSystemDateTime.GetAsDouble();
  if (state->last_check_timestamp != 0.0) {
    const double seconds_since_last = (now - state->last_check_timestamp) * kSecondsPerDay;
    if (seconds_since_last < kPlanCheckIntervalSeconds) {
      return;
    }
  }
  state->last_check_timestamp = now;

  std::error_code ec;
  const bool exists = std::filesystem::exists(plan_path, ec);
  if (ec) {
    LogError(sc, "[plan] Ошибка проверки файла: " + ec.message());
    return;
  }

  if (!exists) {
    if (!state->status_known || state->file_available) {
      LogError(sc, "[plan] Файл не найден: " + plan_path.string());
    }
    state->status_known = true;
    state->file_available = false;
    state->has_last_write = false;
    state->plan.reset();
    const std::string missing_text = "[plan]\nPlan file unavailable.";
    if (state->rendered_text != missing_text) {
      state->rendered_text = missing_text;
      state->dirty = true;
    }
    return;
  }

  if (!state->status_known || !state->file_available) {
    LogInfo(sc, "[plan] Файл обнаружен: " + plan_path.string());
  }
  state->status_known = true;
  state->file_available = true;

  const auto current_write = std::filesystem::last_write_time(plan_path, ec);
  if (ec) {
    LogError(sc, "[plan] Ошибка чтения метки времени: " + ec.message());
    return;
  }

  if (state->has_last_write && current_write == state->last_write) {
    return;
  }

  state->last_write = current_write;
  state->has_last_write = true;

  LogInfo(sc, "[plan] Обнаружено изменение: " + plan_path.string());

  const auto load_result = sierra::core::LoadStudyPlanFromFile(plan_path);
  if (!load_result.success()) {
    LogError(sc, "[plan] File read: ERROR - " + load_result.error_message);
    LogError(sc, "[plan] YAML parsed: ERROR");
    return;
  }

  state->plan = std::make_shared<sierra::core::StudyPlan>(load_result.plan);

  std::ostringstream read_msg;
  const auto file_size = std::filesystem::file_size(plan_path, ec);
  if (ec) {
    read_msg << "[plan] File read: OK (size недоступен)";
  } else {
    read_msg << "[plan] File read: OK (" << file_size << " bytes)";
  }
  LogInfo(sc, read_msg.str());

  std::size_t total_zones = 0;
  std::size_t total_flips = 0;
  for (const auto& [ticker, instrument] : state->plan->instruments) {
    total_zones += instrument.zones.size();
    if (instrument.flip.has_value()) {
      ++total_flips;
    }
  }

  std::ostringstream parsed_msg;
  parsed_msg << "[plan] YAML parsed: OK (version=" << state->plan->version << ", generated_at="
             << state->plan->generated_at_iso8601 << ", instruments=" << state->plan->instruments.size()
             << ", zones=" << total_zones << ", flips=" << total_flips << ")";
  const std::string new_text = sierra::core::FormatPlanAsTable(*state->plan);
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
  tool.DrawingType = DRAWING_TEXT;
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.Color = RGB(255, 165, 0);
  tool.FontSize = 14;
  tool.FontBold = true;
  tool.MultiLineLabel = 1;
  tool.TextAlignment = DT_LEFT | DT_TOP;
  tool.UseRelativeVerticalValues = 1;
  tool.BeginValue = 95.0f;
  SCDateTime anchor_time = sc.BaseDateTimeIn[sc.ArraySize - 1];
  anchor_time -= 1.0;
  tool.BeginDateTime = anchor_time;

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
  SCInputRef planPathInput = sc.Input[1];

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

    planPathInput.Name = "Plan YAML Path";
    planPathInput.SetString("test_files\\nq_intraday_flip_example.yaml");

    sc.DataStartIndex = periodInput.GetInt() - 1;
    return;
  }

  if (sc.LastCallToFunction) {
    if (auto* state = static_cast<PlanWatcherState*>(sc.GetPersistentPointer(kPersistPlanState))) {
      state->rendered_text.clear();
      state->dirty = true;
      RenderPlanTable(sc, *state);
    }
    ReleasePlanState(sc);
    return;
  }

  EnsureLogging(sc);

  auto* state = AcquirePlanState(sc);
  const SCString path_input = planPathInput.GetString();
  std::filesystem::path plan_path;
  if (path_input.GetLength() > 0) {
    plan_path = std::filesystem::path(path_input.GetChars());
  }
  UpdatePlanWatcher(sc, state, plan_path);
  RenderPlanTable(sc, *state);

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












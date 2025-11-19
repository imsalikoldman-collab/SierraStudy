#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <limits>
#include <vector>

#if __has_include(<plog/Log.h>)
#define SIERRA_STUDY_HAS_PLOG 1
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>
#else
#define SIERRA_STUDY_HAS_PLOG 0
#endif

/// \brief Название группы, отображаемое в диалоге Sierra Chart «Add Custom Study».
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistLogging = 1;
constexpr int kPersistBridgeContext = 2;
constexpr int kPersistBridgeInitialized = 3;

#if SIERRA_STUDY_HAS_PLOG
/// @brief Однократно настраивает plog (если он доступен).
/// @param sc Контекст исследования, содержащий persistent-хранилище.
/// @return void.
/// @note Создаёт каталог Logs, включает кольцевой файл журнала и помечает это в persistent-хранилище Sierra Chart, чтобы не повторять работу.
/// @warning При ошибках файловой системы устанавливает флаг `-1` и больше не пытается повторять инициализацию в рамках текущей сессии.
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

struct PipeClient {
  std::string name;
  HANDLE handle = INVALID_HANDLE_VALUE;
  HANDLE connect_event = nullptr;
  OVERLAPPED connect_overlapped{};
  bool connecting = false;
  DWORD last_error_code = ERROR_SUCCESS;
  std::string last_error_stage;

  ~PipeClient() { Close(); }

  bool IsConnected() const { return handle != INVALID_HANDLE_VALUE && !connecting; }
  bool IsConnecting() const { return handle != INVALID_HANDLE_VALUE && connecting; }

  DWORD LastErrorCode() const { return last_error_code; }
  const std::string& LastErrorStage() const { return last_error_stage; }

  void RememberError(const char* stage, DWORD code) {
    last_error_code = code;
    if (stage != nullptr) {
      last_error_stage = stage;
    } else {
      last_error_stage.clear();
    }
  }

  void RememberLastError(const char* stage) { RememberError(stage, GetLastError()); }

  void UpdateName(const std::string& new_name) {
    if (name == new_name) {
      return;
    }
    name = new_name;
    Close();
  }

  void Close() {
    if (connecting && handle != INVALID_HANDLE_VALUE) {
      CancelIo(handle);
    }
    if (handle != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(handle);
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
    if (connect_event) {
      CloseHandle(connect_event);
      connect_event = nullptr;
    }
    connecting = false;
    ZeroMemory(&connect_overlapped, sizeof(connect_overlapped));
  }

  bool BeginConnect() {
    Close();
    if (name.empty()) {
      return false;
    }

    SECURITY_DESCRIPTOR sd{};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;

    handle =
        CreateNamedPipeA(name.c_str(),
                         PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                         1, 64 * 1024, 64 * 1024, 0, &sa);
    if (handle == INVALID_HANDLE_VALUE) {
      RememberLastError("CreateNamedPipeA");
      return false;
    }

    connect_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    ZeroMemory(&connect_overlapped, sizeof(connect_overlapped));
    connect_overlapped.hEvent = connect_event;
    ResetEvent(connect_event);

    BOOL ok = ConnectNamedPipe(handle, &connect_overlapped);
    if (ok) {
      connecting = false;
      return true;
    }

    const DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      connecting = true;
      return false;
    }
    if (err == ERROR_PIPE_CONNECTED) {
      connecting = false;
      return true;
    }

    RememberError("ConnectNamedPipe", err);
    Close();
    return false;
  }

  bool EnsureConnected() {
    if (handle == INVALID_HANDLE_VALUE) {
      return BeginConnect();
    }

    if (connecting) {
      const DWORD wait = WaitForSingleObject(connect_event, 0);
      const bool completed =
          wait == WAIT_OBJECT_0 || HasOverlappedIoCompleted(&connect_overlapped);
      if (completed) {
        DWORD transferred = 0;
        if (GetOverlappedResult(handle, &connect_overlapped, &transferred, FALSE)) {
          connecting = false;
          return true;
        }
        RememberLastError("GetOverlappedResult");
        Close();
        return BeginConnect();
      }
      return false;
    }

    return true;
  }

  bool Send(const std::string& payload) {
    if (payload.empty()) {
      return true;
    }
    if (!EnsureConnected()) {
      return false;
    }

    DWORD written = 0;
    if (!WriteFile(handle, payload.data(), static_cast<DWORD>(payload.size()), &written,
                   nullptr)) {
      RememberLastError("WriteFile");
      Close();
      return false;
    }
    return true;
  }

  bool SendRaw(const std::string& payload) {
    if (payload.empty() || handle == INVALID_HANDLE_VALUE || connecting) {
      return false;
    }
    DWORD written = 0;
    if (!WriteFile(handle, payload.data(), static_cast<DWORD>(payload.size()),
                   &written, nullptr)) {
      RememberLastError("WriteFile");
      Close();
      return false;
    }
    return true;
  }
};

struct MirrorContext {
  PipeClient pipe;
  double last_quantity = 0.0;
  double last_average_price = 0.0;
  std::string last_position_id;
  DWORD last_pipe_error = ERROR_SUCCESS;
  std::string last_pipe_stage;
  std::string last_pipe_error_time;
  int status_drawing_id = 0;
  bool last_pipe_connected = false;
  std::deque<std::string> pending_payloads;
};

/// @brief Возвращает день недели (0 = воскресенье) по григорианскому календарю.
/// @param year Полный год.
/// @param month Месяц (1-12).
/// @param day День месяца (1-31).
/// @return int Значение 0-6, где 0 соответствует воскресенью.
/// @note Используется для вычисления дат перехода на летнее время (EST/EDT).
/// @warning Предполагает григорианский календарь; для дат до 1582 года алгоритм может давать некорректный результат.
int DayOfWeek(int year, int month, int day) {
  if (month < 3) {
    month += 12;
    --year;
  }
  const int K = year % 100;
  const int J = year / 100;
  const int h = (day + (13 * (month + 1)) / 5 + K + (K / 4) + (J / 4) + (5 * J)) % 7;
  return (h + 6) % 7;
}

/// @brief Находит день месяца для n-го вхождения выбранного дня недели.
/// @param year Полный год.
/// @param month Месяц (1-12).
/// @param weekday День недели (0 = воскресенье).
/// @param occurrence Номер вхождения (1 = первое).
/// @return int День месяца.
/// @note Используется для определения дат переключения EST/EDT.
/// @warning Не проверяет существование нужного количества вхождений (например, 5-ое воскресенье в феврале).
int NthWeekdayOfMonth(int year, int month, int weekday, int occurrence) {
  const int first_day_weekday = DayOfWeek(year, month, 1);
  const int delta = (weekday - first_day_weekday + 7) % 7;
  return 1 + delta + (occurrence - 1) * 7;
}

/// @brief Формирует std::time_t в UTC из компонентов календаря.
/// @param year Полный год.
/// @param month Месяц (1-12).
/// @param day День месяца.
/// @param hour Часы (0-23).
/// @param minute Минуты (0-59).
/// @param second Секунды (0-59).
/// @return std::time_t Значение в UTC для переданного момента времени.
/// @note На Windows используется `_mkgmtime`, на других платформах — `timegm`.
/// @warning Не выполняет проверку корректности даты; invalid значения могут привести к неопределённому результату.
std::time_t MakeUtcTime(int year, int month, int day, int hour, int minute, int second) {
  std::tm tm_snapshot{};
  tm_snapshot.tm_year = year - 1900;
  tm_snapshot.tm_mon = month - 1;
  tm_snapshot.tm_mday = day;
  tm_snapshot.tm_hour = hour;
  tm_snapshot.tm_min = minute;
  tm_snapshot.tm_sec = second;
#if defined(_WIN32)
  return _mkgmtime(&tm_snapshot);
#else
  return timegm(&tm_snapshot);
#endif
}

/// @brief Вычисляет момент начала DST (EDT) для часового пояса Нью-Йорка (UTC).
/// @param year Полный год.
/// @return std::time_t UTC-время начала DST.
/// @note Использует правило «второе воскресенье марта, 02:00 по EST».
/// @warning При изменении регуляторных правил США формулу необходимо обновить вручную.
std::time_t GetNewYorkDstStartUtc(int year) {
  const int day = NthWeekdayOfMonth(year, 3, 0, 2);  // вторая неделя марта, воскресенье
  return MakeUtcTime(year, 3, day, 7, 0, 0);  // 02:00 EST = 07:00 UTC
}

/// @brief Вычисляет момент завершения DST для Нью-Йорка (UTC).
/// @param year Полный год.
/// @return std::time_t UTC-время окончания DST.
/// @note Использует правило «первое воскресенье ноября, 02:00 по EDT».
/// @warning Требует обновления при изменении законодательных норм.
std::time_t GetNewYorkDstEndUtc(int year) {
  const int day = NthWeekdayOfMonth(year, 11, 0, 1);  // первая неделя ноября, воскресенье
  return MakeUtcTime(year, 11, day, 6, 0, 0);  // 02:00 EDT = 06:00 UTC
}

/// @brief Определяет смещение часового пояса Нью-Йорка относительно UTC.
/// @param utc_time Текущее время в UTC.
/// @return int Смещение в секундах (-5 часов для EST, -4 часа для EDT).
/// @note Использует расчёт границ DST для выбранного года.
/// @warning Не учитывает возможные внеплановые изменения (force majeure).
int GetNewYorkOffsetSeconds(std::time_t utc_time) {
  std::tm utc_tm{};
  gmtime_s(&utc_tm, &utc_time);
  const int year = utc_tm.tm_year + 1900;
  const std::time_t dst_start = GetNewYorkDstStartUtc(year);
  const std::time_t dst_end = GetNewYorkDstEndUtc(year);
  if (utc_time >= dst_start && utc_time < dst_end) {
    return -4 * 3600;
  }
  return -5 * 3600;
}

/// @brief Переводит UTC во время Нью-Йорка и возвращает готовую структуру std::tm.
/// @param utc_time Исходное время в UTC.
/// @param offset_hours Выходной параметр для смещения (часы, например -5).
/// @return std::tm Локальное время Нью-Йорка.
/// @note Смещение вычисляется функцией GetNewYorkOffsetSeconds.
/// @warning Параметр offset_hours может быть nullptr; проверка обязательна перед записью.
std::tm ComputeNewYorkTime(std::time_t utc_time, int* offset_hours) {
  const int offset_seconds = GetNewYorkOffsetSeconds(utc_time);
  if (offset_hours != nullptr) {
    *offset_hours = offset_seconds / 3600;
  }
  std::tm local_tm{};
  const std::time_t shifted = utc_time + offset_seconds;
  gmtime_s(&local_tm, &shifted);
  return local_tm;
}

/// @brief Форматирует штамп времени в ISO‑8601 со смещением.
/// @param tm_snapshot Локальное время Нью-Йорка.
/// @param offset_hours Смещение часового пояса в часах.
/// @return std::string Строка формата `YYYY-MM-DDTHH:MM:SS±HH:00`.
/// @note Используется для JSON и логов.
/// @warning Смещение указывается только с точностью до часа.
std::string FormatIsoTimestamp(const std::tm& tm_snapshot, int offset_hours) {
  char iso_time[40];
  std::strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%S", &tm_snapshot);
  char offset_buffer[8];
  std::snprintf(offset_buffer, sizeof(offset_buffer), "%+03d:00", offset_hours);
  return std::string(iso_time) + offset_buffer;
}

/// @brief Форматирует штамп в компактном виде без разделителей.
/// @param tm_snapshot Локальное время Нью-Йорка.
/// @return std::string Строка `YYYYMMDDHHMMSS`.
/// @note Применяется для генерации идентификаторов позиций.
/// @warning Не содержит смещения, поэтому не подходит для журналирования.
std::string FormatCompactTimestamp(const std::tm& tm_snapshot) {
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &tm_snapshot);
  return std::string(buffer);
}

/// @brief Возвращает текущий штамп времени в часовом поясе Нью-Йорка.
/// @return std::string Строка ISO‑8601 вида `2025-11-19T04:32:15-05:00`.
/// @note Автоматически учитывает переходы на летнее/зимнее время.
/// @warning Функция обращается к системным часам; при неверной системной дате информация будет некорректной.
std::string CurrentNewYorkIsoTimestamp() {
  const std::time_t now = std::time(nullptr);
  int offset_hours = 0;
  const std::tm ny_time = ComputeNewYorkTime(now, &offset_hours);
  return FormatIsoTimestamp(ny_time, offset_hours);
}

/// @brief Возвращает компактный штамп для генерации идентификаторов позиций.
/// @return std::string Строка вида `20251119043215`.
/// @note Удобен для встраивания в строковые идентификаторы.
/// @warning Не содержит смещения, поэтому не используйте его для журналов.
std::string CurrentNewYorkCompactTimestamp() {
  const std::time_t now = std::time(nullptr);
  int offset_hours = 0;
  const std::tm ny_time = ComputeNewYorkTime(now, &offset_hours);
  return FormatCompactTimestamp(ny_time);
}

/// @brief Генерирует уникальный идентификатор позиции на основе символа/направления.
/// @param symbol Тикер.
/// @param direction Направление (`long/short`).
/// @return std::string Значение, уникальное в пределах текущего запуска (`SYMBOL-direction-YYYYMMDDhhmmss-XXXX`).
/// @note Использует атомарный счётчик; перезапуск study сбрасывает последовательность.
/// @warning Не предназначен для глобальной уникальности между машинами/сессиями.
std::string GeneratePositionId(const std::string& symbol, const std::string& direction) {
  static std::atomic<uint32_t> sequence{0};
  const uint32_t current = sequence.fetch_add(1) + 1;
  std::ostringstream oss;
  oss << symbol << "-" << direction << "-" << CurrentNewYorkCompactTimestamp() << "-"
      << std::setw(4) << std::setfill('0') << (current % 10000);
  return oss.str();
}

/// @brief Пишет диагностическое сообщение моста в файл Logs/sierrastudymt5.log.
/// @param message Текст сообщения.
/// @note Файл создаётся при первом обращении, ошибки подавляются, чтобы не блокировать основной поток.
/// @warning Функция вызывается из торгового потока, поэтому операции должны быть максимально короткими.
void AppendBridgeErrorLog(const std::string& message) {
  try {
    std::filesystem::create_directories("Logs");
    std::ofstream log("Logs/sierrastudymt5.log", std::ios::app);
    if (log.is_open()) {
      log << CurrentNewYorkIsoTimestamp() << " " << message << '\n';
    }
  } catch (...) {
  }
}

/// @brief Фиксирует ошибку пайпа в контексте и в файле лога.
/// @param ctx Контекст исследования.
/// @param stage Контекст операции (опционально, по умолчанию берётся из PipeClient).
/// @note Устанавливает поля `last_pipe_error`, `last_pipe_stage`, `last_pipe_error_time` для отображения на чарте.
/// @warning Не вызывает исключений; любые ошибки записи в файл глушатся.
void RegisterPipeError(MirrorContext* ctx, const std::string& stage) {
  if (ctx == nullptr) {
    return;
  }
  const DWORD code = ctx->pipe.LastErrorCode();
  if (code == ERROR_SUCCESS) {
    return;
  }
  const std::string stage_text =
      stage.empty() ? ctx->pipe.LastErrorStage() : stage;
  ctx->last_pipe_error = code;
  ctx->last_pipe_stage = stage_text;
  ctx->last_pipe_error_time = CurrentNewYorkIsoTimestamp();
  std::ostringstream oss;
  oss << "[pipe] stage=" << stage_text << " code=" << code;
  AppendBridgeErrorLog(oss.str());
}

/// @brief Обновляет строку состояния пайпа в левом нижнем углу графика.
/// @param sc Контекст исследования Sierra Chart.
/// @param ctx Состояние моста.
/// @note Использует `s_UseTool` с относительными координатами, поэтому строка не перекрывает график.
/// @warning Если пользователь вручную удалит рисунок, `LineNumber` обнулится и строка будет создана заново.
void UpdatePipeStatusOverlay(SCStudyGraphRef sc, MirrorContext* ctx) {
  if (ctx == nullptr) {
    return;
  }

  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.Region = 0;
  tool.DrawingType = DRAWING_TEXT;
  tool.LineNumber = ctx->status_drawing_id;
  tool.AddMethod = (ctx->status_drawing_id == 0) ? UTAM_ADD_ALWAYS : UTAM_ADD_OR_ADJUST;
  tool.FontSize = 12;
  tool.FontBold = true;
  tool.UseRelativeVerticalValues = 1;
  tool.BeginValue = 3;  // 3% от нижней границы
  double anchor_time = 0.0;
  if (sc.ArraySize > 0) {
    anchor_time = sc.BaseDateTimeIn[0].GetAsDouble();
  }
  tool.BeginDateTime = anchor_time;
  tool.TextAlignment = DT_LEFT | DT_BOTTOM;

  const COLORREF connected_color = RGB(34, 197, 94);
  const COLORREF waiting_color = RGB(251, 191, 36);
  const COLORREF error_color = RGB(239, 68, 68);
  const bool is_connected = ctx->pipe.IsConnected();
  const bool is_connecting = ctx->pipe.IsConnecting();
  if (is_connected && !ctx->last_pipe_connected) {
    sc.AddMessageToLog("[bridge] Pipe client connected.", 0);
  } else if (!is_connected && ctx->last_pipe_connected && !is_connecting) {
    sc.AddMessageToLog("[bridge] Pipe client disconnected, ожидаем новое подключение.", 1);
  }
  ctx->last_pipe_connected = is_connected;

  if (is_connected) {
    tool.Color = connected_color;
  } else if (is_connecting) {
    tool.Color = waiting_color;
  } else {
    tool.Color = error_color;
  }

  const int horizontal_percent = 3;  // 3% от левого края
  tool.BeginDateTime = horizontal_percent;
  std::ostringstream status;
  status << "PIPE "
         << (is_connected ? "Connected" : (is_connecting ? "Waiting" : "Idle"));
  status << " | Pending: " << ctx->pending_payloads.size();
  if (ctx->last_pipe_error != ERROR_SUCCESS) {
    status << " | LastErr: " << ctx->last_pipe_error;
    if (!ctx->last_pipe_stage.empty()) {
      status << " (" << ctx->last_pipe_stage << ")";
    }
    if (!ctx->last_pipe_error_time.empty()) {
      status << " @" << ctx->last_pipe_error_time;
    }
  } else {
    status << " | LastErr: none";
  }

  tool.Text = status.str().c_str();
  sc.UseTool(tool);
  ctx->status_drawing_id = tool.LineNumber;
}

bool FlushPendingPayloads(MirrorContext* ctx, bool* waiting_for_connection = nullptr) {
  if (ctx == nullptr) {
    return true;
  }
  bool delivered = true;
  if (waiting_for_connection != nullptr) {
    *waiting_for_connection = false;
  }
  ctx->pipe.EnsureConnected();
  while (!ctx->pending_payloads.empty()) {
    if (!ctx->pipe.EnsureConnected()) {
      delivered = false;
      if (ctx->pipe.IsConnecting()) {
        if (waiting_for_connection != nullptr) {
          *waiting_for_connection = true;
        }
      } else {
        RegisterPipeError(ctx, "");
      }
      break;
    }
    if (!ctx->pipe.SendRaw(ctx->pending_payloads.front())) {
      delivered = false;
      RegisterPipeError(ctx, "WriteFile");
      break;
    }
    ctx->pending_payloads.pop_front();
  }
  return delivered;
}

void QueuePayload(MirrorContext* ctx, const std::string& payload) {
  if (ctx == nullptr || payload.empty()) {
    return;
  }
  ctx->pending_payloads.push_back(payload);
}

std::string EscapeJson(const std::string& value) {
  std::ostringstream oss;
  for (char ch : value) {
    switch (ch) {
      case '\"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << ch;
        break;
    }
  }
  return oss.str();
}

/// @brief Сериализует структуру сигнала Sierra → MT5 в JSON.
/// @param symbol Тикер.
/// @param direction Направление (`long/short`).
/// @param status Статус (`open/modify/close`).
/// @param position_id Уникальный идентификатор позиции.
/// @param entry_price Цена входа.
/// @param stop_price Цена стопа (может совпадать с entry при отсутствии данных).
/// @param take_price Цена тейка (0, если не задан).
/// @param note Комментарий пользователя.
/// @return std::string Готовый JSON + перевод строки.
/// @note Все цены форматируются с точностью два знака и временная метка задаётся в часовом поясе Нью-Йорка.
/// @warning Функция не валидирует значения; ответственность за корректные данные лежит на вызывающем коде.
std::string BuildPayload(const std::string& symbol, const std::string& direction,
                         const std::string& status, const std::string& position_id,
                         double entry_price, double stop_price, double take_price,
                         const std::string& note) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "{";
  oss << "\"version\":\"1.1\",";
  oss << "\"symbol\":\"" << EscapeJson(symbol) << "\",";
  oss << "\"position_id\":\"" << EscapeJson(position_id) << "\",";
  oss << "\"direction\":\"" << EscapeJson(direction) << "\",";
  oss << "\"status\":\"" << EscapeJson(status) << "\",";
  oss << "\"entry\":{\"price\":" << entry_price << ",\"type\":\"market\"},";
  oss << "\"stop\":{\"price\":" << stop_price << ",\"type\":\"stop\"},";
  oss << "\"take\":{\"price\":" << take_price << ",\"type\":\"limit\"},";
  oss << "\"timestamp\":\"" << CurrentNewYorkIsoTimestamp() << "\",";
  oss << "\"note\":\"" << EscapeJson(note) << "\"";
  oss << "}\n";
  return oss.str();
}

}  // namespace

/// @brief Обёртка ACSIL, которая перенаправляет данные в ядро Core.
/// @param sc Контекст Sierra Chart для текущего исследования.
/// @return void.
/// @note Повторяет структуру из примеров Sierra Chart: в SetDefaults задаёт все опции, во второй секции формирует буфер и вызывает Core.
/// @warning Перед использованием убедитесь, что `SIERRA_SDK_DIR` и `SIERRA_DATA_DIR` заданы корректно, иначе сборка/копирование DLL не сработают.
SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc) {
  sierra::acsil::LogDllStartup(sc);
  SCSubgraphRef ma = sc.Subgraph[0];
  SCInputRef periodInput = sc.Input[0];

  if (sc.SetDefaults) {
    // Раздел 1 — настройка по умолчанию (как в примерах Sierra Chart).
    sc.GraphName = "SierraStudy - Moving Average";
    sc.StudyDescription = "Example ACSIL study wrapping the core moving average.";
    sc.AutoLoop = 1;
    sc.FreeDLL = 1;  // позволяет перестраивать DLL без перезапуска Sierra Chart
    sc.GraphRegion = 0;

    ma.Name = "Moving Average";
    ma.DrawStyle = DRAWSTYLE_LINE;
    ma.PrimaryColor = RGB(0, 128, 255);
    ma.LineWidth = 2;
    ma.DrawZeros = false;

    periodInput.Name = "Period";
    periodInput.SetInt(20);
    periodInput.SetIntLimits(1, 500);

    sc.DataStartIndex = periodInput.GetInt() - 1;
    return;
  }

  if (sc.LastCallToFunction) {
    return;
  }

  // Раздел 2 — обработка данных исследования.
  EnsureLogging(sc);

  const int period = (std::max)(1, periodInput.GetInt());
  sc.DataStartIndex = period - 1;

  const int length = sc.ArraySize;
  if (length <= 0) {
    ma[sc.Index] = std::numeric_limits<float>::quiet_NaN();
    return;
  }

  // Формируем локальный буфер цен закрытия — в том же стиле, что и примеры
  // Sierra Chart. Так ядро получает std::vector без прямой зависимости от ACSIL.
  std::vector<double> closes(static_cast<std::size_t>(sc.Index + 1));
  for (int i = 0; i <= sc.Index; ++i) {
    closes[static_cast<std::size_t>(i)] = sc.Close[i];
  }

  // Передаём данные в ядро Core: функция вернёт массив SMA, берём последнее
  // значение и выводим его в Subgraph.
  const auto averages =
      sierra::core::moving_average(closes, static_cast<std::size_t>(period));
  const double value = averages.back();
  ma[sc.Index] = std::isnan(value) ? std::numeric_limits<float>::quiet_NaN()
                                   : static_cast<float>(value);
}

SCSFExport scsf_SierraStudyBridge(SCStudyGraphRef sc) {
  SCInputRef pipe_input = sc.Input[0];
  SCInputRef note_input = sc.Input[1];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - MT5 Bridge";
    sc.StudyDescription = "Отправляет события открытия/закрытия позиций Sierra в MT5.";
    sc.AutoLoop = 0;
    sc.UpdateAlways = 1;
    sc.GraphRegion = 0;
    sc.FreeDLL = 1;
    pipe_input.Name = "Pipe Name";
    pipe_input.SetString("\\\\.\\pipe\\SierraStudyAdvisor");

    note_input.Name = "Note (optional)";
    note_input.SetString("");
    // continue processing after full recalculation
  }

  if (sc.LastCallToFunction) {
    auto* ctx = static_cast<MirrorContext*>(sc.GetPersistentPointer(kPersistBridgeContext));
    if (ctx != nullptr && ctx->status_drawing_id != 0) {
      sc.DeleteUserDrawnACSDrawing(sc.ChartNumber, ctx->status_drawing_id);
    }
    delete ctx;
    sc.SetPersistentPointer(kPersistBridgeContext, nullptr);
    return;
  }

  auto* ctx = static_cast<MirrorContext*>(sc.GetPersistentPointer(kPersistBridgeContext));
  if (ctx == nullptr) {
    ctx = new MirrorContext();
    sc.SetPersistentPointer(kPersistBridgeContext, ctx);
  }

  const std::string pipe_name = pipe_input.GetString();
  ctx->pipe.UpdateName(pipe_name);
  ctx->pipe.EnsureConnected();

  if (sc.GetPersistentInt(kPersistBridgeInitialized) == 0) {
    std::string msg = "SierraStudyBridge ready (pipe=" + pipe_name + ")";
    sc.AddMessageToLog(msg.c_str(), 0);
    sc.SetPersistentInt(kPersistBridgeInitialized, 1);
  }

  s_SCPositionData position{};
  if (!sc.SelectedTradeAccount.IsEmpty()) {
    if (sc.GetTradePositionForSymbolAndAccount(position, sc.Symbol, sc.SelectedTradeAccount) == 0) {
      sc.GetTradePosition(position);
    }
  } else {
    sc.GetTradePosition(position);
  }

  const double quantity = position.PositionQuantity;
  const double average_price = position.AveragePrice;
  const std::string note = note_input.GetString();
  const std::string symbol = sc.Symbol.GetChars();

  // Пытаемся доставить ранее отложенные сообщения даже если новых событий нет.
  FlushPendingPayloads(ctx);
  UpdatePipeStatusOverlay(sc, ctx);

  auto ensure_position_id = [&](double qty) -> std::string {
    if (ctx->last_position_id.empty()) {
      ctx->last_position_id =
          GeneratePositionId(symbol, (qty >= 0.0) ? "long" : "short");
    }
    return ctx->last_position_id;
  };

  auto send_event = [&](const std::string& status, double qty, double price,
                        const std::string& position_id) {
    const std::string direction = (qty >= 0.0) ? "long" : "short";
    const std::string payload =
        BuildPayload(symbol, direction, status, position_id, price, 0.0, 0.0, note);
    std::ostringstream oss;
    oss << "[bridge] " << status << " id=" << position_id << " qty=" << qty
        << " price=" << std::fixed << std::setprecision(2) << price;
    sc.AddMessageToLog(oss.str().c_str(), 0);
    QueuePayload(ctx, payload);
    bool waiting_for_client = false;
    if (!FlushPendingPayloads(ctx, &waiting_for_client)) {
      if (waiting_for_client) {
        sc.AddMessageToLog(
            "SierraStudyBridge: ожидаем подключение клиента MT5, сообщение в очереди.", 0);
      } else {
        sc.AddMessageToLog("Failed to send payload to MT5 pipe (SierraStudyBridge).", 1);
        AppendBridgeErrorLog("FlushPendingPayloads failed for SierraStudyBridge.");
      }
    }
  };

  if (quantity == ctx->last_quantity) {
    return;
  }

  if (quantity == 0.0 && ctx->last_quantity != 0.0) {
    const std::string closing_id =
        ctx->last_position_id.empty()
            ? GeneratePositionId(symbol, (ctx->last_quantity >= 0.0) ? "long" : "short")
            : ctx->last_position_id;
    send_event("close", ctx->last_quantity, ctx->last_average_price, closing_id);
    ctx->last_position_id.clear();
  } else if (quantity != 0.0) {
    if (ctx->last_quantity != 0.0 &&
        ((quantity > 0.0) != (ctx->last_quantity > 0.0))) {
      const std::string closing_id =
          ctx->last_position_id.empty()
              ? GeneratePositionId(symbol, (ctx->last_quantity >= 0.0) ? "long" : "short")
              : ctx->last_position_id;
      send_event("close", ctx->last_quantity, ctx->last_average_price, closing_id);
      ctx->last_position_id.clear();
    }

    const std::string status = (ctx->last_quantity == 0.0) ? "open" : "modify";
    const std::string position_id = ensure_position_id(quantity);
    send_event(status, quantity, average_price, position_id);
  }

  ctx->last_quantity = quantity;
  if (quantity != 0.0) {
    ctx->last_average_price = average_price;
  }
  UpdatePipeStatusOverlay(sc, ctx);
}






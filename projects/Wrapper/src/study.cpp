#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
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

/// \brief Название группы, отображаемое в диалоге Sierra Chart «Add Custom Study».
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistLogging = 1;
constexpr int kPersistBridgeContext = 2;
constexpr int kPersistBridgeInitialized = 3;

/// @brief Состояние позиции по символу.
enum class PositionState { kFlat = 0, kLong = 1, kShort = 2 };

/// @brief Режим работы моста согласно спецификации.
enum class BridgeMode { kEntryFirst = 0, kStopFirst = 1 };

/// @brief Снимок защитного стоп-ордера, найденного на стороне Sierra.
struct StopSnapshot {
  bool valid = false;
  double price = 0.0;
  uint32_t internal_id = 0;
  BuySellEnum side = BSE_UNDEFINED;
};

/// @brief Группа полей, описывающих локальное состояние TradeID.
struct TradeState {
  std::string id;
  std::string direction;
  bool entry_sent = false;
  bool stop_sent = false;
  double last_entry_price = 0.0;
  double last_stop_price = 0.0;
  uint32_t last_stop_internal_id = 0;

  /// @brief Сбрасывает состояние TradeID.
  void Reset() {
    id.clear();
    direction.clear();
    entry_sent = false;
    stop_sent = false;
    last_entry_price = 0.0;
    last_stop_price = 0.0;
    last_stop_internal_id = 0;
  }

  /// @brief Возвращает true, если и вход, и стоп отправлены.
  bool IsTransmitted() const { return entry_sent && stop_sent && !id.empty(); }
};

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
  TradeState trade;
  PositionState last_position_state = PositionState::kFlat;
  double last_price = 0.0;
  DWORD last_pipe_error = ERROR_SUCCESS;
  std::string last_pipe_stage;
  std::string last_pipe_error_time;
  int status_drawing_id = 0;
  bool last_pipe_connected = false;
  std::deque<std::string> pending_payloads;
  /// @brief Последний обнаруженный стоп перед входом в режиме STOP_FIRST.
  /// @note Заполняется только в состоянии flat и сбрасывается после использования.
  std::optional<StopSnapshot> pending_stop_before_entry;
  /// @brief Последнее зафиксированное количество контрактов (для контроля частичного закрытия).
  double last_position_quantity = 0.0;
};

/// @brief Определяет состояние позиции на основе количества контрактов.
/// @param quantity Текущее количество контрактов.
/// @return PositionState Возвращает flat/long/short.
PositionState DeterminePositionState(double quantity) {
  constexpr double kEpsilon = 1e-6;
  if (quantity > kEpsilon) {
    return PositionState::kLong;
  }
  if (quantity < -kEpsilon) {
    return PositionState::kShort;
  }
  return PositionState::kFlat;
}

/// @brief Возвращает строковое представление направления сделки.
/// @param state Состояние позиции.
/// @return std::string Строка LONG/SHORT или пустая строка для flat.
std::string DirectionFromState(PositionState state) {
  switch (state) {
    case PositionState::kLong:
      return "LONG";
    case PositionState::kShort:
      return "SHORT";
    default:
      return "";
  }
}

/// @brief Проверяет соответствие направления сделки стороне стоп-ордера.
/// @param direction Строка направления (LONG/SHORT).
/// @param side Направление ордера (Buy/Sell).
/// @return true, если комбинация допустима.
bool DirectionMatchesStop(const std::string& direction, BuySellEnum side) {
  if (direction == "LONG") {
    return side == BSE_SELL || side == BSE_UNDEFINED;
  }
  if (direction == "SHORT") {
    return side == BSE_BUY || side == BSE_UNDEFINED;
  }
  return true;
}

/// @brief Определяет направление сделки по цене стопа и текущей цене.
/// @param snapshot Снимок стоп-ордера.
/// @param reference_price Текущая рыночная цена.
/// @return std::string Строка LONG/SHORT.
std::string DetermineDirectionFromStop(const StopSnapshot& snapshot, double reference_price) {
  if (reference_price <= 0.0) {
    return (snapshot.side == BSE_SELL) ? "LONG" : "SHORT";
  }
  if (snapshot.price < reference_price) {
    return "LONG";
  }
  if (snapshot.price > reference_price) {
    return "SHORT";
  }
  return (snapshot.side == BSE_SELL) ? "LONG" : "SHORT";
}

/// @brief Проверяет, относится ли тип ордера к стоп-ордерам.
/// @param order_type Числовой код типа ордера.
/// @return true, если тип соответствует стоп-ордерам.
bool IsStopOrderType(int order_type) {
  switch (order_type) {
    case SCT_ORDERTYPE_STOP:
    case SCT_ORDERTYPE_STOP_LIMIT:
    case SCT_ORDERTYPE_TRAILING_STOP:
    case SCT_ORDERTYPE_TRAILING_STOP_LIMIT:
    case SCT_ORDERTYPE_TRIGGERED_TRAILING_STOP_3_OFFSETS:
    case SCT_ORDERTYPE_TRIGGERED_TRAILING_STOP_LIMIT_3_OFFSETS:
    case SCT_ORDERTYPE_STEP_TRAILING_STOP:
    case SCT_ORDERTYPE_STEP_TRAILING_STOP_LIMIT:
    case SCT_ORDERTYPE_TRIGGERED_STEP_TRAILING_STOP:
    case SCT_ORDERTYPE_TRIGGERED_STEP_TRAILING_STOP_LIMIT:
    case SCT_ORDERTYPE_OCO_LIMIT_STOP:
    case SCT_ORDERTYPE_OCO_LIMIT_STOP_LIMIT:
    case SCT_ORDERTYPE_OCO_BUY_STOP_SELL_STOP:
    case SCT_ORDERTYPE_OCO_BUY_STOP_LIMIT_SELL_STOP_LIMIT:
    case SCT_ORDERTYPE_BID_ASK_QUANTITY_TRIGGERED_STOP:
    case SCT_ORDERTYPE_TRADE_VOLUME_TRIGGERED_STOP:
    case SCT_ORDERTYPE_STOP_WITH_BID_ASK_TRIGGERING:
    case SCT_ORDERTYPE_STOP_WITH_LAST_TRIGGERING:
    case SCT_ORDERTYPE_TRADE_VOLUME_TRIGGERED_STOP_LIMIT:
    case SCT_ORDERTYPE_STOP_LIMIT_CLIENT_SIDE:
    case SCT_ORDERTYPE_TRIGGERED_STOP:
      return true;
    default:
      return false;
  }
}

/// @brief Возвращает текущую рыночную цену для оценки направления.
/// @param sc Контекст исследования.
/// @return double Цена последнего бара либо 0.
double DetermineReferencePrice(SCStudyGraphRef sc) {
  if (sc.Index >= 0) {
    return sc.Close[sc.Index];
  }
  if (sc.ArraySize > 0) {
    return sc.Close[sc.ArraySize - 1];
  }
  return 0.0;
}

/**
 * @brief Вычисляет размер защитного стопа в пунктах.
 * @param entry_price Цена входа позиции.
 * @param stop_price Цена защитного стоп-ордера.
 * @param tick_size Размер тика инструмента (не используется в расчёте, зарезервировано).
 * @return double Абсолютное расстояние от входа до стопа в ценовых пунктах (price points).
 * @note Используется разность цен без деления на TickSize, чтобы значение соответствовало визуальной метке
 *       «сколько пунктов между входом и стопом» в Sierra.
 * @warning Возвращает 0, если цена входа или стопа не задана (<=0).
 */
double ComputeStopDistancePoints(double entry_price, double stop_price, double tick_size) {
  if (entry_price <= 0.0 || stop_price <= 0.0) {
    return 0.0;
  }
  (void)tick_size;  // подавляем предупреждения об неиспользуемом параметре
  return std::fabs(entry_price - stop_price);
}

/// @brief Ищет активный защитный стоп-ордер по символу и аккаунту.
/// @param sc Контекст исследования.
/// @param state Текущее состояние позиции.
/// @param reference_price Рыночная цена для измерения расстояния.
/// @return std::optional<StopSnapshot> Найденный ордер или пустое значение.
std::optional<StopSnapshot> FindProtectiveStop(SCStudyGraphRef sc,
                                               PositionState state,
                                               double reference_price) {
  s_SCTradeOrder order{};
  int order_index = 0;
  const SCString symbol = sc.Symbol;
  const SCString account = sc.SelectedTradeAccount;
  const char* account_chars = account.IsEmpty() ? nullptr : account.GetChars();
  const char* symbol_chars = symbol.GetChars();

  double best_distance = std::numeric_limits<double>::max();
  StopSnapshot best_snapshot;
  bool found = false;

  auto accepts_order = [state](s_SCTradeOrder& candidate) -> bool {
    if (!candidate.IsWorking() || !IsStopOrderType(candidate.OrderTypeAsInt)) {
      return false;
    }
    if (state == PositionState::kLong) {
      return candidate.BuySell == BSE_SELL;
    }
    if (state == PositionState::kShort) {
      return candidate.BuySell == BSE_BUY;
    }
    return true;
  };

  while (sc.GetOrderForSymbolAndAccountByIndex(symbol_chars, account_chars, order_index, order) > 0) {
    ++order_index;
    if (!accepts_order(order)) {
      continue;
    }
    const double distance =
        (reference_price > 0.0) ? std::fabs(order.Price1 - reference_price) : 0.0;
    if (!found || distance < best_distance) {
      best_distance = distance;
      best_snapshot.valid = true;
      best_snapshot.price = order.Price1;
      best_snapshot.internal_id = order.InternalOrderID;
      best_snapshot.side = order.BuySell;
      found = true;
    }
  }

  if (!found) {
    return std::nullopt;
  }
  return best_snapshot;
}

/**
 * @brief Отменяет все рабочие ордера по символу/аккаунту в Sierra Chart.
 * @param sc Контекст исследования.
 * @param symbol Символ инструмента.
 * @param account Торговый аккаунт (может быть пустым — тогда отменяются ордера по умолчанию).
 * @return int Количество отменённых ордеров.
 * @note Используется при закрытии позиции, чтобы удалить оставшиеся стопы/лимиты.
 * @warning Выполняется вслепую: снимает все рабочие ордера по символу/аккаунту, независимо от направления.
 */
int CancelWorkingOrdersForSymbol(SCStudyGraphRef sc, const SCString& symbol, const SCString& account) {
  s_SCTradeOrder order{};
  int order_index = 0;
  int cancelled = 0;
  const char* account_chars = account.IsEmpty() ? nullptr : account.GetChars();
  const char* symbol_chars = symbol.GetChars();

  while (sc.GetOrderForSymbolAndAccountByIndex(symbol_chars, account_chars, order_index, order) > 0) {
    ++order_index;
    if (!order.IsWorking()) {
      continue;
    }
    sc.CancelOrder(order.InternalOrderID);
    ++cancelled;
  }
  return cancelled;
}

/**
 * @brief Усиливает закрытие: отменяет ордера и принудительно флеттит позицию по символу.
 * @param sc Контекст исследования.
 * @param symbol Символ инструмента.
 * @param account Аккаунт.
 * @return int Количество отменённых ордеров.
 * @note Используется в режиме STOP_FIRST при частичном/полном срабатывании позиции.
 * @warning Предполагается простая логика «1 лот / 1 стоп / 1 тейк». Выполняет FlattenPosition без доп. проверок.
 */
int ForceFlattenAndCancel(SCStudyGraphRef sc, const SCString& symbol, const SCString& account) {
  const int cancelled = CancelWorkingOrdersForSymbol(sc, symbol, account);
  sc.FlattenPosition();
  return cancelled;
}

/// @brief Возвращает UNIX-время в миллисекундах.
/// @return uint64_t Метка времени UTC.
uint64_t CurrentTimestampMsUTC() {
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  return static_cast<uint64_t>(now.time_since_epoch().count());
}

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
std::string GenerateTradeId(const std::string& symbol, const std::string& direction) {
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

/// @brief Добавляет общие поля JSON для сообщений моста.
/// @param oss Поток для записи.
/// @param type Тип сообщения.
/// @param trade_id Текущий идентификатор сделки.
/// @param symbol Символ инструмента.
/// @param direction Направление сделки (может быть пустым).
/// @param note Дополнительное текстовое поле.
void AppendCommonJsonFields(std::ostringstream& oss, const std::string& type,
                            const std::string& trade_id, const std::string& symbol,
                            const std::string& direction, const std::string& note) {
  oss << "\"type\":\"" << EscapeJson(type) << "\",";
  oss << "\"id\":\"" << EscapeJson(trade_id) << "\",";
  oss << "\"symbol\":\"" << EscapeJson(symbol) << "\",";
  if (!direction.empty()) {
    oss << "\"direction\":\"" << EscapeJson(direction) << "\",";
  }
  oss << "\"ts_ms\":" << CurrentTimestampMsUTC() << ",";
  oss << "\"source\":\"sierra\"";
  if (!note.empty()) {
    oss << ",\"note\":\"" << EscapeJson(note) << "\"";
  }
}

/**
 * @brief Формирует JSON для OPEN_SIGNAL.
 * @param trade_id Идентификатор сделки.
 * @param symbol Тикер инструмента в Sierra Chart.
 * @param direction Направление сделки (`LONG`/`SHORT`).
 * @param entry_price Цена входа.
 * @param note Дополнительный текст из Input.
 * @param stop_loss_points Размер стопа в пунктах (или std::nullopt, если поле не требуется).
 * @return std::string Строка JSON с завершающим переводом строки.
 * @note Все числовые поля форматируются с точностью до двух знаков после запятой.
 * @warning Поле `stop_loss_points` добавляется только при наличии значения, клиент MT5 должен быть готов обрабатывать его отсутствие.
 */
std::string BuildOpenSignalJson(const std::string& trade_id,
                                const std::string& symbol,
                                const std::string& direction,
                                double entry_price,
                                const std::string& note,
                                const std::optional<double>& stop_loss_points) {
  std::ostringstream oss;
  oss << "{";
  AppendCommonJsonFields(oss, "OPEN_SIGNAL", trade_id, symbol, direction, note);
  oss << std::fixed << std::setprecision(2);
  oss << ",\"entry_price\":" << entry_price;
  if (stop_loss_points.has_value()) {
    const double rounded =
        std::round(*stop_loss_points * 100.0) / 100.0;  // два знака после запятой
    oss << ",\"stop_loss_points\":" << rounded;
  }
  oss << "}\n";
  return oss.str();
}

/// @brief Формирует JSON для ENTRY_AFTER_STOP.
std::string BuildEntryAfterStopJson(const std::string& trade_id,
                                    const std::string& symbol,
                                    const std::string& direction,
                                    double entry_price,
                                    const std::string& note) {
  std::ostringstream oss;
  oss << "{";
  AppendCommonJsonFields(oss, "ENTRY_AFTER_STOP", trade_id, symbol, direction, note);
  oss << std::fixed << std::setprecision(2);
  oss << ",\"entry_price\":" << entry_price;
  oss << "}\n";
  return oss.str();
}

/// @brief Формирует JSON для STOP_ONLY.
std::string BuildStopOnlyJson(const std::string& trade_id,
                              const std::string& symbol,
                              const std::string& direction,
                              double stop_price,
                              const std::string& note) {
  std::ostringstream oss;
  oss << "{";
  AppendCommonJsonFields(oss, "STOP_ONLY", trade_id, symbol, direction, note);
  oss << std::fixed << std::setprecision(2);
  oss << ",\"stop_price\":" << stop_price;
  oss << ",\"mode\":\"STOP_FIRST\"";
  oss << "}\n";
  return oss.str();
}

/// @brief Формирует JSON для STOP_LEVEL.
std::string BuildStopLevelJson(const std::string& trade_id,
                               const std::string& symbol,
                               const std::string& direction,
                               double stop_price,
                               const std::string& note) {
  std::ostringstream oss;
  oss << "{";
  AppendCommonJsonFields(oss, "STOP_LEVEL", trade_id, symbol, direction, note);
  oss << std::fixed << std::setprecision(2);
  oss << ",\"stop_price\":" << stop_price;
  oss << ",\"mode\":\"ENTRY_FIRST\"";
  oss << "}\n";
  return oss.str();
}

/// @brief Формирует JSON для CLOSE_SIGNAL.
std::string BuildCloseSignalJson(const std::string& trade_id,
                                 const std::string& symbol,
                                 const std::string& direction,
                                 double close_price,
                                 const std::string& close_reason,
                                 const std::string& note) {
  std::ostringstream oss;
  oss << "{";
  AppendCommonJsonFields(oss, "CLOSE_SIGNAL", trade_id, symbol, direction, note);
  oss << ",\"close_reason\":\"" << EscapeJson(close_reason) << "\"";
  oss << std::fixed << std::setprecision(2);
  oss << ",\"close_price\":" << close_price;
  oss << "}\n";
  return oss.str();
}

/// @brief Кладёт JSON в очередь отправки и запускает доставку.
/// @param sc Контекст исследования Sierra Chart.
/// @param ctx Состояние моста.
/// @param payload Готовая строка JSON.
/// @param log_message Сообщение для журнала при успехе.
void EnqueueBridgePayload(SCStudyGraphRef sc, MirrorContext* ctx,
                          const std::string& payload, const std::string& log_message) {
  QueuePayload(ctx, payload);
  bool waiting_for_client = false;
  if (!FlushPendingPayloads(ctx, &waiting_for_client)) {
    if (waiting_for_client) {
      sc.AddMessageToLog(
          "SierraStudyBridge: ожидаем подключение MT5 клиента, сообщение в очереди.", 0);
    } else {
      sc.AddMessageToLog("SierraStudyBridge: не удалось отправить сообщение в MT5 pipe.", 1);
      AppendBridgeErrorLog("Failed to flush payload to MT5 pipe.");
    }
  } else if (!log_message.empty()) {
    sc.AddMessageToLog(log_message.c_str(), 0);
  }
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

/**
 * @brief Реализация моста Sierra ↔ MT5 по спецификации bridge_sierra_mt5_spec.md.
 * @param sc Контекст ACSIL.
 * @note Отслеживает позицию и рабочие стоп-ордера, формирует события OPEN/STOP/CLOSE.
 * @warning После передачи пары «вход + стоп» сделка сопровождается только до закрытия.
 */
SCSFExport scsf_SierraStudyBridge(SCStudyGraphRef sc) {
  SCInputRef pipe_input = sc.Input[0];
  SCInputRef note_input = sc.Input[1];
  SCInputRef mode_input = sc.Input[2];
  SCInputRef close_reason_input = sc.Input[3];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - MT5 Bridge";
    sc.StudyDescription = "Отправляет события по протоколу bridge_sierra_mt5_spec.md.";
    sc.AutoLoop = 0;
    sc.UpdateAlways = 1;
    sc.GraphRegion = 0;
    sc.FreeDLL = 1;
    pipe_input.Name = "Pipe Name";
    pipe_input.SetString("\\\\.\\pipe\\SierraStudyAdvisor");

    note_input.Name = "Note (optional)";
    note_input.SetString("");

    mode_input.Name = "Bridge Mode";
    mode_input.SetCustomInputStrings("ENTRY_FIRST;STOP_FIRST");
    mode_input.SetCustomInputIndex(static_cast<int>(BridgeMode::kStopFirst));

    close_reason_input.Name = "Close Reason";
    close_reason_input.SetCustomInputStrings("signal;manual;flatten");
    close_reason_input.SetCustomInputIndex(0);
    return;
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

  EnsureLogging(sc);

  auto* ctx = static_cast<MirrorContext*>(sc.GetPersistentPointer(kPersistBridgeContext));
  if (ctx == nullptr) {
    ctx = new MirrorContext();
    sc.SetPersistentPointer(kPersistBridgeContext, ctx);
  }

  const SCString pipe_name_sc = pipe_input.GetString();
  const std::string pipe_name = pipe_name_sc.GetChars();
  ctx->pipe.UpdateName(pipe_name);
  ctx->pipe.EnsureConnected();

  if (sc.GetPersistentInt(kPersistBridgeInitialized) == 0) {
    sc.AddMessageToLog(("SierraStudyBridge ready (pipe=" + pipe_name + ")").c_str(), 0);
    sc.SetPersistentInt(kPersistBridgeInitialized, 1);
  }

  const BridgeMode bridge_mode =
      (mode_input.GetIndex() == static_cast<int>(BridgeMode::kStopFirst))
          ? BridgeMode::kStopFirst
          : BridgeMode::kEntryFirst;

  std::string close_reason = "signal";
  switch (close_reason_input.GetIndex()) {
    case 1:
      close_reason = "manual";
      break;
    case 2:
      close_reason = "flatten";
      break;
    default:
      close_reason = "signal";
      break;
  }
  const SCString note_sc = note_input.GetString();
  const std::string note = note_sc.GetChars();
  const std::string symbol = sc.Symbol.GetChars();

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
  const PositionState previous_state = ctx->last_position_state;
  const PositionState current_state = DeterminePositionState(quantity);
  const double reference_price = DetermineReferencePrice(sc);
  ctx->last_price = reference_price;
  const double price_epsilon = (sc.TickSize > 0.0) ? sc.TickSize * 0.25 : 1e-6;

  // В STOP_FIRST при любом уменьшении позиции инициируем принудительное закрытие и снятие ордеров.
  if (bridge_mode == BridgeMode::kStopFirst && previous_state != PositionState::kFlat) {
    const double prev_qty = ctx->last_position_quantity;
    const double curr_qty = quantity;
    const double kQtyEps = 1e-6;
    if (std::fabs(curr_qty) + kQtyEps < std::fabs(prev_qty)) {
      const int cancelled = ForceFlattenAndCancel(sc, sc.Symbol, sc.SelectedTradeAccount);
      if (cancelled > 0) {
        std::ostringstream cancel_log;
        cancel_log << "SierraStudyBridge: частичное/полное срабатывание, отменено ордеров: "
                   << cancelled;
        sc.AddMessageToLog(cancel_log.str().c_str(), 0);
      }
      // После flatten позиция станет flat, оставшаяся логика закроет TradeState через CLOSE_SIGNAL.
    }
  }

  const auto stop_snapshot = FindProtectiveStop(sc, current_state, reference_price);

  if (bridge_mode == BridgeMode::kStopFirst && current_state == PositionState::kFlat) {
    if (stop_snapshot.has_value()) {
      ctx->pending_stop_before_entry = stop_snapshot;
    } else {
      ctx->pending_stop_before_entry.reset();
    }
  }

  if (previous_state == PositionState::kFlat && current_state != PositionState::kFlat) {
    ctx->trade.Reset();
    ctx->trade.direction = DirectionFromState(current_state);

    if (bridge_mode == BridgeMode::kStopFirst) {
      std::optional<StopSnapshot> entry_stop =
          ctx->pending_stop_before_entry.has_value() ? ctx->pending_stop_before_entry
                                                     : stop_snapshot;
      if (!entry_stop.has_value()) {
        sc.AddMessageToLog(
            "SierraStudyBridge: вход в режиме STOP_FIRST без предварительного стоп-ордера, OPEN_SIGNAL "
            "не отправлен.",
            1);
      } else if (!DirectionMatchesStop(ctx->trade.direction, entry_stop->side)) {
        sc.AddMessageToLog(
            "SierraStudyBridge: сторона стоп-ордера не совпала с направлением входа, OPEN_SIGNAL пропущен.",
            1);
      } else {
        ctx->trade.id = GenerateTradeId(symbol, ctx->trade.direction);
        ctx->trade.last_entry_price = average_price;
        ctx->trade.last_stop_price = entry_stop->price;
        ctx->trade.last_stop_internal_id = entry_stop->internal_id;
        const double stop_points =
            ComputeStopDistancePoints(ctx->trade.last_entry_price, ctx->trade.last_stop_price, sc.TickSize);
        const std::optional<double> stop_points_opt(stop_points);
        const std::string payload = BuildOpenSignalJson(ctx->trade.id, symbol, ctx->trade.direction,
                                                        ctx->trade.last_entry_price, note, stop_points_opt);
        std::ostringstream log;
        log << "[bridge] OPEN_SIGNAL trade_id=" << ctx->trade.id
            << " price=" << ctx->trade.last_entry_price;
        log << " stop_pts=" << stop_points;
        EnqueueBridgePayload(sc, ctx, payload, log.str());
        ctx->trade.entry_sent = true;
        ctx->trade.stop_sent = true;
      }
      ctx->pending_stop_before_entry.reset();
    } else {
      ctx->trade.Reset();
      ctx->trade.direction = DirectionFromState(current_state);
      ctx->trade.id = GenerateTradeId(symbol, ctx->trade.direction);
      ctx->trade.last_entry_price = average_price;
      const std::string payload =
          BuildOpenSignalJson(ctx->trade.id, symbol, ctx->trade.direction, average_price, note,
                              std::nullopt);
      std::ostringstream log;
      log << "[bridge] OPEN_SIGNAL trade_id=" << ctx->trade.id << " price=" << average_price;
      EnqueueBridgePayload(sc, ctx, payload, log.str());
      ctx->trade.entry_sent = true;
    }
  }

  if (bridge_mode == BridgeMode::kEntryFirst && ctx->trade.entry_sent &&
      !ctx->trade.stop_sent && stop_snapshot.has_value() &&
      DirectionMatchesStop(ctx->trade.direction, stop_snapshot->side)) {
    const auto& snapshot = stop_snapshot.value();
    const bool is_new_stop =
        snapshot.internal_id != ctx->trade.last_stop_internal_id ||
        std::fabs(snapshot.price - ctx->trade.last_stop_price) > price_epsilon;
    if (is_new_stop) {
      const std::string payload =
          BuildStopLevelJson(ctx->trade.id, symbol, ctx->trade.direction, snapshot.price, note);
      std::ostringstream log;
      log << "[bridge] STOP_LEVEL trade_id=" << ctx->trade.id << " price=" << snapshot.price;
      EnqueueBridgePayload(sc, ctx, payload, log.str());
      ctx->trade.stop_sent = true;
      ctx->trade.last_stop_price = snapshot.price;
      ctx->trade.last_stop_internal_id = snapshot.internal_id;
    }
  }

  if (previous_state != PositionState::kFlat && current_state == PositionState::kFlat &&
      ctx->trade.entry_sent) {
    if (ctx->trade.direction.empty()) {
      ctx->trade.direction = DirectionFromState(previous_state);
    }
    if (bridge_mode == BridgeMode::kEntryFirst && !ctx->trade.stop_sent) {
      sc.AddMessageToLog(
          "SierraStudyBridge: после входа не был обнаружен защитный стоп, отправляем CLOSE_SIGNAL "
          "без подтверждённого стопа.",
          1);
    }
    const std::string payload = BuildCloseSignalJson(
        ctx->trade.id, symbol, ctx->trade.direction, reference_price, close_reason, note);
    std::ostringstream log;
    log << "[bridge] CLOSE_SIGNAL trade_id=" << ctx->trade.id
        << " price=" << reference_price;
    EnqueueBridgePayload(sc, ctx, payload, log.str());

    const int cancelled =
        CancelWorkingOrdersForSymbol(sc, sc.Symbol, sc.SelectedTradeAccount);
    if (cancelled > 0) {
      std::ostringstream cancel_log;
      cancel_log << "SierraStudyBridge: закрытие сделки, отменено рабочих ордеров: " << cancelled;
      sc.AddMessageToLog(cancel_log.str().c_str(), 0);
    }
    ctx->pending_stop_before_entry.reset();
    ctx->trade.Reset();
  }

  ctx->last_position_state = current_state;
  ctx->last_position_quantity = quantity;

  FlushPendingPayloads(ctx);
  UpdatePipeStatusOverlay(sc, ctx);
}

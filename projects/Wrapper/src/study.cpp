#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#if __has_include(<plog/Log.h>)
#define SIERRA_STUDY_HAS_PLOG 1
#include <filesystem>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>
#else
#define SIERRA_STUDY_HAS_PLOG 0
#endif

/// \brief Название группы, отображаемое в диалоге Sierra Chart «Add Custom Study».
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistLogging = 1;

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






#include "sierra/acsil/study.hpp"

#include "sierra/core/moving_average.hpp"

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

namespace {

constexpr int kPersistLogging = 1;
constexpr int kPersistLastPeriod = 2;

#if SIERRA_STUDY_HAS_PLOG
void EnsureLogging(SCStudyGraphRef sc) {
  if (sc.Index != 0) {
    return;
  }

  const int initialized = sc.GetPersistentInt(kPersistLogging);
  if (initialized == 1) {
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

int SanitizePeriod(int period) {
  return period < 1 ? 1 : period;
}

}  // namespace

SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc) {
  SCSubgraphRef ma = sc.Subgraph[0];
  SCInputRef periodInput = sc.Input[0];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - Moving Average";
    sc.StudyDescription = "Example ACSIL study wrapping the core moving average.";
    sc.AutoLoop = 1;
    sc.GraphRegion = 0;

    ma.Name = "Moving Average";
    ma.DrawStyle = DRAWSTYLE_LINE;
    ma.PrimaryColor = RGB(0, 128, 255);
    ma.LineWidth = 2;
    ma.DrawZeros = false;

    periodInput.Name = "Period";
    periodInput.SetInt(20);
    periodInput.SetIntLimits(1, 500);
    sc.UpdateStartIndex = periodInput.GetInt() - 1;
    return;
  }

  if (sc.LastCallToFunction) {
    return;
  }

  EnsureLogging(sc);

  int period = SanitizePeriod(periodInput.GetInt());
  sc.UpdateStartIndex = period - 1;
  const int storedPeriod = sc.GetPersistentInt(kPersistLastPeriod);
  if (storedPeriod != period) {
    sc.SetPersistentInt(kPersistLastPeriod, period);
#if SIERRA_STUDY_HAS_PLOG
    if (storedPeriod != 0) {
      PLOG_INFO << "Updated moving average period to " << period;
    }
#endif
  }

  const int length = sc.Index + 1;
  if (length <= 0) {
    ma[sc.Index] = std::numeric_limits<float>::quiet_NaN();
    return;
  }

  std::vector<double> closes(static_cast<std::size_t>(length));
  for (int i = 0; i < length; ++i) {
    closes[static_cast<std::size_t>(i)] = sc.Close[i];
  }

  const auto averages = sierra::core::moving_average(closes, static_cast<std::size_t>(period));
  const double value = averages.back();
  if (std::isnan(value)) {
    ma[sc.Index] = std::numeric_limits<float>::quiet_NaN();
  } else {
    ma[sc.Index] = static_cast<float>(value);
  }
}

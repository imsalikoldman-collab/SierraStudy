#include "sierra/acsil/study.hpp"

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

// Group name displayed in Sierra Chart "Add Custom Study" dialog.
SCDLLName("SierraStudy Custom Studies")

namespace {

constexpr int kPersistState = 0;
constexpr int kPersistLogging = 1;

struct StudyState {
  std::vector<double> closeBuffer;
};

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

StudyState* GetOrCreateState(SCStudyGraphRef sc) {
  auto* state = static_cast<StudyState*>(sc.GetPersistentPointer(kPersistState));
  if (state == nullptr) {
    state = new StudyState{};
    sc.SetPersistentPointer(kPersistState, state);
  }
  return state;
}

void DestroyState(SCStudyGraphRef sc) {
  auto* state = static_cast<StudyState*>(sc.GetPersistentPointer(kPersistState));
  if (state != nullptr) {
    delete state;
    sc.SetPersistentPointer(kPersistState, nullptr);
  }
}

}  // namespace

SCSFExport scsf_SierraStudyMovingAverage(SCStudyGraphRef sc) {
  SCSubgraphRef ma = sc.Subgraph[0];
  SCInputRef periodInput = sc.Input[0];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - Moving Average";
    sc.StudyDescription = "Example ACSIL study wrapping the core moving average.";
    sc.AutoLoop = 1;
    sc.FreeDLL = 1;
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
    DestroyState(sc);
    return;
  }

  EnsureLogging(sc);

  auto* state = GetOrCreateState(sc);

  const int period = std::max(1, periodInput.GetInt());
  sc.DataStartIndex = period - 1;

  const int length = sc.ArraySize;
  if (length <= 0) {
    ma[sc.Index] = std::numeric_limits<float>::quiet_NaN();
    return;
  }

  state->closeBuffer.resize(static_cast<std::size_t>(sc.Index + 1));
  for (int i = 0; i <= sc.Index; ++i) {
    state->closeBuffer[static_cast<std::size_t>(i)] = sc.Close[i];
  }

  const auto averages =
      sierra::core::moving_average(state->closeBuffer, static_cast<std::size_t>(period));
  const double value = averages.back();
  ma[sc.Index] = std::isnan(value) ? std::numeric_limits<float>::quiet_NaN()
                                   : static_cast<float>(value);
}

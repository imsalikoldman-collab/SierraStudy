#include "sierra/acsil/study.hpp"
#include "sierra/acsil/supportFunction.hpp"

#include "sierra/core/moving_average.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
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

namespace {

constexpr int kDeltaTextToolId = 10001;
constexpr int kVolTextToolId = 10002;

struct ExternalValue {
  double value = 0.0;
  bool available = false;
};

struct StudySelection {
  int chart_number = 0;
  int study_id = 0;
  int subgraph_index = 0;

  bool IsValid() const { return study_id > 0 && subgraph_index >= 0; }
};

StudySelection ResolveStudySelection(const SCInputRef& input,
                                     int fallback_chart) {
  StudySelection selection{};
  const s_ChartStudySubgraphValues values = input.GetChartStudySubgraphValues();
  selection.chart_number =
      values.ChartNumber != 0 ? values.ChartNumber : fallback_chart;
  selection.study_id = values.StudyID;
  selection.subgraph_index = values.SubgraphIndex;
  return selection;
}

/// @brief �������� �������� delta/vol/s �� �������� ����樨.
/// @param sc ����䥩� ACSIL ��� ���������� ������.
/// @param reference ����������� ����� (�����/�����/Subgraph).
/// @param value_index ����� �� ����樨, ��� �������� ��� ��������.
/// @return ����������� ��������, ��������� ��᫥��� ��� �������.
ExternalValue FetchExternalValue(SCStudyGraphRef sc,
                                 const StudySelection& reference,
                                 int value_index) {
  ExternalValue result;
  if (value_index < 0 || !reference.IsValid()) {
    return result;
  }

  SCFloatArray source;
  if (reference.chart_number == sc.ChartNumber) {
    sc.GetStudyArrayUsingID(reference.study_id, reference.subgraph_index,
                            source);
    if (value_index < source.GetArraySize()) {
      result.value = source[value_index];
      result.available = std::isfinite(result.value);
    }
  } else {
    s_ChartStudySubgraphValues request{};
    request.ChartNumber = reference.chart_number;
    request.StudyID = reference.study_id;
    request.SubgraphIndex = reference.subgraph_index;
    sc.GetStudyArrayFromChartUsingID(request, source);
    const int last_index = source.GetArraySize() - 1;
    if (last_index >= 0) {
      result.value = source[last_index];
      result.available = std::isfinite(result.value);
    }
  }
  return result;
}

/// @brief �������� ������ � ��������� delta/vol/s ��� UI.
/// @param label_prefix �������� ����ᯨ� (������� �������������).
/// @param value �������� � ���� �������.
/// @param decimals ���������� ������� ����� ���������.
/// @return ������������ ������ ��� �������.
std::string FormatLabelText(const std::string& label_prefix,
                            const ExternalValue& value,
                            int decimals) {
  std::ostringstream stream;
  if (!label_prefix.empty()) {
    stream << label_prefix;
    if (label_prefix.back() != ' ') {
      stream << ' ';
    }
  }
  if (value.available) {
    stream << std::fixed << std::setprecision(std::max(0, decimals))
           << value.value;
  } else {
    stream << "N/A";
  }
  return stream.str();
}

/// @brief ��������� ������ ������ �� Subgraph ��� выбора ������.
int ResolveFontSize(const SCSubgraphRef& style) {
  const int requested = style.LineWidth;
  return (requested <= 0) ? 12 : requested;
}

/// @brief �������� ����� ��������� ������ через UseTool.
void DrawFloatingText(SCStudyGraphRef sc,
                      int line_number,
                      const SCSubgraphRef& style,
                      COLORREF color,
                      const std::string& text,
                      int base_index,
                      double base_price,
                      int horizontal_offset,
                      double vertical_offset) {
  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.DrawingType = DRAWING_TEXT;
  tool.Region = 0;
  tool.LineNumber = line_number;
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.BeginIndex = base_index + horizontal_offset;
  tool.BeginValue = base_price + vertical_offset;
  tool.Color = color;
  tool.FontSize = ResolveFontSize(style);
  tool.TextAlignment = DT_LEFT;
  tool.FontBold = 0;
  tool.Text = text.c_str();
  tool.AddAsUserDrawnDrawing = 0;
  sc.UseTool(tool);
}

/// @brief �������� ������ �����������绘 �������� при удалении study.
void RemoveFloatingText(SCStudyGraphRef sc) {
  sc.DeleteUserDrawnACSDrawing(sc.ChartNumber, kDeltaTextToolId);
  sc.DeleteUserDrawnACSDrawing(sc.ChartNumber, kVolTextToolId);
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

/// @brief ������ delta + vol/s ��� �������� ���樨.
/// @param sc ����䥩� ACSIL ��� Sierra Chart.
/// @return void.
SCSFExport scsf_SierraStudyDeltaVolHeadsUp(SCStudyGraphRef sc) {
  SCSubgraphRef delta_style = sc.Subgraph[0];
  SCSubgraphRef vol_style = sc.Subgraph[1];

  SCInputRef delta_reference = sc.Input[0];
  SCInputRef vol_reference = sc.Input[1];
  SCInputRef delta_decimals = sc.Input[2];
  SCInputRef vol_decimals = sc.Input[3];
  SCInputRef horizontal_offset = sc.Input[4];
  SCInputRef vertical_spacing = sc.Input[5];
  SCInputRef delta_prefix = sc.Input[6];
  SCInputRef vol_prefix = sc.Input[7];
  SCInputRef delta_color_input = sc.Input[8];
  SCInputRef vol_color_input = sc.Input[9];

  if (sc.SetDefaults) {
    sc.GraphName = "SierraStudy - Delta & Vol/S Label";
    sc.StudyDescription =
        "Displays delta and volume per second taken from other studies in the "
        "main price region.";
    sc.GraphRegion = 0;
    sc.AutoLoop = 0;
    sc.UpdateAlways = 1;
    sc.FreeDLL = 1;

    delta_style.Name = "Delta Label";
    delta_style.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
    delta_style.PrimaryColor = RGB(255, 255, 0);
    delta_style.LineWidth = 14;
    delta_style.DrawZeros = false;

    vol_style.Name = "Vol/S Label";
    vol_style.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
    vol_style.PrimaryColor = RGB(255, 128, 0);
    vol_style.LineWidth = 14;
    vol_style.DrawZeros = false;

    delta_reference.Name = "Delta Chart/Study/Subgraph";
    delta_reference.SetChartStudySubgraphValues(sc.ChartNumber, 0, 0);
    vol_reference.Name = "Vol/S Chart/Study/Subgraph";
    vol_reference.SetChartStudySubgraphValues(sc.ChartNumber, 0, 0);

    delta_decimals.Name = "Delta Decimals";
    delta_decimals.SetInt(2);
    delta_decimals.SetIntLimits(0, 8);

    vol_decimals.Name = "Vol/S Decimals";
    vol_decimals.SetInt(2);
    vol_decimals.SetIntLimits(0, 8);

    horizontal_offset.Name = "Horizontal Offset (Bars)";
    horizontal_offset.SetInt(4);
    horizontal_offset.SetIntLimits(0, 30);

    vertical_spacing.Name = "Vertical Spacing (Ticks)";
    vertical_spacing.SetInt(2);
    vertical_spacing.SetIntLimits(1, 20);

    delta_prefix.Name = "Delta Prefix";
    delta_prefix.SetString("");

    vol_prefix.Name = "Vol/S Prefix";
    vol_prefix.SetString("");

    delta_color_input.Name = "Delta Text Color";
    delta_color_input.SetColor(RGB(255, 128, 0));

    vol_color_input.Name = "Vol/S Text Color";
    vol_color_input.SetColor(RGB(255, 128, 0));

    return;
  }

  if (sc.LastCallToFunction) {
    RemoveFloatingText(sc);
    return;
  }

  if (sc.ArraySize <= 0) {
    return;
  }

  const int last_index = sc.ArraySize - 1;
  const double anchor_price = sc.Close[last_index];
  const double tick_size = (sc.TickSize > 0.0) ? sc.TickSize : 1.0;

  const StudySelection delta_selection =
      ResolveStudySelection(delta_reference, sc.ChartNumber);
  const StudySelection vol_selection =
      ResolveStudySelection(vol_reference, sc.ChartNumber);

  const ExternalValue delta_value =
      FetchExternalValue(sc, delta_selection, last_index);
  const ExternalValue vol_value =
      FetchExternalValue(sc, vol_selection, last_index);

  const std::string delta_prefix_value = delta_prefix.GetString();
  const std::string vol_prefix_value = vol_prefix.GetString();
  const std::string delta_text = FormatLabelText(
      delta_prefix_value, delta_value, delta_decimals.GetInt());
  const std::string vol_text = FormatLabelText(
      vol_prefix_value, vol_value, vol_decimals.GetInt());

  const int horizontal_bars = std::max(0, horizontal_offset.GetInt());
  const double spacing_ticks =
      static_cast<double>(std::max(1, vertical_spacing.GetInt())) * tick_size;

  const double delta_price = anchor_price + spacing_ticks * 0.5;
  const double vol_price = anchor_price - spacing_ticks * 0.5;

  DrawFloatingText(sc, kDeltaTextToolId, delta_style,
                   delta_color_input.GetColor(), delta_text, last_index,
                   delta_price, horizontal_bars, 0.0);
  DrawFloatingText(sc, kVolTextToolId, vol_style, vol_color_input.GetColor(),
                   vol_text, last_index, vol_price, horizontal_bars, 0.0);
}






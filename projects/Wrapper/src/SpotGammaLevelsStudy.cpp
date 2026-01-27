#include "sierra/acsil/spotgamma_levels.hpp"

#include "sierra/acsil/supportFunction.hpp"
#include "sierra/core/spotgamma.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// @brief Идентификаторы линий для стабильного обновления через sc.UseTool.
enum LineNumber : int {
  kCallWall = 1201,
  kPutWall,
  kVolatilityTrigger,
  kZeroGamma,
  kLargeGamma1,
  kLargeGamma2,
  kLargeGamma3,
  kLargeGamma4,
  kCombo1,
  kCombo2,
  kCombo3,
  kCombo4,
  kLegend = 1300,
};

/// @brief Группы стилей (вариант A из ТЗ).
enum class StyleGroup { Walls, LargeGamma, Combo };

struct LevelInfo {
  LineNumber line;
  const char* label;
  std::optional<double> sierra::core::SpotGammaLevels::*field;
  StyleGroup group;
};

const LevelInfo kLevelInfos[] = {
    {kCallWall, "Call Wall", &sierra::core::SpotGammaLevels::call_wall, StyleGroup::Walls},
    {kPutWall, "Put Wall", &sierra::core::SpotGammaLevels::put_wall, StyleGroup::Walls},
    {kVolatilityTrigger, "Volatility Trigger",
     &sierra::core::SpotGammaLevels::volatility_trigger, StyleGroup::Walls},
    {kZeroGamma, "Zero Gamma", &sierra::core::SpotGammaLevels::zero_gamma, StyleGroup::Walls},
    {kLargeGamma1, "Large Gamma 1", &sierra::core::SpotGammaLevels::large_gamma_1,
     StyleGroup::LargeGamma},
    {kLargeGamma2, "Large Gamma 2", &sierra::core::SpotGammaLevels::large_gamma_2,
     StyleGroup::LargeGamma},
    {kLargeGamma3, "Large Gamma 3", &sierra::core::SpotGammaLevels::large_gamma_3,
     StyleGroup::LargeGamma},
    {kLargeGamma4, "Large Gamma 4", &sierra::core::SpotGammaLevels::large_gamma_4,
     StyleGroup::LargeGamma},
    {kCombo1, "Combo 1", &sierra::core::SpotGammaLevels::combo_1, StyleGroup::Combo},
    {kCombo2, "Combo 2", &sierra::core::SpotGammaLevels::combo_2, StyleGroup::Combo},
    {kCombo3, "Combo 3", &sierra::core::SpotGammaLevels::combo_3, StyleGroup::Combo},
    {kCombo4, "Combo 4", &sierra::core::SpotGammaLevels::combo_4, StyleGroup::Combo},
};

struct StyleOptions {
  COLORREF color{};
  int line_width{1};
  int line_style{LINESTYLE_SOLID};
};

/**
 * @brief Считывает стиль из входов по индексу начала группы.
 * @param sc Контекст исследования.
 * @param colorIndex Индекс Input с цветом.
 * @param widthIndex Индекс Input с толщиной.
 * @param styleIndex Индекс Input со стилем линии.
 * @return Заполненная структура StyleOptions.
 * @note Допускает значения Sierra Chart LINESTYLE_*.
 * @warning Неверные значения (например, отрицательная ширина) не нормализуются и уйдут в UseTool как есть.
 */
StyleOptions ReadStyleInputs(SCStudyGraphRef sc, int colorIndex, int widthIndex, int styleIndex) {
  StyleOptions s;
  s.color = sc.Input[colorIndex].GetColor();
  s.line_width = sc.Input[widthIndex].GetInt();
  s.line_style = sc.Input[styleIndex].GetInt();
  return s;
}

/**
 * @brief Удаляет линию/текст по LineNumber.
 * @param sc Контекст исследования.
 * @param lineNumber Уникальный идентификатор линии/текста.
 * @return void Функция не возвращает значение.
 * @note Использует UTAM_DELETE, чтобы не оставлять “хвосты” при ошибках или выключении стади.
 * @warning При отсутствии линии Sierra Chart просто проигнорирует вызов, это ожидаемо.
 */
void DeleteDrawing(SCStudyGraphRef sc, LineNumber lineNumber) {
  sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING,
                           static_cast<int>(lineNumber));
}

/**
 * @brief Применяет стиль и рисует/обновляет горизонтальную линию.
 * @param sc Контекст исследования.
 * @param info Метаданные уровня (метка, поле, line number).
 * @param value Цена уровня.
 * @param style Стиль из настроек.
 * @param showLabel Отображать подпись на линии.
 * @return void Функция не возвращает значение.
 * @note Линия тянется “от края до края” за счёт DRAWING_HORIZONTALLINE.
 * @warning Линии создаются как служебные (AddAsUserDrawnDrawing = 0), пользователь не сможет перемещать их мышью.
 */
void DrawLevelLine(SCStudyGraphRef sc,
                   const LevelInfo& info,
                   double value,
                   const StyleOptions& style,
                   bool showLabel) {
  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.LineNumber = static_cast<int>(info.line);
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.DrawingType = DRAWING_HORIZONTALLINE;
  tool.AddAsUserDrawnDrawing = 0;
  tool.BeginValue = value;
  tool.EndValue = value;
  tool.Color = style.color;
  tool.LineStyle = static_cast<SubgraphLineStyles>(style.line_style);
  tool.LineWidth = style.line_width;
  tool.Region = 0;

  if (showLabel) {
    SCString formatted = sc.FormatGraphValue(value, sc.BaseGraphValueFormat);
    tool.Text.Format("%s: %s", info.label, formatted.GetChars());
  }

  sc.UseTool(tool);
}

/**
 * @brief Формирует текст легенды для отображения в правом верхнем углу.
 * @param sc Контекст исследования.
 * @param section Секция SpotGamma с выбранными уровнями.
 * @return SCString с многострочным списком Level ID + значение.
 * @note В легенду включаются только уровни, у которых есть значение.
 * @warning Формат цены берётся из sc.BaseGraphValueFormat; при отличии от формата данных возможны расхождения в знаках после запятой.
 */
SCString BuildLegendText(SCStudyGraphRef sc, const sierra::core::SpotGammaSection& section) {
  SCString legend;
  legend.Format("SpotGamma %s\n", section.chart_symbol.c_str());

  for (const auto& info : kLevelInfos) {
    const auto valueOpt = section.levels.*(info.field);
    if (!valueOpt.has_value()) {
      continue;
    }
    SCString formatted = sc.FormatGraphValue(*valueOpt, sc.BaseGraphValueFormat);
    legend += info.label;
    legend += ": ";
    legend += formatted;
    legend += "\n";
  }

  return legend;
}

/**
 * @brief Рисует или обновляет легенду в правом верхнем углу.
 * @param sc Контекст исследования.
 * @param legendText Сформированная строка легенды.
 * @param fontSize Размер шрифта (input).
 * @return void Функция не возвращает значение.
 * @warning Прозрачность фиксирована (60); при желании сделать непрозрачной потребуется изменить код.
 */
void DrawLegend(SCStudyGraphRef sc, const SCString& legendText, int fontSize) {
  s_UseTool tool;
  tool.Clear();
  tool.ChartNumber = sc.ChartNumber;
  tool.LineNumber = static_cast<int>(LineNumber::kLegend);
  tool.AddMethod = UTAM_ADD_OR_ADJUST;
  tool.DrawingType = DRAWING_TEXT;
  tool.Text = legendText;
  tool.FontSize = fontSize;
  tool.Color = RGB(255, 255, 255);
  tool.TransparencyLevel = 60;
  tool.Region = 0;
  tool.AddAsUserDrawnDrawing = 0;
  tool.TextAlignment = DT_RIGHT | DT_TOP;
  tool.UseRelativeVerticalValues = 1;
  tool.BeginValue = 0.98f;
  tool.BeginDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
  sc.UseTool(tool);
}

}  // namespace

SCSFExport scsf_SpotGammaLevels(SCStudyGraphRef sc) {
  sierra::acsil::LogDllStartup(sc);

  // Inputs
  SCInputRef inputEnable = sc.Input[0];
  SCInputRef inputCsv = sc.Input[1];
  SCInputRef inputDrawLines = sc.Input[2];
  SCInputRef inputShowLegend = sc.Input[3];
  SCInputRef inputShowLabels = sc.Input[4];
  SCInputRef inputWallsColor = sc.Input[5];
  SCInputRef inputWallsWidth = sc.Input[6];
  SCInputRef inputWallsStyle = sc.Input[7];
  SCInputRef inputLargeColor = sc.Input[8];
  SCInputRef inputLargeWidth = sc.Input[9];
  SCInputRef inputLargeStyle = sc.Input[10];
  SCInputRef inputComboColor = sc.Input[11];
  SCInputRef inputComboWidth = sc.Input[12];
  SCInputRef inputComboStyle = sc.Input[13];
  SCInputRef inputLegendFont = sc.Input[14];

  if (sc.SetDefaults) {
    sc.GraphName = "SpotGamma Levels";
    sc.StudyDescription = "Parses SpotGamma CSV row and draws levels with legend.";
    sc.AutoLoop = 0;
    sc.UpdateAlways = 1;
    sc.FreeDLL = 1;
    sc.GraphRegion = 0;

    inputEnable.Name = "Enable Study";
    inputEnable.SetYesNo(1);

    inputCsv.Name = "SpotGamma CSV string";
    inputCsv.SetString("");

    inputDrawLines.Name = "Draw Lines";
    inputDrawLines.SetYesNo(1);

    inputShowLegend.Name = "Show Legend Top-Right";
    inputShowLegend.SetYesNo(1);

    inputShowLabels.Name = "Show Labels On Lines";
    inputShowLabels.SetYesNo(1);

    inputWallsColor.Name = "Walls/Triggers Color";
    inputWallsColor.SetColor(RGB(0, 128, 255));
    inputWallsWidth.Name = "Walls/Triggers Width";
    inputWallsWidth.SetInt(2);
    inputWallsStyle.Name = "Walls/Triggers Style";
    inputWallsStyle.SetInt(static_cast<int>(LINESTYLE_SOLID));

    inputLargeColor.Name = "Large Gamma Color";
    inputLargeColor.SetColor(RGB(255, 128, 0));
    inputLargeWidth.Name = "Large Gamma Width";
    inputLargeWidth.SetInt(2);
    inputLargeStyle.Name = "Large Gamma Style";
    inputLargeStyle.SetInt(static_cast<int>(LINESTYLE_SOLID));

    inputComboColor.Name = "Combo Levels Color";
    inputComboColor.SetColor(RGB(0, 200, 0));
    inputComboWidth.Name = "Combo Levels Width";
    inputComboWidth.SetInt(2);
    inputComboStyle.Name = "Combo Levels Style";
    inputComboStyle.SetInt(static_cast<int>(LINESTYLE_DASH));

    inputLegendFont.Name = "Legend Font Size";
    inputLegendFont.SetInt(12);

    return;
  }

  if (sc.LastCallToFunction) {
    for (const auto& info : kLevelInfos) {
      DeleteDrawing(sc, info.line);
    }
    DeleteDrawing(sc, LineNumber::kLegend);
    return;
  }

  if (!inputEnable.GetYesNo()) {
    for (const auto& info : kLevelInfos) {
      DeleteDrawing(sc, info.line);
    }
    DeleteDrawing(sc, LineNumber::kLegend);
    return;
  }

  const SCString csv_sc = inputCsv.GetString();
  const std::string csv = csv_sc.GetChars();
  if (csv.empty()) {
    for (const auto& info : kLevelInfos) {
      DeleteDrawing(sc, info.line);
    }
    DeleteDrawing(sc, LineNumber::kLegend);
    return;
  }

  const auto sections = sierra::core::parse_spotgamma_row(csv);
  const auto section = sierra::core::select_spotgamma_section(sections, sc.Symbol.GetChars());
  if (!section.has_value()) {
    for (const auto& info : kLevelInfos) {
      DeleteDrawing(sc, info.line);
    }
    DeleteDrawing(sc, LineNumber::kLegend);
    return;
  }

  const StyleOptions styleWalls = ReadStyleInputs(sc, 5, 6, 7);
  const StyleOptions styleLarge = ReadStyleInputs(sc, 8, 9, 10);
  const StyleOptions styleCombo = ReadStyleInputs(sc, 11, 12, 13);

  const auto pickStyle = [&](StyleGroup g) -> const StyleOptions& {
    switch (g) {
      case StyleGroup::Walls:
        return styleWalls;
      case StyleGroup::LargeGamma:
        return styleLarge;
      case StyleGroup::Combo:
      default:
        return styleCombo;
    }
  };

  const bool drawLines = inputDrawLines.GetYesNo();
  const bool showLabels = inputShowLabels.GetYesNo();

  for (const auto& info : kLevelInfos) {
    const auto valueOpt = section->levels.*(info.field);
    if (drawLines && valueOpt.has_value()) {
      DrawLevelLine(sc, info, *valueOpt, pickStyle(info.group), showLabels);
    } else {
      DeleteDrawing(sc, info.line);
    }
  }

  if (inputShowLegend.GetYesNo()) {
    const auto legend = BuildLegendText(sc, *section);
    DrawLegend(sc, legend, inputLegendFont.GetInt());
  } else {
    DeleteDrawing(sc, LineNumber::kLegend);
  }
}

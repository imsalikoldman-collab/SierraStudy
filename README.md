# SierraStudy

SierraStudy — каркас для разработки и горячей загрузки кастомных исследований (custom studies) Sierra Chart с жёстким разделением на ядро, обёртку и тесты.

## Архитектура компонентов
- **Core** (`projects/Core/src`, `projects/Core/include/sierra/core`) — статическая библиотека с бизнес-логикой без зависимостей от ACSIL:
  - расчёт скользящей средней (`moving_average.cpp`);
  - загрузка торговых планов из YAML через RapidYAML (`yaml_plan_loader.cpp`);
  - форматирование планов в табличный вид (`plan_formatter.cpp`);
  - модели зон, flip и структуры StudyPlan (`plan.hpp`).
- **Wrapper** (`projects/Wrapper/src`, `projects/Wrapper/include/sierra/acsil`) — динамическая библиотека-адаптер Sierra Chart ↔ Core:
  - `study.cpp` реализует `scsf_SierraStudyMovingAverage`, управляет `PlanWatcherState`, опрашивает YAML и строит таблицу;
  - `supportFunction.cpp` содержит вспомогательные функции для логирования, конвертации времени, отрисовки линий, зон и подписей.
- **Tests** (`projects/Tests/unit`) — консольное приложение Google Test, линкуется только с `Core.lib` и тестирует расчёт SMA, форматирование и парсер YAML, используя данные из `test_files/`.

## Поток выполнения
1. `SetDefaults` в `scsf_SierraStudyMovingAverage` объявляет Inputs (период SMA, директория/имя плана, интервал опроса, стили маркера) и настраивает Subgraph.
2. Во время основного цикла:
   - лениво инициализируется plog (`EnsureLogging`);
   - persistent-состояние извлекается через `AcquirePlanState`;
   - `UpdatePlanWatcher` проверяет, изменился ли YAML, и при необходимости загружает его (`LoadStudyPlanFromFile`), формирует текст (`FormatPlanAsTable`) и обновляет выбранный инструмент;
   - `RenderPlanTable` рисует текст плана, а `RenderPlanGraphics` делегирует прорисовку зон функции `RenderInstrumentPlanGraphics` из `supportFunction.cpp`;
   - собирается массив закрытий и вычисляется SMA (`sierra::core::moving_average`), результат записывается в `sc.Subgraph[0]`.
3. При `sc.LastCallToFunction` рисунки удаляются, persistent-память очищается (`ReleasePlanState`), обеспечивая чистый повторный запуск DLL.

## Инструменты и автоматизация
- **MSBuild**: общие настройки сохранены в `build/props/Directory.Build.props` (toolset v143, `stdcpp17`, include-пути до Core, plog, RapidYAML, использование `SIERRA_SDK_DIR`).
- **VS Code**: `.vscode/tasks.json` предоставляет задачи Build/Test/Hot-Swap.
- **PowerShell 7**:
  - `scripts/BuildAndSwap.ps1` выполняет `git submodule update`, сборку конфигураций, запуск тестов и горячую замену DLL;
  - `scripts/HotSwap.ps1` копирует DLL в `SIERRA_DATA_DIR`, при необходимости отправляя Sierra Chart UDP-команды RELEASE/ALLOW;
  - `scripts/Invoke-Build.ps1`, `scripts/Invoke-Tests.ps1`, `go.ps1` ускоряют цикл Debug → Test → Deploy.
- **Зависимости**: GoogleTest, plog и RapidYAML лежат в `third_party/` (подмодули/вендорные исходники). Обязательные переменные среды: `SIERRA_SDK_DIR` (путь к `ACS_Source`) и `SIERRA_DATA_DIR` (каталог `Data` для развёртывания DLL и тестовых файлов).

## Быстрый старт
1. Установить Visual Studio 2022 Build Tools (MSVC v143) и PowerShell 7, задать `SIERRA_SDK_DIR` и `SIERRA_DATA_DIR`.
2. Собрать solution: `msbuild SierraStudy.sln /m /p:Configuration=Debug /p:Platform=x64`.
3. Запустить тесты: `out\x64\Debug\SierraStudy.Tests.exe`.
4. Выполнить горячую замену DLL: `pwsh -File scripts/HotSwap.ps1 -Dll out\x64\Debug\SierraStudy.dll -SierraDataDir $env:SIERRA_DATA_DIR` или использовать `scripts/BuildAndSwap.ps1`.
5. В Sierra Chart выбрать `Analysis → Add Custom Study` и подключить `SierraStudy - Moving Average`.

## Нюансы и рекомендации
- Документация функций в C++ оформляется Doxygen-комментариями на русском языке (см. `AGENTS.md`).
- Persistent-данные храните через `sc.Get/SetPersistent*`; глобальные статические переменные не используйте.
- Интервал опроса YAML регулируется Input'ом `Plan Poll Interval`, по умолчанию 15 секунд.
- Если в консоли появляются «кракозябры», убедитесь, что файлы сохранены в UTF-8 и консоль настроена на Unicode.
- Дополнительные инструкции и материалы — в `AGENTS.md`, `external/README.md`, `docs/SierraACSILQuickTips.md`.

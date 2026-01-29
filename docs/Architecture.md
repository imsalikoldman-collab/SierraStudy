# Архитектура SierraStudy

Документ описывает ключевые компоненты, зависимости и рабочие процессы проекта SierraStudy. Фокус сделан на разграничении ответственности между проектами Core/Wrapper/Tests, обработке данных торгового плана и автоматизации сборки.

## Общий обзор

| Слой      | Проект        | Тип бинаря | Основная задача                                           |
|-----------|---------------|------------|-----------------------------------------------------------|
| Бизнес-логика | `projects/Core`   | Static Library (`.lib`) | Алгоритмы без зависимостей от ACSIL/Sierra Chart.         |
| Адаптер ACSIL | `projects/Wrapper` | Dynamic Library (`.dll`) | Коммуникация с Sierra Chart, state management, рендеринг. |
| Тесты         | `projects/Tests`   | Console (`.exe`)        | Проверка Core через Google Test.                          |

Проекты собираются в рамках `SierraStudy.sln` и используют общие настройки из `build/props/Directory.Build.props`.

## Core

### Структура
- `include/sierra/core/*.hpp` — публичные заголовки.
- `src/*.cpp` — реализации.

### Основные модули
- `moving_average.{hpp,cpp}` — O(n)-реализация скользящей средней с возвращением `NaN`, пока недостаточно данных.
- `plan.hpp` — структуры `PriceRange`, `Zone`, `FlipZone`, `InstrumentPlan`, `StudyPlan`, `PlanLoadResult`.
- `yaml_plan_loader.{hpp,cpp}` — загрузка YAML планов (формат v1.5-min-obj) с использованием RapidYAML (ryml).
- `plan_formatter.{hpp,cpp}` — преобразование `StudyPlan` в строковую таблицу с локализацией даты/времени под America/New_York.

### Ограничения
- Нет зависимостей от ACSIL, WinAPI или любых Sierra-специфичных типов.
- Вся логика должна быть покрыта модульными тестами в проекте Tests.

## Wrapper

### Структура
- `include/sierra/acsil/*.hpp` — публичные заголовки для ACSIL-окружения.
- `src/*.cpp` — реализация ACSIL-адаптера и вспомогательных функций.

### Ключевые элементы
- `study.cpp`:
  - экспортируемая функция `scsf_SierraStudyMovingAverage` (SCSFExport);
  - класс `PlanWatcherState` для хранения persistent-состояния (последний план, расписание опроса, line numbers);
  - `UpdatePlanWatcher` — опрос файловой системы, загрузка YAML, логирование статуса;
  - `RenderPlanTable` — размещение текста плана через `DRAWING_STATIONARY_TEXT`;
  - `RenderPlanGraphics` — делегирование отрисовки зон в поддержку.
- `supportFunction.cpp`:
  - `LogDllStartup`, `EnsureLogging` — интеграция с plog и Sierra Chart Message Log;
  - `ConvertIso8601ToSCDateTime` — преобразование ISO-8601 времени в `SCDateTime` с учётом DST;
  - `RenderStandaloneDebugLine` — вспомогательная линия для отладки;
  - `RenderInstrumentPlanGraphics` — построение зон, подписей SL/TP/Flip и маркеров `generated_at`.

### Особенности взаимодействия
- Persistent-данные хранятся через `sc.Get/SetPersistent*` (индексы `kPersistLogging`, `kPersistPlanState`, `kPersistDebugLine`).
- Логирование условно: в наличии plog (`third_party/plog`) создаётся `Logs/SierraStudy.log`, иначе используются только сообщения Sierra Chart.
- Отрисовка зон использует `UseTool` с регистрацией линий/прямоугольников, сохранением `LineNumber` в `PlanWatcherState` и их очисткой при обновлении/выходе.

## Tests

### Структура
- `unit/*.cpp` — тесты Google Test.
- `data/` — статические YAML-файлы с валидными/невалидными примерами.

### Покрытие
- `test_moving_average.cpp` — проверка NaN до накопления периода и выброса `std::invalid_argument` при `period == 0`.
- `test_plan_formatter.cpp` — корректность форматирования таблицы (даты, времени, зон).
- `test_yaml_plan_loader.cpp` — позитивный сценарий, отсутствие файла, ошибки структуры.
- `test_yaml_parser.cpp` — smoke-тесты RapidYAML (ryml) и вспомогательные проверки парсинга.

### Правила
- Тесты линкуются только с `Core.lib`.
- Для добавления новой публичной функции в Core необходимо подготовить соответствующий тест.

## Поток выполнения на графике

1. **SetDefaults** (`study.cpp`):
   - регистрируются Inputs (`Period`, `Plan Directory`, `Plan File Name`, `Plan Poll Interval`, настройки маркера);
   - настраивается `Subgraph[0]` под SMA.
2. **Основной цикл**:
   - инициализация plog (однократно на `sc.Index == 0`);
   - загрузка/создание `PlanWatcherState` через `AcquirePlanState`;
   - `UpdatePlanWatcher`:
     - нормализация путей, проверка интервала опроса;
     - чтение `last_write_time`, перезапуск состояния после смены пути/имени файла;
     - загрузка `StudyPlan` при обновлении;
     - подготовка текста через `FormatPlanAsTable`;
     - вычисление `SCDateTime` для `generated_at`.
   - `RenderPlanTable` и `RenderPlanGraphics` отображают текст и зоны на графике.
   - SMA считается из массива `Close` и записывается в `Subgraph[0][sc.Index]`.
3. **LastCallToFunction**:
   - удаление кастомных рисунков (`ClearPlanDrawings`);
   - сброс persistent-ссылок (`ReleasePlanState`);
   - очистка отладочной линии.

## Автоматизация

### Сборка и тесты
- `msbuild SierraStudy.sln` — сборка всех проектов; задачи VS Code (`.vscode/tasks.json`) запускают MSBuild и тесты.
- `scripts/Invoke-Build.ps1` — обёртка над MSBuild с логированием в UTF-8.
- `scripts/Invoke-Tests.ps1` — запуск Google Test с опциональным фильтром.

### Горячая замена DLL
- `scripts/HotSwap.ps1`:
  - простое копирование или режим с UDP-командами (`RELEASE_DLL--`, `ALLOW_LOAD_DLL--`);
  - опциональный auto-fallback при sharing violation.
- `scripts/BuildAndSwap.ps1`:
  - обновление подмодулей;
  - сборка основной/горячей конфигураций;
  - копирование файлов плана в `$SIERRA_DATA_DIR\test_files`;
  - запуск тестов (если не отключены);
  - вызов `HotSwap.ps1`.
- `go.ps1` — упрощённый сценарий Debug → Tests → Hot-Swap.

## Внешние зависимости

- **libcurl** — сетевые вызовы (HTTPS); ставится через vcpkg в статически линкованном варианте (`x64-windows-static`, Schannel, http2).
- **RapidYAML (ryml)** — парсинг YAML; ставится через vcpkg (manifest).
- **c4core** — зависимость RapidYAML (транзитивно через vcpkg).
- **Google Test** — через vcpkg; используется только в проекте Tests.
- **plog** — header-only логгер через vcpkg.

Управление зависимостями выполняется через manifest `vcpkg.json`; подмодули Git не используются.

### Режим работы vcpkg
- В репозитории принят **manifest‑режим**: зависимости описаны в `vcpkg.json`, триплет по умолчанию `x64-windows-static`, локальный кэш пакетов лежит в `vcpkg_installed/` (под git‑ignore).
- MSBuild получает настройки через `build/props/Directory.Build.props` (импорт `vcpkg.props`, `VcpkgEnableManifest=true`, `VcpkgRoot` по умолчанию `C:\dev\vcpkg`).
- Классический режим (global install) не используется в проекте; единственная команда локально — `vcpkg.exe install --triplet x64-windows-static`, которая читает текущий manifest.

### Сборка
- Инструмент: `MSBuild` (VS 2022, toolset v143), общие параметры заданы в `build/props/Directory.Build.props` (`stdcpp17`, `/W4`, статический CRT для Release/Debug, include Core/Wrapper + `SIERRA_SDK_DIR`, импорт vcpkg manifest).
- Конфигурации: `Debug` и `Release`, платформа `x64`.
- Вывод артефактов: `out/x64/<Config>/` — `SierraStudy.dll` (Wrapper), `SierraStudy.Core.lib` (Core), `SierraStudy.Tests.exe` (Tests).
- Запуск задач из VS Code: `.vscode/tasks.json` (`Build`, `Test`, `Hot-Swap`) — передают msbuild путь и триплет через переменные окружения, используют manifest vcpkg.
- Ручные команды:
  - Сборка Debug: `msbuild SierraStudy.sln /m /p:Configuration=Debug /p:Platform=x64`
  - Сборка Release: `msbuild SierraStudy.sln /m /p:Configuration=Release /p:Platform=x64 /p:VcpkgRoot=C:\dev\vcpkg /p:VcpkgTriplet=x64-windows-static /p:VcpkgInstalledDir=C:\dev\vcpkg\installed-manifest /p:VcpkgEnableManifest=true`
- Тесты: `out\x64\<Config>\SierraStudy.Tests.exe` (линкуется только с `Core.lib`, зависимости подтягивает vcpkg).
- Кратко о «виде сборки»: MSBuild + manifest vcpkg, C++17, /W4, статический CRT, x64, конфигурации Debug/Release, артефакты в `out/x64/<Config>/`.

## Конфигурация среды

- `SIERRA_SDK_DIR` — путь к установленному Sierra Chart (`...\ACS_Source`).
- `SIERRA_DATA_DIR` — каталог `Data`, куда копируется DLL и тестовые планы.
- `PATH` должен содержать MSBuild (`Visual Studio 2022 Build Tools`) и PowerShell 7 (`pwsh`).

## Практические рекомендации

- Соблюдайте разграничение ответственности: Core не должен зависеть от Wrapper.
- Все новые публичные API Core сопровождайте тестами (Tests) и обновлением Wrapper при необходимости.
- Документируйте код Doxygen-комментариями на русском языке (`@brief`, `@param`, `@return`, `@note`, `@warning`).
- Используйте `sc.Get/SetPersistent*` для хранения состояния, освобождайте ресурсы в `LastCallToFunction`.
- Минимизируйте логирование в горячих циклах; plog включайте только при реальной необходимости.
- Перед горячей заменой DLL убедитесь, что Sierra Chart освободила файл (`Analysis → Build → Release All DLLs and Deny Load`) или используйте UDP-режим.

## Связанные материалы

- `README.md` — краткий обзор и инструкции по запуску.
- `AGENTS.md` — подробное руководство по процессам разработки.
- `docs/SierraACSILQuickTips.md` — подсказки по `SCDateTime`, `UseTool` и приёмам отладки в Sierra Chart.

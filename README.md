# SierraStudy

SierraStudy — каркас для разработки и горячей загрузки кастомных исследований (custom studies) Sierra Chart с жёстким разделением на ядро, обёртку и тесты.

## Архитектура компонентов
- **Core** (`projects/Core/src`, `projects/Core/include/sierra/core`) — статическая библиотека с бизнес-логикой без зависимостей от ACSIL:
  - расчёт скользящей средней (`moving_average.cpp`);
  - загрузка торговых планов из YAML через RapidYAML (`yaml_plan_loader.cpp`);
  - форматирование планов в табличный вид (`plan_formatter.cpp`);
  - модели зон, flip и структуры StudyPlan (`plan.hpp`).
- **Wrapper** (`projects/Wrapper/src`, `projects/Wrapper/include/sierra/acsil`) — динамическая библиотека-адаптер Sierra Chart ↔ Core:
  - `study.cpp` — опросчик GexBot: по символу графика маппит тикер (ES/MES→ES_SPX, NQ/MNQ→NQ_NDX), выполняет GET `https://api.gexbot.com/{TICKER}/state/{GREEK}?key=...` через libcurl, парсит JSON ответ RapidYAML и выводит текстом на графике;
  - панель уровней specified_greek: в режиме *Positive Only* рисует горизонтальные линии на strike, длина пропорциональна значению greek (нормализация по максимуму в видимой шкале), правый край выровнен по правой части графика; режим *All Values (TBD)* подключён как безопасная заглушка;
  - Inputs: `GexBot API Key` (по умолчанию IZiEb6yDrgxE), `Greek` (delta_zero, gamma_zero, delta_one, gamma_one, charm_zero, vanna_zero, charm_one, vanna_one; дефолт **gamma_zero**), `Poll Interval (seconds)` (10–100, по умолчанию 30);
  - `supportFunction.cpp` содержит вспомогательные функции для логирования, конвертации времени, отрисовки линий, зон и подписей (в текущем цикле не вызываются).
- **Tests** (`projects/Tests/unit`) — консольное приложение Google Test (из vcpkg), линкуется только с `Core.lib` и тестирует расчёт SMA, форматирование и парсер YAML, используя данные из `test_files/`.
- **Сетевые зависимости** — libcurl подтягивается через vcpkg (manifest), триплет по умолчанию `x64-windows-static`, корень ожидается в `C:\dev\vcpkg`.

## Поток выполнения
1. `SetDefaults` задаёт имя `SierraStudy - GexBot Poller`, скрытый Subgraph и Inputs для API ключа, греческой буквы и периода опроса.
2. В рабочем цикле persistent-структура хранит время последнего запроса и последний текст. Каждые 10–100 секунд (по Input) выполняется GET к GexBot, ошибки логируются в Message Log.
3. Ответ форматируется и выводится на график как `DRAWING_STATIONARY_TEXT` (обновление через `UseTool`).
4. `LastCallToFunction` освобождает persistent‑память.

## Инструменты и автоматизация
- **MSBuild**: общие настройки сохранены в `build/props/Directory.Build.props` (toolset v143, `stdcpp17`, include-пути до Core/Wrapper и `SIERRA_SDK_DIR`, подключение vcpkg manifest).
  - Предупреждения из внешнего Sierra SDK отключены (`ExternalWarningLevel=TurnOffAllWarnings` → `/external:W0`, `ExternalDiagnostics=false`); свой код остаётся на `/W4`.
- **VS Code**: `.vscode/tasks.json` предоставляет задачи Build/Test/Hot-Swap.
- **PowerShell 7**:
  - `scripts/BuildAndSwap.ps1` выполняет сборку конфигураций, запуск тестов и горячую замену DLL (зависимости берёт из vcpkg manifest);
  - `scripts/HotSwap.ps1` копирует DLL в `SIERRA_DATA_DIR`, при необходимости отправляя Sierra Chart UDP-команды RELEASE/ALLOW;
  - `scripts/Invoke-Build.ps1`, `scripts/Invoke-Tests.ps1`, `go.ps1` ускоряют цикл Debug → Test → Deploy.
- **Формат ответа GexBot**: см. `docs/GexBotResponseFormat.md` (список полей и структура `mini_contracts`).
- **Зависимости**: GoogleTest, plog, RapidYAML (ryml) и libcurl ставятся через vcpkg manifest (`vcpkg.json`, фича http2, schannel по умолчанию). Обязательные переменные среды: `SIERRA_SDK_DIR` (путь к `ACS_Source`), `SIERRA_DATA_DIR` (каталог `Data`), `VCPKG_ROOT` (по умолчанию `C:\dev\vcpkg`), при необходимости `VCPKG_DEFAULT_TRIPLET` (`x64-windows-static`).

## Быстрый старт
1. Установить Visual Studio 2022 Build Tools (MSVC v143) и PowerShell 7, задать `SIERRA_SDK_DIR` и `SIERRA_DATA_DIR`.
2. Собрать solution: `msbuild SierraStudy.sln /m /p:Configuration=Debug /p:Platform=x64`.
3. Запустить тесты: `out\x64\Debug\SierraStudy.Tests.exe`.
4. Выполнить горячую замену DLL: `pwsh -File scripts/HotSwap.ps1 -Dll out\x64\Debug\SierraStudy_GexBot.dll -SierraDataDir $env:SIERRA_DATA_DIR` или использовать `scripts/BuildAndSwap.ps1`.
5. В Sierra Chart выбрать `Analysis → Add Custom Study` и подключить `SierraStudy - GexBot Poller`.
6. Для обновления зависимостей через vcpkg: `C:\dev\vcpkg\vcpkg.exe install --triplet x64-windows-static` (читает `vcpkg.json`).
7. Сборка Release (с теми же зависимостями):  
   `msbuild SierraStudy.sln /m /p:Configuration=Release /p:Platform=x64 /p:VcpkgRoot=C:\dev\vcpkg /p:VcpkgTriplet=x64-windows-static /p:VcpkgInstalledDir=C:\dev\vcpkg\installed-manifest /p:VcpkgEnableManifest=true`

## Нюансы и рекомендации
- Документация функций в C++ оформляется Doxygen-комментариями на русском языке (см. `AGENTS.md`).
- Persistent-данные храните через `sc.Get/SetPersistent*`; глобальные статические переменные не используйте.
- Интервал опроса GexBot настраивается Input'ом `Poll Interval (seconds)` (10–100, по умолчанию 30).
- Если в консоли появляются «кракозябры», убедитесь, что файлы сохранены в UTF-8 и консоль настроена на Unicode.
- Дополнительные инструкции и материалы — в `AGENTS.md`, `external/README.md`, `docs/SierraACSILQuickTips.md`.

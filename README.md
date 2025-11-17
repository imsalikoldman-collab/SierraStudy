# SierraStudy

SierraStudy — шаблон репозитория для пользовательского исследования (custom study) Sierra Chart с разделением на независимые модули.

- `Core` — статическая библиотека (.lib) с бизнес-логикой, без зависимостей от ACSIL.
- `Wrapper` — динамическая библиотека (.dll), адаптер ACSIL ⇄ Core.
- `Tests` — консольное приложение с Google Test, линковка только с `Core.lib`.
- `Advisor` — советник MetaTrader 5 (MQL5).
- `AdvisorBridge` — WinAPI DLL для работы с именованными каналами и вызовов из MQL5.

## Предварительные требования
- Visual Studio 2022 Build Tools (MSVC v143) и MSBuild.
- PowerShell 7 (`pwsh`) и доступ к MSBuild в `PATH` или через VS Developer Prompt.
- Установленная Sierra Chart с исходниками ACSIL.
- Переменные окружения:
  - `SIERRA_SDK_DIR` — путь к каталогу `ACS_Source` из установки Sierra Chart.
  - `SIERRA_DATA_DIR` — путь к каталогу `Data`, куда будет копироваться DLL.

## Структура каталога
- `projects/` — проекты Visual C++ и MQL5: `Core`, `Wrapper`, `Tests`, `Advisor`, `AdvisorBridge`.
- `build/` — общие props/targets для MSBuild.
- `third_party/` — зависимости (Google Test, plog).
- `scripts/` — PowerShell-скрипты для сборки и горячей замены DLL.
- `examples/` — примеры использования ядра и обёртки.

## Базовый цикл
1. Сборка: `msbuild SierraStudy.sln /p:Configuration=Debug /p:Platform=x64`.
2. Запуск тестов: `out\x64\Debug\SierraStudy.Tests.exe`.
3. Копирование DLL: `scripts\HotSwap.ps1`.
4. Проверка в Sierra Chart и коммит.

## Зависимости
- **Google Test** — находится в `third_party/googletest` (подмодуль или ручная копия).
- **plog** — header-only логгер в `third_party/plog`.

Дополнительные детали см. в `AGENTS.md` и `external/README.md`.

## MetaTrader 5 Advisor
- Проект `Advisor` содержит шаблон советника `projects/Advisor/src/SierraStudyAdvisor.mq5`, который через `#import` вызывает DLL `SierraStudyAdvisorBridge.dll`.
- `AdvisorBridge` (см. `projects/AdvisorBridge`) реализует WinAPI-слой: `SierraPipeConnect/Read/Write/Close`.
- Сборка выполняется скриптом `scripts/CompileAdvisor.ps1`, который вызывает `MetaEditor.exe`, складывает `.ex5` и Bridge DLL в `out/mt5/Experts`, а также копирует их в `MQL5\Experts\SierraStudy` и `MQL5\Libraries`.
- Необходимы переменные окружения:
  - `MT5_DATA_DIR` — каталог данных MetaTrader 5 (содержит `MQL5\`).
  - `METAEDITOR_EXE` (опц.) — путь к `MetaEditor.exe`, иначе используются стандартные пути (включая `C:\Program Files\Tickmill MT5 Terminal`).
- Перед запуском скрипта убедитесь, что `AdvisorBridge` собран (например, `msbuild projects/AdvisorBridge/SierraStudy.AdvisorBridge.vcxproj /p:Configuration=Release /p:Platform=x64`). Все настройки (имя PIPE, интервал, автоподключение) задаются через входные параметры советника.

# SierraStudy

SierraStudy — шаблон репозитория для пользовательского исследования (custom study) Sierra Chart с разделением на три проекта.

- `Core` — статическая библиотека (.lib) с бизнес-логикой, без зависимостей от ACSIL.
- `Wrapper` — динамическая библиотека (.dll), адаптер ACSIL ⇄ Core.
- `Tests` — консольное приложение с Google Test, линковка только с `Core.lib`.

## Предварительные требования
- Visual Studio 2022 Build Tools (MSVC v143) и MSBuild.
- PowerShell 7 (`pwsh`) и доступ к MSBuild в `PATH` или через VS Developer Prompt.
- Установленная Sierra Chart с исходниками ACSIL.
- Переменные окружения:
  - `SIERRA_SDK_DIR` — путь к каталогу `ACS_Source` из установки Sierra Chart.
  - `SIERRA_DATA_DIR` — путь к каталогу `Data`, куда будет копироваться DLL.

## Структура каталога
- `projects/` — проекты Visual C++: `Core`, `Wrapper`, `Tests`.
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
- **RapidYAML (ryml)** - git submodule - `third_party/rapidyaml` ��� ������������ YAML � Core/Wrapper.

Дополнительные детали см. в `AGENTS.md` и `external/README.md`.

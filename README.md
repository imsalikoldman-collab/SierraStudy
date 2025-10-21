# SierraStudy

SierraStudy — это шаблонный проект для создания пользовательских исследований (custom study) в Sierra Chart с чётким разделением бизнес‑логики и ACSIL-обёртки. Решение включает:
- `Core` — статическая библиотека (.lib) с платформо-независимыми расчётами.
- `Wrapper` — динамическая библиотека (.dll), адаптирующая ввод/вывод ACSIL к ядру.
- `Tests` — консольное приложение на Google Test для проверки кода из Core.

## Предварительные требования
- Visual Studio 2022 Build Tools (MSVC v143) и MSBuild.
- PowerShell 7 (`pwsh`) в переменной окружения `PATH`.
- Установленная Sierra Chart с заголовками ACSIL.
- Переменные окружения:
  - `SIERRA_SDK_DIR` — путь к каталогу `ACS_Source` вашей установки Sierra Chart.
  - `SIERRA_DATA_DIR` — путь к каталогу `Data`, где следует размещать DLL.

## Структура репозитория
Подробности смотрите в `AGENTS.md`. Основные директории:
- `projects/` — проекты Visual C++ для `Core`, `Wrapper`, `Tests`.
- `build/` — общие настройки MSBuild (`Directory.Build.props`, дополнительные таргеты).
- `third_party/` — сторонние зависимости (Google Test, plog).
- `scripts/` — скрипты PowerShell для сборки, тестирования и горячей замены DLL.
- `examples/` — примеры использования ядра и ACSIL-обёртки.

## Типовой цикл работы
1. Сборка решения: `msbuild SierraStudy.sln /p:Configuration=Debug /p:Platform=x64`.
2. Запуск тестов: `out\x64\Debug\SierraStudy.Tests.exe`.
3. Горячая замена DLL: `scripts\HotSwap.ps1`.
4. Перезапуск/перечитывание исследования в Sierra Chart.

## Сторонние библиотеки
- **Google Test** — разместите исходники в `third_party/googletest` (например, через git submodule или `git clone`) перед сборкой тестов.
- **plog** — заголовочный логгер в `third_party/plog`.

Информацию о локальной конфигурации SDK и других внешних ресурсах смотрите в `external/README.md`.

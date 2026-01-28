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

## Реализованные study
- **SierraStudyMovingAverage** — пример обёртки SMA (ядро `moving_average` в Core).
- **GexbotGammaLevels (State)** — HTTP-запрос к `api.gexbot.com/{ticker}/state/{gamma_*}` (только `gamma_zero|gamma_one`), фильтрация положительной гаммы, отрисовка уровней и подписи справа на графике. Поддерживаемые символы графика: ES/MES → `ES_SPX`, NQ/MNQ → `NQ_NDX` (других тикеров сервер не принимает). Требуется Input `API Key`. В левом нижнем углу отображается статус запросов: красный «нет запроса», жёлтый «нет ответа», зелёный «ОК». В режиме Debug в лог выводится собранный URL (ключ маскируется) и первые 50 символов ответа.

## Базовый цикл
1. Сборка: `msbuild SierraStudy.sln /p:Configuration=Debug /p:Platform=x64`.
2. Запуск тестов: `out\x64\Debug\SierraStudy.Tests.exe`.
3. Копирование DLL: `scripts\HotSwap.ps1`.
4. Проверка в Sierra Chart и коммит.

## Зависимости
- **Google Test** — находится в `third_party/googletest` (подмодуль или ручная копия).
- **plog** — header-only логгер в `third_party/plog`.

Дополнительные детали см. в `AGENTS.md` и `external/README.md`.

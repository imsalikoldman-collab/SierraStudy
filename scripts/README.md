# PowerShell Scripts Overview

| Script | Назначение | Основные параметры |
|--------|------------|-------------------|
| `Resolve-Msbuild.ps1` | Находит `MSBuild.exe`: использует опциональный `msbuild.path.ps1`, переменную `MSBUILD_EXE`, `vswhere`, затем типовые пути. Возвращает полный путь либо выбрасывает исключение с подсказкой. | `-ThrowIfNotFound` — заставляет выбрасывать исключение вместо `null`. |
| `Invoke-Build.ps1` | Обёртка над MSBuild. Собирает указанное решение/проект, записывает лог в `build/logs/msbuild-<Configuration>.log` (UTF-8). | `-Solution` (обяз.), `-Configuration` (`Debug` по умолчанию), `-Platform` (`x64`), `-Log` — кастомный путь к логу. |
| `Invoke-Tests.ps1` | Запускает тестовый exe Google Test, прокидывает фильтр и дополнительные аргументы, падает при красных тестах. | `-Executable` (обяз.), `-Filter`, `-AdditionalArgs`. |
| `HotSwap.ps1` | Копирует собранную DLL (`SierraStudy.dll`) в `SIERRA_DATA_DIR`. При ошибке напоминает выполнить в Sierra Chart «Release All DLLs and Deny Load». | `-Dll` (обяз.), `-SierraDataDir` (обяз.), `-TargetName` — имя DLL в целевой папке. |
| `BuildAndSwap.ps1` | Оркестратор «build → test → hot-swap». Вызывает обёртки выше, поддерживает пропуск тестов/копирования, GTest-фильтр и выбор конфигурации для горячей замены. | `-Configuration` (`Debug`/`Release`), `-HotSwapConfiguration` (по умолчанию `Release`), `-Platform`, `-SkipTests`, `-NoHotSwap`, `-TestFilter`. |
| `Invoke-All.ps1` | Локальный / CI сценарий: последовательно запускает `BuildAndSwap.ps1` для `Debug` и `Release`, собирая логи. | `-SkipHotSwap`, `-SkipTests`, `-TestFilter`. |

## Настройки
- Для ручного указания пути к MSBuild создайте `scripts/msbuild.path.ps1` с переменной `$MsbuildPath = 'C:\Full\Path\To\MSBuild.exe'`.
- Переменные окружения: `SIERRA_SDK_DIR` (например `C:\2308`) и `SIERRA_DATA_DIR` (например `C:\2308\Data`) должны быть заданы перед запуском hot-swap.

## Типовые сценарии
```powershell
# Сборка и тесты Debug без копирования DLL
pwsh -File scripts\BuildAndSwap.ps1 -Configuration Debug -NoHotSwap

# Полный цикл Debug -> Release (без hot-swap)
pwsh -File scripts\Invoke-All.ps1 -SkipHotSwap

# Запуск только тестов с фильтром
pwsh -File scripts\Invoke-Tests.ps1 -Executable out\x64\Debug\SierraStudy.Tests.exe -Filter "MovingAverageTest.*"

# Горячая замена релизной DLL в Sierra Chart
pwsh -File scripts\BuildAndSwap.ps1 -Configuration Release
```

## Что сделать в Sierra Chart после копирования
1. В меню `Analysis → Build → Release All DLLs and Deny Load`, чтобы разблокировать предыдущую версию.
2. `Analysis → Add Custom Study → Add Custom Study → Browse` и выберите `C:\2308\Data\SierraStudy.dll`. После этого в списке появится `SierraStudy - Moving Average`.

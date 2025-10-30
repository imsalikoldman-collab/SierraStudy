# PowerShell Scripts Overview

| Script | Назначение | Основные параметры |
|--------|------------|-------------------|
| `Resolve-Msbuild.ps1` | Находит `MSBuild.exe`: проверяет `msbuild.path.ps1`, переменную `MSBUILD_EXE`, `vswhere`, затем типовые пути. Возвращает путь или бросает исключение. | `-ThrowIfNotFound` — требовать ошибку при отсутствии MSBuild. |
| `Invoke-Build.ps1` | Обёртка над MSBuild. Собирает выбранный проект/solution и пишет лог в `build/logs/msbuild-<Configuration>.log` (UTF-8). | `-Solution`, `-Configuration`, `-Platform`, `-Log`. |
| `Invoke-Tests.ps1` | Запускает тестовый exe Google Test, прокидывает фильтр и дополнительные аргументы, падает при красных тестах. | `-Executable`, `-Filter`, `-AdditionalArgs`. |
| `HotSwap.ps1` | Горячая замена DLL (`SierraStudy.dll`) в `SIERRA_DATA_DIR`. Поддерживает локальное копирование и удалённый сценарий через UDP-команды Release/Allow. | `-Dll`, `-SierraDataDir`, `-TargetName`, `-UseRemoteRelease`, `-SierraHost`, `-SierraPort`, `-ReleaseCommandFormat`, `-AllowCommandFormat`, `-WaitTimeoutSeconds`, `-WaitIntervalMilliseconds`. |
| ` `BuildAndSwap.ps1` ` | Оркестратор «build → test → hot-swap». Управляет сборкой, тестированием и локальным/удалённым развёртыванием DLL. | `-Configuration`, `-HotSwapConfiguration`, `-Platform`, `-SkipTests`, `-NoHotSwap`, `-TestFilter`, `-RemoteHotSwap`, ` `-DisableRemoteFallback` `, `-SierraHost`, `-SierraPort`, `-ReleaseCommandFormat`, `-AllowCommandFormat`, `-WaitTimeoutSeconds`, `-WaitIntervalMilliseconds`. |
| `Invoke-All.ps1` | Комплексный прогон для CI/локальной проверки: собирает Debug и Release подряд, запускает тесты, при необходимости пропускает hot-swap. | `-SkipHotSwap`, `-SkipTests`, `-TestFilter`. |

## Настройки
- Для ручного указания пути к MSBuild создайте `scripts/msbuild.path.ps1` с переменной `$MsbuildPath = 'C:\Full\Path\To\MSBuild.exe'`.
- Переменные окружения: `SIERRA_SDK_DIR` (например `C:\2308`) и `SIERRA_DATA_DIR` (например `C:\2308\Data`) должны быть заданы до запуска сценариев.

## Примеры использования
```powershell
# Сборка и тесты Debug без копирования DLL
pwsh -File scripts\ `BuildAndSwap.ps1`  -Configuration Debug -NoHotSwap

# Полный цикл Debug -> Release (без hot-swap)
pwsh -File scripts\Invoke-All.ps1 -SkipHotSwap

# Запуск только тестов с фильтром
pwsh -File scripts\Invoke-Tests.ps1 -Executable out\x64\Debug\SierraStudy.Tests.exe -Filter "MovingAverageTest.*"

# Горячая замена Release-DLL (локально)
pwsh -File scripts\ `BuildAndSwap.ps1`  -Configuration Release

# Удалённая горячая замена через UDP (через порт 11099) (Sierra Chart должна слушать порт 11099)
pwsh -File scripts\ `BuildAndSwap.ps1`  -Configuration Release -RemoteHotSwap -SierraHost 127.0.0.1 -SierraPort 11099
```

## Действия в Sierra Chart после обновления DLL
1. Меню `Analysis → Build → Release All DLLs and Deny Load`, чтобы гарантированно отпустить файл.
2. `Analysis → Add Custom Study → Add Custom Study → Browse` и выберите `C:\2308\Data\SierraStudy.dll`. После этого исследование появится как `SierraStudy - Moving Average`.

### Примечания
- По умолчанию  `BuildAndSwap.ps1`  при обнаружении блокировки файла пытается выполнить удалённый сценарий Release/Allow. Отключить поведение можно ключом  `-DisableRemoteFallback` .






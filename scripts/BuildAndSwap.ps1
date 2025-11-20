<#
@brief Полный цикл сборки, тестирования и (опционально) горячей замены DLL.
@param Configuration Конфигурация сборки для тестов.
@param HotSwapConfiguration Конфигурация, из которой берём DLL для горячей замены (обычно Release).
@param SkipTests Пропустить запуск юнит-тестов.
@param NoHotSwap Пропустить копирование DLL.
@param Platform Целевая платформа (x64).
@param TestFilter Фильтр Google Test (опционально).
@param RemoteHotSwap Включить удалённый режим (послать UDP-команды RELEASE/ALLOW).
@param DisableRemoteFallback Отключить автоматический переход в удалённый режим при блокировке файла.
@param SierraHost Хост Sierra Chart для удалённых команд.
@param SierraPort Порт UDP Sierra Chart.
@param ReleaseCommandFormat Формат строки команды для освобождения DLL.
@param AllowCommandFormat Формат строки команды для разрешения загрузки DLL.
@param WaitTimeoutSeconds Таймаут ожидания освобождения файла.
@param WaitIntervalMilliseconds Интервал проверки освобождения.
@return Ничего. При ошибках сборки/тестов/копирования будет выброшено исключение.
@note Скрипт вызывает вспомогательные Invoke-Build, Invoke-Tests и HotSwap. По умолчанию при блокировке файла активируется удалённый сценарий Release/Allow (отключается ключом -DisableRemoteFallback).
@warning Убедитесь, что переменная окружения SIERRA_DATA_DIR установлена перед запуском горячей замены.
#>
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',

  [ValidateSet('Debug', 'Release')]
  [string]$HotSwapConfiguration = 'Release',

  [switch]$SkipTests,

  [switch]$NoHotSwap,
  [switch]$BuildAdvisor,

  [string]$Platform = 'x64',

  [string]$TestFilter,

  [switch]$RemoteHotSwap,

  [switch]$DisableRemoteFallback,

  [string]$SierraHost = "127.0.0.1",

  [int]$SierraPort = 11099,

  [string]$ReleaseCommandFormat = "RELEASE_DLL--{0}",

  [string]$AllowCommandFormat = "ALLOW_LOAD_DLL--{0}",

  [int]$WaitTimeoutSeconds = 20,

  [int]$WaitIntervalMilliseconds = 250
)

$ErrorActionPreference = 'Stop'

$solution = Join-Path $PSScriptRoot '..\SierraStudy.sln'
$invokeBuild = Join-Path $PSScriptRoot 'Invoke-Build.ps1'
if (-not (Test-Path -LiteralPath $invokeBuild)) {
  throw "Invoke-Build script not found: $invokeBuild"
}

# Build configuration for tests first.
& $invokeBuild -Solution $solution -Configuration $Configuration -Platform $Platform

# If hot swap configuration differs, ensure release binaries are built as well.
if (-not $NoHotSwap -and $HotSwapConfiguration -ne $Configuration) {
  & $invokeBuild -Solution $solution -Configuration $HotSwapConfiguration -Platform $Platform
}

$testOutputDir = Join-Path $PSScriptRoot "..\out\$Platform\$Configuration"
$testExe = Join-Path $testOutputDir 'SierraStudy.Tests.exe'
$hotSwapDir = Join-Path $PSScriptRoot "..\out\$Platform\$HotSwapConfiguration"
$wrapperDll = Join-Path $hotSwapDir 'SierraStudyMT5.dll'

if (-not $SkipTests) {
  $invokeTests = Join-Path $PSScriptRoot 'Invoke-Tests.ps1'
  if (-not (Test-Path -LiteralPath $invokeTests)) {
    throw "Invoke-Tests script not found: $invokeTests"
  }
  $testArgs = @{
    Executable = $testExe
  }
  if ($TestFilter) {
    $testArgs.Filter = $TestFilter
  }
  & $invokeTests @testArgs
} else {
  Write-Warning 'Skipping tests as requested.'
}

if (-not $NoHotSwap) {
  $hotSwapScript = Join-Path $PSScriptRoot 'HotSwap.ps1'
  if (-not (Test-Path -LiteralPath $hotSwapScript)) {
    throw "HotSwap script not found: $hotSwapScript"
  }

  if ($env:SIERRA_DATA_DIR) {
    & pwsh -NoProfile -File $hotSwapScript `
      -Dll $wrapperDll `
      -SierraDataDir $env:SIERRA_DATA_DIR `
      -UseRemoteRelease:$RemoteHotSwap `
      -AutoRemoteFallback:(!$DisableRemoteFallback) `
      -SierraHost $SierraHost `
      -SierraPort $SierraPort `
      -ReleaseCommandFormat $ReleaseCommandFormat `
      -AllowCommandFormat $AllowCommandFormat `
      -WaitTimeoutSeconds $WaitTimeoutSeconds `
      -WaitIntervalMilliseconds $WaitIntervalMilliseconds
  } else {
    Write-Warning 'SIERRA_DATA_DIR is not set. Skipping hot-swap step.'
  }
} else {
  Write-Warning 'Hot-swap disabled by -NoHotSwap.'
}

# Compile MT5 advisor and copy bridge DLL if requested.
if ($BuildAdvisor) {
  # Для этой машины используем фиксированный путь, если переменная не задана.
  $defaultMt5DataDir = 'C:\Users\admin\AppData\Roaming\MetaQuotes\Terminal\29E91DA909EB4475AB204481D1C2CE7D'
  $mt5DataDir = if ($env:MT5_DATA_DIR) { $env:MT5_DATA_DIR } elseif (Test-Path -LiteralPath $defaultMt5DataDir) { $defaultMt5DataDir } else { $null }

  if (-not $mt5DataDir) {
    Write-Warning 'MT5_DATA_DIR is not set and fallback path not found. Skipping advisor compilation.'
  } else {
    $env:MT5_DATA_DIR = $mt5DataDir
    $compileAdvisorScript = Join-Path $PSScriptRoot 'CompileAdvisor.ps1'
    if (-not (Test-Path -LiteralPath $compileAdvisorScript)) {
      Write-Warning "CompileAdvisor script not found: $compileAdvisorScript"
    } else {
      & pwsh -NoProfile -File $compileAdvisorScript `
        -Source (Join-Path $PSScriptRoot '..\projects\Advisor\src\SierraStudyAdvisor.mq5') `
        -OutputDir (Join-Path $PSScriptRoot '..\out\mt5') `
        -Mt5DataDir $mt5DataDir `
        -BridgeBinary (Join-Path $PSScriptRoot '..\x64\Release\SierraStudyAdvisorBridgeMT5.dll')
    }
  }
}


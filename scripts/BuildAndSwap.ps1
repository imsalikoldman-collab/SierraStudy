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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot '..')
Push-Location $repoRoot
try {
  $gitCmd = Get-Command git -ErrorAction SilentlyContinue
  if ($gitCmd) {
    Write-Host "[deps] git submodule update --init --recursive"
    & $gitCmd.Source 'submodule' 'update' '--init' '--recursive'
    if ($LASTEXITCODE -ne 0) {
      throw "git submodule update failed with exit code $LASTEXITCODE."
    }
  } else {
    Write-Warning "[deps] git executable not found, skipping submodule update."
  }
} finally {
  Pop-Location
}

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
$wrapperDll = Join-Path $hotSwapDir 'SierraStudy.dll'

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


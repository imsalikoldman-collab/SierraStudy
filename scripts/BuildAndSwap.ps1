param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',

  [switch]$SkipTests,

  [switch]$NoHotSwap,

  [string]$Platform = 'x64',

  [string]$TestFilter
)

$ErrorActionPreference = 'Stop'

$solution = Join-Path $PSScriptRoot '..\SierraStudy.sln'
$invokeBuild = Join-Path $PSScriptRoot 'Invoke-Build.ps1'
if (-not (Test-Path -LiteralPath $invokeBuild)) {
  throw "Invoke-Build script not found: $invokeBuild"
}

& $invokeBuild -Solution $solution -Configuration $Configuration -Platform $Platform

$outputDir = Join-Path $PSScriptRoot "..\out\$Platform\$Configuration"
$testExe = Join-Path $outputDir 'SierraStudy.Tests.exe'
$wrapperDll = Join-Path $outputDir 'SierraStudy.Wrapper.dll'

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
    & pwsh -NoProfile -File $hotSwapScript -Dll $wrapperDll -SierraDataDir $env:SIERRA_DATA_DIR
  } else {
    Write-Warning 'SIERRA_DATA_DIR is not set. Skipping hot-swap step.'
  }
} else {
  Write-Warning 'Hot-swap disabled by -NoHotSwap.'
}

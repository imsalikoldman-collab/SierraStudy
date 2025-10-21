<#
.SYNOPSIS
  Convenience script to run full build + test cycle for Debug and Release.

.PARAMETER SkipHotSwap
  Skip DLL copying step.

.PARAMETER SkipTests
  Skip running unit tests.

.PARAMETER TestFilter
  Optional Google Test filter applied to both configurations.
#>
param(
  [switch]$SkipHotSwap,
  [switch]$SkipTests,
  [string]$TestFilter
)

$ErrorActionPreference = 'Stop'

$buildScript = Join-Path $PSScriptRoot 'BuildAndSwap.ps1'
if (-not (Test-Path -LiteralPath $buildScript)) {
  throw "BuildAndSwap script not found: $buildScript"
}

$configs = @('Debug', 'Release')

foreach ($cfg in $configs) {
  Write-Host ""
  Write-Host "=== Running $cfg configuration ==="

  $args = @{
    Configuration = $cfg
    Platform = 'x64'
  }

  if ($SkipHotSwap) {
    $args.NoHotSwap = $true
  }
  if ($SkipTests) {
    $args.SkipTests = $true
  }
  if ($TestFilter) {
    $args.TestFilter = $TestFilter
  }

  & $buildScript @args
}

Write-Host ""
Write-Host "=== All configurations completed successfully ==="

<#
.SYNOPSIS
  Runs the Google Test executable with optional filters.

.PARAMETER Executable
  Path to the compiled test binary (e.g., out\x64\Debug\SierraStudy.Tests.exe).

.PARAMETER Filter
  Optional gtest filter expression (e.g., "MovingAverageTest.*").

.PARAMETER AdditionalArgs
  Additional arguments to forward to the test executable.
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$Executable,

  [string]$Filter,

  [string[]]$AdditionalArgs
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable)) {
  throw "Test executable not found: $Executable"
}

$argsList = @('--gtest_color=yes')
if ($Filter) {
  $argsList += "--gtest_filter=$Filter"
}
if ($AdditionalArgs) {
  $argsList += $AdditionalArgs
}

Write-Host "[test] $Executable $($argsList -join ' ')"

& $Executable @argsList
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
  throw "Tests failed with exit code $exitCode."
}

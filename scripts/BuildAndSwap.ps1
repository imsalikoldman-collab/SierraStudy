param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$solution = Join-Path $PSScriptRoot '..\SierraStudy.sln'
$outputDir = Join-Path $PSScriptRoot "..\out\x64\$Configuration"
$testExe = Join-Path $outputDir 'SierraStudy.Tests.exe'
$wrapperDll = Join-Path $outputDir 'SierraStudy.Wrapper.dll'

& msbuild.exe $solution /m /p:Configuration=$Configuration /p:Platform=x64
& $testExe --gtest_color=yes
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'HotSwap.ps1') -Dll $wrapperDll -SierraDataDir $env:SIERRA_DATA_DIR

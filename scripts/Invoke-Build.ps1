<#
.SYNOPSIS
  Wrapper around MSBuild with unified logging.

.PARAMETER Solution
  Path to the .sln or project file to build.

.PARAMETER Configuration
  Configuration name (Debug/Release).

.PARAMETER Platform
  Platform name (defaults to x64).

.PARAMETER Log
  Optional path to the logfile. When omitted, a file is created under
  build/logs/msbuild-<Configuration>.log encoded as UTF-8.
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$Solution,

  [string]$Configuration = 'Debug',

  [string]$Platform = 'x64',

  [string]$Log
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot '..')
$resolveScript = Join-Path $scriptRoot 'Resolve-Msbuild.ps1'
$msbuildPath = & $resolveScript -ThrowIfNotFound

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot -and (Test-Path 'C:\dev\vcpkg')) {
  $vcpkgRoot = 'C:\dev\vcpkg'
}
$vcpkgTriplet = if ($env:VCPKG_DEFAULT_TRIPLET) { $env:VCPKG_DEFAULT_TRIPLET } else { 'x64-windows-static' }
$vcpkgInstalledDir = if ($env:VCPKG_INSTALLED_DIR) { $env:VCPKG_INSTALLED_DIR } else { 'C:\dev\vcpkg\installed-manifest' }

if (-not (Test-Path -LiteralPath $Solution)) {
  throw "Solution or project file not found: $Solution"
}

if (-not $Log) {
  $logDir = Join-Path $scriptRoot '..\build\logs'
  if (-not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
  }
  $cfgSanitized = $Configuration -replace '[^\w\-]', '_'
  $Log = Join-Path $logDir "msbuild-$cfgSanitized.log"
}

$msbuildArgs = @(
  (Resolve-Path -LiteralPath $Solution).ProviderPath,
  '/m',
  "/p:Configuration=$Configuration",
  "/p:Platform=$Platform",
  "/p:VcpkgRoot=$vcpkgRoot",
  "/p:VcpkgTriplet=$vcpkgTriplet",
  "/p:VcpkgEnableManifest=true",
  "/p:VcpkgInstalledDir=$vcpkgInstalledDir",
  '/fileLogger',
  "/fileLoggerParameters:LogFile=$Log;Encoding=UTF-8"
)

Write-Host "[build] $Configuration|$Platform -> $Solution"
Write-Host "[build] log: $Log"
Write-Host "[vcpkg] root=$vcpkgRoot triplet=$vcpkgTriplet installed=$vcpkgInstalledDir"

$result = & $msbuildPath @msbuildArgs
if ($LASTEXITCODE -ne 0) {
  throw "MSBuild failed with exit code $LASTEXITCODE. See log at $Log."
}
return $result

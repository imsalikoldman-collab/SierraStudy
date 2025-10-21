<#
.SYNOPSIS
  Resolves the full path to MSBuild.exe for the current machine.

.DESCRIPTION
  Tries multiple strategies:
    1. Optional manual override file `msbuild.path.ps1` located in the same folder.
       Define `$MsbuildPath` inside to hard-code a location.
    2. Environment variable `MSBUILD_EXE`.
    3. `vswhere.exe` (installed with Visual Studio) to locate the latest MSBuild.
    4. A list of common installation paths.

  The first existing executable is returned. When `-ThrowIfNotFound` is passed
  an exception is raised if nothing is found; otherwise `$null` is returned.
#>
param(
  [switch]$ThrowIfNotFound
)

function Resolve-CandidatePath {
  param([string]$Path)
  if ([string]::IsNullOrWhiteSpace($Path)) {
    return $null
  }
  if (Test-Path -LiteralPath $Path) {
    return (Resolve-Path -LiteralPath $Path).ProviderPath
  }
  return $null
}

$candidates = @()

# 1. Manual override file (optional).
$manualPathFile = Join-Path $PSScriptRoot 'msbuild.path.ps1'
if (Test-Path -LiteralPath $manualPathFile) {
  . $manualPathFile
  if (Get-Variable -Name MsbuildPath -Scope Script -ErrorAction SilentlyContinue) {
    $candidates += $MsbuildPath
  } elseif (Get-Variable -Name MsbuildPath -Scope Global -ErrorAction SilentlyContinue) {
    $candidates += $Global:MsbuildPath
  }
}

# 2. Environment variable.
if ($env:MSBUILD_EXE) {
  $candidates += $env:MSBUILD_EXE
}

# 3. vswhere lookup.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
  $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (Test-Path -LiteralPath $vswhere) {
  try {
    $vswhereArgs = @(
      '-latest',
      '-requires', 'Microsoft.Component.MSBuild',
      '-find', 'MSBuild\**\Bin\MSBuild.exe'
    )
    $found = & $vswhere @vswhereArgs 2>$null
    if ($found) {
      $candidates += $found
    }
  } catch {
    # Suppress vswhere errors; fall back to default paths.
  }
}

# 4. Known default locations (add both Community and Build Tools).
$knownRoots = @(
  'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
  'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
  'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe',
  'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
)
$candidates += $knownRoots

foreach ($candidate in $candidates | Where-Object { $_ }) {
  $resolved = Resolve-CandidatePath -Path $candidate
  if ($resolved) {
    return $resolved
  }
}

if ($ThrowIfNotFound) {
  throw "MSBuild.exe not found. Provide a manual path in scripts\msbuild.path.ps1 or set MSBUILD_EXE."
}

return $null

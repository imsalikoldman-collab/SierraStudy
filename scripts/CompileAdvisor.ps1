<#
.SYNOPSIS
  Compiles the MetaTrader 5 advisor and copies the resulting EX5 file.

.DESCRIPTION
  Requires MetaEditor.exe (MetaTrader 5) and a configured MT5 data directory
  containing the MQL5 folder. The script copies the source .mq5 into
  MQL5\Experts\SierraStudy, calls MetaEditor with /compile, and then copies
  the generated .ex5 back into the repository output folder for convenience.

.PARAMETER Source
  Path to the .mq5 advisor source file.

.PARAMETER OutputDir
  Directory where the compiled .ex5 (and logs) will be copied.

.PARAMETER MetaEditorPath
  Optional explicit path to MetaEditor.exe. When omitted the script checks
  the METAEDITOR_EXE environment variable and a set of well-known locations.

.PARAMETER Mt5DataDir
  MT5 data directory containing the MQL5 folder. Defaults to $env:MT5_DATA_DIR.
#>
param(
  [string]$Source = (Join-Path $PSScriptRoot '..\projects\Advisor\src\SierraStudyAdvisor.mq5'),
  [string]$OutputDir = (Join-Path $PSScriptRoot '..\out\mt5'),
  [string]$MetaEditorPath,
  [string]$Mt5DataDir = $env:MT5_DATA_DIR,
  [string]$BridgeBinary = (Join-Path $PSScriptRoot '..\projects\AdvisorBridge\x64\Release\SierraStudyAdvisorBridgeMT5.dll')
)

$ErrorActionPreference = 'Stop'

function Resolve-MetaEditorPath {
  param([string]$ExplicitPath)
  $candidates = @()
  if ($ExplicitPath) {
    $candidates += $ExplicitPath
  }
  if ($env:METAEDITOR_EXE) {
    $candidates += $env:METAEDITOR_EXE
  }
  $candidates += @(
    'C:\Program Files\MetaTrader 5\metaeditor64.exe',
    'C:\Program Files\MetaTrader 5\metaeditor.exe',
    'C:\Program Files (x86)\MetaTrader 5\metaeditor64.exe',
    'C:\Program Files (x86)\MetaTrader 5\metaeditor.exe',
    'C:\Program Files\Tickmill MT5 Terminal\MetaEditor64.exe',
    'C:\Program Files\Tickmill MT5 Terminal\MetaEditor.exe'
  )
  foreach ($candidate in $candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) {
      return (Resolve-Path -LiteralPath $candidate).ProviderPath
    }
  }
  throw "MetaEditor.exe not found. Specify -MetaEditorPath or set METAEDITOR_EXE."
}

if (-not (Test-Path -LiteralPath $Source)) {
  throw "Advisor source not found: $Source"
}

if (-not $Mt5DataDir) {
  throw "MT5 data directory not provided. Set MT5_DATA_DIR or pass -Mt5DataDir."
}
if (-not (Test-Path -LiteralPath $Mt5DataDir)) {
  throw "MT5 data directory does not exist: $Mt5DataDir"
}

$metaEditor = Resolve-MetaEditorPath -ExplicitPath $MetaEditorPath

$expertsDir = Join-Path $Mt5DataDir 'MQL5\Experts\SierraStudy'
if (-not (Test-Path -LiteralPath $expertsDir)) {
  New-Item -ItemType Directory -Path $expertsDir -Force | Out-Null
}

$sourceName = Split-Path -Leaf $Source
$advisorBaseName = [System.IO.Path]::GetFileNameWithoutExtension($sourceName)
$workingSourceName = "{0}.build.mq5" -f $advisorBaseName
$targetSource = Join-Path $expertsDir $workingSourceName
Copy-Item -LiteralPath $Source -Destination $targetSource -Force

if (-not (Test-Path -LiteralPath $OutputDir)) {
  New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}
$outputResolved = (Resolve-Path -LiteralPath $OutputDir).ProviderPath
$logPath = Join-Path $outputResolved 'MetaEditor.log'

$mt5Mql = (Resolve-Path -LiteralPath (Join-Path $Mt5DataDir 'MQL5')).ProviderPath
$mt5Inc = $mt5Mql
$metaArgs = @(
  "/compile:`"$targetSource`"",
  "/log:`"$logPath`"",
  "/mql5:`"$mt5Mql`"",
  "/inc:`"$mt5Inc`""
)

$process = Start-Process -FilePath $metaEditor -ArgumentList $metaArgs -NoNewWindow -PassThru -Wait
$exitCode = $process.ExitCode
if ($exitCode -ne 0) {
  Write-Warning "MetaEditor exited with code $exitCode (see $logPath). Continuing if EX5 present."
}

$compiledTempPath = [System.IO.Path]::ChangeExtension($targetSource, '.ex5')
if (-not (Test-Path -LiteralPath $compiledTempPath)) {
  throw "Compilation finished but EX5 not found at $compiledTempPath"
}

$outputExpertDir = Join-Path $OutputDir 'Experts'
if (-not (Test-Path -LiteralPath $outputExpertDir)) {
  New-Item -ItemType Directory -Path $outputExpertDir -Force | Out-Null
}
$finalEx5Name = "{0}.ex5" -f $advisorBaseName
$dst = Join-Path $outputExpertDir $finalEx5Name
Copy-Item -LiteralPath $compiledTempPath -Destination $dst -Force
$finalExpertEx5 = Join-Path $expertsDir $finalEx5Name
Copy-Item -LiteralPath $compiledTempPath -Destination $finalExpertEx5 -Force

Write-Host "[advisor] Compiled advisor copied to $dst"

Remove-Item -LiteralPath $targetSource -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $compiledTempPath -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $BridgeBinary) {
  $bridgeDestRepo = Join-Path $OutputDir 'Experts\SierraStudyAdvisorBridgeMT5.dll'
  Copy-Item -LiteralPath $BridgeBinary -Destination $bridgeDestRepo -Force
  $bridgeDestMt5 = Join-Path $Mt5DataDir 'MQL5\Libraries\SierraStudyAdvisorBridgeMT5.dll'
  Copy-Item -LiteralPath $BridgeBinary -Destination $bridgeDestMt5 -Force
  Write-Host "[advisor] Bridge DLL copied to MT5 Libraries."
} else {
  Write-Warning "Bridge DLL not found at $BridgeBinary. Ensure AdvisorBridge is built (Release x64)."
}

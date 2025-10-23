param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

if (-not $env:UTF8_CONSOLE_READY) {
    try {
        chcp 65001 > $null
    } catch {}
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [Console]::OutputEncoding = $utf8
    $OutputEncoding = $utf8
    $env:UTF8_CONSOLE_READY = "1"
}
$env:VSLANG = "1033"
$env:PreferredUILang = "en-US"

$ErrorActionPreference = "Stop"

$includePath = "C:\2308\ACS_Source\"
$projectRoot = Resolve-Path -Path "$PSScriptRoot\.."
$buildScript = Join-Path $PSScriptRoot "build.ps1"

if (-not (Test-Path $buildScript)) {
    throw "Expected build script at $buildScript was not found."
}

if (-not (Test-Path $includePath)) {
    throw "Include path $includePath was not found. Ensure Sierra Chart headers are present."
}

& $buildScript -Configuration $Configuration -Platform $Platform -Clean

$outputDll = Join-Path $projectRoot "build\$Platform\$Configuration\StudyEMA20.dll"
if (-not (Test-Path $outputDll)) {
    throw "Expected output $outputDll does not exist. Check build logs for details."
}

$sourcePath = Join-Path $projectRoot "src\EMA20Study.cpp"
$emaUsage = Select-String -Path $sourcePath -Pattern "MOVAVGTYPE_EXPONENTIAL" -SimpleMatch -ErrorAction SilentlyContinue
if (-not $emaUsage) {
    throw "Verification failed: EMA calculation pattern was not found in $sourcePath."
}

Write-Host "Verification completed. Output: $outputDll"



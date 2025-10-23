param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$SkipHotDeploy,
    [string]$SierraHost = "127.0.0.1",
    [int]$SierraPort = 11099,
    [int]$WaitTimeoutSeconds = 20,
    [int]$WaitIntervalMilliseconds = 250,
    [string]$Destination,
    [string]$ReleaseCommandFormat,
    [string]$AllowCommandFormat,
    [switch]$SkipCopy
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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $scriptRoot "build.ps1"
$hotdeployScript = Join-Path $scriptRoot "hotdeploy.ps1"

if (-not (Test-Path -LiteralPath $buildScript)) {
    throw "Build script not found at $buildScript."
}

if (-not (Test-Path -LiteralPath $hotdeployScript)) {
    throw "Hot deploy script not found at $hotdeployScript."
}

if (-not $SkipBuild) {
    $buildArgs = @{
        Configuration = $Configuration
        Platform      = $Platform
    }

    if ($Clean) {
        $buildArgs.Clean = $true
    }

    Write-Host "Starting build: $Configuration|$Platform"
    & $buildScript @buildArgs
    Write-Host "Build completed."
}
else {
    Write-Host "Skipping build step by request."
}

if (-not $SkipHotDeploy) {
    $hotdeployArgs = @{
        Configuration        = $Configuration
        Platform             = $Platform
        SierraHost           = $SierraHost
        SierraPort           = $SierraPort
        WaitTimeoutSeconds   = $WaitTimeoutSeconds
        WaitIntervalMilliseconds = $WaitIntervalMilliseconds
    }

    if ($PSBoundParameters.ContainsKey('Destination')) {
        $hotdeployArgs.Destination = $Destination
    }

    if ($PSBoundParameters.ContainsKey('ReleaseCommandFormat')) {
        $hotdeployArgs.ReleaseCommandFormat = $ReleaseCommandFormat
    }

    if ($PSBoundParameters.ContainsKey('AllowCommandFormat')) {
        $hotdeployArgs.AllowCommandFormat = $AllowCommandFormat
    }

    if ($SkipCopy) {
        $hotdeployArgs.SkipCopy = $true
    }

    Write-Host "Starting hot deploy."
    & $hotdeployScript @hotdeployArgs
    Write-Host "Hot deploy completed."
}
else {
    Write-Host "Skipping hot deploy step by request."
}



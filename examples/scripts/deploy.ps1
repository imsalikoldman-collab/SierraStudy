param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Destination = "C:\2308\Data\",
    [int]$WaitTimeoutSeconds = 20,
    [int]$WaitIntervalMilliseconds = 250,
    [switch]$SkipWait,
    [string]$SierraHost = "127.0.0.1",
    [int]$SierraPort = 11099,
    [string]$ReleaseCommandFormat = "RELEASE_DLL--{0}",
    [switch]$DisableRelease
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

if (-not $Destination) {
    throw "Destination path is not specified. Provide -Destination or set the SIERRA_CHART_HOME environment variable."
}

function Wait-ForFileUnlock {
    param(
        [string]$Path,
        [int]$TimeoutSeconds,
        [int]$IntervalMilliseconds
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        if (-not (Test-Path -LiteralPath $Path)) {
            return $true
        }

        try {
            $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
            $stream.Dispose()
            return $true
        } catch {
            if ($stopwatch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                return $false
            }
            Start-Sleep -Milliseconds $IntervalMilliseconds
        }
    }
}

function Send-UdpCommand {
    param(
        [string]$Command,
        [System.Net.IPEndPoint]$Endpoint
    )

    if (-not $Command -or -not $Endpoint) {
        return
    }

    $payload = [System.Text.Encoding]::ASCII.GetBytes(($Command -replace "\0.*$",""))
    $udpClient = [System.Net.Sockets.UdpClient]::new()
    try {
        [void]$udpClient.Send($payload, $payload.Length, $Endpoint)
        Write-Host "Sent UDP command '$Command' to $($Endpoint.Address):$($Endpoint.Port)."
    } finally {
        $udpClient.Dispose()
    }
}

function Format-Command {
    param(
        [string]$Format,
        [string]$Value
    )

    if (-not $Format) {
        return $null
    }

    if ($Format.Contains("{0}")) {
        return ([string]::Format($Format, $Value)).Trim()
    }

    return $Format.Trim()
}

$projectRoot = Resolve-Path -Path "$PSScriptRoot\.."
$buildOutput = Join-Path $projectRoot "build\$Platform\$Configuration\StudyEMA20.dll"

if (-not (Test-Path $buildOutput)) {
    throw "Build output $buildOutput does not exist. Run scripts/build.ps1 first."
}

if (-not (Test-Path $Destination)) {
    Write-Host "Destination $Destination does not exist. Creating it."
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
}

$targetPath = Join-Path $Destination "StudyEMA20.dll"

$endpoint = $null
if (-not $DisableRelease) {
    $resolvedHostAddresses = [System.Net.Dns]::GetHostAddresses($SierraHost)
    if (-not $resolvedHostAddresses -or $resolvedHostAddresses.Length -eq 0) {
        throw "Failed to resolve host '$SierraHost'."
    }
    $endpoint = [System.Net.IPEndPoint]::new($resolvedHostAddresses[0], $SierraPort)
    $releaseCommand = Format-Command -Format $ReleaseCommandFormat -Value $targetPath
    Send-UdpCommand -Command $releaseCommand -Endpoint $endpoint
}

if (-not $SkipWait) {
    if (-not (Wait-ForFileUnlock -Path $targetPath -TimeoutSeconds $WaitTimeoutSeconds -IntervalMilliseconds $WaitIntervalMilliseconds)) {
        throw "Timed out waiting for '$targetPath' to be released. Increase -WaitTimeoutSeconds or unload the study manually."
    }
}

Copy-Item -Path $buildOutput -Destination $targetPath -Force

Write-Host "Deployed StudyEMA20.dll to $targetPath"

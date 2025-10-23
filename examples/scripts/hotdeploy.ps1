param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Destination = "C:\2308\Data\",
    [string]$SierraHost = "127.0.0.1",
    [int]$SierraPort = 11099,
    [string]$ReleaseCommandFormat = "RELEASE_DLL--{0}",
    [string]$AllowCommandFormat = "ALLOW_LOAD_DLL--{0}",
    [int]$WaitTimeoutSeconds = 20,
    [int]$WaitIntervalMilliseconds = 250,
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

function Send-UdpCommand {
    param(
        [string]$Command,
        [System.Net.IPEndPoint]$Endpoint
    )

    if (-not $Command) {
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

$projectRoot = Resolve-Path -Path "$PSScriptRoot\.."
$buildOutput = Join-Path $projectRoot "build\$Platform\$Configuration\StudyEMA20.dll"

if (-not (Test-Path -LiteralPath $buildOutput)) {
    throw "Build output $buildOutput does not exist. Run scripts/build.ps1 first."
}

$resolvedHostAddresses = [System.Net.Dns]::GetHostAddresses($SierraHost)
if (-not $resolvedHostAddresses -or $resolvedHostAddresses.Length -eq 0) {
    throw "Failed to resolve host '$SierraHost'."
}

$endpoint = [System.Net.IPEndPoint]::new($resolvedHostAddresses[0], $SierraPort)

$targetPath = Join-Path $Destination "StudyEMA20.dll"

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

$releaseCommand = Format-Command -Format $ReleaseCommandFormat -Value $targetPath
$allowCommand = Format-Command -Format $AllowCommandFormat -Value $targetPath
$tempPath = $null

try {
    if (-not $SkipCopy) {
        if (-not (Test-Path -LiteralPath (Split-Path -Parent $targetPath))) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $targetPath) -Force | Out-Null
        }

        $tempPath = Join-Path (Split-Path -Parent $targetPath) ("{0}.{1}.tmp" -f (Split-Path -Leaf $targetPath), [guid]::NewGuid().ToString("N"))
        Copy-Item -LiteralPath $buildOutput -Destination $tempPath -Force
        Write-Host "Prepared temp payload '$tempPath'."

        Send-UdpCommand -Command $releaseCommand -Endpoint $endpoint

        if (-not (Wait-ForFileUnlock -Path $targetPath -TimeoutSeconds $WaitTimeoutSeconds -IntervalMilliseconds $WaitIntervalMilliseconds)) {
            throw "Timed out waiting for '$targetPath' to be released. Increase -WaitTimeoutSeconds or unload the study manually."
        }

        Copy-Item -LiteralPath $tempPath -Destination $targetPath -Force
        Write-Host "Copied new DLL to '$targetPath'."
    }

    Send-UdpCommand -Command $allowCommand -Endpoint $endpoint
} finally {
    if ($tempPath -and (Test-Path -LiteralPath $tempPath)) {
        Remove-Item -LiteralPath $tempPath -ErrorAction SilentlyContinue
    }
}



param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [switch]$Clean
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

$projectRoot = Resolve-Path -Path "$PSScriptRoot\.."
$projectFile = Join-Path $projectRoot "StudyEMA20.vcxproj"

function Get-MSBuild {
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath) {
        $installationPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                return @{
                    FilePath = $candidate
                    Arguments = @()
                }
            }
        }
    }

    $fallback = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($fallback) {
        return @{
            FilePath = $fallback.Source
            Arguments = @()
        }
    }

    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($dotnet) {
        return @{
            FilePath = $dotnet.Source
            Arguments = @("msbuild")
        }
    }

    return $null
}

$msbuild = Get-MSBuild
if (-not $msbuild) {
    throw "MSBuild could not be located. Install Visual Studio Build Tools or add MSBuild to PATH."
}

$arguments = @()
if ($msbuild.Arguments) {
    $arguments += $msbuild.Arguments
}

$arguments += $projectFile
$arguments += "/m"
$arguments += "/p:Configuration=$Configuration"
$arguments += "/p:Platform=$Platform"
$arguments += "/p:PreferredUILang=en-US"

if ($Clean) {
    $arguments += "/t:Clean,Build"
} else {
    $arguments += "/t:Build"
}

Write-Host "Building StudyEMA20 ($Configuration|$Platform) using $($msbuild.FilePath)"
& $msbuild.FilePath @arguments

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild finished with exit code $LASTEXITCODE."
}



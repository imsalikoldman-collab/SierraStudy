$ErrorActionPreference = 'Stop'
if (-not $env:SIERRA_DATA_DIR) {
    $env:SIERRA_DATA_DIR = 'D:\2308\Data'
}
if (-not $env:VCPKG_ROOT -and (Test-Path 'D:\dev\vcpkg')) {
    $env:VCPKG_ROOT = 'D:\dev\vcpkg'
}
if (-not $env:VCPKG_DEFAULT_TRIPLET) {
    $env:VCPKG_DEFAULT_TRIPLET = 'x64-windows-static'
}
if (-not $env:VCPKG_INSTALLED_DIR) {
    $env:VCPKG_INSTALLED_DIR = 'C:\dev\vcpkg\installed-manifest'
}

function Copy-TestFiles {
    param(
        [string]$RepoRoot,
        [string]$DestinationRoot
    )

    $source = Join-Path $RepoRoot 'test_files'
    if (-not (Test-Path -LiteralPath $source)) {
        return
    }

    $destination = Join-Path $DestinationRoot 'test_files'
    if (-not (Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
    }

    Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force

    foreach ($file in Get-ChildItem -Path $source -File -Recurse) {
        $relative = $file.FullName.Substring($source.Length).TrimStart('\','/')
        $target = Join-Path $destination $relative
        if (-not (Test-Path -LiteralPath $target)) {
            throw "[test-files] copy check failed: $target"
        }
    }
    Write-Host "[test-files] copied to $destination"
}

try {
    & "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
      "$PSScriptRoot\SierraStudy.sln" `
      /m `
      /p:Configuration=Debug `
      /p:Platform=x64 `
      /p:VcpkgRoot=$env:VCPKG_ROOT `
      /p:VcpkgTriplet=$env:VCPKG_DEFAULT_TRIPLET `
      /p:VcpkgEnableManifest=true `
      /p:VcpkgInstalledDir=$env:VCPKG_INSTALLED_DIR
    Write-Host "[build] ok"
} catch {
    Write-Host "[build] fail: $($_.Exception.Message)"
    exit 1
}

try {
    & "$PSScriptRoot\out\x64\Debug\SierraStudy.Tests.exe"
    Write-Host "[tests] ok"
} catch {
    Write-Host "[tests] fail: $($_.Exception.Message)"
    exit 1
}

try {
    Copy-TestFiles -RepoRoot $PSScriptRoot -DestinationRoot $env:SIERRA_DATA_DIR
    pwsh -NoProfile -File "$PSScriptRoot\scripts\HotSwap.ps1" -Dll "$PSScriptRoot\out\x64\Debug\SierraStudy_GexBot.dll" -SierraDataDir $env:SIERRA_DATA_DIR -AutoRemoteFallback
    Write-Host "[deploy] ok"
} catch {
    Write-Host "[deploy] fail: $($_.Exception.Message)"
    exit 1
}


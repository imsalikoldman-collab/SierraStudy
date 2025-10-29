$ErrorActionPreference='Stop'
$env:SIERRA_DATA_DIR = 'C:\2308\Data\'
try{& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "$PSScriptRoot\SierraStudy.sln" /m /p:Configuration=Debug /p:Platform=x64; Write-Host "[build] ok"}catch{Write-Host "[build] fail: $($_.Exception.Message)"; exit 1}
try{& "$PSScriptRoot\out\x64\Debug\SierraStudy.Tests.exe"; Write-Host "[tests] ok"}catch{Write-Host "[tests] fail: $($_.Exception.Message)"; exit 1}
try{pwsh -NoProfile -File "$PSScriptRoot\scripts\HotSwap.ps1" -Dll "$PSScriptRoot\out\x64\Debug\SierraStudy.dll" -SierraDataDir $env:SIERRA_DATA_DIR -AutoRemoteFallback; Write-Host "[deploy] ok"}catch{Write-Host "[deploy] fail: $($_.Exception.Message)"; exit 1}

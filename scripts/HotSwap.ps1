param(
  [Parameter(Mandatory = $true)]
  [string]$Dll,
  [Parameter(Mandatory = $true)]
  [string]$SierraDataDir,
  [string]$TargetName = (Split-Path -Leaf $Dll)
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Dll)) {
  throw "DLL not found: $Dll"
}

if (-not (Test-Path -LiteralPath $SierraDataDir)) {
  throw "Sierra data directory not found: $SierraDataDir"
}

$destination = Join-Path -Path $SierraDataDir -ChildPath $TargetName

Write-Host "[hot-swap] Preparing to copy '$Dll' -> '$destination'"

try {
  Copy-Item -LiteralPath $Dll -Destination $destination -Force
  Write-Host "[hot-swap] Updated: $destination"
} catch {
  Write-Warning "Copy failed. In Sierra Chart execute 'Analysis → Build → Release All DLLs and Deny Load' and retry."
  throw
}

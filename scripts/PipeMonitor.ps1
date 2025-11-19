param(
  [string]$PipeName = 'SierraStudyAdvisor',
  [int]$MaxMessages = 0
)

$ErrorActionPreference = 'Stop'

function Normalize-PipeName {
  param([string]$Name)

  $trimmed = $Name.Trim()
  if ([string]::IsNullOrWhiteSpace($trimmed)) {
    throw "Pipe name '$Name' is invalid."
  }

  $prefixes = @('\\.\pipe\', '\\?\pipe\')
  $normalized = $trimmed
  $wasNormalized = $false
  do {
    $matched = $false
    foreach ($prefix in $prefixes) {
      if ($normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring($prefix.Length)
        $matched = $true
        $wasNormalized = $true
        break
      }
    }
  } while ($matched -and -not [string]::IsNullOrWhiteSpace($normalized))

  if ([string]::IsNullOrWhiteSpace($normalized)) {
    throw "Pipe name '$Name' is invalid after normalization."
  }

  if ($wasNormalized) {
    Write-Host "[pipe-monitor] Input '$Name' нормализован до '$normalized'." -ForegroundColor DarkGray
  }

  if ($normalized.Contains('\')) {
    Write-Warning "Pipe name '$Name' содержит символы '\' после нормализации. Используется '$normalized'. Клиент может не подключиться."
  }

  return $normalized
}

$pipeNameTrimmed = Normalize-PipeName -Name $PipeName
Write-Host "[pipe-monitor] Waiting for pipe '\\.\pipe\\$pipeNameTrimmed' (server='.', name='$pipeNameTrimmed')..." -ForegroundColor Cyan

function Connect-PipeClient([string]$name) {
  $client = [System.IO.Pipes.NamedPipeClientStream]::new('.', $name, [System.IO.Pipes.PipeDirection]::In)
  $client.Connect(5000)
  return $client
}

while ($true) {
  try {
    $pipe = Connect-PipeClient -name $pipeNameTrimmed
    Write-Host "[pipe-monitor] Connected." -ForegroundColor Green
    $reader = New-Object System.IO.StreamReader($pipe)
    $count = 0
    while ($pipe.IsConnected) {
      $line = $reader.ReadLine()
      if ($null -eq $line) { break }
      $timestamp = (Get-Date).ToString('HH:mm:ss.fff')
      Write-Host "[$timestamp] $line" -ForegroundColor Yellow
      $count++
      if ($MaxMessages -gt 0 -and $count -ge $MaxMessages) { break }
    }
  } catch {
    Write-Warning "[pipe-monitor] $_"
  } finally {
    if ($pipe) { $pipe.Dispose() }
  }
  if ($MaxMessages -gt 0 -and $count -ge $MaxMessages) { break }
  Start-Sleep -Seconds 2
  Write-Host "[pipe-monitor] Reconnecting..." -ForegroundColor Cyan
}

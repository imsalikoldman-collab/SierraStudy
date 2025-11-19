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

function Format-BridgeMessage {
<#
@brief Формирует человекочитаемую строку по JSON-сообщению моста.
@param Line Строка, полученная из pipe.
@return PSCustomObject|$null Объект с полями Text/Color либо $null, если строка не JSON.
@note Использует ConvertFrom-Json и распознаёт OPEN/STOP/CLOSE события.
@warning При некорректном JSON функция возвращает $null без выброса исключения.
#>
  param([string]$Line)

  try {
    $payload = $Line | ConvertFrom-Json -ErrorAction Stop
  } catch {
    return $null
  }

  $timestamp = if ($payload.ts_ms -ne $null) {
    try {
      [DateTimeOffset]::FromUnixTimeMilliseconds([int64]$payload.ts_ms).ToLocalTime().ToString('HH:mm:ss.fff')
    } catch {
      (Get-Date).ToString('HH:mm:ss.fff')
    }
  } else {
    (Get-Date).ToString('HH:mm:ss.fff')
  }

  $formatPrice = {
    param($value)
    if ($value -eq $null) { return '-' }
    return [string]::Format('{0:F2}', [double]$value)
  }

  $noteSuffix = if ($payload.PSObject.Properties.Name -contains 'note' -and $payload.note) {
    " note='$($payload.note)'"
  } else {
    ""
  }

  switch ($payload.type) {
    'OPEN_SIGNAL' {
      $text = "OPEN_SIGNAL trade_id=$($payload.trade_id) dir=$($payload.direction) entry=$(&$formatPrice $payload.entry_price)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Green' }
    }
    'ENTRY_AFTER_STOP' {
      $text = "ENTRY_AFTER_STOP trade_id=$($payload.trade_id) dir=$($payload.direction) entry=$(&$formatPrice $payload.entry_price)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Green' }
    }
    'STOP_ONLY' {
      $text = "STOP_ONLY trade_id=$($payload.trade_id) dir=$($payload.direction) stop=$(&$formatPrice $payload.stop_price) mode=$($payload.mode)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Yellow' }
    }
    'STOP_LEVEL' {
      $text = "STOP_LEVEL trade_id=$($payload.trade_id) dir=$($payload.direction) stop=$(&$formatPrice $payload.stop_price) mode=$($payload.mode)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Yellow' }
    }
    'CLOSE_SIGNAL' {
      $text = "CLOSE_SIGNAL trade_id=$($payload.trade_id) dir=$($payload.direction) close=$(&$formatPrice $payload.close_price) reason=$($payload.close_reason)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Cyan' }
    }
    default {
      $text = "$($payload.type) trade_id=$($payload.trade_id)$noteSuffix"
      return [pscustomobject]@{ Text = "[$timestamp] $text"; Color = 'Yellow' }
    }
  }
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
      $formatted = Format-BridgeMessage -Line $line
      if ($null -eq $formatted) {
        $timestamp = (Get-Date).ToString('HH:mm:ss.fff')
        Write-Host "[$timestamp] $line" -ForegroundColor Yellow
      } else {
        Write-Host $formatted.Text -ForegroundColor $formatted.Color
      }
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

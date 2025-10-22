<#
@brief Горячая замена DLL Sierra Chart.
@param Dll Путь к собранной DLL, которую нужно развернуть.
@param SierraDataDir Каталог данных Sierra Chart (обычно содержит каталог Studies).
@param TargetName Имя файла назначения (по умолчанию берётся из Dll).
@param UseRemoteRelease Если указан, управление отправляется по UDP (Release/Allow) перед копированием.
@param SierraHost Хост Sierra Chart для удалённых команд.
@param SierraPort Порт UDP, который слушает Sierra Chart.
@param ReleaseCommandFormat Формат строки команды для освобождения DLL (используется подстановка {0}).
@param AllowCommandFormat Формат строки команды для разрешения загрузки DLL.
@param WaitTimeoutSeconds Время ожидания освобождения файла при удалённом сценарии.
@param WaitIntervalMilliseconds Интервал проверки освобождения файла.
@param AutoRemoteFallback Включает автоматический переход в удалённый сценарий при блокировке файла.
@return Ничего. Скрипт бросит исключение при ошибке копирования или таймауте.
@note При локальном сценарии выполняется прямое копирование поверх существующего файла. Если выбран удалённый режим или сработал AutoRemoteFallback, DLL сначала копируется во временный файл, затем отправляются команды Release/Allow.
@warning Убедитесь, что Sierra Chart слушает указанный порт и поддерживает команды Release/Allow, иначе удалённый режим завершится ошибкой.
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$Dll,

  [Parameter(Mandatory = $true)]
  [string]$SierraDataDir,

  [string]$TargetName = (Split-Path -Leaf $Dll),

  [switch]$UseRemoteRelease,

  [string]$SierraHost = "127.0.0.1",

  [int]$SierraPort = 11099,

  [string]$ReleaseCommandFormat = "RELEASE_DLL--{0}",

  [string]$AllowCommandFormat = "ALLOW_LOAD_DLL--{0}",

  [int]$WaitTimeoutSeconds = 20,

  [int]$WaitIntervalMilliseconds = 250,

  [switch]$AutoRemoteFallback
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Dll)) {
  throw "DLL not found: $Dll"
}

if (-not (Test-Path -LiteralPath $SierraDataDir)) {
  throw "Sierra data directory not found: $SierraDataDir"
}

$destination = Join-Path -Path $SierraDataDir -ChildPath $TargetName
$destinationDir = Split-Path -Parent $destination

if (-not (Test-Path -LiteralPath $destinationDir)) {
  New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
}

function Format-RemoteCommand {
<#
@brief Формирует строку команды с учётом шаблона.
@param Format Шаблон строки (может содержать {0}).
@param Value Значение, которым подменяется {0}.
@return Готовая команда или $null, если шаблон пуст.
@note Строка обрезается по пробелам и символам обнуления.
@warning При отсутствии {0} возвращается исходный формат без подстановки.
#>
  param(
    [string]$Format,
    [string]$Value
  )

  if (-not $Format) {
    return $null
  }

  $trimmed = $Format.Trim()
  if ($trimmed.Contains("{0}")) {
    return ([string]::Format($trimmed, $Value)).Trim()
  }

  return $trimmed
}

function Send-UdpCommand {
<#
@brief Отправляет UDP-команду Sierra Chart.
@param Command Строка команды.
@param Endpoint Конечная точка (IP + порт).
@return Ничего.
@note Использует ASCII-кодировку, аналогичную примерным скриптам Sierra.
@warning Если UDP-порт недоступен, будет выброшено исключение.
#>
  param(
    [string]$Command,
    [System.Net.IPEndPoint]$Endpoint
  )

  if (-not $Command) {
    return
  }

  $payload = [System.Text.Encoding]::ASCII.GetBytes(($Command -replace "\0.*$", ""))
  $udpClient = [System.Net.Sockets.UdpClient]::new()
  try {
    [void]$udpClient.Send($payload, $payload.Length, $Endpoint)
    Write-Host "[hot-swap] Sent UDP command '$Command' to $($Endpoint.Address):$($Endpoint.Port)."
  } finally {
    $udpClient.Dispose()
  }
}

function Wait-ForFileUnlock {
<#
@brief Ожидает освобождения файла Sierra Chart.
@param Path Полный путь к файлу.
@param TimeoutSeconds Максимальное время ожидания.
@param IntervalMilliseconds Интервал проверки.
@return $true, если файл доступен, иначе $false.
@note Если файл не существует, считается, что он уже свободен.
@warning При превышении таймаута стоит проверить, освобождена ли DLL в Sierra Chart.
#>
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

function Invoke-RemoteSwap {
  if (-not $env:UTF8_CONSOLE_READY) {
    try {
      chcp 65001 > $null
    } catch {}
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    [Console]::OutputEncoding = $utf8
    $OutputEncoding = $utf8
    $env:UTF8_CONSOLE_READY = "1"
  }

  $resolvedHostAddresses = [System.Net.Dns]::GetHostAddresses($SierraHost)
  if (-not $resolvedHostAddresses -or $resolvedHostAddresses.Length -eq 0) {
    throw "Failed to resolve host '$SierraHost'."
  }
  $endpoint = [System.Net.IPEndPoint]::new($resolvedHostAddresses[0], $SierraPort)

  $releaseCommand = Format-RemoteCommand -Format $ReleaseCommandFormat -Value $destination
  $allowCommand = Format-RemoteCommand -Format $AllowCommandFormat -Value $destination

  $tempPath = Join-Path $destinationDir ("{0}.{1}.tmp" -f (Split-Path -Leaf $destination), [guid]::NewGuid().ToString("N"))
  try {
    Copy-Item -LiteralPath $Dll -Destination $tempPath -Force
    Write-Host "[hot-swap] Prepared temp payload '$tempPath'."

    if ($releaseCommand) {
      Send-UdpCommand -Command $releaseCommand -Endpoint $endpoint
    }

    if (-not (Wait-ForFileUnlock -Path $destination -TimeoutSeconds $WaitTimeoutSeconds -IntervalMilliseconds $WaitIntervalMilliseconds)) {
      throw "Timed out waiting for '$destination' to be released by Sierra Chart."
    }

    Copy-Item -LiteralPath $tempPath -Destination $destination -Force
    Write-Host "[hot-swap] Copied new DLL to '$destination'."

    if ($allowCommand) {
      Send-UdpCommand -Command $allowCommand -Endpoint $endpoint
    }
  } finally {
    if ($tempPath -and (Test-Path -LiteralPath $tempPath)) {
      Remove-Item -LiteralPath $tempPath -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "[hot-swap] Preparing to copy '$Dll' -> '$destination'"

$sharingViolationHResult = -2147024864
$autoFallback = $AutoRemoteFallback.IsPresent

if ($UseRemoteRelease) {
  Invoke-RemoteSwap
} else {
  try {
    Copy-Item -LiteralPath $Dll -Destination $destination -Force
    Write-Host "[hot-swap] Updated: $destination"
  } catch {
    $ioException = $_.Exception
    $isSharingViolation = $ioException -is [System.IO.IOException] -and $ioException.HResult -eq $sharingViolationHResult
    if ($autoFallback -and $isSharingViolation) {
      Write-Warning "[hot-swap] Local copy failed (sharing violation). Fallback to remote release/allow."
      Invoke-RemoteSwap
    } else {
      Write-Warning "Copy failed. В Sierra Chart выполните 'Analysis → Build → Release All DLLs and Deny Load' и повторите попытку."
      throw
    }
  }
}

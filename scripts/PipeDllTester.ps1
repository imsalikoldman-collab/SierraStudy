param(
  [string]$DllPath = (Join-Path $PSScriptRoot '..\x64\Release\SierraStudyAdvisorBridgeMT5.dll'),
  [string]$PipeName = '\\.\pipe\SierraStudyAdvisor',
  [int]$DurationSeconds = 20,
  [int]$PollMs = 200,
  [int]$MaxMessages = 100,
  [int]$ReconnectAttempts = 5,
  [int]$ReconnectDelayMs = 500,
  [string]$LogPath = '',
  [switch]$AutoSwitchToDll
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $DllPath)) {
  throw "DLL not found: $DllPath"
}

$dllFullPath = [System.IO.Path]::GetFullPath($DllPath)
$dllEscaped = $dllFullPath.Replace('\', '\\')

# Логирование
$logFile = $null
if ($LogPath -and $LogPath.Trim().Length -gt 0) {
  $logFile = [System.IO.Path]::GetFullPath($LogPath)
  $logDir = [System.IO.Path]::GetDirectoryName($logFile)
  if ($logDir -and -not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
  }
  Write-Host "[pipe-tester] Logging to $logFile"
}
function Write-Line([string]$line) {
  Write-Host $line
  if ($logFile) { Add-Content -Path $logFile -Value $line }
}

# Стадия 1: managed клиент → ждём OPEN/CLOSE.
$stage1Ok = $false
$managedOk = $false
$openSeen = $false
$closeSeen = $false
$pipeShortName = ($PipeName -replace '^\\\\\.\\pipe\\')
try {
  $deadline = (Get-Date).AddSeconds($DurationSeconds)
  $client = $null
  $reader = $null
  while ((Get-Date) -lt $deadline -and -not ($openSeen -and $closeSeen)) {
    if (-not $client) {
      try {
        $direction = [System.IO.Pipes.PipeDirection]::In
        $client = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeShortName, $direction)
        $client.Connect(500)
        if ($client.IsConnected) {
          $managedOk = $true
          Write-Line "[stage1] Managed client connected."
          $reader = New-Object System.IO.StreamReader($client)
        } else {
          $client.Dispose()
          $client = $null
          Start-Sleep -Milliseconds $ReconnectDelayMs
          continue
        }
      } catch {
        if ($client) { $client.Dispose() }
        $client = $null
        Start-Sleep -Milliseconds $ReconnectDelayMs
        continue
      }
    }

    if ($client -and $client.IsConnected -and $client.DataAvailable) {
      $line = $reader.ReadLine()
      if ($line) {
        $trim = $line.Trim()
        Write-Line ("[stage1 {0:HH:mm:ss.fff}] {1}" -f (Get-Date), $trim)
        if ($trim -match 'OPEN_SIGNAL') { $openSeen = $true }
        if ($trim -match 'CLOSE_SIGNAL') { $closeSeen = $true }
      }
    } elseif ($client -and -not $client.IsConnected) {
      Write-Line "[stage1] Managed client disconnected, retry..."
      $reader = $null
      $client.Dispose()
      $client = $null
      Start-Sleep -Milliseconds $ReconnectDelayMs
    } else {
      Start-Sleep -Milliseconds $PollMs
    }
  }
  if ($reader) { $reader.Close() }
  if ($client) { $client.Dispose() }
  if ($openSeen -and $closeSeen) { $stage1Ok = $true }
} catch {
  Write-Line "[stage1] Exception: $_"
}

if (-not $stage1Ok) {
  Write-Line "[stage1] FAILED: OPEN/CLOSE не получены. managed_ok=$managedOk. Переходим к DLL-тесту по принуждению."
}

if (-not $AutoSwitchToDll) {
  $answer = Read-Host "Stage1 OK (OPEN/CLOSE получены). Перейти к DLL-тесту? [y/N]"
  if ($answer.Trim().ToLower() -ne 'y') {
    Write-Line "[pipe-tester] DLL-тест пропущен по запросу пользователя."
    exit 0
  }
}

# Поддержка DLL (P/Invoke)
$csharp = @"
using System;
using System.Runtime.InteropServices;
namespace Sierra.Tester {
    public static class PipeNative {
        [DllImport("$dllEscaped", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int SierraPipeConnect(string pipe_name);
        [DllImport("$dllEscaped", CallingConvention = CallingConvention.Cdecl)]
        public static extern void SierraPipeClose();
        [DllImport("$dllEscaped", CallingConvention = CallingConvention.Cdecl)]
        public static extern int SierraPipeWrite(byte[] data, int size);
        [DllImport("$dllEscaped", CallingConvention = CallingConvention.Cdecl)]
        public static extern int SierraPipeRead(byte[] buffer, int size, int timeout_ms);
    }
}
"@
Add-Type -TypeDefinition $csharp -Language CSharp
Write-Line "[pipe-tester] DLL loaded: $dllFullPath"
$connectType = [Sierra.Tester.PipeNative]

function Try-ConnectDll {
  param([int]$attempt)
  $res = $connectType::SierraPipeConnect($PipeName)
  if ($res -eq 1) { return $true }
  Write-Warning "[stage2] Connect failed (res=$res, attempt=$attempt, pipe=$PipeName, managed_ok=$managedOk)"
  return $false
}

# Стадия 2: DLL-клиент.
Write-Line "[stage2] Старт DLL-клиента..."
$connected = $false
for ($i = 1; $i -le $ReconnectAttempts; $i++) {
  if (Try-ConnectDll -attempt $i) { $connected = $true; break }
  Start-Sleep -Milliseconds $ReconnectDelayMs
}
if (-not $connected) {
  throw "[pipe-tester] DLL подключение не удалось после $ReconnectAttempts попыток (pipe=$PipeName)"
}

$buffer = New-Object byte[] 4096
$start = Get-Date
$receivedCount = 0

while ($true) {
  $res = $connectType::SierraPipeRead($buffer, $buffer.Length, $PollMs)
  if ($res -gt 0) {
    $text = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $res)
    Write-Line ("[stage2 {0:HH:mm:ss.fff}] {1}" -f (Get-Date), $text.TrimEnd("`r","`n"))
    $receivedCount++
    if ($MaxMessages -gt 0 -and $receivedCount -ge $MaxMessages) { break }
  } elseif ($res -lt 0) {
    Write-Line ("[warn {0:HH:mm:ss.fff}] Read failed (res={1}), reconnect..." -f (Get-Date), $res)
    $connectType::SierraPipeClose()
    Start-Sleep -Milliseconds $ReconnectDelayMs
    $connected = $false
    for ($i = 1; $i -le $ReconnectAttempts; $i++) {
      if (Try-ConnectDll -attempt $i) { $connected = $true; break }
      Start-Sleep -Milliseconds $ReconnectDelayMs
    }
    if (-not $connected) {
      Write-Line "[stage2] Reconnect failed, stopping."
      break
    }
    continue
  }

  if ((Get-Date) - $start -gt [TimeSpan]::FromSeconds($DurationSeconds)) {
    break
  }
}

$connectType::SierraPipeClose()
Write-Host "[pipe-tester] Done. Messages received (DLL stage): $receivedCount"

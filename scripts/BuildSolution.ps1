<#
@brief Полная сборка решения SierraStudy с проверкой артефактов, тестами и компиляцией MT5-советника.
@param Configuration Конфигурация MSBuild (Debug/Release).
@param Platform Целевая платформа (обычно x64).
@param SkipTests Пропускает запуск Google Test, если установлен.
@param TestFilter Необязательный фильтр Google Test.
@param SkipAdvisor Пропускает компиляцию MT5-советника и копирование DLL моста.
@param MetaEditorPath Явный путь к MetaEditor.exe.
@param Mt5DataDir Каталог данных MT5 (по умолчанию берётся из переменной MT5_DATA_DIR).
@param AdvisorSource MQ5-файл советника.
@param AdvisorOutputDir Папка вывода артефактов советника (EX5, логи).
@return Код выхода 0 при успехе, иначе 1.
@note Скрипт вызывает Invoke-Build, Invoke-Tests и CompileAdvisor, формируя краткий итоговый отчёт.
@warning Для этапа MT5 необходимы установленный MetaEditor и каталог данных MT5; при отсутствии окружения шаг завершится ошибкой.
#>
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release',

  [string]$Platform = 'x64',

  [switch]$SkipTests,

  [string]$TestFilter,

  [switch]$SkipAdvisor,

  [string]$MetaEditorPath,

  [string]$Mt5DataDir = $env:MT5_DATA_DIR,

  [string]$AdvisorSource = (Join-Path $PSScriptRoot '..\projects\Advisor\src\SierraStudyAdvisor.mq5'),

  [string]$AdvisorOutputDir = (Join-Path $PSScriptRoot '..\out\mt5')
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).ProviderPath
$solution = Join-Path $repoRoot 'SierraStudy.sln'
$invokeBuild = Join-Path $PSScriptRoot 'Invoke-Build.ps1'
$invokeTests = Join-Path $PSScriptRoot 'Invoke-Tests.ps1'
$compileAdvisor = Join-Path $PSScriptRoot 'CompileAdvisor.ps1'

if (-not (Test-Path -LiteralPath $invokeBuild)) {
  throw "Не найден Invoke-Build.ps1: $invokeBuild"
}
if (-not (Test-Path -LiteralPath $invokeTests)) {
  throw "Не найден Invoke-Tests.ps1: $invokeTests"
}
if (-not (Test-Path -LiteralPath $compileAdvisor)) {
  throw "Не найден CompileAdvisor.ps1: $compileAdvisor"
}
if (-not (Test-Path -LiteralPath $solution)) {
  throw "Не найдена solution: $solution"
}

$summary = New-Object System.Collections.Generic.List[object]
$scriptFailed = $false
$artifacts = $null

function Add-Summary {
  param(
    [string]$Step,
    [string]$Status,
    [string]$Details
  )
  $summary.Add([pscustomobject]@{
      Step    = $Step
      Status  = $Status
      Details = $Details
    })
}

function Invoke-Step {
  param(
    [string]$Name,
    [scriptblock]$Action,
    [string]$SuccessMessage = 'Готово'
  )

  Write-Host ""
  Write-Host "[build] $Name..."
  try {
    $result = & $Action
    Add-Summary -Step $Name -Status 'OK' -Details $SuccessMessage
    return $result
  } catch {
    $message = $_.Exception.Message.Trim()
    if (-not $message) {
      $message = 'Неизвестная ошибка'
    }
    Add-Summary -Step $Name -Status 'FAIL' -Details $message
    throw
  }
}

try {
  $artifacts = Invoke-Step -Name ("Сборка MSBuild ({0}|{1})" -f $Configuration, $Platform) -SuccessMessage "Готово: Wrapper+AdvisorBridge" -Action {
    & $invokeBuild -Solution $solution -Configuration $Configuration -Platform $Platform

    $wrapperDll = Join-Path $repoRoot "out\$Platform\$Configuration\SierraStudyMT5.dll"
    if (-not (Test-Path -LiteralPath $wrapperDll)) {
      throw "Не найден DLL исследования: $wrapperDll"
    }

    $bridgeDll = Join-Path $repoRoot "x64\$Configuration\SierraStudyAdvisorBridgeMT5.dll"
    if (-not (Test-Path -LiteralPath $bridgeDll)) {
      throw "Не найден DLL моста советника: $bridgeDll"
    }

    return [pscustomobject]@{
      Wrapper = $wrapperDll
      Bridge  = $bridgeDll
    }
  }

  if (-not $SkipTests) {
    Invoke-Step -Name 'Тесты Google Test' -SuccessMessage 'Все тесты пройдены' -Action {
      $testExe = Join-Path $repoRoot "out\$Platform\$Configuration\SierraStudy.Tests.exe"
      if (-not (Test-Path -LiteralPath $testExe)) {
        throw "Не найден исполняемый файл тестов: $testExe"
      }

      $testArgs = @{ Executable = $testExe }
      if ($TestFilter) {
        $testArgs.Filter = $TestFilter
      }
      & $invokeTests @testArgs
    }
  } else {
    Add-Summary -Step 'Тесты Google Test' -Status 'SKIP' -Details 'Пропущено по флагу -SkipTests'
  }

  if (-not $SkipAdvisor) {
    Invoke-Step -Name 'Компиляция MT5 советника' -SuccessMessage 'Советник и мост готовы' -Action {
      if (-not $Mt5DataDir) {
        throw "Не задан каталог данных MT5 (параметр -Mt5DataDir или переменная MT5_DATA_DIR)."
      }

      $advisorArgs = @{
        Source         = $AdvisorSource
        OutputDir      = $AdvisorOutputDir
        MetaEditorPath = $MetaEditorPath
        Mt5DataDir     = $Mt5DataDir
        BridgeBinary   = $artifacts.Bridge
      }
      & $compileAdvisor @advisorArgs
    }
  } else {
    Add-Summary -Step 'Компиляция MT5 советника' -Status 'SKIP' -Details 'Пропущено по флагу -SkipAdvisor'
  }

  if ($artifacts) {
    Add-Summary -Step 'Артефакты' -Status 'INFO' -Details ("Wrapper: {0}; AdvisorBridge: {1}" -f $artifacts.Wrapper, $artifacts.Bridge)
  }
} catch {
  $scriptFailed = $true
  Write-Error $_
} finally {
  Write-Host ""
  Write-Host "=== Итоговый отчёт сборки ==="
  foreach ($entry in $summary) {
    Write-Host ("- {0}: {1} — {2}" -f $entry.Step, $entry.Status, $entry.Details)
  }

  if ($scriptFailed) {
    exit 1
  }
}

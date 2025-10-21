# AGENTS.md — VS Code + Codex GPT для разработки Sierra Chart (ACSIL, MSBuild, VS2022)

**Цель:** единый цикл *редактирование в VS Code (расширение Codex GPT) → сборка MSBuild → тесты Google Test → горячая замена DLL в Sierra Chart* при строгом разделении `Core/Wrapper/Tests`. Хранилище — GitHub. Логи (по необходимости) — `plog` в отдельный файл. Примеры кода и примеры использования ASCIL — в `examples/`. Скрипты PowerShell 7 — в `scripts/`.

---

## 1) Архитектура solution
**Одна `.sln` и три проекта:**
- **Core** — *Static Library (.lib)*. Чистая бизнес‑логика. **Без** `SierraChart.h` и любых ACSIL‑типов.
- **Wrapper** — *Dynamic Library (.dll)*. Обёртка ACSIL: читает входы/создаёт Subgraphs, вызывает Core, пишет результат в `sc.Subgraph`. Инициализирует логирование `plog` (если нужно).
- **Tests** — *Console (exe)* на **Google Test**, линкуется **только** с `Core.lib`. Тесты актуализируем совместно с кодом.

**Принципы:**
- Код в **Core** не зависит от **Wrapper**: никакого `SierraChart.h`, WinAPI и т.п.
- **Wrapper** — тонкий адаптер ACSIL ⇄ Core, хранение состояния через `sc.Get/SetPersistent*`.
- Любое изменение публичного API Core → обновить/добавить соответствующие тесты в `Tests`.

---

## 2) Структура репозитория
```
SierraStudy/
├─ SierraStudy.sln
├─ README.md
├─ AGENTS.md
├─ .gitignore
├─ .editorconfig
├─ .vscode/
│  ├─ tasks.json               # Build / Test / Hot‑Swap
│  └─ launch.json              # (опц.) отладка тестов
│
├─ build/
│  ├─ props/Directory.Build.props
│  └─ targets/Sierra.PostBuild.targets   # (опц.) автокопирование DLL
│
├─ external/                   # пути до SDK/утилит (под игнором)
│  └─ README.md                # опишите локальную установку SDK Sierra
│
├─ third_party/
│  ├─ googletest/              # submodule или вручную
│  └─ plog/                    # header‑only логгер
│
├─ projects/
│  ├─ Core/      (include/sierra/core/, src/, SierraStudy.Core.vcxproj)
│  ├─ Wrapper/   (include/sierra/ascil/, src/, SierraStudy.Wrapper.vcxproj)
│  └─ Tests/     (unit/, data/, SierraStudy.Tests.vcxproj)
│
├─ scripts/                    # PowerShell 7 для сборки/замены DLL
│  ├─ HotSwap.ps1              # горячая замена DLL в SIERRA_DATA_DIR
│  └─ BuildAndSwap.ps1         # сборка → тесты → hot‑swap
│
└─ examples/                   # примеры кода и использования ASCIL
   ├─ core_usage/...
   └─ ascil_usage/...
```

**Переменные окружения (на каждой машине):**
- `SIERRA_SDK_DIR` → папка с заголовками ACSIL (обычно `.../SierraChart/ACS_Source`).
- `SIERRA_DATA_DIR` → папка *Data* Sierra Chart (например `C:\SierraChart\Data`).

---

## 3) Предустановки
- **VS Code** + расширения: *Codex GPT*, *C/C++* (IntelliSense).
- **Visual Studio 2022 Build Tools** (MSVC v143, MSBuild).
- **PowerShell 7** (`pwsh`) в PATH.
- **Git** + доступ к GitHub.
- *(опц.)* `vcpkg` — для быстрой установки GTest.

---

## 4) MSBuild (общие настройки)
`build/props/Directory.Build.props`:
```xml
<Project>
  <PropertyGroup>
    <Platform>x64</Platform>
    <PlatformToolset>v143</PlatformToolset>
    <LanguageStandard>stdcpp17</LanguageStandard>
    <CharacterSet>Unicode</CharacterSet>
    <WarningLevel>Level4</WarningLevel>
    <TreatWarningsAsErrors>false</TreatWarningsAsErrors>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>
        $(SolutionDir)projects\Core\include;
        $(SolutionDir)third_party\plog;
        $(SIERRA_SDK_DIR)\ACS_Source;
        %(AdditionalIncludeDirectories)
      </AdditionalIncludeDirectories>
      <PreprocessorDefinitions>
        NOMINMAX;WIN32_LEAN_AND_MEAN;_CRT_SECURE_NO_WARNINGS;
        %(PreprocessorDefinitions)
      </PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
```

---

## 5) Зависимости: Google Test и plog

### Google Test
Подключение на выбор:
- **Ручной**: исходники в `third_party/googletest`, проект *Tests* добавляет include‑путь `googletest/include` и собирает `gtest`/`gtest_main` (как часть solution или отдельными .lib).
- **vcpkg**: `vcpkg install gtest` и добавить библиотеки к проекту *Tests*.

### plog
Header‑only. Инициализируйте лог один раз (например, при `sc.Index == 0`) и пишите **в отдельный файл**:
```cpp
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>
inline void InitLogging() {
  plog::init(plog::info, "Logs/SierraStudy.log", 5*1024*1024, 3); // 5МБ, 3 файла
  PLOG_INFO << "Logger initialized";
}
```
> Лог включаем **только при необходимости** (минимум в горячих циклах). В отладке можно повышать уровень.

---

## 6) VS Code: задачи (Build/Test/Hot‑Swap)
`.vscode/tasks.json` (минимум):
```jsonc
{
  "version": "2.0.0",
  "tasks": [
    { "label": "Build", "type": "shell",
      "command": "msbuild",
      "args": ["${workspaceFolder}/SierraStudy.sln","/m","/p:Configuration=${input:cfg}","/p:Platform=x64"],
      "problemMatcher": "$msCompile", "group": "build" },

    { "label": "Test", "type": "shell",
      "command": "${workspaceFolder}/out/x64/${input:cfg}/SierraStudy.Tests.exe",
      "group": "test" },

    { "label": "Hot-Swap", "type": "shell",
      "command": "pwsh",
      "args": ["-NoProfile","-File","${workspaceFolder}/scripts/HotSwap.ps1",
        "-Dll","${workspaceFolder}/out/x64/${input:cfg}/SierraStudy.Wrapper.dll",
        "-SierraDataDir","${env:SIERRA_DATA_DIR}"] }
  ],
  "inputs": [
    { "id": "cfg", "type": "pickString", "options": ["Debug","Release"], "default": "Debug" }
  ]
}
```

---

## 7) Скрипты в `scripts/` (PowerShell 7)

`scripts/HotSwap.ps1`:
```powershell
param([string]$Dll,[string]$SierraDataDir,[string]$TargetName=(Split-Path -Leaf $Dll))
$ErrorActionPreference='Stop'
if(!(Test-Path $Dll)){throw "Нет DLL: $Dll"}
if(!(Test-Path $SierraDataDir)){throw "Нет папки данных Sierra: $SierraDataDir"}
$dst = Join-Path $SierraDataDir $TargetName

# Базовый и надёжный сценарий: в Sierra нажмите
# Analysis → Build → "Release All DLLs and Deny Load", затем запустите скрипт.
Copy-Item -Force $Dll $dst
Write-Host "[hot-swap] Обновлено: $dst"
```

`scripts/BuildAndSwap.ps1`:
```powershell
param([ValidateSet('Debug','Release')]$Configuration='Debug')
$ErrorActionPreference='Stop'

& msbuild.exe "$PSScriptRoot\..\SierraStudy.sln" /m /p:Configuration=$Configuration /p:Platform=x64
& "$PSScriptRoot\..\out\x64\$Configuration\SierraStudy.Tests.exe" --gtest_color=yes
& pwsh -NoProfile -File "$PSScriptRoot\HotSwap.ps1" -Dll "$PSScriptRoot\..\out\x64\$Configuration\SierraStudy.Wrapper.dll" -SierraDataDir $env:SIERRA_DATA_DIR
```

---

## 8) Границы Core ⇄ Wrapper (минимальный скелет)
```cpp
SCSFExport scsf_MyMA(SCStudyGraphRef sc){
  if (sc.SetDefaults){
    sc.GraphName = "My MA";
    sc.AutoLoop = 1;
    sc.Subgraph[0].Name = "MA";
    return;
  }
  if (sc.Index == 0) InitLogging(); // по необходимости

  const int n = sc.ArraySize;
  std::vector<double> close(n);
  for (int i = 0; i < n; ++i) close[i] = sc.Close[i];

  auto out = sierra::core::moving_average(close, /*period*/ 20);
  sc.Subgraph[0][sc.Index] = (float)out[sc.Index];
}
```

---

## 9) GitHub: рабочий процесс
- Ветки: `main` (стабильная) / `dev` / `feature/*`.
- PR в `main` только при «зелёных» тестах.
- Теги релизов: `vX.Y.Z`.
- *(опц.)* CI (GitHub Actions): шаги build + tests, загрузка артефакта DLL.

---

## 10) Codex GPT в работе (VS Code)
- Храните **краткие промпты** рядом с кодом (`/prompts/*.md`) и запускайте их через расширение Codex GPT.
- Примеры промптов:
  - *Core*: «Реализуй `moving_average` в `projects/Core/src/indicators.cpp` без ACSIL‑типов; O(n); NaN при недостатке данных. Обнови/добавь тесты в `projects/Tests/unit/test_indicators_ma.cpp`. Верни патчи.»
  - *Wrapper*: «Добавь SCSFExport study с Input `Period`, вызов ядра, вывод в Subgraph[0]. Без глобальных переменных; состояние через persistent.»
  - *Tests*: «На баг N напиши падающий Google Test, затем исправь код и добейся зелёного прогона.»

---

## 11) Ежедневный цикл (коротко)
1) **Build** → 2) **Test** → 3) **Hot‑Swap** → 4) **Commit/Push**.  
Тесты и код поддерживаем синхронно; лог включаем только при необходимости и пишем в отдельный файл `Logs/SierraStudy.log`.
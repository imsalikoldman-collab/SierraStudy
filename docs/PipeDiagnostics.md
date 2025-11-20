# Диагностический чек-лист пайпа SierraStudy → MT5

Последовательность проверок, чтобы убедиться в работоспособности пайпа `\\.\pipe\SierraStudyAdvisor`, DLL моста и советника MT5.

## 1. Проверка стадии Sierra
- Запустить `SierraStudyBridge` на графике с именем пайпа `\\.\pipe\SierraStudyAdvisor`.
- Через `PipeMonitor.ps1` убедиться, что приходят OPEN/CLOSE (Sierra → MT5), журнал без ошибок.

## 2. Проверка наличия пайпа
- В PowerShell выполнить:
  ```pwsh
  Get-ChildItem "\\.\pipe\" | Where-Object { $_ -like '*SierraStudyAdvisor*' }
  ```
- Ожидаем увидеть `SierraStudyAdvisor` в списке. Если нет — стадия не создала пайп.
- Очистить всех клиентов (PipeMonitor, MT5 советник), пайп одноподключенческий.

## 3. Прослушка пайпа .NET‑клиентом
- Убедиться, что пайп свободен (нет подключённого клиента).
- Запустить `scripts/PipeDllTester.ps1` в режиме AutoSwitch, дождаться Stage1:
  ```pwsh
  pwsh -NoProfile -File scripts/PipeDllTester.ps1 -DurationSeconds 15 -AutoSwitchToDll -LogPath out\pipe_dll_tester.log
  ```
- Stage1 (Managed) должен подключиться и увидеть два сообщения `OPEN_SIGNAL` и `CLOSE_SIGNAL`. Если не получил — пайп недоступен или уже занят.

## 4. Проверка DLL моста
- В том же `PipeDllTester.ps1` Stage2 (DLL) стартует после Stage1 или принудительно. Он подключается через `SierraStudyAdvisorBridgeMT5.dll` и логирует входящие строки.
- Если Stage2 не подключается (res != 1) — пайп занят/недоступен или ошибка в DLL/ACL.

## 5. Проверка советника MT5
- В MT5: убедиться, что `MQL5\Experts\SierraStudy\SierraStudyAdvisor.ex5` и `MQL5\Libraries\SierraStudyAdvisorBridgeMT5.dll` обновлены (перезапустить терминал).
- Включить “Разрешить импорт DLL”, повесить советник на график, запустить. HUD должен появиться, в журнале нет ошибок коннекта, и при приходе OPEN/CLOSE советник открывает/закрывает позицию.

> Примечание: сервер пайпа одноподключенческий. Одновременно может подключаться только один клиент (PipeMonitor/Diag/советник). Если один клиент в работе, остальные получат res=0/timeouts.

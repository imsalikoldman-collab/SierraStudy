# Troubleshooting (решения частых ошибок)

## Hot-swap: DLL заблокирована (sharing violation)
- **Симптом**: `HotSwap.ps1` выводит предупреждение `Copy failed... The process cannot access the file '...\\SierraStudy_GexBot.dll' because it is being used by another process.` и остановку копирования.
- **Причина**: Sierra Chart держит загруженную DLL и не выполнил команду Release.
- **Решения**:
  1. В Sierra Chart выполнить `Analysis → Build → Release All DLLs and Deny Load`, затем повторить `HotSwap.ps1`.
  2. Использовать автоматический fallback, который сам отправит команды RELEASE/ALLOW по UDP и скопирует DLL через временный файл:  
     `pwsh -NoProfile -File scripts/HotSwap.ps1 -Dll out\\x64\\Release\\SierraStudy_GexBot.dll -SierraDataDir C:\\2308\\Data -AutoRemoteFallback`
- **Требования**: Sierra Chart должна слушать UDP на `127.0.0.1:11099` и принимать команды `RELEASE_DLL--<path>` / `ALLOW_LOAD_DLL--<path>`.

## Предупреждения из ACSIL заголовков Sierra Chart
- **Симптом**: при сборке Wrapper появляются десятки предупреждений (C4100, C4245, C4458, C4121/C4201), все указывают на файлы `C:\\2308\\ACS_Source\\*.h`.
- **Причина**: особенности реализаций в сторонних заголовках Sierra Chart; наш код Core/Wrapper/Tests их не генерирует.
- **Что делаем**:
  - Сохраняем уровень `/W4`, но оставляем `TreatWarningsAsErrors=false`, чтобы сборка не падала из‑за чужих хедеров.
  - При желании шум можно уменьшить, опустив уровень для внешних include (`/external:W3`) или точечно подавив коды `/wd4100 /wd4245 /wd4458` в Wrapper; пока не применяем, чтобы видеть новые проблемы.

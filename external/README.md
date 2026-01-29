# Внешние зависимости

Каталог `external/` хранит локальные заметки и пути, которые не попадают в Git:
- сюда можно положить ярлык на установленный SDK Sierra Chart;
- описать шаги настройки инструментов, используемых только локально.

Актуальные значения переменных окружения на рабочей машине:
```
SIERRA_SDK_DIR = C:\2308
SIERRA_DATA_DIR = C:\2308\Data
VCPKG_ROOT     = C:\dev\vcpkg
VCPKG_INSTALLED_DIR = C:\dev\vcpkg\installed-manifest
```
Если ваша установка расположена иначе, обновите переменные, сохранив структуру `...\ACS_Source` и `...\Data`.

Пример общего значения для `SIERRA_SDK_DIR`:
```
C:\SierraChart\ACS_Source
```

Примечание по vcpkg:
- vcpkg установлен локально в `C:\dev\vcpkg\`.
- При работе с manifest-режимом указывайте `VCPKG_ROOT=C:\dev\vcpkg`, `VCPKG_INSTALLED_DIR=C:\dev\vcpkg\installed-manifest` и, при необходимости, `VCPKG_DEFAULT_TRIPLET` (например, `x64-windows-static`).
- Типовая команда сборки Release с libcurl:  
  `msbuild SierraStudy.sln /m /p:Configuration=Release /p:Platform=x64 /p:VcpkgRoot=C:\dev\vcpkg /p:VcpkgTriplet=x64-windows-static /p:VcpkgInstalledDir=C:\dev\vcpkg\installed-manifest /p:VcpkgEnableManifest=true`

Добавляйте сюда дополнительные инструкции по подключению SDK, библиотек или утилит, чтобы другие разработчики могли воспроизвести окружение.

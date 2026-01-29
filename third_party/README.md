# Сторонние зависимости

Все сторонние зависимости устанавливаются через vcpkg manifest (`vcpkg.json`):
- `curl` (Schannel, http2)
- `gtest`
- `plog`
- `ryml` (RapidYAML)

Локальный `third_party/` больше не содержит исходников или подмодулей. Корневые переменные:
- `VCPKG_ROOT=C:\dev\vcpkg`
- `VCPKG_INSTALLED_DIR=C:\dev\vcpkg\installed-manifest`
- `VCPKG_DEFAULT_TRIPLET=x64-windows-static`

Пример установки зависимостей:
```
C:\dev\vcpkg\vcpkg.exe install --triplet x64-windows-static
```

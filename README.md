# SierraStudy

SierraStudy is a template solution for building Sierra Chart custom studies with a clean separation between reusable business logic and ACSIL integration. The repository ships with:
- `Core` static library that keeps platform-agnostic calculations.
- `Wrapper` DLL that adapts Sierra Chart ACSIL inputs/outputs to the core routines.
- `Tests` console application powered by Google Test for validating the core library.

## Prerequisites
- Visual Studio 2022 Build Tools (MSVC v143) with MSBuild.
- PowerShell 7 (`pwsh`) available in `PATH`.
- Sierra Chart installed with the ACSIL SDK headers.
- Environment variables:
  - `SIERRA_SDK_DIR` pointing to the `ACS_Source` directory.
  - `SIERRA_DATA_DIR` pointing to the Sierra Chart `Data` directory.

## Repository Layout
See `AGENTS.md` for the full architecture. The important directories are:
- `projects/` – contains the `Core`, `Wrapper`, and `Tests` Visual C++ projects.
- `build/` – MSBuild property and target overrides.
- `third_party/` – vendored third-party dependencies (`googletest`, `plog`).
- `scripts/` – PowerShell utilities for building and hot-swapping the Wrapper DLL.
- `examples/` – usage snippets for both the core library and ACSIL wrapper.

## Typical Workflow
1. Build the solution: `msbuild SierraStudy.sln /p:Configuration=Debug /p:Platform=x64`.
2. Run tests: `out\x64\Debug\SierraStudy.Tests.exe`.
3. Hot-swap the wrapper DLL into Sierra Chart via `scripts\HotSwap.ps1`.
4. Launch Sierra Chart and reload the study.

## Third-Party Dependencies
- **Google Test** – vendored under `third_party/googletest`. Initialize the submodule or clone the repository before building the tests.
- **plog** – header-only logger stored under `third_party/plog`.

Refer to `external/README.md` to configure local SDK paths.

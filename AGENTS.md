# TalkInput — Agent Guide

## Build & Run

```powershell
pwsh msvc.ps1 cmake --preset release --fresh
pwsh msvc.ps1 cmake --build build
.\build\bin\TalkInput.exe
```

Requires C++23, Qt6 (Widgets/Core/Gui/Multimedia/Network/Svg/Sql), libarchive (vcpkg), spdlog (vcpkg), **sherpa-onnx v1.13.3** static release archive under `ThirdParties/sherpa-onnx/`.

The CMake module `cmake/SherpaOnnxRelease.cmake` auto-extracts the matching Debug/Release archive into `build/third_parties/sherpa-onnx/`. If the archive is missing, configure fails with a clear message.

## Icon

`resources/*.svg` — SVG icons (Feather-style, 24×24 viewBox, `stroke="#333"`, 2px), loaded via qt_add_resources with `PREFIX "/"` and accessed as `:/resources/xxx.svg`.

## History

- SQLite DB at `QStandardPaths::AppDataLocation/history.db`, table `recognitions(id, text, created_at)`.

## SVG Icons

All button icons are Feather-style SVGs stored in `resources/` and accessed as `:/resources/xxx.svg`. Qt6::Svg must be linked. `windeployqt` deploys the `qsvg.dll` image format plugin. `QIcon(":/resources/xxx.svg")` works only when Qt6::Svg is linked.

## Commit Rule

Every modification must be committed immediately after a successful compile. Do not batch multiple logical changes into one commit unless they are trivial and atomic (e.g. a single-line fix). Each commit message should concisely describe what was changed.

## Dependencies

`QT` is installed in an external folder.
`sherpa-onnx` prebuilt binaries are downloaded from github release.
other dependencies are installed with vcpkg.
by default in `CMakeUserPresets.json`  option  `VCPKG_MANIFEST_INSTALL` is "OFF" to boost config step, if vcpkg.json is modified set it to `ON` to remove or install dependencies.
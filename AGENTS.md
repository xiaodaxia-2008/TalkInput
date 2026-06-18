# TalkInput — Agent Guide

## Build & Run

Requires C++23, Qt6 (Widgets/Core/Gui/Multimedia/Network/Svg/Sql), libarchive (vcpkg), spdlog (vcpkg), **sherpa-onnx v1.13.3** static release archive under `third_parties/sherpa-onnx/`.

The CMake module `cmake/SherpaOnnxRelease.cmake` auto-extracts the matching Debug/Release archive into `build/third_parties/sherpa-onnx/`. If the archive is missing, configure fails with a clear message.

```powershell
pwsh msvc.ps1 cmake --preset release --fresh
pwsh msvc.ps1 cmake --build build
.\build\bin\TalkInput.exe
```

### update translations
```
pwsh msvc.ps1 cmake --build build -t update_translations
```

Then search unfinished items in `src/TalkInput_zh.ts` and update them.

## Icon

`resources/icons/*.svg` — SVG icons (Feather-style, 24×24 viewBox, `stroke="#333"`, 2px), loaded via qt_add_resources with `PREFIX "/"` and accessed as `:/resources/icons/xxx.svg`.

## History

- SQLite DB at `appDataDir()/history.db`, table `recognitions(id, text, created_at)`.

## Commit Rule

Every modification must be committed immediately after a successful compile. 

## Dependencies

`QT` is installed in an external folder.
`sherpa-onnx` prebuilt binaries are downloaded from github release.
other dependencies are installed with vcpkg.
by default in `CMakeUserPresets.json`  option  `VCPKG_MANIFEST_INSTALL` is "OFF" to boost config step, if vcpkg.json is modified set it to `ON` to remove or install dependencies.
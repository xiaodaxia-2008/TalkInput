# TalkInput — Agent Guide

## Build & Run

Requires C++23, Qt6 (Widgets/Core/Gui/Multimedia/Network/Svg/Sql), libarchive (vcpkg), spdlog (vcpkg), **sherpa-onnx v1.13.3** static release archive under `third_parties/sherpa-onnx/`.

The CMake module `cmake/SherpaOnnxRelease.cmake` auto-extracts the matching Debug/Release archive into `build/third_parties/sherpa-onnx/`. If the archive is missing, configure fails with a clear message.

kill the `build/bin/TalkInput` process if it's locked before build.

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

## Common Rules
- 特定操作系统相关的代码: 在 src 目录放声明的 .h, 实现全部放在 windows/linux/macos 子文件夹中， 编译时在 cmake 中配置使用哪些源文件，在代码库中禁止使用任何判断平台的宏， 比如 `Q_OS_WIN` ，第三方库的代码除外。
- 使用相对路径 include 项目里的头文件，如果在子文件夹中，比如 `src/windows/system_ocr_recognizer.cpp`, 使用 `#include "../system_ocr_recognizer.h"`
- 源代码文件本身就是 **UTF-8** 格式，**必须直接使用 UTF-8 原始字符**，严禁使用 `\uXXXX` / `\UXXXXXXXX` 转义序列（如 `🎙` ✅，`\U0001f399` ❌）。
- 所有权定义清晰的场景下，使用 unique_ptr 表示拥有所有权的指针，裸指针默认无所有权；复杂场景下可以使用 shared_ptr/weak_ptr 。
- 使用 clang-foramt 格式化 .h/.cpp 文件，不要使用它格式化 CMakeLists.txt 
- Every modification must be staged immediately after a successful compile; and ask user to confirm commit.

## Icons

 `resources/icons/*.svg` — SVG icons (Feather-style, 24×24 viewBox, `stroke="#333"`, 2px), loaded via qt_add_resources with `PREFIX "/"` and accessed as `:/resources/icons/xxx.svg`.

## Dependencies

`QT` is installed in an external folder.
`sherpa-onnx` prebuilt binaries are downloaded from github release.
other dependencies are installed with vcpkg.
by default in `CMakeUserPresets.json`  option  `VCPKG_MANIFEST_INSTALL` is "OFF" to boost config step, if vcpkg.json is modified set it to `ON` to remove or install dependencies.

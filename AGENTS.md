# TalkInput — Agent Guide

## Build & Run

```powershell
cmake --preset release --fresh
cmake --build --preset release
.\build\bin\TalkInput.exe
```

Requires C++23, Qt6 (Widgets/Core/Gui/Multimedia/Network/Svg/Sql), libarchive (vcpkg), spdlog (vcpkg), **sherpa-onnx v1.13.3** static release archive under `ThirdParties/sherpa-onnx/`.

The CMake module `cmake/SherpaOnnxRelease.cmake` auto-extracts the matching Debug/Release archive into `build/ThirdParties/sherpa-onnx/`. If the archive is missing, configure fails with a clear message.

`Qt6_DIR` or `CMAKE_PREFIX_PATH` may need pointing at your Qt install.

## Test & Benchmark Executables

All build alongside the main app:

| Target | Description |
|---|---|
| `TalkInputAsrTest` | Console: decodes via ffmpeg + streaming transducer |
| `TalkInputSenseVoiceTest` | Console: same but uses SenseVoice offline model |
| `TalkInputBenchmark` | Benchmarks all supported models |

Usage: `.\build\bin\TalkInputAsrTest.exe [model-dir] [audio-file]`

## Layout

```
Src/
  main.cpp              — entrypoint, QApplication setup
  main_window.ui        — Qt Designer UI (two tabs: Recognition + Models)
  main_window.h/.cpp    — UI logic, audio capture, model loading
  setting_widget.h/.cpp — Models tab widget (table, download, delete archive)
  recognition_history.h/.cpp — SQLite persistence for recognition results
  speech_recognizer.h/.cpp   — streaming transducer wrapper (sherpa-onnx C API)
```

`resources/*.svg` — SVG icons (Feather-style, 24×24 viewBox, `stroke="#333"`, 2px), loaded via qt_add_resources with `PREFIX "/"` and accessed as `:/resources/xxx.svg`.

## Recognition

- **Only streaming transducer models** (encoder/decoder/joiner ONNX + tokens.txt) are supported for real-time recognition.
- Audio capture uses `QAudioSource` → PCM16 conversion → `SpeechRecognizer::acceptPcm16()`.
- Model loading is async (`QtConcurrent::run`) with a modal loading dialog.
- Hot words are supported via `SpeechRecognizer::Config::hotwordsText`.

## Models Tab

- 6 preset models listed (SenseVoice, FunASR Nano, Qwen3-ASR, Paraformer, Zipformer x2).
- Download → `.tar.bz2` to cache dir via `QNetworkAccessManager` → auto-extract with libarchive.
- Delete asks confirmation then `removeRecursively()`.
- Cache dir: `QStandardPaths::CacheLocation/models/`.
- "Use Archive" file dialog defaults to `DownloadLocation`.
- Download URL pattern: `https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/<name>.tar.bz2`

## History

- SQLite DB at `QStandardPaths::AppDataLocation/history.db`, table `recognitions(id, text, created_at)`.
- Each final recognition result is persisted and shown in the history table (first ~55 chars).
- Copy/Delete per-row buttons (SVG: copy.svg / delete.svg).

## Persistence

`QSettings` saves/restores `model/directory` and `model/name` (last selected model).

## SVG Icons

All button icons are Feather-style SVGs stored in `resources/` and accessed as `:/resources/xxx.svg`. Qt6::Svg must be linked. `windeployqt` deploys the `qsvg.dll` image format plugin. `QIcon(":/resources/xxx.svg")` works only when Qt6::Svg is linked.

## Commit Rule

Every modification must be committed immediately after a successful compile. Do not batch multiple logical changes into one commit unless they are trivial and atomic (e.g. a single-line fix). Each commit message should concisely describe what was changed.

## Dependencies

`QT` is installed in an external folder.
`sherpa-onnx` prebuilt binaries are downloaded from github release.
other dependencies are installed with vcpkg.
by default in `CMakeUserPresets.json`  option  `VCPKG_MANIFEST_INSTALL` is "OFF" to boost config step, if vcpkg.json is modified set it to `ON` to remove or install dependencies.
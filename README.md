# TalkInput

TalkInput is a Qt desktop application scaffold for a voice input method.

The current version contains a Qt welcome window, microphone capture, and
streaming speech recognition through sherpa-onnx. Recognition results are
printed to the in-app log and Qt debug output. The GUI can also pass optional
hotwords to the streaming transducer recognizer.

The main window is defined in `Src/main_window.ui`; C++ code handles only the
recognition and model-download behavior.

## Requirements

- CMake 3.21 or newer
- C++23 compiler
- Qt 6 with `Widgets`, `Core`, and `Gui`
- Ninja or another CMake generator
- sherpa-onnx release archives under `ThirdParties/sherpa-onnx`

Configure and build with the provided presets:

```powershell
cmake --preset release --fresh
cmake --build --preset release
```

If Qt is installed somewhere else, adjust `CMAKE_PREFIX_PATH` or set `Qt6_DIR`
to the Qt CMake package before configuring.

This project links sherpa-onnx from the local static release archives in
`ThirdParties/sherpa-onnx`. CMake extracts the matching Debug or Release archive
into the build directory and links the bundled `sherpa-onnx-c-api.lib`,
`sherpa-onnx-core.lib`, and `onnxruntime.lib`.

## Model

Download a sherpa-onnx streaming transducer model and select its directory in
the app.

The current defaults use:

```text
Models/sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30
```

Expected files in the selected model directory:

- `encoder.int8.onnx`
- `decoder.onnx`
- `joiner.int8.onnx`
- `tokens.txt`

The app also provides a `Models` tab with several preset sherpa-onnx download
links. Direct downloads are saved under the Qt cache location, in a `models`
subdirectory, unless another directory is selected. After download, the app
uses libarchive to extract `.tar.bz2` archives and selects the extracted model
directory automatically.

## Project Layout

```text
.
├── CMakeLists.txt
├── Src
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── main_window.cpp
│   ├── main_window.h
│   ├── speech_recognizer.cpp
│   └── speech_recognizer.h
└── README.md
```

## Run

After building with the release preset, the executable is generated under
`build/bin/TalkInput.exe` on Windows.

## ASR Test

The console test program decodes an audio/video file through `ffmpeg` and prints
the transcript:

```powershell
.\build\bin\TalkInputAsrTest.exe
```

Defaults:

- Model: `Models/sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30`
- Audio: `C:/Users/xiaoz/Music/meetily-recordings/audio.mp4`

You can override both paths:

```powershell
.\build-release\bin\TalkInputAsrTest.exe <model-dir> <audio-file>
```

## SenseVoice Test

The offline SenseVoice test program also decodes the input file through
`ffmpeg`, so the default MP4 test file does not need to be converted to WAV
first:

```powershell
.\build\bin\TalkInputSenseVoiceTest.exe
```

Defaults:

- Model: `Models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17`
- Audio: `C:/Users/xiaoz/Music/meetily-recordings/audio.mp4`

You can override both paths:

```powershell
.\build\bin\TalkInputSenseVoiceTest.exe <model-dir> <audio-file>
```

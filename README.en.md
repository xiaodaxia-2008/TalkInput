English | [简体中文](README.md)

# TalkInput

A local voice input tool that captures speech via microphone, supports OCR on the currently focused window for context, and uses LLM post-processing to correct recognition errors — results are automatically injected into any application's text field.

## Features

- **Multi-engine ASR**: Supports multiple ASR models.
- **AI Polish**: Recognized text is refined by an LLM for corrections and improvements.
- **OCR Context Awareness**: Captures the currently focused window, extracts text via OCR as LLM context, improving correction accuracy.

![ASR Settings](docs/imgs/talkinput_asr_setting_en.png)

- **Global Hotkey**: `Ctrl+Alt+Space` to start anytime.
- **Voice Overlay**: A floating text preview window appears during recording, showing real-time recognition progress and mode.

![Voice Overlay](docs/imgs/overlay_en.png)

- **Multiple Input Modes**:
  - 🎙 (ASR only)
  - 🎙✨ (ASR + AI polish)
  - 🎙✨📄 (ASR + OCR context + AI polish)

- **Recognition History**: All results are saved in a local SQLite database. Browse, copy, edit, or delete entries.

![Recognition History](docs/imgs/talkinput_history_en.png)

## Installation

On Windows, download the pre-built NSIS installer from [GitHub Releases](https://github.com/ZenShawn/TalkInput/releases) and run it.

On macOS / Linux, you need to build from source for now.

## Usage

After installation, click into any text input field, press `Ctrl+Alt+Space` to start recording, press again to stop and recognize. The result is automatically pasted into the input field.

### Speech Recognition Models

The installer bundles the `SenseVoice` model, which supports Chinese and English. The three preset ASR models are:

| Model | Languages | Streaming | Hotwords | Notes |
|---|---|---|---|
| **Paraformer** | zh, en | ✅ | ❌ | Real-time streaming, low latency, ideal for conversations |
| **SenseVoice** | zh/en/ja/ko/yue | ❌ | ❌ | Multilingual high accuracy, offline, very fast |
| **FunASR Nano** | zh, en | ❌ | ✅ | Highest accuracy, hotword correction, moderate speed |

After switching models, click the checkmark button to load the model. If the model hasn't been downloaded, it will download automatically. If the download fails, click the browser button next to the checkmark to download with your system browser. Once downloaded, click the import button in the middle to load the model.

### Optical Character Recognition (OCR) Models

Supports system OCR (built into Windows) or Tesseract OCR.

### Large Language Model (LLM)

Supports any OpenAI-compatible API endpoint.

With roughly 100 characters per recognition plus OCR context, a single recognition consumes less than 1,000 tokens. At DeepSeek V4 Flash pricing, this costs approximately $0.0001 per use.

### Global Hotkeys

| Hotkey (configurable) | Action |
|---|---|
| `Ctrl+Alt+Space` | Start / stop voice input |
| `Ctrl+Alt+Enter` | Cycle input mode: 🎙 (ASR) → 🎙✨ (ASR + AI polish) → 🎙✨📄 (ASR + OCR context + AI polish) |

In the settings, click into a hotkey input field and then press your desired key combination to customize.

### Audio File Recognition

Click "Recognize File" on the toolbar or select it from the menu to import audio files (WAV, MP3, M4A, etc.). The recognized text is displayed and saved to history.

## Development

### Project Structure

```
src/                 — Application source
  recognizers/       — ASR engine implementations (Paraformer / SenseVoice / FunASR / System)
  windows/           — Windows-specific implementation
  linux/             — Linux-specific implementation
  macos/             — macOS-specific implementation
resources/           — Icons, stylesheets, default configuration
cmake/               — CMake modules
third_parties/       — Third-party libraries
  sherpa-onnx/       — sherpa-onnx SDK and headers
  KDToolBox/         — Utility library
```

### Build from Source

**Prerequisites:**
- C++23 compiler (MSVC / Clang / GCC)
- [CMake](https://cmake.org/) ≥ 3.21
- [Qt 6](https://www.qt.io/) (Widgets / Core / Gui / Multimedia / Network / Svg / Sql)
- [vcpkg](https://github.com/microsoft/vcpkg) (libarchive, spdlog, nlohmann-json)
- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) v1.13.3 static library

```bash
git clone https://github.com/ZenShawn/TalkInput.git
cd TalkInput
cmake --preset release
cmake --build build
./build/bin/TalkInput
```

To package the installer

```bash
cd build
cpack
```

> The preset file contains Qt and vcpkg paths — edit `CMakePresets.json` to match your environment before building.

## Credits

- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — Offline speech recognition engine
- [Qt](https://www.qt.io/) — Cross-platform framework
- [QCoro](https://github.com/qcoro/qcoro) — C++ coroutine library
- [QHotkey](https://github.com/Skycoder42/QHotkey) — Global hotkey library

## License

[GNU General Public License v3.0](LICENSE)

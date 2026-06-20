# TalkInput — Desktop Voice Input

[中文](README_zh.md) | English

TalkInput is a desktop voice input tool powered by [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). Speak into your microphone and get text transcribed in real time — with optional LLM polishing and OCR-based text injection into any input field.

> **License**: GNU General Public License v3.0. See [LICENSE](LICENSE).

## Features

- **Real-time speech recognition** — speaks into your microphone and see text appear instantly
- **Multiple ASR backends** — Zipformer, SenseVoice, FunASR Nano, Paraformer, and system-provided engines
- **Model manager** — built-in model downloader with one-click install and auto-extraction
- **LLM post-processing** — polish and correct recognized text with a local LLM server
- **OCR text injection** — capture the focused input rectangle and inject text at the cursor position
- **Global hotkeys** — trigger recording from anywhere with customizable shortcuts for ASR / ASR+LLM / ASR+LLM+OCR modes
- **Hotwords** — define custom phrases to boost recognition accuracy for domain-specific terms
- **Audio file recognition** — transcribe pre-recorded audio files
- **Recognition history** — local SQLite storage with search and export
- **System tray** — runs quietly in the background with a tray icon and overlay indicator
- **Bilingual UI** — English and Chinese interface, switchable at runtime

## Download

Get the latest installer from the [Releases](https://github.com/xiaodaxia-2008/TalkInput/releases) page on GitHub.

## Requirements

- Windows 10 / 11 64-bit
- A microphone
- (Optional) A local LLM server compatible with the OpenAI chat completions API for text polishing

## Usage

1. **Launch** TalkInput — the main window has *Recognition* and *Model* tabs
2. **Choose a model** — switch to the *Model* tab, click a download button for a model (e.g. Zipformer Chinese), or use *Open archive* to select a local model zip
3. **Start recording** — click the microphone button on the *Recognition* tab; speech is transcribed in real time
4. **Copy text** — click the copy button on any result row to put it on the clipboard
5. **Hotkeys** (optional) — configure global shortcuts to trigger voice input without opening the window

### Pipeline Modes

| Mode | Description |
|------|-------------|
| ASR only | Speech → text (fastest) |
| ASR + LLM | Speech → text → LLM polish (more natural output) |
| ASR + LLM + OCR | Speech → text → LLM polish → inject at cursor |

## Building from Source

### Prerequisites

- **C++23** compiler (MSVC 2022 or later)
- **CMake** 3.21+
- **Qt 6** (Widgets, Core, Gui, Multimedia, Network, Svg, Sql)
- **vcpkg** with: libarchive, spdlog, nlohmann_json
- **sherpa-onnx** v1.13.3 — place the static release archive under `third_parties/sherpa-onnx/`

### Build

```powershell
# Configure
pwsh msvc.ps1 cmake --preset release --fresh

# Build
pwsh msvc.ps1 cmake --build build

# Run
.\build\bin\TalkInput.exe
```

### Package

The project uses CPack with NSIS to generate a Windows installer:

```powershell
pwsh msvc.ps1 cmake --build build -t package
```

The installer will be placed in `build/`.

## Project Structure

```
TalkInput/
├── src/                  # Application source code
│   ├── recognizers/      # ASR engine implementations
│   ├── windows/          # Windows-specific implementations
│   ├── linux/            # Linux-specific implementations
│   ├── macos/            # macOS-specific implementations
│   ├── scripts/          # Python helper scripts (RapidOCR)
│   └── tests/            # Unit tests
├── resources/            # Icons, QSS stylesheet, app config
├── cmake/                # CMake modules (SherpaOnnx, QtDeploy, GitInfo)
├── third_parties/        # Bundled third-party sources / binaries
├── CMakeLists.txt        # Root build file
├── CMakePresets.json     # CMake presets (base, release, relwithdebinfo)
└── msvc.ps1              # PowerShell build wrapper
```

## Contributing

Please read `AGENTS.md` for coding conventions and build instructions. Pull requests are welcome.

## Credits

- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — the core speech recognition engine
- [QCoro](https://github.com/qcoro/qcoro) — C++20 coroutines for Qt
- [QHotkey](https://github.com/Skycoder42/QHotkey) — global hotkey registration
- [nlohmann/json](https://github.com/nlohmann/json) — JSON library
- [spdlog](https://github.com/gabime/spdlog) — logging
- [libarchive](https://www.libarchive.org/) — archive extraction

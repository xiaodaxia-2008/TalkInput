[English](README.en.md) | 简体中文

# TalkInput 语音输入法

本地语音输入工具，通过麦克风采集语音，支持 OCR 识别输入框所在窗口的文字作为上下文，结合 LLM 后处理修正识别错误，结果自动注入到任意应用程序的输入框。

## 功能

- **多引擎语音识别**: 支持多个 ASR 模型。
- **AI 润色**: 识别文本经由大语言模型进行润色修改。
- **OCR 上下文感知**: 支持截取当前焦点窗口屏幕，然后通过 OCR 识别出文字作为 LLM 上下文，提升修正准确率。

![ASR 设置](docs/imgs/talkinput_asr_setting_zh.png)

- **全局快捷键**： `ctrl+alt+空格` 随时唤醒
- **语音覆盖层**: 录音时显示浮动文字预览窗口，实时展示识别进度模式

![语音覆盖层](docs/imgs/overlay_zh.png)

- **多种输入模型切换**:
  - 🎙 (仅语音识别) 
  - 🎙✨(语音识别+AI润色) 
  - 🎙✨📄(语音识别+OCR上下文+AI润色) 
  
- **识别历史**: 所有识别结果自动存入本地 SQLite 数据库，支持浏览、复制、编辑、删除。

![识别历史](docs/imgs/talkinput_history_zh.png)

## 安装

Windows 系统可以从 [GitHub Releases](https://github.com/ZenShawn/TalkInput/releases) 下载预编译的 NSIS 安装包，运行安装即可。

MacOS / Linux 暂时需要自己从源码编译。

## 使用方法

安装完成之后，鼠标点击任意输入框，按下 `Ctrl+Alt+Space` 开始录音，再次按下，停止录音并识别，识别完成后结果会自动粘贴到输入框。

### 语音识别模型

安装包里面自带了 `Sense Voice` 模型，支持中英。预设的三个语音识别 (ASR) 模型特点如下：

| 模型 | 语言 | 流式识别 | 热词 | 特点 |
|---|---|---|---|---|
| **Paraformer** | 中、英 | ✅ | ❌ | 流式实时输出，低延迟，适合连续对话 |
| **SenseVoice** | 中英日韩粤 | ❌ | ❌ | 多语言高精度，离线识别，速度极快 |
| **FunASR Nano** | 中、英 | ❌ | ✅ | 精度最高，支持热词纠偏，速度一般 |

切换模型之后点击右侧的对号来加载模型。如果模型没有下载，会自动下载；如果下载失败，可以点击对号旁边的浏览器按钮，使用系统自带的浏览器去进行下载。下载完成后，点击中间的导入按钮，把下载后的模型加载进来就可以了。

### 文字识别（OCR) 模型

支持使用系统OCR（Windows 系统自带），或者 Tesseract OCR 。

### 大语言（LLM) 模型

支持设置任意与 OpenAI 接口兼容的大模型提供商。

按照一次输入100字左右，加上 OCR 上下文，基本上一次识别大致会消耗不到一千的 tokens，按照 deepseek-v4-flash 的价格，大概是 ￥0.001，也就是 0.1 分钱。

### 全局快捷键

| 快捷键（可自定义） | 功能 |
|---|---|
| `Ctrl+Alt+Space` | 开始/停止语音输入 |
| `Ctrl+Alt+Enter` | 循环切换识别模式： 🎙 (仅语音识别) → 🎙✨(语音识别+AI润色) → 🎙✨📄(语音识别+OCR上下文+AI润色) |

在设置界面中，鼠标点击到快捷键输入框，然后按下想要设置的快捷键，自定义快捷键。

### 音频文件识别

点击工具栏「识别文件」按钮或菜单栏选择「识别文件」，导入 WAV、MP3、M4A 等常见格式的音频文件，识别结果将显示在窗口中并存入历史记录。

## 开发

### 项目结构

```
src/                 — 应用源码
  recognizers/       — ASR 引擎实现（Paraformer / SenseVoice / FunASR / System）
  windows/           — Windows 平台特定实现
  linux/             — Linux 平台特定实现
  macos/             — macOS 平台特定实现
resources/           — 图标、样式表、默认配置
cmake/               — CMake 模块
third_parties/       — 第三方库
  sherpa-onnx/       — sherpa-onnx SDK 及头文件
  KDToolBox/         — 工具库
```

### 从源码构建

**前置要求：**
- C++23 编译器（MSVC / Clang / GCC）
- [CMake](https://cmake.org/) ≥ 3.21
- [Qt 6](https://www.qt.io/) (Widgets / Core / Gui / Multimedia / Network / Svg / Sql)
- [vcpkg](https://github.com/microsoft/vcpkg)（管理 libarchive、spdlog、nlohmann-json）
- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) v1.13.3 静态库

```bash
git clone https://github.com/ZenShawn/TalkInput.git
cd TalkInput
cmake --preset release
cmake --build build
./build/bin/TalkInput
```

打包安装程序

```bash
cd build
cpack
```

> 预设文件中包含了 Qt 与 vcpkg 路径，使用前请根据你的环境修改 `CMakePresets.json`。

## 致谢

- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — 离线语音识别引擎
- [Qt](https://www.qt.io/) — 跨平台框架
- [QCoro](https://github.com/qcoro/qcoro) — C++ 协程库
- [QHotkey](https://github.com/Skycoder42/QHotkey) — 全局热键库

## 许可证

[GNU General Public License v3.0](LICENSE)

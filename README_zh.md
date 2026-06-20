# TalkInput — 桌面语音输入法

[English](README.md) | 中文

TalkInput 是一款基于 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 的桌面语音输入工具。对着麦克风说话，实时转写成文字——支持本地 LLM 润色和 OCR 光标注入。

> **开源协议**：GNU General Public License v3.0。详见 [LICENSE](LICENSE)。

## 功能特性

- **实时语音识别** — 对着麦克风说话，文字即时显示
- **多种 ASR 引擎** — 支持 Zipformer、SenseVoice、FunASR Nano、Paraformer 及系统自带引擎
- **模型管理器** — 内置模型下载器，一键安装并自动解压
- **LLM 后处理** — 使用本地 LLM 服务对识别文本进行润色纠错
- **OCR 文本注入** — 截取当前焦点输入框区域，在光标位置注入文本
- **全局快捷键** — 可在任意应用中触发录音，支持纯 ASR / ASR+LLM / ASR+LLM+OCR 三种模式
- **热词** — 自定义短语，提升特定领域词汇的识别准确率
- **音频文件识别** — 支持导入预录音频文件进行转写
- **识别历史** — 本地 SQLite 存储，支持搜索和导出
- **系统托盘** — 后台静默运行，托盘图标 + 悬浮窗指示录音状态
- **双语界面** — 中英文界面，运行时随时切换

## 下载

从 [Releases](https://github.com/xiaodaxia-2008/TalkInput/releases) 页面下载最新安装包。

## 系统要求

- Windows 10 / 11 64 位
- 麦克风设备
- （可选）兼容 OpenAI Chat Completions API 的本地 LLM 服务，用于文本润色

## 使用说明

1. **启动应用** — 主界面分为「识别」和「模型」两个标签页
2. **选择模型** — 切换到「模型」标签页，点击下载按钮选择模型（如 Zipformer 中文），或点击「使用压缩包」选择本地的模型 zip 文件
3. **开始录音** — 回到「识别」标签页，点击麦克风按钮开始说话，识别结果实时显示
4. **复制文字** — 点击每条结果右侧的复制按钮，文字自动存入剪贴板
5. **快捷键**（可选）— 配置全局快捷键，无需打开窗口即可触发语音输入

### 流水线模式

| 模式 | 说明 |
|------|------|
| 纯 ASR | 语音 → 文字（最快） |
| ASR + LLM | 语音 → 文字 → LLM 润色（更自然） |
| ASR + LLM + OCR | 语音 → 文字 → LLM 润色 → 注入光标位置 |

## 从源码构建

### 环境准备

- **C++23** 编译器（MSVC 2022 或更高版本）
- **CMake** 3.21+
- **Qt 6**（Widgets, Core, Gui, Multimedia, Network, Svg, Sql）
- **vcpkg**，需安装：libarchive, spdlog, nlohmann_json
- **sherpa-onnx** v1.13.3 — 将静态发布压缩包放入 `third_parties/sherpa-onnx/`

### 构建命令

```powershell
# 配置
pwsh msvc.ps1 cmake --preset release --fresh

# 构建
pwsh msvc.ps1 cmake --build build

# 运行
.\build\bin\TalkInput.exe
```

### 打包

项目使用 CPack + NSIS 生成 Windows 安装程序：

```powershell
pwsh msvc.ps1 cmake --build build -t package
```

安装包将生成在 `build/` 目录下。

## 项目结构

```
TalkInput/
├── src/                  # 应用源码
│   ├── recognizers/      # ASR 引擎实现
│   ├── windows/          # Windows 平台实现
│   ├── linux/            # Linux 平台实现
│   ├── macos/            # macOS 平台实现
│   ├── scripts/          # Python 辅助脚本（RapidOCR）
│   └── tests/            # 单元测试
├── resources/            # 图标、QSS 样式表、应用配置
├── cmake/                # CMake 模块（SherpaOnnx, QtDeploy, GitInfo）
├── third_parties/        # 第三方源码及二进制
├── CMakeLists.txt        # 根构建文件
├── CMakePresets.json     # CMake 预设（base, release, relwithdebinfo）
└── msvc.ps1              # PowerShell 构建包装脚本
```

## 贡献

编码规范和构建说明请参阅 `AGENTS.md`。欢迎提交 Pull Request。

## 致谢

- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — 核心语音识别引擎
- [QCoro](https://github.com/qcoro/qcoro) — Qt C++20 协程
- [QHotkey](https://github.com/Skycoder42/QHotkey) — 全局热键注册
- [nlohmann/json](https://github.com/nlohmann/json) — JSON 库
- [spdlog](https://github.com/gabime/spdlog) — 日志
- [libarchive](https://www.libarchive.org/) — 压缩包解压

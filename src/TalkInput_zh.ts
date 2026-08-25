<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh">
<context>
    <name>ApiServerSettingsWidget</name>
    <message>
        <location filename="api_server_settings_widget.ui" line="27"/>
        <source>Expose the loaded ASR and OCR providers through an OpenAI-compatible HTTP API</source>
        <translation type="unfinished">通过 OpenAI 兼容的 HTTP API 提供已加载的语音和 OCR 服务</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="28"/>
        <source>Enable local API server</source>
        <translation type="unfinished">启用本地 API 服务器</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="31"/>
        <source>Host</source>
        <translation type="unfinished">主机</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="32"/>
        <source>127.0.0.1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="33"/>
        <source>Port</source>
        <translation type="unfinished">端口</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="35"/>
        <source>Leave empty to allow requests without authentication</source>
        <translation type="unfinished">留空则允许无认证请求</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="35"/>
        <source>API Key</source>
        <translation type="unfinished">API 密钥</translation>
    </message>
    <message>
        <location filename="api_server_settings_widget.ui" line="44"/>
        <source>Local API endpoints:
GET /, /health, /healthz — health check
GET /v1/models — list the currently loaded recognition model
POST /v1/audio/transcriptions — transcribe an audio file (multipart/form-data)
POST /v1/ocr — recognize text in an image (multipart/form-data)
POST /v1/images/ocr — alias for /v1/ocr
POST /v1/audio/speech — convert text to speech (JSON)</source>
        <translation type="unfinished">本地 API 接口：
GET /、/health、/healthz —— 健康检查
GET /v1/models —— 查看当前加载的语音识别模型
POST /v1/audio/transcriptions —— 音频转文字（multipart/form-data）
POST /v1/ocr —— 图片文字识别（multipart/form-data）
POST /v1/images/ocr —— /v1/ocr 的别名
POST /v1/audio/speech —— 文字转语音（JSON）</translation>
    </message>
</context>
<context>
    <name>AppearanceSettingsWidget</name>
    <message>
        <location filename="appearance_settings_widget.ui" line="9"/>
        <source>Theme</source>
        <translation type="unfinished">主题</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="9"/>
        <source>Follow system</source>
        <translation type="unfinished">跟随系统</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="9"/>
        <source>Light</source>
        <translation type="unfinished">浅色</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="9"/>
        <source>Dark</source>
        <translation type="unfinished">深色</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="10"/>
        <source>Language</source>
        <translation type="unfinished">语言</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="10"/>
        <source>Chinese (简体中文)</source>
        <translation type="unfinished">中文（简体）</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="10"/>
        <source>English</source>
        <translation type="unfinished">英语</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="11"/>
        <source>Startup</source>
        <translation type="unfinished">启动</translation>
    </message>
    <message>
        <location filename="appearance_settings_widget.ui" line="11"/>
        <source>Start minimized</source>
        <translation type="unfinished">启动时最小化</translation>
    </message>
</context>
<context>
    <name>GeneralSettingsWidget</name>
    <message>
        <location filename="general_settings_widget.ui" line="9"/>
        <source>General</source>
        <translation type="unfinished">常规</translation>
    </message>
    <message>
        <location filename="general_settings_widget.ui" line="10"/>
        <source>Reset Settings</source>
        <translation type="unfinished">重置设置</translation>
    </message>
    <message>
        <location filename="general_settings_widget.ui" line="11"/>
        <source>Open Data Directory</source>
        <translation type="unfinished">打开数据目录</translation>
    </message>
    <message>
        <location filename="general_settings_widget.ui" line="12"/>
        <source>About</source>
        <translation type="unfinished">关于</translation>
    </message>
    <message>
        <location filename="general_settings_widget.ui" line="13"/>
        <source>Exit</source>
        <translation type="unfinished">退出</translation>
    </message>
</context>
<context>
    <name>HistoryEditDialog</name>
    <message>
        <location filename="history_edit_dialog.ui" line="6"/>
        <source>Edit Recognition Text</source>
        <translation type="unfinished">编辑识别文本</translation>
    </message>
</context>
<context>
    <name>HistoryWidget</name>
    <message>
        <location filename="history_widget.ui" line="29"/>
        <source>historyTitleLabel</source>
        <translation>historyTitleLabel</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="32"/>
        <source>Recognition History</source>
        <translation>识别历史</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="99"/>
        <source>historyClearButton</source>
        <translation>historyClearButton</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="102"/>
        <source>Clear</source>
        <translation>清除</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="56"/>
        <source>Edit text</source>
        <translation>编辑文本</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="59"/>
        <source>Edit</source>
        <translation>编辑</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="66"/>
        <source>Copy text</source>
        <translation>复制文本</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="69"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="76"/>
        <source>Delete entry</source>
        <translation>删除条目</translation>
    </message>
    <message>
        <location filename="history_widget.ui" line="79"/>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
</context>
<context>
    <name>LlmSettingsWidget</name>
    <message>
        <location filename="llm_settings_widget.ui" line="21"/>
        <source>Provider</source>
        <translation type="unfinished">服务商</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="23"/>
        <source>Endpoint</source>
        <translation type="unfinished">接口地址</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="24"/>
        <source>https://api.openai.com</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="25"/>
        <source>API Key</source>
        <translation type="unfinished">API 密钥</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="26"/>
        <source>sk-...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="27"/>
        <source>Model</source>
        <translation type="unfinished">模型</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="31"/>
        <source>Model name sent to the LLM service</source>
        <translation type="unfinished">发送给 LLM 服务的模型名称</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="32"/>
        <source>Fetch models from the configured endpoint</source>
        <translation type="unfinished">从配置的接口获取模型列表</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="32"/>
        <source>Refresh models</source>
        <translation type="unfinished">刷新模型</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.ui" line="41"/>
        <source>&lt;b&gt;Prompt&lt;/b&gt;&lt;br&gt;&lt;small&gt;Available variables: {{input}}, {{context}}, {{hotwords}}&lt;/small&gt;</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>LogPanel</name>
    <message>
        <location filename="log_panel.ui" line="1"/>
        <source>Clear</source>
        <translation type="unfinished">清除</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="main_window.ui" line="20"/>
        <source>TalkInput</source>
        <translation>TalkInput</translation>
    </message>
    <message>
        <location filename="main_window.ui" line="206"/>
        <source>Recognition</source>
        <translation>识别</translation>
    </message>
    <message>
        <location filename="main_window.ui" line="236"/>
        <location filename="main_window.ui" line="239"/>
        <source>Start recognition</source>
        <translation>开始识别</translation>
    </message>
    <message>
        <location filename="main_window.ui" line="248"/>
        <source>Recognize file</source>
        <translation>识别文件</translation>
    </message>
    <message>
        <location filename="main_window.ui" line="251"/>
        <source>Import an audio file for recognition</source>
        <translation>导入音频文件进行识别</translation>
    </message>
</context>
<context>
    <name>ModelDownload</name>
    <message>
        <location filename="model_download.cpp" line="32"/>
        <source>Multilingual</source>
        <translation>多语言</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="43"/>
        <source>Real-time</source>
        <translation>实时</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="44"/>
        <source>Offline</source>
        <translation>离线</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="93"/>
        <source>Model preset is invalid.</source>
        <translation>模型预设无效。</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="100"/>
        <source>Failed to create model cache directory.</source>
        <translation>无法创建模型缓存目录。</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="128"/>
        <source>Downloading %1 model: %2 … %3%</source>
        <translation>正在下载 %1 模型：%2 … %3%</translation>
    </message>
    <message>
        <location filename="model_download.cpp" line="156"/>
        <source>Extracting %1 model: %2</source>
        <translation>正在解压 %1 模型：%2</translation>
    </message>
</context>
<context>
    <name>OcrSettingsWidget</name>
    <message>
        <location filename="ocr_settings_widget.ui" line="9"/>
        <source>Provider:</source>
        <translation type="unfinished">服务商：</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.ui" line="10"/>
        <source>Recognize clipboard image</source>
        <translation type="unfinished">识别剪贴板图像</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.ui" line="10"/>
        <source>Open image and recognize</source>
        <translation type="unfinished">打开图片并识别</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.ui" line="10"/>
        <source>Copy OCR result to clipboard</source>
        <translation type="unfinished">复制 OCR 识别结果到剪贴板</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.ui" line="11"/>
        <source>OCR result will appear here</source>
        <translation type="unfinished">OCR 识别结果将显示在这里</translation>
    </message>
</context>
<context>
    <name>QHotkey</name>
    <message>
        <location filename="../build/_deps/qhotkey-src/QHotkey/qhotkey.cpp" line="294"/>
        <source>Failed to register %1. Error: %2</source>
        <translation>注册 %1 失败。错误：%2</translation>
    </message>
    <message>
        <location filename="../build/_deps/qhotkey-src/QHotkey/qhotkey.cpp" line="314"/>
        <source>Failed to unregister %1. Error: %2</source>
        <translation>注销 %1 失败。错误：%2</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="main.cpp" line="85"/>
        <source>An error occurred: %1

Reset configuration to defaults?</source>
        <translation>发生错误：%1

重置为默认配置？</translation>
    </message>
    <message>
        <location filename="main.cpp" line="93"/>
        <source>Configuration has been reset. Please restart the application.</source>
        <translation>配置已重置，请重新启动软件。</translation>
    </message>
</context>
<context>
    <name>ScrollTextDisplay</name>
    <message>
        <location filename="scroll_text_display.ui" line="7"/>
        <source>Recording...</source>
        <oldsource>Listening...</oldsource>
        <translation>正在录音...</translation>
    </message>
</context>
<context>
    <name>ShortcutSettingsWidget</name>
    <message>
        <location filename="shortcut_settings_widget.ui" line="9"/>
        <source>Shortcuts</source>
        <translation type="unfinished">快捷键</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="10"/>
        <source>Global hotkey to trigger the current active mode</source>
        <translation type="unfinished">用于触发当前模式的全局热键</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="10"/>
        <source>Global Input Method Trigger</source>
        <translation type="unfinished">全局输入法唤醒</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="11"/>
        <location filename="shortcut_settings_widget.ui" line="13"/>
        <source>Apply shortcut</source>
        <translation type="unfinished">应用快捷键</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="11"/>
        <location filename="shortcut_settings_widget.ui" line="13"/>
        <source>✓</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="12"/>
        <source>Global hotkey to cycle the active pipeline mode</source>
        <translation type="unfinished">用于循环切换当前流水线模式的全局热键</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.ui" line="12"/>
        <source>Voice Input Mode</source>
        <translation type="unfinished">语音输入模式</translation>
    </message>
</context>
<context>
    <name>SttSettingsWidget</name>
    <message>
        <location filename="stt_settings_widget.ui" line="9"/>
        <source>Model:</source>
        <translation type="unfinished">模型：</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="9"/>
        <source>Open download page in browser</source>
        <translation type="unfinished">在浏览器中打开下载页面</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="9"/>
        <source>Import downloaded model archive</source>
        <translation type="unfinished">导入已下载的模型压缩包</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="9"/>
        <source>Use this model</source>
        <translation type="unfinished">使用此模型</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="10"/>
        <source>Mode:</source>
        <translation type="unfinished">模式：</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="11"/>
        <source>💡</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="11"/>
        <source>&lt;b&gt;Hot Words&lt;/b&gt; — one per line. Saved hot words are applied by reloading the speech recognition model.</source>
        <translation type="unfinished">&lt;b&gt;热词&lt;/b&gt; —— 每行一个热词，保存热词后会重新加载语音识别模型使其生效。</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="11"/>
        <source>Save hot words and reload model</source>
        <translation type="unfinished">保存热词并重新加载模型</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Use clipboard to paste text</source>
        <translation type="unfinished">使用剪贴板粘贴文本</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Use Clipboard</source>
        <translation type="unfinished">使用剪贴板</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Copy result to clipboard</source>
        <translation type="unfinished">将结果复制到剪贴板</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Copy to Clipboard</source>
        <translation type="unfinished">复制到剪贴板</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Restore original clipboard content after paste</source>
        <translation type="unfinished">粘贴后还原剪贴板原有内容</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Restore Clipboard</source>
        <translation type="unfinished">还原剪贴板</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Save OCR context screenshot locally for debugging</source>
        <translation type="unfinished">将 OCR 上下文截图保存到本地以供调试</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Save Screenshot</source>
        <translation type="unfinished">保存截图</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Save recorded audio to disk for debugging</source>
        <translation type="unfinished">将录音保存到磁盘以供调试</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.ui" line="15"/>
        <source>Save Audio</source>
        <translation type="unfinished">保存音频</translation>
    </message>
</context>
<context>
    <name>TtsSettingsWidget</name>
    <message>
        <location filename="tts_settings_widget.ui" line="10"/>
        <source>Provider</source>
        <translation type="unfinished">服务商</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="11"/>
        <source>Voice</source>
        <translation type="unfinished">音色</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="11"/>
        <source>Voice name, e.g. zh-CN-XiaoxiaoNeural</source>
        <translation type="unfinished">音色名称，例如 zh-CN-XiaoxiaoNeural</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="12"/>
        <source>Model</source>
        <translation type="unfinished">模型</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="13"/>
        <source>Preview</source>
        <translation type="unfinished">试听</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="13"/>
        <source>Enter text to synthesize</source>
        <translation type="unfinished">输入要转换的文本</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="13"/>
        <source>Convert to speech</source>
        <translation type="unfinished">转换为语音</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="13"/>
        <source>Play</source>
        <translation type="unfinished">播放</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.ui" line="13"/>
        <source>Save MP3</source>
        <translation type="unfinished">保存 MP3</translation>
    </message>
</context>
<context>
    <name>VoiceOverlay</name>
    <message>
        <location filename="voice_overlay.ui" line="5"/>
        <source>TalkInput</source>
        <translation type="unfinished">TalkInput</translation>
    </message>
    <message>
        <location filename="voice_overlay.ui" line="9"/>
        <source>🎙</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>talkinput::HistoryWidget</name>
    <message>
        <location filename="history_widget.cpp" line="225"/>
        <source>Updated</source>
        <translation>已更新</translation>
    </message>
    <message>
        <location filename="history_widget.cpp" line="246"/>
        <source>Copied</source>
        <translation>已复制</translation>
    </message>
    <message>
        <location filename="history_widget.cpp" line="270"/>
        <source>Deleted</source>
        <translation>已删除</translation>
    </message>
    <message>
        <location filename="history_widget.cpp" line="280"/>
        <source>Clear History</source>
        <translation>清除历史</translation>
    </message>
    <message>
        <location filename="history_widget.cpp" line="281"/>
        <source>Are you sure you want to clear all recognition history?</source>
        <translation>确定要清除所有识别历史吗？</translation>
    </message>
    <message>
        <location filename="history_widget.cpp" line="289"/>
        <source>History cleared</source>
        <translation>历史已清除</translation>
    </message>
</context>
<context>
    <name>talkinput::LlmPostProcessor</name>
    <message>
        <location filename="llm_post_processor.cpp" line="182"/>
        <source>LLM post-processing failed; using original text.</source>
        <translation>LLM 后处理失败；使用原始文本。</translation>
    </message>
    <message>
        <location filename="llm_post_processor.cpp" line="184"/>
        <source>LLM post-processing complete.</source>
        <translation>LLM 后处理完成。</translation>
    </message>
</context>
<context>
    <name>talkinput::LlmSettingsWidget</name>
    <message>
        <location filename="llm_settings_widget.cpp" line="82"/>
        <source>LLM endpoint saved</source>
        <translation>LLM 接口地址已保存</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="93"/>
        <source>LLM model saved</source>
        <translation>LLM 模型已保存</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="107"/>
        <source>LLM API key saved</source>
        <translation>LLM API 密钥已保存</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="176"/>
        <source>LLM provider saved: %1</source>
        <translation>LLM 服务商已保存：%1</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="225"/>
        <source>Invalid LLM endpoint</source>
        <translation>大模型接口地址无效</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="248"/>
        <source>Failed to refresh models: %1</source>
        <translation>刷新模型失败：%1</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="257"/>
        <location filename="llm_settings_widget.cpp" line="294"/>
        <source>Model list response is invalid</source>
        <translation>模型列表响应格式无效</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="277"/>
        <source>No models returned by endpoint</source>
        <translation>接口未返回模型</translation>
    </message>
    <message>
        <location filename="llm_settings_widget.cpp" line="291"/>
        <source>Models refreshed: %1</source>
        <translation>模型已刷新：%1 个</translation>
    </message>
</context>
<context>
    <name>talkinput::MainWindow</name>
    <message>
        <location filename="main_window.cpp" line="593"/>
        <source>About TalkInput</source>
        <translation>关于 TalkInput</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="594"/>
        <source>&lt;h3&gt;TalkInput %1&lt;/h3&gt;&lt;p&gt;Local voice input method.&lt;/p&gt;&lt;table&gt;&lt;tr&gt;&lt;td&gt;Commit&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Date&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;/table&gt;</source>
        <translation>&lt;h3&gt;TalkInput %1&lt;/h3&gt;&lt;p&gt;本地语音输入法。&lt;/p&gt;&lt;table&gt;&lt;tr&gt;&lt;td&gt;提交&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;日期&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;/table&gt;</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="421"/>
        <source>Show Window</source>
        <translation>显示窗口</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="424"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="498"/>
        <source>Speech recognition</source>
        <translation>语音识别</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="447"/>
        <location filename="main_window.cpp" line="448"/>
        <source>Stop recognition</source>
        <translation>停止识别</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="291"/>
        <location filename="main_window.cpp" line="333"/>
        <source>Text Recognition (OCR)</source>
        <translation>OCR 文字识别</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="294"/>
        <location filename="main_window.cpp" line="334"/>
        <source>LLM Configuration</source>
        <translation>LLM 大模型配置</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="297"/>
        <location filename="main_window.cpp" line="335"/>
        <source>Speech Synthesis (TTS)</source>
        <translation>TTS 语音合成</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="450"/>
        <source>Listening...</source>
        <translation>正在听写...</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="451"/>
        <source>Listening — %1</source>
        <translation>正在听写 — %1</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="456"/>
        <location filename="main_window.cpp" line="457"/>
        <source>Start recognition</source>
        <translation>开始识别</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="499"/>
        <source>Speech recognition model is still loading.

Please wait for it to load, then try again.</source>
        <translation>语音识别模型仍在加载。

请等待加载完成后重试。</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="459"/>
        <source>No speech recognition model selected</source>
        <translation>未选择语音识别模型</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="288"/>
        <location filename="main_window.cpp" line="332"/>
        <source>Speech Recognition (STT)</source>
        <oldsource>Speech Recognition</oldsource>
        <translation>STT 语音识别</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="286"/>
        <location filename="main_window.cpp" line="331"/>
        <source>Services</source>
        <translation>服务</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="300"/>
        <location filename="main_window.cpp" line="336"/>
        <source>API Server</source>
        <translation>API 服务器</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="304"/>
        <location filename="main_window.cpp" line="337"/>
        <source>Shortcuts</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="306"/>
        <location filename="main_window.cpp" line="338"/>
        <source>Appearance</source>
        <translation>外观</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="309"/>
        <location filename="main_window.cpp" line="339"/>
        <source>History</source>
        <translation>历史</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="311"/>
        <location filename="main_window.cpp" line="340"/>
        <source>Log</source>
        <translation>日志</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="313"/>
        <location filename="main_window.cpp" line="341"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="462"/>
        <source>Speech recognition model: %1</source>
        <translation>语音识别模型：%1</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="510"/>
        <source>Select Audio File</source>
        <translation>选择音频文件</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="511"/>
        <source>Audio Files (*.wav *.mp3 *.ogg *.flac *.m4a *.aac *.opus);;All Files (*)</source>
        <translation>音频文件 (*.wav *.mp3 *.ogg *.flac *.m4a *.aac *.opus);;所有文件 (*)</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="517"/>
        <source>Decoding audio...</source>
        <translation>正在解码音频...</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="522"/>
        <source>Failed to decode audio file.</source>
        <translation>音频文件解码失败。</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="538"/>
        <source>Recognition sent to ASR engine</source>
        <translation>已发送到 ASR 引擎</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="558"/>
        <location filename="main_window.cpp" line="569"/>
        <source>Reset Settings</source>
        <translation>重置设置</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="559"/>
        <source>Reset all user settings in this file to bundled defaults?

%1

Model downloads and recognition history will not be deleted.</source>
        <translation>将此文件中的所有用户设置重置为默认值？

%1

模型下载和识别历史不会被删除。</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="570"/>
        <source>Failed to reset settings.</source>
        <translation>重置设置失败。</translation>
    </message>
    <message>
        <location filename="main_window.cpp" line="588"/>
        <source>Settings reset to defaults</source>
        <translation>设置已重置为默认</translation>
    </message>
</context>
<context>
    <name>talkinput::OcrSettingsWidget</name>
    <message>
        <location filename="ocr_settings_widget.cpp" line="192"/>
        <location filename="ocr_settings_widget.cpp" line="214"/>
        <location filename="ocr_settings_widget.cpp" line="321"/>
        <source>OCR image preview will appear here</source>
        <translation>OCR 图片预览将显示在这里</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="251"/>
        <source>The clipboard does not contain an image.</source>
        <translation>剪贴板中没有图像</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="260"/>
        <source>Open image</source>
        <translation>打开图片</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="261"/>
        <source>Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;All files (*)</source>
        <translation>图像 (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;所有文件 (*)</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="268"/>
        <source>Failed to open image.</source>
        <translation>打开图片失败</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="278"/>
        <source>OCR provider is not available.</source>
        <translation>OCR 服务不可用。</translation>
    </message>
    <message>
        <location filename="ocr_settings_widget.cpp" line="313"/>
        <source>OCR result copied</source>
        <translation>OCR 结果已复制</translation>
    </message>
</context>
<context>
    <name>talkinput::ShortcutSettingsWidget</name>
    <message>
        <location filename="shortcut_settings_widget.cpp" line="54"/>
        <source>Trigger shortcut applied</source>
        <translation>触发快捷键已应用</translation>
    </message>
    <message>
        <location filename="shortcut_settings_widget.cpp" line="63"/>
        <source>Mode switch shortcut applied</source>
        <translation>模式切换快捷键已应用</translation>
    </message>
</context>
<context>
    <name>talkinput::SttSettingsWidget</name>
    <message>
        <location filename="stt_settings_widget.cpp" line="120"/>
        <source>ASR only</source>
        <translation type="unfinished">仅语音识别</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="121"/>
        <source>ASR + AI Polish</source>
        <translation type="unfinished">语音识别 + AI 润色</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="123"/>
        <source>ASR + OCR context + AI Polish</source>
        <translation type="unfinished">语音识别 + OCR 上下文 + AI 润色</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="179"/>
        <source>Active mode changed to %1</source>
        <translation type="unfinished">当前模式已切换为 %1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="217"/>
        <source>Model not installed: %1</source>
        <translation type="unfinished">模型未安装：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="230"/>
        <source>ASR model loaded: %1</source>
        <translation type="unfinished">语音识别模型已加载：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="234"/>
        <source>ASR model load failed: %1</source>
        <translation type="unfinished">语音识别模型加载失败：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="252"/>
        <source> (Using)</source>
        <translation type="unfinished">（使用中）</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="256"/>
        <source> (Installed)</source>
        <translation type="unfinished">（已安装）</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="259"/>
        <source> (Not Installed)</source>
        <translation type="unfinished">（未安装）</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="277"/>
        <source>Model preset is invalid.</source>
        <translation type="unfinished">模型预设无效。</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="295"/>
        <source>ASR model download failed: %1</source>
        <translation type="unfinished">语音识别模型下载失败：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="325"/>
        <source>Model Already Loaded</source>
        <translation type="unfinished">模型已加载</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="326"/>
        <source>This model is already loaded. Do you want to reload it?</source>
        <translation type="unfinished">该模型已加载。是否重新加载？</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="359"/>
        <location filename="stt_settings_widget.cpp" line="381"/>
        <source>No download URL for this model.</source>
        <translation type="unfinished">该模型没有可用的下载地址。</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="388"/>
        <source>Import Model Archive</source>
        <translation type="unfinished">导入模型压缩包</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="390"/>
        <source>Archives (%1);;All files (*)</source>
        <translation type="unfinished">压缩包 (%1);;所有文件 (*)</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="399"/>
        <source>Invalid File</source>
        <translation type="unfinished">无效文件</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="400"/>
        <source>The selected file must be named:
%1

Selected:
%2</source>
        <translation type="unfinished">所选文件的名称必须是：
%1

已选择：
%2</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="407"/>
        <source>Failed to create model cache directory.</source>
        <translation type="unfinished">无法创建模型缓存目录。</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="417"/>
        <source>Failed to import model archive.</source>
        <translation type="unfinished">模型压缩包导入失败。</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="422"/>
        <source>Extracting ASR model: %1</source>
        <translation type="unfinished">正在解压语音识别模型：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="427"/>
        <source>ASR model extraction failed: %1</source>
        <translation type="unfinished">语音识别模型解压失败：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="431"/>
        <source>ASR model imported: %1</source>
        <translation type="unfinished">语音识别模型已导入：%1</translation>
    </message>
    <message>
        <location filename="stt_settings_widget.cpp" line="457"/>
        <source>Hot words saved, reloading speech recognition model...</source>
        <translation type="unfinished">热词已保存，正在重新加载语音识别模型…</translation>
    </message>
</context>
<context>
    <name>talkinput::TtsSettingsWidget</name>
    <message>
        <location filename="tts_settings_widget.cpp" line="55"/>
        <source>Edge (Online)</source>
        <translation>Edge（在线）</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="56"/>
        <source>MeloTTS (Offline)</source>
        <translation>MeloTTS（离线）</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="77"/>
        <source>TTS voice saved</source>
        <translation>TTS 音色已保存</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="110"/>
        <source>Enter text to synthesize.</source>
        <translation>请输入要转换的文本。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="129"/>
        <source>Speech conversion failed: %1</source>
        <translation>语音转换失败：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="140"/>
        <location filename="tts_settings_widget.cpp" line="145"/>
        <source>Could not prepare audio playback.</source>
        <translation>无法准备音频播放。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="170"/>
        <source>Save speech</source>
        <translation>保存语音</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="170"/>
        <source>MP3 audio (*.mp3)</source>
        <translation>MP3 音频 (*.mp3)</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="178"/>
        <source>Failed to save MP3: %1</source>
        <translation>保存 MP3 失败：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="184"/>
        <source>Failed to save MP3 file.</source>
        <translation>保存 MP3 文件失败。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="187"/>
        <source>MP3 saved.</source>
        <translation>MP3 已保存。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="238"/>
        <source>MeloTTS model installed</source>
        <translation>MeloTTS 模型已安装</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="239"/>
        <source>MeloTTS model not installed</source>
        <translation>MeloTTS 模型未安装</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="252"/>
        <source>TTS provider saved: %1</source>
        <translation>TTS 服务商已保存：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="258"/>
        <source>MeloTTS model is already installed.</source>
        <translation>MeloTTS 模型已安装。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="272"/>
        <source>TTS model download failed: %1</source>
        <translation>TTS 模型下载失败：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="277"/>
        <source>MeloTTS model installed.</source>
        <translation>MeloTTS 模型已安装。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="284"/>
        <source>No download URL for this model.</source>
        <translation>该模型没有可用的下载地址。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="298"/>
        <source>Import Model Archive</source>
        <translation>导入模型压缩包</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="300"/>
        <source>Archives (%1);;All files (*)</source>
        <translation>压缩包 (%1);;所有文件 (*)</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="309"/>
        <source>Invalid File</source>
        <translation>无效文件</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="310"/>
        <source>The selected file must be named:
%1

Selected:
%2</source>
        <translation>所选文件的名称必须是：
%1

已选择：
%2</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="317"/>
        <source>Failed to create model cache directory.</source>
        <translation>无法创建模型缓存目录。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="327"/>
        <source>Failed to import model archive.</source>
        <translation>模型压缩包导入失败。</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="332"/>
        <source>Extracting TTS model: %1</source>
        <translation>正在解压 TTS 模型：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="337"/>
        <source>TTS model extraction failed: %1</source>
        <translation>TTS 模型解压失败：%1</translation>
    </message>
    <message>
        <location filename="tts_settings_widget.cpp" line="343"/>
        <source>TTS model imported: %1</source>
        <translation>TTS 模型已导入：%1</translation>
    </message>
</context>
<context>
    <name>talkinput::VoiceInputController</name>
    <message>
        <location filename="voice_input_controller.cpp" line="230"/>
        <location filename="voice_input_controller.cpp" line="679"/>
        <source>Speech recognition model not loaded yet. Please wait or select a model.</source>
        <translation>语音识别模型尚未加载。请等待或选择模型。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="222"/>
        <location filename="voice_input_controller.cpp" line="420"/>
        <location filename="voice_input_controller.cpp" line="670"/>
        <source>Recognition is still processing.</source>
        <translation>识别仍在处理中。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="357"/>
        <source>Recording...</source>
        <translation>正在录音...</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="362"/>
        <location filename="voice_input_controller.cpp" line="447"/>
        <source>Recognizing...</source>
        <translation>识别中...</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="372"/>
        <source>Post-processing recognition result...</source>
        <translation>正在后处理识别结果...</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="468"/>
        <source>No microphone available</source>
        <translation>没有可用的麦克风</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="483"/>
        <source>Microphone format not supported.</source>
        <translation>不支持的麦克风音频格式。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="491"/>
        <source>Failed to start microphone</source>
        <translation>无法启动麦克风</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="578"/>
        <location filename="voice_input_controller.cpp" line="606"/>
        <source>Speech recognition model load failed: %1</source>
        <translation>语音识别模型加载失败：%1</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="719"/>
        <source>ASR engine is busy.</source>
        <translation>ASR 引擎正忙。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="723"/>
        <source>Speech recognition model not loaded yet.</source>
        <translation>语音识别模型尚未加载。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="746"/>
        <source>Speech recognition model was unloaded.</source>
        <translation>语音识别模型已被卸载。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="791"/>
        <location filename="voice_input_controller.cpp" line="807"/>
        <source>OCR provider was unloaded.</source>
        <translation>OCR 服务已卸载。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="830"/>
        <location filename="voice_input_controller.cpp" line="857"/>
        <source>OCR engine is busy.</source>
        <translation>OCR 引擎正忙。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="834"/>
        <location filename="voice_input_controller.cpp" line="861"/>
        <source>OCR provider is not available.</source>
        <translation>OCR 服务不可用。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="838"/>
        <location filename="voice_input_controller.cpp" line="865"/>
        <source>The image is empty.</source>
        <translation>图片为空。</translation>
    </message>
    <message>
        <location filename="voice_input_controller.cpp" line="367"/>
        <source>Reading focused input context...</source>
        <translation>正在读取聚焦输入上下文...</translation>
    </message>
</context>
</TS>

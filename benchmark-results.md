# 模型基准测试结果

**测试音频:** `data/demo.m4a`（33秒, 16kHz 单声道）
**热词文件:** `data/hotwords.txt`（纤维铺放机, vericut, FiberArt, QT, c++, cmake）
**基准文本:** FiberArt 是用于自动纤维铺放机路径规划、仿真的软件。然后它对标的是国外的vericut for composites。FiberArt的话，是基于QT，用c++编写的。要构建这个项目的话，要先用cmake进行一下配置，然后在build目录下运行cmake --build . 这个命令

---

## 性能对比

| 模型 | 类型 | 参数量 | 加载时间 | 推理时间 | 总计 | 热词支持 |
|------|------|--------|---------|---------|------|---------|
| SenseVoice | offline | ~230M | 1.11s | 0.92s | **2.03s** | ❌ 仅greedy_search |
| FireRedAsr v2 CTC | offline | ~740M | 1.46s | 12.80s | 14.26s | ✅ hotwords_file |
| FunASR Nano | offline | ~1B | 5.61s | 6.96s | 12.57s | ✅ hotwords_file |
| Qwen3-ASR 0.6B | offline | ~820M | 3.48s | 10.94s | 14.41s | ✅ 内联逗号分隔 |
| Streaming Zipformer | online | ~300M | N/A | 9.68s | 9.68s | ✅ hotwords_buf |
| Moonshine v2 zh | offline | ~135M | 0.78s | 0.50s | 1.29s | ❌ ONNX损坏 |

## 转写质量对比

| 字段 | 基准 | SenseVoice | FireRedAsr CTC | FunASR Nano | Qwen3-ASR | Transducer |
|------|------|-----------|---------------|-------------|-----------|------------|
| **FiberArt** | FiberArt | fib art | FT | FIBRART | **Fiber Art** 🟡 | 发布二/三八二 ❌ |
| **纤维铺放机** | 纤维铺放机 | 纤维放机 ❌ | 纤维放 ❌ | 纤维播放 ❌ | **纤维铺放机** ✅ | 纤维布放 ❌ |
| **vericut** | vericut | very cut | VERY CARD 🆖 | **very cut** 🟡 | VeryCut 🟡 | VCAFORCOMPOSIC ❌ |
| **for composites** | for composites | forcomp ❌ | FOR COMPOS 🆖 | for composites ✅ | **for Composites** 🟡 | FORCOMPOSIC ❌ |
| **QT** | QT | QT ✅ | QT ✅ | QT ✅ | **QT** ✅ | QT ✅ |
| **c++** | c++ | C加加 🟡 | C加加 🟡 | **C++** ✅ | C加加 🟡 | C加 🟡 |
| **cmake** | cmake | cm ❌ | MI ❌ | Cmake 🟡 | **CMake** 🟡 | C贝卡/CMUCOBI ❌ |
| **--build .** | --build . | 杠杠build 🟡 | I杠杠BUILL 🟡 | **杠杠build** 🟡 | 杠杠build 🟡 | 杠杠BO ❌ |
| **标点** | 有标点 | ✅ | ❌ | ✅ 部分 | ✅ | ❌ |
| **大小写** | 正确 | 小写 🟡 | 全大写 ❌ | 全大写 ❌ | **首字母大写** 🟡 | 混乱 ❌ |

## 详细输出

### SenseVoice (2.03s)
```
fib art是用于自动纤维放机路径规划仿真的软件，然后它对标的是国外的very cut forcomp
fi art的话是基于QT，然后用C加加编写的要构建这个项目的话，需要先用cm进行一下配置，
然后在build目录下去运行cm杠杠build点这个命令。
```

### FireRedAsr v2 CTC (14.26s)
```
FT是用于自动纤维放及路径规划仿针的软件然后它对标的是国外的 VERY CARD FOR COMPOS
FBT的话是基于 QT然后用 C加加编写的要构建这个项目的话需要先用MI进行一下配置
然后在 B的目录下去运行I杠杠 BUILL点这个命令
```

### FunASR Nano (12.57s)
```
FIBRART是用于自动纤维播放及路径规划仿真的软件，然后它对标。或者是国外的 very cut
for composites，herbert 的话是基于 QT 然后用 C++编写的。要构建这个项目的话，
需要先用Cmake进行一下配置，然后在build目录下去运行Cmake杠杠build点。这个命令。
```

### Qwen3-ASR 0.6B + 热词 (14.41s)
```
Fiber Art是用于自动纤维铺放机路径规划仿真的软件。然后它对标的是国外的
VeryCut for Composites。Fiber Art的话是基于QT，然后用C加加编写的。
呃，要构建这个项目的话，需要先用CMake进行一下配置，然后在build目录下去运行
CMake杠杠build点这个命令。
```

### Streaming Zipformer (9.68s)
```
发布二的是用于自动纤维布放及路径规划仿真的软件然后它对标的是国外的VCAFORCOMPOSIC
的话是基于QT然后用C加编写的嗯要构建这个项目的话需要先用C贝卡进行一下配置
然后在BEL的目录下去运行CMUCOBI的点这个命令
```

### Moonshine v2 zh ❌
```
这 个 命 令
```
ONNX Runtime `Add` 广播错误（`4 by 1248`），模型以固定形状导出，不支持变长音频。

## 结论

| 排名 | 模型 | 评分 | 理由 |
|------|------|------|------|
| 🥇 | **Qwen3-ASR 0.6B** | ⭐⭐⭐⭐⭐ | 准确率最高——"纤维铺放机"正确，"Fiber Art"接近，完整标点，热词生效 |
| 🥈 | **FunASR Nano** | ⭐⭐⭐⭐ | 转写流畅带标点，"C++"正确，速度快于 Qwen3（6.96s vs 10.94s） |
| 🥉 | **SenseVoice** | ⭐⭐⭐ | 速度最快（0.92s推理），无热词支持，英文部分较差 |
| 4 | **Streaming Zipformer** | ⭐⭐ | 支持流式，但准确率低，英文品牌完全错乱 |
| 5 | **FireRedAsr CTC** | ⭐⭐ | 全大写英文，中文错字多（仿针→仿真） |
| ❌ | **Moonshine v2 zh** | - | ONNX导出损坏，无法使用 |

# DolbyRouter 文档索引

让**选定应用**的音频走「普通混音链」，从而挂上小米的 **杜比（Dolby DAP）音质音效**。
纯 native 实现：KernelSU / Zygisk 模块在目标进程内 inline hook，统一覆盖 Java / OpenSL ES / AAudio。

目标设备：小米平板 6（codename `liuqin`，vendor SKU `cape`，Android 15，KernelSU Next + Zygisk Next）。

## 文档列表

| 编号 | 文档 | 内容 |
| --- | --- | --- |
| 00 | [本文件](README.md) | 索引与阅读顺序 |
| 01 | [01-背景与目标.md](01-背景与目标.md) | 需求、机制结论、交付形态、目标设备、非目标 |
| 02 | [02-音频栈与Dolby机制.md](02-音频栈与Dolby机制.md) | API → AudioFlinger 链路、FAST 来源、OpenSL 契约、Dolby DAP 挂载 |
| 03 | [03-技术方案.md](03-技术方案.md) | Zygisk 架构、配置模型、收口点选择、已知限制 |
| 04 | [04-已实现功能.md](04-已实现功能.md) | 目录结构、源文件职责、已验证行为、修复过的问题 |
| 05 | [05-待办与验证.md](05-待办与验证.md) | 验证实验、待办、风险回退、方案决策（F 节） |
| 06 | [06-构建与部署.md](06-构建与部署.md) | 主机环境、构建/部署命令、调试命令 |
| 07 | [07-参考与来源.md](07-参考与来源.md) | AOSP 源码链接、第三方项目、离线 API 文档、设备符号偏移 |

## 阅读顺序建议

- 想快速了解**要做什么** → 01
- 想理解**为什么这样做、底层发生了什么** → 02
- 想了解**方案怎么落地** → 03
- 想接手/继续开发 → 04 → 05
- 想编译安装调试 → 06

## 项目根目录

即本仓库根目录（含 `docs/`、`app/`、`zygisk/`）。

## 术语表

| 术语 | 含义 |
| --- | --- |
| Dolby DAP | `/vendor/lib64/soundfx/libhwdap.so` 提供的硬件音效，UUID `9d4921da-8225-4f29-aefa-39537a04bcaa`，在 mixer 中显示为 `DAP_offload` |
| 普通混音链 / 普通轨 | 非 FAST 的 MIXER 输出线程（本机 deep_buffer `AudioOut_15`），会跑 effect chain |
| FAST / low-latency | `AUDIO_OUTPUT_FLAG_FAST (0x4)`，走 FastMixer 直写 HAL，绕过 effect chain |
| 收口点 | `AudioTrack::createTrack_l()`，三条 API 共用、又能在不改语义前提下清 mFlags 的位置 |
| Zygisk module | KernelSU Next 上由 Zygisk Next 加载的注入模块（`zygisk/arm64-v8a.so`） |
| 二值语义 | 命中名单 = 强制挂杜比（`force_normal`）；未命中 = 放行（`default`） |

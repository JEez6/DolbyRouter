# DolbyRouter v2.0.0

让**选定应用**挂上小米平板 6 的 **杜比（Dolby DAP / `DAP_offload`）**音效。
纯 native 实现：Zygisk 模块在目标进程内 inline hook，统一覆盖 **Java AudioTrack / OpenSL ES / AAudio**。

## 下载

| 文件 | 说明 |
| --- | --- |
| `DolbyRouter-zygisk.zip` | Zygisk 模块（在 KernelSU / Magisk 里刷入后**重启**） |
| `DolbyRouter-ui.apk` | 配置 UI，勾选要挂杜比的应用（`versionCode 200` / `v2.0.0`） |

## 安装

1. 确保设备已启用 **KernelSU Next（或 Magisk）+ Zygisk Next**。
2. 刷入 `DolbyRouter-zygisk.zip`，**重启设备**。
3. 安装 `DolbyRouter-ui.apk`，打开后勾选需要挂杜比的应用。
4. 杀掉目标应用重开即可生效（改配置无需重启）。

## 原理

杜比 DAP 挂在**普通混音轨**（本机 `AudioOut_15`, deep_buffer）的 effect chain 上；
**FAST 轨**由 FastMixer 直写 HAL，绕过 effect chain。模块在 `AudioTrack::createTrack_l()`
入口清掉 `mFlags` 的 `FAST(0x4)` 与 `PRIMARY(0x2)`，让选定应用回落到普通轨，从而经过 Dolby DAP。

不用 Xposed / LSPosed。

## 已知问题

- **实时低延迟 OpenSL 游戏会卡顿/慢放**（如喵斯快跑 `com.prpr.musedash.TapTap`），其 BufferQueue 依赖 FAST 时序 —— 该应用不支持，请放名单外。
- 仅支持 `arm64-v8a` 应用。
- 依赖固定偏移 `mFlags @ this+0x378`，系统升级后需重新核对。

## 测试环境

Xiaomi Pad 6 (`liuqin`, Android 15 / SDK 35)，KernelSU Next `ksud 3.4.0`，Zygisk Next `1.5.0 (843)`，实测通过。

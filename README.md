# DolbyRouter

让**选定的应用**挂上小米平板 6 的**杜比（Dolby DAP / `DAP_offload`）**音质音效。

纯 native 实现：一个 **Zygisk 模块**在目标应用进程内 inline hook，统一覆盖 **Java AudioTrack / OpenSL ES / AAudio** 三条播放路径；配一个极简 **配置 UI APK** 用来勾选应用。不依赖 Xposed / LSPosed。

> 目标设备：小米平板 6（codename `liuqin`，vendor SKU `cape`，Android 15，KernelSU Next + Zygisk Next）。
> 仅 `arm64-v8a`。

---

## 1. 为什么普通播放没杜比？

小米系统把 **Dolby DAP** 作为硬件音效（`/vendor/lib64/soundfx/libhwdap.so`，UUID `9d4921da-8225-4f29-aefa-39537a04bcaa`）挂在**普通混音输出线程**（本机 `AudioOut_15`，`AUDIO_OUTPUT_FLAG_DEEP_BUFFER`）的 effect chain 上。

而 **FAST 轨**（`AUDIO_OUTPUT_FLAG_FAST = 0x4`）由 `FastMixer` 直接写 HAL，**绕过 effect chain**，因此听不到杜比。

决定有没有杜比的不是「用哪个 API」，而是**音频轨最终落到 FAST 输出还是普通输出**：

| 播放路径 | 默认落点 | 杜比 |
| --- | --- | --- |
| Java AudioTrack（普通） | 普通轨 `AudioOut_15` | ✅ |
| OpenSL ES（BufferQueue，默认） | 普通轨 `AudioOut_15` | ✅ |
| Java AudioTrack（`PERFORMANCE_MODE_LOW_LATENCY`） | FAST 轨 | ❌ |
| OpenSL ES（`SL_ANDROID_PERFORMANCE_LATENCY` / `LATENCY_EFFECTS`） | FAST 轨 | ❌ |
| AAudio（默认 `SHARED`） | FAST 轨 | ❌ |

所以思路很简单：**把选中应用的音频轨强制变回普通轨**，它就会经过 Dolby DAP。

---

## 2. 怎么实现的

### 收口点：`AudioTrack::createTrack_l()`

客户端侧决定 FAST 的根因是 `AudioTrack::set()`：

```c
// system/media/audio/include/system/audio.h
if (audio_flags & AUDIO_FLAG_LOW_LATENCY)  // 0x100
    *flags |= AUDIO_OUTPUT_FLAG_FAST;       // 0x4
```

但我们**不在** `set()` 层改，原因是 OpenSL 的 BufferQueue 播放器传入 `notificationFrames = -mNumBuffers`，这是**仅 FAST 合法**的契约；在 `set()` 清 FAST（或把 `-N` 归零）会触发校验失败或破坏 refill 节奏，导致**慢放 / 卡顿**。

改为在 `AudioTrack::createTrack_l()` 入口清 `mFlags`：此时 `set()` 已完成校验、`mFlags` 已定，是**唯一既能去 FAST 又不改语义**、且三条 API 共用的时点。

| Hook 目标 | 所在库 | 动作 |
| --- | --- | --- |
| `android::AudioTrack::createTrack_l()` | `libaudioclient.so` | `mFlags(this+0x378) &= ~(FAST\|PRIMARY)` |
| `aaudio::AudioGlobal_getMMapPolicy()` | `libaaudio_internal.so` | 返回 `0`（NEVER），强制 AAudio 回落 legacy AudioTrack |

- `mFlags` 偏移 `0x378`、符号名、`flags` 寄存器位置均对设备 `/system/lib64/libaudioclient.so` 反汇编核对。
- 清 `PRIMARY(0x2)` 是因为 AAudio legacy 轨带 PRIMARY，只清 FAST 仍会落 `AudioOut_D`（PRIMARY\|FAST 输出）。
- inline hook 框架：[shadowhook](https://github.com/bytedance/android-inline-hook)（静态编入）。

### 模块与配置

```
┌────────────────────────────────────────────────────────────┐
│ com.dolbyrouter APK（仅配置 UI）                             │
│   勾选应用 → /data/data/com.dolbyrouter/shared_prefs/        │
│              config.xml（镜像为 world-readable）             │
├────────────────────────────────────────────────────────────┤
│ DolbyRouter Zygisk 模块（zygisk/arm64-v8a.so）              │
│   root companion：读 config.xml，按包名匹配                  │
│   preAppSpecialize：命中 → 该进程内 inline hook              │
│     createTrack_l() 清 FAST|PRIMARY → 普通轨 → 挂 DAP        │
│     AudioGlobal_getMMapPolicy() → 0 → 回落 legacy            │
└────────────────────────────────────────────────────────────┘
```

---

## 3. 安装

前置：设备已有 **KernelSU Next**（或 Magisk）且启用 **Zygisk Next**。

### 3.1 安装模块

1. 从 [Releases](../../releases) 下载 `DolbyRouter-zygisk.zip`。
2. 在 KernelSU 管理器里作为模块安装，**重启设备**。
3. 验证（可选，需 root）：
   ```bash
   su -c "ksud module list"   # dolbyrouter enabled=true
   su -c "znctl status"       # modules 列表含 dolbyrouter
   ```

### 3.2 安装配置 UI

从 [Releases](../../releases) 下载 `DolbyRouter-ui.apk` 并安装（或自行构建，见第 5 节）。

### 3.3 配置

打开 **DolbyRouter**：

- 顶部总开关 = 模块总开关。
- 点「添加应用」勾选需要挂杜比的应用 → 保存。
- 勾选即「强制普通轨（挂杜比）」；取消即「放行（原样）」。

> 改配置**不需要重启**：模块在每次进程启动时现读配置。只需杀掉目标应用重开。

**无 UI 手动配置**（调试用）：模块目录 `/data/adb/modules/dolbyrouter/perapp.conf`：

```
enabled=1
app=com.netease.cloudmusic
app=tv.danmaku.bili
```

---

## 4. 已知问题 / 限制

| 问题 | 说明 | 处理 |
| --- | --- | --- |
| **实时低延迟 OpenSL 游戏会卡顿/慢放** | 如**喵斯快跑** `com.prpr.musedash.TapTap`：其 OpenSL BufferQueue 依赖 FAST 轨的 notification/RT 时序，转普通轨后不同步 | 该应用**不支持**，放名单外 |
| 仅 `arm64-v8a` | 模块只带 arm64 库 | 32 位应用不覆盖 |
| 依赖固定偏移 | `mFlags @ this+0x378` | 系统升级若改 `libaudioclient.so` 布局需重新核对 |
| 强制普通轨增加延迟 | 普通轨缓冲比 FAST 大 | 对音游/实时应用不友好 |
| AAudio MMAP | 通过 `getMMapPolicy=NEVER` 回落 | 若应用绕过该符号则需另找收口点 |

---

## 5. 从源码构建

### 环境

| 项 | 值 |
| --- | --- |
| Android SDK | 含 `build-tools 36.1.0` |
| NDK | r27c（`27.2.12479018`） |
| 主机 | `cmake` + `ninja`（构建 native） |
| JDK | 21（构建 APK：`JAVA_HOME=/usr/lib/jvm/java-21-openjdk`） |

### Native 模块

```bash
cd zygisk
bash build.sh      # -> out/zygisk/arm64-v8a.so
bash package.sh    # -> out/DolbyRouter-zygisk.zip（可刷）
```

### 配置 UI

```bash
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
./gradlew :app:assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

### 开发部署（手动）

```bash
adb push zygisk/out/zygisk/arm64-v8a.so /data/local/tmp/dr.so
adb shell su -c "cp /data/local/tmp/dr.so /data/adb/modules/dolbyrouter/zygisk/arm64-v8a.so \
  && chmod 0644 /data/adb/modules/dolbyrouter/zygisk/arm64-v8a.so \
  && chcon u:object_r:system_file:s0 /data/adb/modules/dolbyrouter/zygisk/arm64-v8a.so"
adb reboot         # 换 .so 必须重启（zygiskd 只在 post-fs-data 重扫模块）
```

### 调试

```bash
adb logcat -d -s DolbyRouter:V                 # 模块日志
adb shell dumpsys media.audio_flinger          # 看轨落点 / FastMixer / DAP_offload
```

命中并生效时日志形如：

```
companion: proc='com.netease.cloudmusic' enabled=1 matched=1 ...
activate 'com.netease.cloudmusic': strip_fast=1 block_mmap=1
hook AudioTrack::createTrack_l ok
hook AudioGlobal_getMMapPolicy ok
```

---

## 6. 目录结构

```
DolbyRouter/
├── README.md
├── docs/                     # 详细文档（见下）
├── api-docs/                 # 离线参考源码与 API 文档
│   ├── libaudioclient/       #   AOSP media/libaudioclient 源（AudioTrack.cpp 等）
│   ├── KSU/                  #   KernelSU-Next API 文档
│   └── LSP/                  #   Xposed/LSPosed api-102（历史参考）
├── app/                      # 配置 UI APK
└── zygisk/                   # Zygisk native 模块
    ├── jni/
    │   ├── src/dolby_router.cpp
    │   ├── src/abi.h
    │   ├── zygisk.hpp
    │   └── third_party/shadowhook/
    ├── module.prop
    ├── customize.sh
    ├── build.sh
    └── package.sh
```

详细文档见 [`docs/README.md`](docs/README.md)，参考链接与来源见 [`docs/07-参考与来源.md`](docs/07-参考与来源.md)。

---

## 7. License

仅供学习研究，请遵守当地法律法规与相关软件许可。第三方组件（shadowhook、zygisk.hpp、AOSP 源码）版权归各自作者。

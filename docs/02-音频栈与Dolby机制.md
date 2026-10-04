# 02 音频栈与 Dolby 机制

本文件说明**为什么把 FAST 轨改成普通轨能挂上小米杜比**，以及本机实测到的音频栈事实。

## 1. 三条 API 到 AudioFlinger 的映射

```
应用
 ├─ Java AudioTrack ─────────────┐
 ├─ OpenSL ES (libOpenSLES/wilhelm) ─┤──► AudioFlinger ──► HAL ──► 扬声器
 └─ AAudio (libaaudio / Oboe) ───┘
```

三条路径最终都进入 `AudioFlinger`，再经 `AudioPolicyService` 选择输出线程。当前设备观察到的输出线程：

| 输出线程 | 类型 | flags | 说明 |
| --- | --- | --- | --- |
| `AudioOut_D` | MIXER | `PRIMARY\|FAST` | 低延迟主混音，带 FastMixer |
| `AudioOut_15` | MIXER | `DEEP_BUFFER` | 深缓冲（普通轨） |
| `AudioOut_1D` | DIRECT | | 直通 |
| `AudioOut_25` | OFFLOAD | | 压缩直通 |
| `AudioOut_2D/3D` | TELEPHONY_TX / INCALL_MUSIC | | 通话相关 |
| `AudioOut_35` | VOIP_RX | | |

> 关键：**MIXER 类型线程（primary/deep_buffer）才会跑 effect chain**，其中 Dolby `DAP_offload` 挂在普通 mixer 输出线程上。

## 2. Dolby DAP 挂在哪里（实测）

- 生效的效果配置文件：`/vendor/etc/audio/sku_cape/audio_effects.xml`
- 生效的音频策略：`/vendor/etc/audio/sku_cape_qssi/audio_policy_configuration.xml`
  （AudioPolicyManager 日志中的 “Config source” 指向 **sku_cape_qssi**，不是 sku_cape）
- 普通输出线程的 effect chain 含：

| Effect | 名称 | UUID | 状态 |
| --- | --- | --- | --- |
| 11 | `DAP_offload` | `9d4921da-8225-4f29-aefa-39537a04bcaa`（Dolby Laboratories） | **Enabled / Active** |
| 19 | `MiSound` | `5b8e36a5-144a-4c38-b1d7-0002a5d5c51b`（Xiaomi） | Disabled（可被拉起） |

- 扬声器输出 profile：**PCM_16_BIT / 48kHz / stereo**，device port **2（Speaker）**。
- 策略 XML 里**没有显式 Dolby 条目** → DAP 由小米 framework/音频策略按 usage/device 动态挂载。

## 3. 结论：决定是否挂 Dolby 的是「FAST 轨 vs 普通轨」

AudioProbe 8 后端盲听与同期 `dumpsys media.audio_flinger` 抓取**完全一致**（见 [05](05-待办与验证.md) A3）：

| 后端 | 听感 | 实际落点 | 轨类型 | 走普通链（挂 DAP） |
| --- | --- | --- | --- | --- |
| Java AudioTrack 普通构造 / 旧构造 | **有杜比** | `AudioOut_15` | 普通轨 | ✅ |
| OpenSL ES 默认 1024 / 8192 | **有杜比** | `AudioOut_15` | 普通轨 | ✅ |
| Java AudioTrack 低延迟 | 无杜比 | `AudioOut_D` | **FAST 轨 + FastMixer** | ❌ |
| OpenSL ES `LATENCY_EFFECTS` | 无杜比 | `AudioOut_D` | **FAST 轨 + FastMixer** | ❌ |
| AAudio 低延迟 / 普通 | 无杜比 | `AudioOut_D` | **FAST 轨 + FastMixer** | ❌ |

### 3.1 FAST 的客户端来源（AOSP android-15.0.0_r1 源码 + 反汇编）

- `AudioTrack::set()` 保存 `mAttributes = *pAttributes;`，随后
  `audio_flags_to_audio_output_flags(mAttributes.flags, &flags)`：
  - `if (audio_flags & AUDIO_FLAG_LOW_LATENCY) *flags |= AUDIO_OUTPUT_FLAG_FAST;` ← **FAST 的最终来源**
  - 常量：`AUDIO_FLAG_LOW_LATENCY = 0x100`、`AUDIO_OUTPUT_FLAG_FAST = 0x4`、`AUDIO_OUTPUT_FLAG_PRIMARY = 0x2`。
- `createTrack_l()` 自身**不生成** FAST，只做「请求 FAST 但 server 不支持则清掉」，随后 `input.flags = mFlags`。
- 反汇编 `/system/lib64/libaudioclient.so`：`createTrack_l` @0xb4f84，
  `mFlags` 位于 `this+0x378`（`ldr w8,[x19,#0x378]; and w8,w8,#0xfffffffb; str w8,[x19,#0x378]`）。

### 3.2 OpenSL ES 的 notification 契约（重要，勿在 set 层乱动）

Android 15 的 OpenSL 实现在独立仓库 `frameworks/wilhelm`。`AudioPlayer_to_android.cpp`：

```c
if ((policy & AUDIO_OUTPUT_FLAG_FAST) != 0)
    notificationFrames = -pAudioPlayer->mBufferQueue.mNumBuffers;  // FAST 时传负值
else
    notificationFrames = 0;
```

- 负 `notificationFrames` 编码「每 track buffer 请求 N 次 notification」，只有 FAST 轨接受；
  `set()` 会把 -N 转成 `mNotificationsPerBufferReq`。
- 若在 `set()`/构造函数层清 FAST 或把 -N 强制改 0，会**破坏 OpenSL 的 refill 节奏**（变卡/慢放）。
- 因此收口点选在 **`createTrack_l()` 入口**：此时 `set()` 已完成校验、保留了 -N 契约，
  只把最终要发给 AudioFlinger 的 `mFlags` 里的 FAST/PRIMARY 清掉。

## 4. 全局手段实测：只改 audio_policy_configuration.xml 去掉 FAST 不够

把 `primary output` mixPort 的 `AUDIO_OUTPUT_FLAG_FAST` 去掉，经 **init mount namespace**
`bind mount` 覆盖 `/vendor/etc/audio/sku_cape_qssi/audio_policy_configuration.xml`，重启 audioserver：

- **生效确认**：`dumpsys media.audio_policy` 的 primary output 从 `0x0006` 变 `0x0002`。
- **结果**：Java 低延迟 / OpenSL `LATENCY_EFFECTS` 仍创建 **FastMixer + FAST 轨**。
- **结论**：**光去掉 policy 的 FAST 位不足以阻止客户端创建 fast 轨**，必须在客户端侧拦截。
- `/vendor` 是 erofs 只读，运行时靠 `nsenter -t 1 -m -- mount --bind` 覆盖才生效，重启即恢复。

## 5. 最终实现：Zygisk native 单点 hook

见 [03](03-技术方案.md)。收口点 `AudioTrack::createTrack_l()` 被 Java AudioTrack、libwilhelm(OpenSL)、
libaaudio(legacy) **共用**，一处覆盖三条路径。AAudio 的 MMAP 另 hook `AudioGlobal_getMMapPolicy()` 返回 0 回落 legacy。

## 6. 相关 Dolby / 音效库

`/vendor/lib64/soundfx/`：

| 库 | 作用 |
| --- | --- |
| `libhwdap.so` | 硬件 Dolby Audio Processing（`DAP_offload` 背后实现） |
| `libswgamedap.so` | 游戏场景 DAP |
| `libswvqe.so` | 语音质量增强 |
| `libswdap.so` | 软件 DAP |
| `libmisoundfx.so` | 小米音效（`MiSound`） |
| `libeffectproxy.so` | effect proxy |

Dolby 配置：`/vendor/etc/dolby/dax-default.xml`。

## 7. 其它设备事实（供调试）

- `media.stagefright.audio.deep=true`。
- 无 `tinymix`；`/system/bin/tinyplay`、`/system/bin/tinycap` 可用。
- `dumpsys media.audio_flinger` / `media.audio_policy` 可查看 track flags、输出线程、effect chain。
- 取证脚本：抓 `dumpsys media.audio_flinger`，找含目标 pid 的 track 行，其所在 thread 即落点；
  `Type` 列 `F<digit>` = FAST 轨。

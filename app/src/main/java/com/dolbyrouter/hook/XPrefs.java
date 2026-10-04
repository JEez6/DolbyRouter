package com.dolbyrouter.hook;

/**
 * 配置常量。
 *
 * 配置写入端：主应用 /data/data/com.dolbyrouter/shared_prefs/config.xml
 * 读取端：Zygisk 原生模块的 root companion（以 root 读取镜像后的 XML）。
 *
 * 配置模型（二值语义）：
 *  - enabled_global         总开关
 *  - scope_<pkg>=true       该应用纳入处理范围 = 强制走普通混音链（挂杜比）
 */
public final class XPrefs {

    public static final String PREFS_NAME = "config";
    public static final String KEY_ENABLED = "enabled_global";
    public static final String PREFIX_SCOPE = "scope_";

    private XPrefs() {
    }
}

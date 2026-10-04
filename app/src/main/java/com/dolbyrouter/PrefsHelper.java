package com.dolbyrouter;

import android.content.Context;
import android.content.SharedPreferences;

import com.dolbyrouter.hook.XPrefs;

import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.util.Map;

/**
 * 配置写入 + 权限修复工具。
 *
 * 重要（MIUI/HyperOS 坑）：
 * 本机 MIUI 会把应用的 {@link SharedPreferences} 写入**重定向**到
 * {@code /data/misc/<uuid>/prefs/<pkg>/config.xml}，导致标准路径
 * {@code /data/data/<pkg>/shared_prefs/config.xml} 永远停留在旧内容。
 * 而 LSPosed 的 XSharedPreferences 只读标准路径，于是被注入进程始终读到旧配置。
 *
 * 解决：SharedPreferences 仍作为应用自身 UI 的数据源，但每次写入后额外把整份配置
 * **直接用文件 IO 镜像**到标准路径（绕过 MIUI 重定向），并保证 world-readable。
 */
public final class PrefsHelper {

    private PrefsHelper() {
    }

    public static SharedPreferences get(Context ctx) {
        return ctx.getSharedPreferences(XPrefs.PREFS_NAME, Context.MODE_PRIVATE);
    }

    /** 读取当前已选应用集合（供模块 UI 使用）。 */
    public static java.util.Set<String> scopeSet(Context ctx) {
        java.util.Set<String> out = new java.util.HashSet<>();
        for (Map.Entry<String, ?> e : get(ctx).getAll().entrySet()) {
            if (e.getKey().startsWith(XPrefs.PREFIX_SCOPE)
                    && Boolean.TRUE.equals(e.getValue())) {
                out.add(e.getKey().substring(XPrefs.PREFIX_SCOPE.length()));
            }
        }
        return out;
    }

    public static void setEnabled(Context ctx, boolean enabled) {
        get(ctx).edit().putBoolean(XPrefs.KEY_ENABLED, enabled).apply();
        persist(ctx);
    }

    public static void setScope(Context ctx, String pkg, boolean in) {
        SharedPreferences.Editor e = get(ctx).edit();
        if (in) {
            e.putBoolean(XPrefs.PREFIX_SCOPE + pkg, true);
        } else {
            e.remove(XPrefs.PREFIX_SCOPE + pkg);
        }
        e.commit();
        persist(ctx);
    }

    /**
     * 把当前 SharedPreferences 全量镜像到标准路径，供 XSharedPreferences 读取。
     * 直接文件写入以绕过 MIUI 的 SharedPreferences 重定向。
     */
    public static void persist(Context ctx) {
        try {
            File dir = new File(ctx.getDataDir(), "shared_prefs");
            if (!dir.exists()) {
                dir.mkdirs();
            }
            File f = new File(dir, XPrefs.PREFS_NAME + ".xml");
            String xml = serialize(get(ctx));
            File tmp = new File(dir, XPrefs.PREFS_NAME + ".xml.tmp");
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                out.write(xml.getBytes("UTF-8"));
                out.flush();
                out.getFD().sync();
            }
            // 直接覆盖目标（不用 rename，避免父目录权限问题）
            try (FileOutputStream out = new FileOutputStream(f)) {
                out.write(xml.getBytes("UTF-8"));
                out.flush();
                out.getFD().sync();
            }
            tmp.delete();
            makeReadable(ctx);
            android.util.Log.e("DolbyRouter", "persist mirrored -> " + f + " len="
                    + f.length() + " keys=" + get(ctx).getAll().keySet());
        } catch (Throwable t) {
            android.util.Log.e("DolbyRouter", "persist failed", t);
        }
    }

    private static String serialize(SharedPreferences sp) {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n");
        for (Map.Entry<String, ?> en : sp.getAll().entrySet()) {
            String k = esc(en.getKey());
            Object v = en.getValue();
            if (v instanceof Boolean) {
                sb.append("    <boolean name=\"").append(k).append("\" value=\"")
                        .append(v).append("\" />\n");
            } else if (v instanceof String) {
                sb.append("    <string name=\"").append(k).append("\">")
                        .append(esc((String) v)).append("</string>\n");
            } else if (v instanceof Integer) {
                sb.append("    <int name=\"").append(k).append("\" value=\"")
                        .append(v).append("\" />\n");
            } else if (v instanceof Long) {
                sb.append("    <long name=\"").append(k).append("\" value=\"")
                        .append(v).append("\" />\n");
            } else if (v instanceof Float) {
                sb.append("    <float name=\"").append(k).append("\" value=\"")
                        .append(v).append("\" />\n");
            } else if (v != null) {
                sb.append("    <string name=\"").append(k).append("\">")
                        .append(esc(String.valueOf(v))).append("</string>\n");
            }
        }
        sb.append("</map>\n");
        return sb.toString();
    }

    private static String esc(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;").replace("\"", "&quot;");
    }

    /** LSPosed 通过 XSharedPreferences 读取，需保证文件其它用户可读。 */
    public static void makeReadable(Context ctx) {
        try {
            File data = ctx.getDataDir();
            File dir = new File(data, "shared_prefs");
            File f = new File(dir, XPrefs.PREFS_NAME + ".xml");
            data.setExecutable(true, false);
            data.setReadable(true, false);
            dir.setExecutable(true, false);
            dir.setReadable(true, false);
            f.setReadable(true, false);
            try (RandomAccessFile raf = new RandomAccessFile(f, "rw")) {
                // 兜底：确保其它用户可读位
                f.setReadable(true, false);
            } catch (Throwable ignored) {
            }
        } catch (Throwable ignored) {
        }
    }
}

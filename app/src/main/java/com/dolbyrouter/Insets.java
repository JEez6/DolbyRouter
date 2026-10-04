package com.dolbyrouter;

import android.view.View;
import android.view.WindowInsets;

/** 处理 Android 15 强制 edge-to-edge：给内容加系统栏内边距，避免被状态栏/标题遮挡。 */
public final class Insets {

    private Insets() {
    }

    public static void apply(final View v, final int extra) {
        v.setOnApplyWindowInsetsListener((view, insets) -> {
            android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
            view.setPadding(extra, extra + bars.top, extra, extra + bars.bottom);
            return insets;
        });
        v.requestApplyInsets();
    }
}

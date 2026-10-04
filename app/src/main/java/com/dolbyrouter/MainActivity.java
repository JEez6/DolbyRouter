package com.dolbyrouter;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import com.dolbyrouter.hook.XPrefs;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

/**
 * 主界面：总开关 + 应用范围列表。
 *
 * 语义已简化为二值（见 docs/03）：在范围内的应用 = 强制走普通混音链（挂杜比），
 * 不在范围内 = 放行。不再区分 OpenSL / AudioTrack / AAudio 策略。
 */
public class MainActivity extends Activity {

    private SharedPreferences prefs;
    private LinearLayout listContainer;
    private TextView emptyText;
    private Switch enableSwitch;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = PrefsHelper.get(this);

        ScrollView sv = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);
        sv.addView(root);

        TextView title = new TextView(this);
        title.setText("DolbyRouter");
        title.setTextSize(22);
        root.addView(title);

        TextView desc = new TextView(this);
        desc.setText("让选中应用的音频走普通混音链，启用小米音质音效（Dolby）。\n\n"
                + "由 KernelSU / Zygisk 原生模块在进程内统一处理 Java、OpenSL ES、AAudio 三条路径。\n\n"
                + "注意：少数实时低延迟游戏（如喵斯快跑）强制转普通轨会出现卡顿，暂不支持。");
        desc.setTextSize(13);
        desc.setPadding(0, dp(6), 0, dp(12));
        root.addView(desc);

        enableSwitch = new Switch(this);
        enableSwitch.setText("启用模块（总开关）");
        enableSwitch.setChecked(prefs.getBoolean(XPrefs.KEY_ENABLED, true));
        enableSwitch.setOnCheckedChangeListener((v, checked) -> PrefsHelper.setEnabled(this, checked));
        root.addView(enableSwitch);

        Button pick = new Button(this);
        pick.setText("选择应用范围");
        pick.setOnClickListener(v ->
                startActivity(new Intent(this, AppPickerActivity.class)));
        root.addView(pick);

        emptyText = new TextView(this);
        emptyText.setText("尚未选择任何应用");
        emptyText.setPadding(0, dp(12), 0, dp(4));
        root.addView(emptyText);

        listContainer = new LinearLayout(this);
        listContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(listContainer);

        setContentView(sv);
        Insets.apply(root, pad);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        listContainer.removeAllViews();
        Set<String> scope = PrefsHelper.scopeSet(this);
        List<String> pkgs = new ArrayList<>(scope);
        Collections.sort(pkgs);
        emptyText.setVisibility(pkgs.isEmpty() ? TextView.VISIBLE : TextView.GONE);

        for (String pkg : pkgs) {
            TextView row = new TextView(this);
            row.setText(appLabel(pkg) + "\n" + pkg + "   [强制杜比]");
            row.setTextSize(14);
            row.setPadding(dp(4), dp(10), dp(4), dp(10));
            row.setOnClickListener(v -> showRemoveDialog(pkg));
            listContainer.addView(row);
        }
    }

    private void showRemoveDialog(String pkg) {
        new AlertDialog.Builder(this)
                .setTitle(appLabel(pkg))
                .setMessage(pkg + "\n\n当前：强制走普通混音链（挂杜比）")
                .setNeutralButton("移出范围", (d, w) -> {
                    PrefsHelper.setScope(this, pkg, false);
                    refresh();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private String appLabel(String pkg) {
        try {
            return getPackageManager().getApplicationLabel(
                    getPackageManager().getApplicationInfo(pkg, 0)).toString();
        } catch (Throwable t) {
            return pkg;
        }
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}

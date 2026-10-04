package com.dolbyrouter;

import android.app.Activity;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.dolbyrouter.hook.XPrefs;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * 应用范围选择器：勾选需要走杜比（普通混音链）的应用。
 *
 * 采用「声明 QUERY_ALL_PACKAGES + 应用内多选」方案，而非逐个请求
 * {@code MANAGE_EXTERNAL_STORAGE} 之外的范围授权，避免应用被杀后丢失。
 */
public class AppPickerActivity extends Activity {

    private final Set<String> scope = new HashSet<>();
    private LinearLayout listContainer;
    private EditText filter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        scope.addAll(PrefsHelper.scopeSet(this));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(12);
        root.setPadding(pad, pad, pad, pad);

        TextView tip = new TextView(this);
        tip.setText("勾选需要启用杜比的应用（" + scope.size() + " 个已选）");
        tip.setPadding(0, 0, 0, dp(8));
        root.addView(tip);

        filter = new EditText(this);
        filter.setHint("搜索应用名或包名");
        filter.setSingleLine(true);
        filter.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH);
        filter.setInputType(android.text.InputType.TYPE_CLASS_TEXT);

        LinearLayout searchRow = new LinearLayout(this);
        searchRow.setOrientation(LinearLayout.HORIZONTAL);
        searchRow.setGravity(android.view.Gravity.CENTER_VERTICAL);
        filter.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        searchRow.addView(filter);

        Button searchBtn = new Button(this);
        searchBtn.setText("搜索");
        searchBtn.setOnClickListener(v -> doSearch());
        searchRow.addView(searchBtn);
        root.addView(searchRow);

        filter.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH
                    || actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE
                    || (event != null && event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER
                    && event.getAction() == android.view.KeyEvent.ACTION_DOWN)) {
                doSearch();
                return true;
            }
            return false;
        });

        listContainer = new LinearLayout(this);
        listContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(listContainer);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);
        Insets.apply(root, pad);

        buildList("");
    }

    private void doSearch() {
        buildList(filter.getText().toString().trim().toLowerCase());
        android.view.inputmethod.InputMethodManager imm =
                (android.view.inputmethod.InputMethodManager)
                        getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(filter.getWindowToken(), 0);
        }
    }

    private static final class Entry {
        final String pkg;
        final String label;

        Entry(String pkg, String label) {
            this.pkg = pkg;
            this.label = label;
        }
    }

    private void buildList(String q) {
        listContainer.removeAllViews();
        List<Entry> apps = loadApps();
        for (Entry e : apps) {
            if (!q.isEmpty()
                    && !e.label.toLowerCase().contains(q)
                    && !e.pkg.toLowerCase().contains(q)) {
                continue;
            }
            CheckBox cb = new CheckBox(this);
            cb.setText(e.label + "\n" + e.pkg);
            cb.setTextSize(14);
            cb.setChecked(scope.contains(e.pkg));
            cb.setOnCheckedChangeListener((v, checked) -> {
                if (checked) {
                    scope.add(e.pkg);
                    PrefsHelper.setScope(this, e.pkg, true);
                } else {
                    scope.remove(e.pkg);
                    PrefsHelper.setScope(this, e.pkg, false);
                }
            });
            listContainer.addView(cb);
        }
    }

    private List<Entry> loadApps() {
        List<Entry> out = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        android.content.pm.PackageManager pm = getPackageManager();

        // 1) 通过 <queries> MAIN/LAUNCHER 枚举可见应用。
        //    MIUI/HyperOS 会无视 QUERY_ALL_PACKAGES，导致 getInstalledPackages()
        //    只返回自身；而 MAIN/LAUNCHER 的 <queries> 可见性是厂商无法屏蔽的。
        try {
            android.content.Intent main = new android.content.Intent(
                    android.content.Intent.ACTION_MAIN);
            main.addCategory(android.content.Intent.CATEGORY_LAUNCHER);
            List<android.content.pm.ResolveInfo> ris = pm.queryIntentActivities(main, 0);
            for (android.content.pm.ResolveInfo ri : ris) {
                if (ri.activityInfo == null) {
                    continue;
                }
                String pkg = ri.activityInfo.packageName;
                if (pkg.equals(getPackageName()) || !seen.add(pkg)) {
                    continue;
                }
                boolean system = (ri.activityInfo.applicationInfo.flags
                        & android.content.pm.ApplicationInfo.FLAG_SYSTEM) != 0;
                String label;
                try {
                    label = ri.loadLabel(pm).toString();
                } catch (Throwable t) {
                    label = pkg;
                }
                out.add(new Entry(pkg, (system ? "[系统] " : "") + label));
            }
            android.util.Log.e("DolbyRouter", "launcher apps -> " + out.size());
        } catch (Throwable t) {
            android.util.Log.e("DolbyRouter", "queryIntentActivities failed", t);
        }

        // 2) 若 QUERY_ALL_PACKAGES 生效，则补充非启动器应用（如无界面的后台服务）。
        try {
            List<android.content.pm.PackageInfo> infos = pm.getInstalledPackages(0);
            for (android.content.pm.PackageInfo pi : infos) {
                String pkg = pi.packageName;
                if (pkg.equals(getPackageName()) || !seen.add(pkg)) {
                    continue;
                }
                if (pi.applicationInfo == null) {
                    continue;
                }
                boolean system = (pi.applicationInfo.flags
                        & android.content.pm.ApplicationInfo.FLAG_SYSTEM) != 0;
                String label;
                try {
                    label = pm.getApplicationLabel(pi.applicationInfo).toString();
                } catch (Throwable t) {
                    label = pkg;
                }
                out.add(new Entry(pkg, (system ? "[系统] " : "") + label));
            }
        } catch (Throwable t) {
            android.util.Log.e("DolbyRouter", "getInstalledPackages failed", t);
        }

        android.util.Log.e("DolbyRouter", "loadApps total -> " + out.size());
        Collections.sort(out, new Comparator<Entry>() {
            @Override
            public int compare(Entry a, Entry b) {
                boolean sa = a.label.startsWith("[系统]");
                boolean sb = b.label.startsWith("[系统]");
                if (sa != sb) {
                    return sa ? 1 : -1;
                }
                return a.label.compareToIgnoreCase(b.label);
            }
        });
        return out;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}

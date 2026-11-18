package com.omni.tagrepair;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.DialogInterface;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import com.omni.tagrepair.core.Classify;
import com.omni.tagrepair.core.CopyFlac;
import com.omni.tagrepair.core.Msg;
import com.omni.tagrepair.core.Rio;
import com.omni.tagrepair.core.SelfTest;
import com.omni.tagrepair.core.TagFile;
import com.omni.tagrepair.core.TagRule;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * 唯一的界面。与 Windows 版"把文件拖上去"的那种用法一样:先只读地检查、分类列出来;
 * 然后三样事(原地修复 / WAV 另存为 FLAC / 16 bit 另存 24 bit)各有一个按钮,点了先说明这件事做什么,点「开始」才动手。
 * 文件只经系统的文件选择器、OmniAudio 的插件区或者"分享"给进来;本应用没有任何权限、不联网。
 * 约束:无 AndroidX、无 lambda、Java 8。
 */
public class MainActivity extends Activity {
    static final String TAG = "tagrepair";
    static final String VERSION = "1.1.0";
    private static final int REQ_FILES = 1, REQ_TREE = 2, REQ_OUT_TREE = 3, REQ_SAVE_PENDING = 4;
    private static final String[] AUDIO_EXT = { "flac", "mp3", "wav", "m4a", "aac", "ogg", "opus", "wma", "ape", "alac", "dsf", "dff", "aiff", "aif", "mka",
            "ac3", "eac3", "ec3", "dts", "thd", "mlp", "wv", "tta", "tak", "m4b", "mp2", "oga", "w64" };

    private LinearLayout root, summary, actions, logBox;
    private TextView status;
    private ProgressBar bar;
    private Button cancelBtn;
    private Store store;
    // 进程里同时只许一个任务(检查 / 修复 / 另存 / 接着写回 / 自检):别的界面实例(分享过来、配置变了重建)也看得到
    private static volatile boolean cancel;
    private static volatile boolean running;
    private static MainActivity owner;            // 发起这个任务的界面;它被关掉时才取消

    /** 一个要处理的文件 */
    static final class Item {
        Uri uri;
        String name;
        Uri treeUri;          // 来自选的文件夹:另存的新文件放在它所在的文件夹里
        String parentDocId;
        TagFile.Scan scan;
    }

    private final List<Item> items = new ArrayList<Item>();
    private Uri outTree;                      // 不是来自文件夹的文件,另存时放到这里(用户选的)
    private CopyFlac.Kind pendingCopyKind;    // 等用户选完放新文件的文件夹再做
    private Store.Pending savingPending;

    // ---------------------------------------------------------------------------
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        store = new Store(this);
        buildUi();
        resumePending();
        whenIdle(getIntent(), 0);
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        setIntent(i);
        whenIdle(i, 0);
    }

    @Override
    protected void onDestroy() {
        if (owner == this) cancel = true;
        super.onDestroy();
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private CharSequence two(Msg.Text t) {
        SpannableStringBuilder s = new SpannableStringBuilder();
        s.append(t.zh);
        if (!t.en.equals(t.zh) && !t.en.isEmpty()) {
            int a = s.length();
            s.append('\n').append(t.en);
            s.setSpan(new ForegroundColorSpan(0xFF8FA3B3), a, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            s.setSpan(new RelativeSizeSpan(0.85f), a, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return s;
    }

    private TextView text(CharSequence s, float sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setPadding(0, dp(4), 0, dp(4));
        return t;
    }

    private Button button(Msg.Text label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setAllCaps(false);
        b.setText(label.zh + (label.en.equals(label.zh) ? "" : "  ·  " + label.en));
        b.setOnClickListener(l);
        return b;
    }

    private void buildUi() {
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(0xFF101820);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(20), dp(16), dp(24));
        sv.addView(root);
        // 安卓 15 起应用默认铺满整屏:按系统栏让出位置,标题和底下的按钮不被状态栏 / 导航栏盖住
        sv.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            public android.view.WindowInsets onApplyWindowInsets(View v, android.view.WindowInsets in) {
                root.setPadding(dp(16) + in.getSystemWindowInsetLeft(), dp(20) + in.getSystemWindowInsetTop(), dp(16) + in.getSystemWindowInsetRight(),
                        dp(24) + in.getSystemWindowInsetBottom());
                return in;
            }
        });
        setContentView(sv);

        TextView title = text("MQA标签修复 · tagrepair " + VERSION, 20, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setOnLongClickListener(new View.OnLongClickListener() {
            public boolean onLongClick(View v) { runSelfTest(); return true; }
        });
        root.addView(title);
        root.addView(text(two(Msg.of(Msg.IntroWhat)), 14, 0xFFDDE6EE));
        root.addView(text(two(Msg.of(Msg.AUiHow)), 13, 0xFFB8C6D2));
        root.addView(text(two(Msg.of(Msg.AUiPlayNote)), 13, 0xFFB8C6D2));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.addView(button(Msg.of(Msg.AUiPickFiles), new View.OnClickListener() { public void onClick(View v) { pickFiles(); } }));
        row.addView(button(Msg.of(Msg.AUiPickFolder), new View.OnClickListener() { public void onClick(View v) { pickTree(REQ_TREE); } }));
        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        Button n = button(Msg.of(Msg.AUiNotice), new View.OnClickListener() { public void onClick(View v) { showAsset(new String[] { "STATEMENT.txt" }, true); } });
        Button l = button(Msg.of(Msg.AUiLicense), new View.OnClickListener() { public void onClick(View v) { showAsset(new String[] { "NOTICE.txt", "LICENSE.txt" }, false); } });
        row2.addView(n, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        row2.addView(l, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        row.addView(row2);
        root.addView(row);

        status = text("", 13, 0xFFFFD54F);
        status.setVisibility(View.GONE);
        root.addView(status);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setVisibility(View.GONE);
        root.addView(bar);
        cancelBtn = button(Msg.of(Msg.AUiCancel), new View.OnClickListener() { public void onClick(View v) { cancel = true; } });
        cancelBtn.setVisibility(View.GONE);
        root.addView(cancelBtn);

        summary = new LinearLayout(this);
        summary.setOrientation(LinearLayout.VERTICAL);
        root.addView(summary);
        actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.VERTICAL);
        root.addView(actions);
        logBox = new LinearLayout(this);
        logBox.setOrientation(LinearLayout.VERTICAL);
        root.addView(logBox);
    }

    private void log(Msg.Text t) { logBox.addView(text(two(t), 13, 0xFFE0E0E0)); }
    private void logPlain(String s, int color) { logBox.addView(text(s, 12, color)); }

    /** 原文照登,一个字不改;几个文件之间空一行 */
    private void showAsset(String[] assets, boolean withAndroidNote) {
        StringBuilder all = new StringBuilder();
        for (String asset : assets) {
            try {
                InputStream in = getAssets().open(asset);
                ByteArrayOutputStream o = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int k;
                while ((k = in.read(buf)) > 0) o.write(buf, 0, k);
                in.close();
                String s = new String(o.toByteArray(), StandardCharsets.UTF_8);
                if (s.length() > 0 && s.charAt(0) == (char) 0xFEFF) s = s.substring(1);
                if (all.length() > 0) all.append("\n\n");
                all.append(s);
            } catch (Exception e) {
                all.append(e.toString());
            }
        }
        String body = all.toString();
        ScrollView sv = new ScrollView(this);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(16), dp(12), dp(16), dp(12));
        TextView t = new TextView(this);
        t.setText(body);                              // 原文照登,一个字不改
        t.setTextSize(12);
        t.setTextIsSelectable(true);
        box.addView(t);
        if (withAndroidNote) {
            TextView note = new TextView(this);
            note.setText(two(Msg.of(Msg.AUiAndroidNote7)));
            note.setTextSize(12);
            note.setPadding(0, dp(16), 0, 0);
            box.addView(note);
        }
        sv.addView(box);
        new AlertDialog.Builder(this).setTitle(withAndroidNote ? "声明 · NOTICE" : "LICENSE").setView(sv)
                .setPositiveButton(Msg.AUiClose.zh + " · " + Msg.AUiClose.en, null).show();
    }

    // ---------------------------------------------------------------------------
    // 文件从哪来
    // ---------------------------------------------------------------------------
    /** 刚打开时接着写回的那件事可能还在做:等它做完(最多 20 秒)再处理给进来的文件 */
    private void whenIdle(final Intent in, final int tries) {
        if (isFinishing()) return;
        if (!running) { handleIntent(in); return; }
        root.postDelayed(new Runnable() { public void run() { whenIdle(in, tries + 1); } }, 200);
    }

    /** 别的应用给进来的文件:只认 content://(file:// 之类一律不认);给的东西不对就当没给 */
    private void handleIntent(Intent in) {
        if (in == null) return;
        try {
            if (in.getBooleanExtra("selftest", false)) { runSelfTest(); return; }
            List<Uri> uris = new ArrayList<Uri>();
            String a = in.getAction();
            if (Intent.ACTION_SEND_MULTIPLE.equals(a)) {
                ArrayList<Uri> l = in.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (l != null) uris.addAll(l);
            } else if (Intent.ACTION_SEND.equals(a) || "com.omni.audio.action.PLUGIN".equals(a)) {
                Uri u = in.getParcelableExtra(Intent.EXTRA_STREAM);
                if (u != null) uris.add(u);
                else if (in.getClipData() != null && in.getClipData().getItemCount() > 0) uris.add(in.getClipData().getItemAt(0).getUri());
            }
            List<Item> list = new ArrayList<Item>();
            for (Uri u : uris) if (u != null && ContentResolver.SCHEME_CONTENT.equals(u.getScheme())) list.add(itemFor(u, null, null));
            if (!list.isEmpty()) startScan(list);
        } catch (Exception e) {
            Log.w(TAG, "ignored a malformed intent");
        }
    }

    private Item itemFor(Uri u, Uri tree, String parentDocId) {
        Item it = new Item();
        it.uri = u;
        it.treeUri = tree;
        it.parentDocId = parentDocId;
        it.name = displayName(u);
        return it;
    }

    private String displayName(Uri u) {
        Cursor c = null;
        try {
            c = getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
        } catch (Exception ignored) {
        } finally {
            if (c != null) c.close();
        }
        String s = u.getLastPathSegment();
        return s == null ? u.toString() : s;
    }

    private void pickFiles() {
        if (busy()) { toastBusy(); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "audio/*", "application/octet-stream" });
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(i, REQ_FILES); } catch (Exception e) { logPlain(e.toString(), 0xFFFF8A80); }
    }

    private void pickTree(int req) {
        if (req == REQ_TREE && busy()) { toastBusy(); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(i, req); } catch (Exception e) { logPlain(e.toString(), 0xFFFF8A80); }
    }

    private void persist(Uri u) {
        try {
            getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Exception ignored) {}
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_SAVE_PENDING) { finishSavePending(res == RESULT_OK && data != null ? data.getData() : null); return; }
        if (req == REQ_OUT_TREE) {
            CopyFlac.Kind k = pendingCopyKind;
            pendingCopyKind = null;
            if (res != RESULT_OK || data == null || data.getData() == null) { log(Msg.of(Msg.ACopyNoFolder)); return; }
            outTree = data.getData();
            persist(outTree);
            if (k != null) runCopies(k);
            return;
        }
        if (res != RESULT_OK || data == null) return;
        if (req == REQ_FILES) {
            List<Item> list = new ArrayList<Item>();
            ClipData cd = data.getClipData();
            if (cd != null) for (int i = 0; i < cd.getItemCount(); i++) { Uri u = cd.getItemAt(i).getUri(); persist(u); list.add(itemFor(u, null, null)); }
            else if (data.getData() != null) { persist(data.getData()); list.add(itemFor(data.getData(), null, null)); }
            startScan(list);
        } else if (req == REQ_TREE && data.getData() != null) {
            final Uri tree = data.getData();
            persist(tree);
            startScan(null, tree);
        }
    }

    /** 文件夹里的音频文件(连子文件夹;按扩展名认,和 Windows 版同一张表) */
    private void listTree(Uri tree, String docId, List<Item> out, int depth) {
        if (depth > 32 || cancel) return;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        Cursor c = null;
        try {
            c = getContentResolver().query(children, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null);
            List<String[]> dirs = new ArrayList<String[]>();
            while (c != null && c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) { dirs.add(new String[] { id, name }); continue; }
                if (!isAudioName(name)) continue;
                Item it = new Item();
                it.uri = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                it.name = name;
                it.treeUri = tree;
                it.parentDocId = docId;
                out.add(it);
            }
            if (c != null) { c.close(); c = null; }
            for (String[] d : dirs) listTree(tree, d[0], out, depth + 1);
        } catch (Exception e) {
            Log.w(TAG, "listing a folder failed");
        } finally {
            if (c != null) c.close();
        }
    }

    static boolean isAudioName(String name) {
        if (name == null) return false;
        int dot = name.lastIndexOf('.');
        if (dot < 0) return false;
        String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
        for (String e : AUDIO_EXT) if (e.equals(ext)) return true;
        return false;
    }

    // ---------------------------------------------------------------------------
    // 打开 / 检查
    // ---------------------------------------------------------------------------
    private FdRio open(Uri u, boolean write) {
        ContentResolver cr = getContentResolver();
        if (write) {
            try {
                ParcelFileDescriptor p = cr.openFileDescriptor(u, "rw");
                if (p != null) return new FdRio(p, true);
            } catch (Exception ignored) {}
        }
        try {
            ParcelFileDescriptor p = cr.openFileDescriptor(u, "r");
            if (p != null) return new FdRio(p, false);
        } catch (Exception ignored) {}
        return null;
    }

    private static void closeQuietly(FdRio r) { Rio.Util.closeQuietly(r); }

    private boolean busy() { return running; }

    private void toastBusy() { Toast.makeText(this, Msg.AUiBusy.zh + "\n" + Msg.AUiBusy.en, Toast.LENGTH_LONG).show(); }

    /** 开一个任务;已经有任务在跑就不开(说一声),返回 false */
    private boolean startJob(final Runnable body) {
        synchronized (MainActivity.class) {
            if (running) {
                toastBusy();
                return false;
            }
            running = true;
            cancel = false;
            owner = this;
        }
        new Thread(new Runnable() {
            public void run() {
                try { body.run(); } finally { running = false; }
            }
        }).start();
        return true;
    }

    private void setRunning(final boolean on, final Msg.Text what) {
        runOnUiThread(new Runnable() {
            public void run() {
                status.setVisibility(on ? View.VISIBLE : View.GONE);
                bar.setVisibility(on ? View.VISIBLE : View.GONE);
                cancelBtn.setVisibility(on ? View.VISIBLE : View.GONE);
                if (on) status.setText(two(what.plus(new Msg.Text("\n")).plus(Msg.of(Msg.AUiKeepOpen))));
                bar.setProgress(0);
                if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });
    }

    private void progress(final int index, final int total, final double within, final String name) {
        runOnUiThread(new Runnable() {
            public void run() {
                double whole = total == 0 ? 1 : (index + Math.max(0, Math.min(1, within))) / total;
                bar.setProgress((int) (whole * 1000));
                status.setText(String.format(Locale.ROOT, "%d / %d  %s", Math.min(index + 1, total), total, name));
            }
        });
    }

    private void ui(Runnable r) { runOnUiThread(r); }

    private void startScan(List<Item> list) { startScan(list, null); }

    private void startScan(final List<Item> given, final Uri tree) {
        if (busy()) { toastBusy(); return; }
        items.clear();
        summary.removeAllViews();
        actions.removeAllViews();
        logBox.removeAllViews();
        outTree = null;
        startJob(new Runnable() {
            public void run() {
                final List<Item> list = given != null ? given : new ArrayList<Item>();
                if (tree != null) listTree(tree, DocumentsContract.getTreeDocumentId(tree), list, 0);
                setRunning(true, Msg.of(Msg.AskScanning, Msg.num(list.size())));
                int i = 0;
                for (; i < list.size(); i++) {
                    if (cancel) break;
                    Item it = list.get(i);
                    progress(i, list.size(), 0, it.name);
                    FdRio r = open(it.uri, false);
                    if (r == null) {
                        it.scan = new TagFile.Scan();
                        it.scan.info.error = Msg.of(Msg.WhyUnreadable);
                    } else {
                        try { it.scan = TagFile.scan(r, TagRule.DEFAULT); } catch (Throwable t) { it.scan = new TagFile.Scan(); it.scan.info.error = Msg.data(t.toString()); }
                        closeQuietly(r);
                    }
                    items.add(it);
                }
                final int scanned = i, total = list.size();
                final boolean stopped = cancel;
                setRunning(false, Msg.EMPTY);
                ui(new Runnable() { public void run() { showSummary(scanned, total, stopped); } });
            }
        });
    }

    private String copyTargetName(Item it, CopyFlac.Kind k) { return CopyFlac.targetName(it.name, k); }

    private void showSummary(int scanned, int total, boolean stopped) {
        summary.removeAllViews();
        actions.removeAllViews();
        if (stopped && scanned < total) { summary.addView(text(two(Msg.of(Msg.CancelScan, Msg.num(scanned), Msg.num(total))), 14, 0xFFFFD54F)); return; }
        if (total == 0) { summary.addView(text(two(Msg.of(Msg.AUiNoFiles)), 14, 0xFFFFD54F)); return; }
        int complete = 0, noMarker = 0;
        List<Item> rep = new ArrayList<Item>(), wav = new ArrayList<Item>(), c24 = new ArrayList<Item>(), cant = new ArrayList<Item>();
        for (Item it : items) {
            switch (it.scan.state) {
            case COMPLETE: complete++; break;
            case REPAIRABLE: rep.add(it); break;
            case CONVERTIBLE: break;
            case NO_MARKER:
            case NOT_LOSSLESS: noMarker++; break;
            default: cant.add(it); break;
            }
            if (it.scan.canWavToFlac) wav.add(it);
            if (it.scan.canCopy24) c24.add(it);
        }
        summary.addView(text(two(Msg.of(Msg.AskScanned, Msg.num(total))), 15, Color.WHITE));
        summary.addView(text(two(Msg.of(Msg.AskComplete, Msg.num(complete))), 13, 0xFFC8E6C9));
        summary.addView(text(two(Msg.of(Msg.AskRepairable, Msg.num(rep.size()))), 13, 0xFFFFE082));
        for (Item it : rep) summary.addView(text("    " + it.name + "\n    " + TagFile.stateMsg(it.scan).zh, 12, 0xFFBDBDBD));
        summary.addView(text(two(Msg.of(Msg.AskWav, Msg.num(wav.size()))), 13, 0xFFFFE082));
        for (Item it : wav) summary.addView(text("    " + it.name + "  →  " + copyTargetName(it, CopyFlac.Kind.WAV_TO_FLAC), 12, 0xFFBDBDBD));
        summary.addView(text(two(Msg.of(Msg.Ask16, Msg.num(c24.size()))), 13, 0xFFFFE082));
        for (Item it : c24) summary.addView(text("    " + it.name + "  →  " + copyTargetName(it, CopyFlac.Kind.TO_24BIT), 12, 0xFFBDBDBD));
        summary.addView(text(two(Msg.of(Msg.AskNoMarker, Msg.num(noMarker))), 13, 0xFFB0BEC5));
        summary.addView(text(two(Msg.of(Msg.AskCannot, Msg.num(cant.size()))), 13, 0xFFB0BEC5));
        for (Item it : cant) summary.addView(text("    " + it.name + " — " + TagFile.stateMsg(it.scan).zh, 12, 0xFFBDBDBD));
        if (rep.isEmpty() && wav.isEmpty() && c24.isEmpty()) {
            summary.addView(text(two(Msg.of(Msg.AskNothingToDo)), 14, 0xFFC8E6C9));
            return;
        }
        if (!rep.isEmpty()) actions.addView(button(Msg.of(Msg.AUiRepairBtn, Msg.num(rep.size())), new View.OnClickListener() {
            public void onClick(View v) { confirm(Msg.of(Msg.AskRepairExplain), new Runnable() { public void run() { runRepairs(); } }, Msg.of(Msg.SkipRepair)); }
        }));
        if (!wav.isEmpty()) actions.addView(button(Msg.of(Msg.AUiWavBtn, Msg.num(wav.size())), new View.OnClickListener() {
            public void onClick(View v) { confirm(Msg.of(Msg.AskWavExplain), new Runnable() { public void run() { prepareCopies(CopyFlac.Kind.WAV_TO_FLAC); } }, Msg.of(Msg.SkipWav)); }
        }));
        if (!c24.isEmpty()) actions.addView(button(Msg.of(Msg.AUi24Btn, Msg.num(c24.size())), new View.OnClickListener() {
            public void onClick(View v) { confirm(Msg.of(Msg.Ask24Explain), new Runnable() { public void run() { prepareCopies(CopyFlac.Kind.TO_24BIT); } }, Msg.of(Msg.Skip24)); }
        }));
    }

    private void confirm(Msg.Text explain, final Runnable go, final Msg.Text skipped) {
        if (busy()) return;
        new AlertDialog.Builder(this).setMessage(two(explain))
                .setPositiveButton(Msg.AUiStart.zh + " · " + Msg.AUiStart.en, new DialogInterface.OnClickListener() { public void onClick(DialogInterface d, int w) { go.run(); } })
                .setNegativeButton(Msg.AUiNo.zh + " · " + Msg.AUiNo.en, new DialogInterface.OnClickListener() { public void onClick(DialogInterface d, int w) { log(skipped); } })
                .show();
    }

    // ---------------------------------------------------------------------------
    // 原地修复
    // ---------------------------------------------------------------------------
    private void runRepairs() {
        if (busy()) return;
        final List<Item> todo = new ArrayList<Item>();
        for (Item it : items) if (it.scan.state == Classify.State.REPAIRABLE) todo.add(it);
        log(Msg.of(Msg.StartRepair, Msg.num(todo.size())));
        startJob(new Runnable() {
            public void run() {
                setRunning(true, Msg.of(Msg.PgRepair));
                int fixed = 0, unchanged = 0, failed = 0, stopped = 0, i = 0;
                for (; i < todo.size(); i++) {
                    if (cancel) break;
                    final Item it = todo.get(i);
                    final int idx = i;
                    progress(i, todo.size(), 0, it.name);
                    FdRio r = open(it.uri, true);
                    TagFile.FixReport rep;
                    if (r == null) {
                        rep = new TagFile.FixReport();
                        rep.outcome = TagFile.Outcome.REFUSED;
                        rep.why = Msg.of(Msg.FixOpenFailed);
                    } else {
                        TagFile.FixOptions o = new TagFile.FixOptions();
                        o.cancel = new TagFile.Cancel() { public boolean requested() { return cancel; } };
                        o.progress = new TagFile.Progress() { public void at(double f) { progress(idx, todo.size(), f, it.name); } };
                        try {
                            rep = TagFile.fix(r, r.writable, o, store.forFile(it.uri, it.name));
                        } catch (Throwable t) {
                            rep = new TagFile.FixReport();
                            rep.outcome = TagFile.Outcome.FAILED;
                            rep.why = Msg.data(t.toString());
                        }
                        closeQuietly(r);
                    }
                    switch (rep.outcome) {
                    case FIXED: fixed++; break;
                    case UNCHANGED: unchanged++; break;
                    case CANCELLED: stopped++; break;
                    default: failed++; break;
                    }
                    final TagFile.FixReport fr = rep;
                    ui(new Runnable() { public void run() { logFix(it, fr); } });
                }
                final int notStarted = todo.size() - i;
                final Msg.Text end = (notStarted > 0 || stopped > 0)
                        ? cancelText(Msg.CancelRepair, fixed, todo.size(), unchanged, failed, stopped, notStarted)
                        : Msg.of(Msg.DoneRepair, Msg.num(fixed), Msg.num(unchanged), Msg.num(failed)).plus(failed > 0 ? Msg.of(Msg.DoneSeeAbove) : Msg.EMPTY);
                setRunning(false, Msg.EMPTY);
                ui(new Runnable() { public void run() { log(end); rescanAfter(); } });
            }
        });
    }

    static Msg.Text cancelText(Msg.Entry head, int done, int total, int skipped, int notDone, int stopped, int notStarted) {
        Msg.Text t = Msg.of(head, Msg.num(done), Msg.num(total));
        if (skipped > 0) t = t.plus(Msg.of(Msg.CancelPartSkipped, Msg.num(skipped)));
        if (notDone > 0) t = t.plus(Msg.of(Msg.CancelPartNotDone, Msg.num(notDone)));
        if (stopped > 0) t = t.plus(Msg.of(Msg.CancelPartStopped, Msg.num(stopped)));
        return t.plus(Msg.of(Msg.CancelPartNotStarted, Msg.num(notStarted)));
    }

    private void logFix(Item it, TagFile.FixReport r) {
        logPlain(it.name, 0xFF90CAF9);
        if (r.outcome == TagFile.Outcome.FIXED) {
            log(Msg.of(Msg.OutFixed, Msg.of(r.inPlace ? Msg.ModeInPlace : Msg.AModeRewriteBack)));
            StringBuilder s = new StringBuilder();
            for (int k = 0; k < TagRule.KEYS; k++) s.append("  ").append(TagRule.keyName(k)).append(" = ").append(r.after.count[k] > 0 ? TagRule.shown(r.after.value[k]) : "-").append('\n');
            logPlain(s.toString().trim(), 0xFFC8E6C9);
            log(Msg.of(Msg.OutAudioSame, Msg.num(r.audioBytes), Msg.data(r.audioSha256.substring(0, Math.min(16, r.audioSha256.length()))), Msg.data(r.pcmMd5)));
        } else {
            log(r.why);
        }
    }

    /** 做完之后再检查一遍,按钮按新的状态重排 */
    private void rescanAfter() {
        final List<Item> again = new ArrayList<Item>();
        for (Item it : items) { Item n = new Item(); n.uri = it.uri; n.name = it.name; n.treeUri = it.treeUri; n.parentDocId = it.parentDocId; again.add(n); }
        final Uri keepOut = outTree;
        if (running) {                            // 刚做完的那个任务还在收尾:等它放手再查
            root.postDelayed(new Runnable() { public void run() { rescanAfter(); } }, 150);
            return;
        }
        startJob(new Runnable() {
            public void run() {
                List<Item> done = new ArrayList<Item>();
                for (Item it : again) {
                    FdRio r = open(it.uri, false);
                    if (r == null) { it.scan = new TagFile.Scan(); }
                    else { try { it.scan = TagFile.scan(r, TagRule.DEFAULT); } catch (Throwable t) { it.scan = new TagFile.Scan(); } closeQuietly(r); }
                    done.add(it);
                }
                items.clear();
                items.addAll(done);
                ui(new Runnable() { public void run() { outTree = keepOut; showSummary(items.size(), items.size(), false); } });
            }
        });
    }

    // ---------------------------------------------------------------------------
    // 另存
    // ---------------------------------------------------------------------------
    private void prepareCopies(CopyFlac.Kind kind) {
        boolean needFolder = false;
        for (Item it : items) if ((kind == CopyFlac.Kind.WAV_TO_FLAC ? it.scan.canWavToFlac : it.scan.canCopy24) && it.treeUri == null) needFolder = true;
        if (needFolder && outTree == null) {
            pendingCopyKind = kind;
            log(Msg.of(Msg.AUiChooseOutFolder));
            pickTree(REQ_OUT_TREE);
            return;
        }
        runCopies(kind);
    }

    /** 新文件:放在源文件所在的文件夹(来自选的文件夹时),否则放在用户选的那个文件夹里 */
    private CopyFlac.Target targetFor(final Item it, CopyFlac.Kind kind) {
        final Uri tree;
        final String parentId;
        if (it.treeUri != null) { tree = it.treeUri; parentId = it.parentDocId; }
        else if (outTree != null) { tree = outTree; parentId = DocumentsContract.getTreeDocumentId(outTree); }
        else return null;
        final String name = copyTargetName(it, kind);
        final ContentResolver cr = getContentResolver();
        return new CopyFlac.Target() {
            Uri made;
            FdRio rio;
            public String name() { return name; }
            public boolean exists() {
                Cursor c = null;
                try {
                    c = cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId), new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null);
                    while (c != null && c.moveToNext()) if (name.equalsIgnoreCase(c.getString(0))) return true;
                    return false;
                } catch (Exception e) {
                    return true;                      // 看不了就当有:宁可不写
                } finally {
                    if (c != null) c.close();
                }
            }
            public Rio create() {
                try {
                    made = DocumentsContract.createDocument(cr, DocumentsContract.buildDocumentUriUsingTree(tree, parentId), "audio/flac", name);
                    if (made == null) return null;
                    if (!name.equals(displayName(made))) {      // 提供者自己改了名字(说明这个名字已经有了):不用
                        DocumentsContract.deleteDocument(cr, made);
                        made = null;
                        return null;
                    }
                    rio = open(made, true);
                    if (rio == null || !rio.writable) { discard(); return null; }
                    return rio;
                } catch (Exception e) {
                    return null;
                }
            }
            public void discard() {
                closeQuietly(rio);
                rio = null;
                try { if (made != null) DocumentsContract.deleteDocument(cr, made); } catch (Exception ignored) {}
                made = null;
            }
            public void finish() { closeQuietly(rio); rio = null; }
        };
    }

    private void runCopies(final CopyFlac.Kind kind) {
        if (busy()) return;
        final List<Item> todo = new ArrayList<Item>();
        for (Item it : items) if (kind == CopyFlac.Kind.WAV_TO_FLAC ? it.scan.canWavToFlac : it.scan.canCopy24) todo.add(it);
        log(Msg.of(kind == CopyFlac.Kind.WAV_TO_FLAC ? Msg.StartWav : Msg.Start24, Msg.num(todo.size())));
        startJob(new Runnable() {
            public void run() {
                setRunning(true, Msg.of(Msg.PgSave));
                int made = 0, failed = 0, stopped = 0, i = 0;
                for (; i < todo.size(); i++) {
                    if (cancel) break;
                    final Item it = todo.get(i);
                    final int idx = i;
                    progress(i, todo.size(), 0, it.name);
                    FdRio r = open(it.uri, false);
                    CopyFlac.Report rep;
                    CopyFlac.Target t = targetFor(it, kind);
                    if (r == null) {
                        rep = new CopyFlac.Report();
                        rep.outcome = CopyFlac.Outcome.REFUSED;
                        rep.why = Msg.of(Msg.CopySourceOpen, Msg.of(Msg.WhyUnreadable));
                    } else {
                        CopyFlac.Options o = new CopyFlac.Options();
                        o.cancel = new TagFile.Cancel() { public boolean requested() { return cancel; } };
                        o.progress = new TagFile.Progress() { public void at(double f) { progress(idx, todo.size(), f, it.name); } };
                        try {
                            rep = CopyFlac.copy(r, kind, t, o, store.forFile(it.uri, it.name));
                        } catch (Throwable e) {
                            rep = new CopyFlac.Report();
                            rep.outcome = CopyFlac.Outcome.FAILED;
                            rep.why = Msg.data(e.toString());
                            if (t != null) t.discard();
                        }
                        closeQuietly(r);
                    }
                    switch (rep.outcome) {
                    case CREATED: made++; break;
                    case CANCELLED: stopped++; break;
                    default: failed++; break;
                    }
                    final CopyFlac.Report cr = rep;
                    ui(new Runnable() {
                        public void run() {
                            logPlain(it.name, 0xFF90CAF9);
                            log(cr.why);
                            if (cr.outcome == CopyFlac.Outcome.CREATED) {
                                StringBuilder s = new StringBuilder();
                                for (int k = 0; k < TagRule.KEYS; k++) s.append("  ").append(TagRule.keyName(k)).append(" = ").append(TagRule.shown(cr.fin[k])).append('\n');
                                logPlain(s.toString().trim(), 0xFFC8E6C9);
                            }
                        }
                    });
                }
                final int notStarted = todo.size() - i;
                final Msg.Text end = (notStarted > 0 || stopped > 0) ? cancelText(Msg.CancelSave, made, todo.size(), 0, failed, stopped, notStarted)
                        : Msg.of(Msg.DoneSave, Msg.num(made), Msg.num(failed)).plus(failed > 0 ? Msg.of(Msg.DoneSeeAbove) : Msg.EMPTY);
                setRunning(false, Msg.EMPTY);
                ui(new Runnable() { public void run() { log(end); } });
            }
        });
    }

    // ---------------------------------------------------------------------------
    // 上次被打断的写回
    // ---------------------------------------------------------------------------
    private void resumePending() {
        if (running) return;                                    // 别的界面实例的任务在跑:这次不接着写、什么都不清(下次打开再做)
        startJob(new Runnable() {
            public void run() {
                List<Store.Pending> todo = store.pending();
                if (!todo.isEmpty()) setRunning(true, Msg.of(Msg.AUiResuming));
                for (final Store.Pending p : todo) {
                    Store.FileTemp temp = p.temp.exists() ? Store.openTemp(p.temp) : null;
                    if (temp == null) {                        // 核对过的新版本已经不在了:这条登记没用了
                        store.done(p);
                        ui(new Runnable() { public void run() { log(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), Msg.of(Msg.AVerWhole))); } });
                        continue;
                    }
                    FdRio target = open(p.uri, true);
                    TagFile.Recover rc = target == null || !target.writable ? TagFile.Recover.NOT_OURS : TagFile.finishOverwrite(temp, target, p.journal);
                    closeQuietly(target);
                    temp.close();
                    if (rc == TagFile.Recover.DONE) {
                        store.done(p);
                        ui(new Runnable() { public void run() { log(Msg.of(Msg.AWriteBackDone, Msg.data(p.name))); } });
                    } else if (rc == TagFile.Recover.ORIGINAL) {           // 写不成、已经恢复成原样:原文件完好,登记删掉
                        store.done(p);
                        ui(new Runnable() { public void run() { log(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), Msg.of(Msg.FixWriteFailed, Msg.of(Msg.LayIo)).plus(Msg.of(Msg.FixTailRestored)))); } });
                    } else if (rc == TagFile.Recover.TEMP_BAD) {
                        store.done(p);
                        ui(new Runnable() { public void run() { log(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), Msg.of(Msg.AVerWhole))); } });
                    } else {
                        final Msg.Text why = Msg.of(target == null || !target.writable ? Msg.AFixNoWrite : rc == TagFile.Recover.NOT_OURS ? Msg.AFixChanged : Msg.AVerWhole);
                        ui(new Runnable() { public void run() { offerSavePending(p, why); } });
                    }
                }
                store.removeStrayTemps();
                releaseUnusedGrants();
                setRunning(false, Msg.EMPTY);
            }
        });
    }

    /** 系统给的长期读写授权:登记里还用得着的留着,别的放掉 */
    private void releaseUnusedGrants() {
        try {
            List<Uri> keep = store.urisInUse();
            for (android.content.UriPermission up : getContentResolver().getPersistedUriPermissions())
                if (!keep.contains(up.getUri()))
                    getContentResolver().releasePersistableUriPermission(up.getUri(), Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Exception ignored) {}
    }

    private void offerSavePending(final Store.Pending p, Msg.Text why) {
        log(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), why));
        if (isFinishing() || isDestroyed()) return;              // 界面已经关了:下次打开再问
        new AlertDialog.Builder(this).setMessage(two(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), why)))
                .setPositiveButton(Msg.AUiSaveRepaired.zh + " · " + Msg.AUiSaveRepaired.en, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) {
                        savingPending = p;
                        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                        i.addCategory(Intent.CATEGORY_OPENABLE);
                        i.setType("audio/flac");
                        i.putExtra(Intent.EXTRA_TITLE, p.name);
                        try { startActivityForResult(i, REQ_SAVE_PENDING); } catch (Exception e) { logPlain(e.toString(), 0xFFFF8A80); }
                    }
                })
                .setNegativeButton(Msg.AUiLater.zh + " · " + Msg.AUiLater.en, null).show();
    }

    private void finishSavePending(final Uri dest) {
        final Store.Pending p = savingPending;
        if (p == null || dest == null) { savingPending = null; return; }
        if (busy()) {                                         // 别的事还在做:等它做完再存,不丢
            root.postDelayed(new Runnable() { public void run() { finishSavePending(dest); } }, 300);
            return;
        }
        savingPending = null;
        startJob(new Runnable() {                             // 整个文件拷一遍、核对:放到后台做
            public void run() {
                setRunning(true, Msg.of(Msg.AUiSaveRepaired));
                Store.FileTemp temp = Store.openTemp(p.temp);
                FdRio out = open(dest, true);
                final Msg.Text why = temp == null || out == null || !out.writable ? Msg.of(Msg.AFixNoWrite)
                        : TagFile.copyVerified(temp, out, p.journal.newSize, p.journal.newSha) ? null : Msg.of(Msg.AVerWhole);
                closeQuietly(out);
                if (temp != null) temp.close();
                if (why == null) store.done(p);
                final String shown = displayName(dest);
                setRunning(false, Msg.EMPTY);
                ui(new Runnable() {
                    public void run() {
                        if (why == null) log(Msg.of(Msg.AUiSavedTo, Msg.data(shown)));
                        else log(Msg.of(Msg.AWriteBackFailed, Msg.data(p.name), why));
                    }
                });
            }
        });
    }

    // ---------------------------------------------------------------------------
    // 自检(长按标题;或者 adb:am start -n com.omni.tagrepair/.MainActivity --ez selftest true)
    // ---------------------------------------------------------------------------
    private void runSelfTest() {
        if (busy()) return;
        logBox.removeAllViews();
        startJob(new Runnable() {
            public void run() {
                final long t0 = System.currentTimeMillis();
                final SelfTest.Result r = SelfTest.run(null);
                final long ms = System.currentTimeMillis() - t0;
                String head = "selftest pass " + r.pass + " fail " + r.fail + " (" + ms + " ms)";
                Log.i(TAG, head);
                for (String line : r.log.split("\n")) Log.i(TAG, line);
                ui(new Runnable() {
                    public void run() {
                        log(Msg.of(Msg.AUiSelfTest, Msg.num(r.pass), Msg.num(r.fail)));
                        logPlain(r.log, r.fail == 0 ? 0xFFC8E6C9 : 0xFFFF8A80);
                    }
                });
            }
        });
    }
}

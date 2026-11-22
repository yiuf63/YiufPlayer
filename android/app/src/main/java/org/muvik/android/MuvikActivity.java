package org.muvik.android;
import android.content.Intent;
import android.net.Uri;
import android.provider.Settings;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.graphics.PixelFormat;
import android.view.WindowManager;
import android.view.View;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * Muvik 在 Android 上的入口。
 *
 * 为什么继承 SDLActivity：
 *   borealis 的 Android 后端建立在 SDL2 上（渲染、输入、音频都由 SDL 桥接）。
 *   SDLActivity 内部会加载 native 库并调用 C++ 侧的 main()，
 *   所以这里不需要手写 JNI 入口。
 *
 * 注意：原生库名必须是 libmain.so —— SDL 的约定是 main() 在名为 "main"
 * 的库里。CMakeLists.txt 里对此有对应的输出名设置。
 */
public class MuvikActivity extends SDLActivity {

    private static final String TAG = "Muvik";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 用 RGBA_8888：nanoVG 与 mpv 的 GL 渲染需要 8 位色通道，
        // 默认的 RGB_565 会产生色带。
        if (mSurface != null) {
            mSurface.getHolder().setFormat(PixelFormat.RGBA_8888);
        }

        // 全面屏：允许内容延伸进刘海/挖孔区域。
        // 掌机与小尺寸平板常常有异形屏，不处理会出现黑色边框。
        if (Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // 保持屏幕常亮：播放器在播放时不应该让屏幕自动熄灭。
        // （长时间不操作仍允许休眠，由系统策略决定，这里只是不主动熄屏。）
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // 沉浸式全屏：隐藏状态栏与导航栏，播放器需要完整屏幕。
        hideSystemUi();

        requestMediaPermissions();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUi();
        }
    }

    /**
     * 请求读取媒体文件的权限。
     *
     * 为什么必须做：Android 6 起读外部存储是运行时权限，不申请的话
     * MediaLibrary 扫描会得到空结果 —— 表现为「文件目录不显示」，
     * 而且不会有任何错误提示（open/read 只是失败）。
     *
     * Android 13 (API 33) 起权限拆成了 READ_MEDIA_VIDEO / _AUDIO，
     * 旧的 READ_EXTERNAL_STORAGE 在 33+ 上不再授予读取媒体文件的权限。
     */
    private void requestMediaPermissions() {
        if (Build.VERSION.SDK_INT < 23) return;   // 6.0 以下安装即授予

        java.util.List<String> need = new java.util.ArrayList<>();
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(Manifest.permission.READ_MEDIA_VIDEO)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.READ_MEDIA_VIDEO);
            if (checkSelfPermission(Manifest.permission.READ_MEDIA_AUDIO)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.READ_MEDIA_AUDIO);
        } else {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.READ_EXTERNAL_STORAGE);
        }

        if (!need.isEmpty()) {
            Log.i(TAG, "请求媒体权限: " + need);
            requestPermissions(need.toArray(new String[0]), 1001);
        } else {
            Log.i(TAG, "媒体权限已授予");
        }

        // 「所有文件访问」是特殊权限，**不能**走 requestPermissions，
        // 只能引导用户去系统设置页手动开启。
        ensureAllFilesAccess();
    }

    /**
     * 检查并引导开启「所有文件访问」(MANAGE_EXTERNAL_STORAGE)。
     *
     * 为什么需要它：本项目的媒体库是**自己递归遍历目录**的，
     * 而不是通过 MediaStore 查询。Android 11 起，
     * READ_MEDIA_VIDEO/AUDIO 只在 MediaStore 查询路径上生效，
     * 应用直接用 opendir/readdir 访问 /sdcard 会被拒绝。
     *
     * 实测（Android 16，权限已全部授予的前提下）：
     *     ls /sdcard/Movies -> Permission denied
     *
     * 表现就是用户看到的两个问题：
     *     · 添加媒体库时目录是空的
     *     · 字幕选不了文件（文件选择器列不出 .srt/.ass）
     */
    private void ensureAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30) return;   // Android 11 以下没有这个限制
        if (Environment.isExternalStorageManager()) {
            Log.i(TAG, "所有文件访问权限已授予");
            return;
        }
        Log.w(TAG, "缺少「所有文件访问」权限：媒体库扫描与字幕选择都会是空的");
        try {
            Intent intent = new Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            // 部分 ROM 没有这个 Activity，退回到「所有文件访问」总列表
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception e2) {
                Log.e(TAG, "无法打开所有文件访问设置页: " + e2.getMessage());
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != 1001) return;
        boolean allGranted = grantResults.length > 0;
        for (int r : grantResults) {
            if (r != PackageManager.PERMISSION_GRANTED) { allGranted = false; break; }
        }
        Log.i(TAG, "权限请求结果: " + (allGranted ? "全部授予" : "有拒绝"));
    }

    /**
     * 提供给 native 侧查询的默认媒体目录。
     *
     * Android 把用户视频放在 /sdcard/Movies、音乐在 /sdcard/Music，
     * 但真实路径可能因厂商而异（如 /storage/emulated/0/…）。
     * 这里用 Environment 拿到权威路径，供 native 侧作为默认扫描位置。
     */
    public static String[] getDefaultMediaDirs() {
        java.util.List<String> dirs = new java.util.ArrayList<>();
        try {
            java.io.File movies = Environment
                    .getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES);
            java.io.File music = Environment
                    .getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC);
            java.io.File dcim = Environment
                    .getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM);
            if (movies != null) dirs.add(movies.getAbsolutePath());
            if (music != null) dirs.add(music.getAbsolutePath());
            if (dcim != null) dirs.add(dcim.getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "获取默认媒体目录失败", e);
        }
        return dirs.toArray(new String[0]);
    }

    private void hideSystemUi() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
              View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN);
    }

    /**
     * 把 Intent 里的文件路径传给 native 侧的 argv。
     *
     * 为什么必须做：
     *   SDLActivity 的默认实现返回**空数组**，于是 native 的 main(argc, argv)
     *   拿不到任何参数，无法「打开指定文件」。
     *   这既是调试需要（自动化测试要直接进播放页），
     *   也是播放器的基本能力 —— 从文件管理器「用其他应用打开」必须能播。
     *
     * 支持的两种来源：
     *   1. ACTION_VIEW 的 data URI（外部应用打开文件时）
     *   2. extra "path"（adb / 脚本显式指定）
     */
    @Override
    protected String[] getArguments() {
        java.util.List<String> args = new java.util.ArrayList<>();

        android.content.Intent intent = getIntent();
        if (intent != null) {
            String extra = intent.getStringExtra("path");
            if (extra != null && !extra.isEmpty()) {
                args.add(extra);
            }
            android.net.Uri data = intent.getData();
            if (data != null) {
                String scheme = data.getScheme();
                if ("file".equals(scheme) || scheme == null) {
                    args.add(data.getPath());
                } else if ("content".equals(scheme)) {
                    // 外部应用通常给 content:// —— 需要复制到应用私有目录
                    String copied = copyContentUriToCache(data);
                    if (copied != null) args.add(copied);
                }
            }
        }

        Log.i(TAG, "native 参数: " + args);
        return args.toArray(new String[0]);
    }

    /**
     * content:// 无法直接用路径打开（分区存储），必须复制到应用可读的位置。
     * 视频通常较大，这里复制到 cacheDir；系统会在空间不足时清理。
     */
    private String copyContentUriToCache(android.net.Uri uri) {
        try {
            String name = "opened_media";
            android.database.Cursor c = getContentResolver().query(uri, null, null, null, null);
            if (c != null) {
                int idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (idx >= 0 && c.moveToFirst()) {
                    String dn = c.getString(idx);
                    if (dn != null && !dn.isEmpty()) name = dn;
                }
                c.close();
            }
            java.io.File out = new java.io.File(getCacheDir(), name);
            try (java.io.InputStream in = getContentResolver().openInputStream(uri);
                 java.io.OutputStream os = new java.io.FileOutputStream(out)) {
                if (in == null) return null;
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) != -1) os.write(buf, 0, n);
            }
            Log.i(TAG, "content URI 已复制到 " + out.getAbsolutePath());
            return out.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "复制 content URI 失败", e);
            return null;
        }
    }

    /**
     * 原生库加载顺序（**不能随意调整**）。
     *
     * libmpv.so 引用了 libc++ 的符号（如 std::to_chars），但它的
     * DT_NEEDED 里并没有 libc++_shared.so。因此必须先手动加载
     * libc++_shared，否则 libmpv 会以
     *   dlopen failed: cannot locate symbol
     * 失败。FFmpeg 各库同理，要按依赖顺序排在 libmpv 之前。
     *
     * 最后的 "main" 是 SDL 约定：SDLActivity 会把 getLibraries() 的
     * 最后一项当作主库，从中找 SDL_main 入口。
     */
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "c++_shared",
            "avutil",
            "swresample",
            "avcodec",
            "avformat",
            "swscale",
            "avfilter",
            "mpv",
            "main"
        };
    }

    /**
     * SDLActivity 默认用 "lib" + getLibraries() 最后一项 + ".so" 拼路径。
     * 这里显式返回绝对路径，避免不同 Android 版本对 nativeLibraryDir
     * 解析不一致。
     */
    @Override
    protected String getMainSharedObject() {
        return getContext().getApplicationInfo().nativeLibraryDir + "/libmain.so";
    }
}

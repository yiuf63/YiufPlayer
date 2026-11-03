package org.libsdl.app;

import android.util.Log;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Message;
import android.provider.Settings;
import android.view.Window;
import android.view.WindowManager;

public class PlatformUtils {

    // 这些方法全部通过 JNI 被 native 侧调用，异常必须就地捕获，
    // 否则会以 pending exception 形式导致整个进程 abort。
    private static final String TAG = "PlatformUtils";

    public static boolean isBatterySupported() {
        Context context = SDLActivity.getContext();
        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        return batteryIntent != null;
    }

    public static int getBatteryLevel() {
        Context context = SDLActivity.getContext();

        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (batteryIntent == null) {
            return 0;
        }
        int level = batteryIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = batteryIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);

        if (level >= 0 && scale > 0) {
            return (level * 100) / scale;
        }

        return 0;
    }

    public static boolean isBatteryCharging() {
        Context context = SDLActivity.getContext();

        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        Intent batteryStatus = context.registerReceiver(null, filter);

        int status = batteryStatus.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        return status == BatteryManager.BATTERY_STATUS_CHARGING ||
                status == BatteryManager.BATTERY_STATUS_FULL;
    }

    public static boolean isEthernetConnected() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;
            ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            Network[] networks = cm.getAllNetworks();
            for (Network network : networks) {
                NetworkCapabilities capabilities = cm.getNetworkCapabilities(network);
                if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                    return true;
                }
            }
            return false;
        } catch (Throwable t) {
            Log.w(TAG, "isEthernetConnected 失败，降级为 false: " + t);
            return false;
        }
    }

    public static boolean isWifiSupported() {
        // 这些方法由 native 侧经 JNI 调用。**抛出的异常如果不就地捕获，
        // 会以 pending exception 的形式留在 JNI 环境里**，下一次任意
        // FindClass/CallMethod 都会直接 abort 整个进程。
        // 实测崩溃：缺少 ACCESS_WIFI_STATE 时
        //   PlatformUtils.isWifiSupported -> SecurityException -> SIGABRT
        // 状态栏少显示一个 WiFi 图标远好于整个播放器退出，
        // 因此这里一律降级为 false。
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;
            WifiManager wifiManager = (WifiManager) context.getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            return wifiManager != null && wifiManager.isWifiEnabled();
        } catch (Throwable t) {
            Log.w(TAG, "isWifiSupported 失败，降级为 false: " + t);
            return false;
        }
    }

    public static boolean isWifiConnected() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;
            ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo wifiInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
            return wifiInfo != null && wifiInfo.isConnected();
        } catch (Throwable t) {
            Log.w(TAG, "isWifiConnected 失败，降级为 false: " + t);
            return false;
        }
    }

    public static int getWifiSignalStrength() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return -100;
            WifiManager wifiManager = (WifiManager) context.getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            if (wifiManager == null) return -100;
            WifiInfo wifiInfo = wifiManager.getConnectionInfo();
            return wifiInfo != null ? wifiInfo.getRssi() : -100;
        } catch (Throwable t) {
            Log.w(TAG, "getWifiSignalStrength 失败，降级为 -100: " + t);
            return -100;
        }
    }

    private static int getWifiSignalStrengthUnsafe() {
        Context context = SDLActivity.getContext();
        WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifiManager.getConnectionInfo();
        return wifiInfo.getRssi();
    }

    public static void openBrowser(String url) {
        Context context = SDLActivity.getContext();

        Uri webpage = Uri.parse(url);
        Intent intent = new Intent(Intent.ACTION_VIEW, webpage);
        if (intent.resolveActivity(context.getPackageManager()) != null) {
            context.startActivity(intent);
        }
    }

    public static float getSystemScreenBrightness(Context context) {
        ContentResolver contentResolver = context.getContentResolver();
        return Settings.System.getInt(contentResolver,
                Settings.System.SCREEN_BRIGHTNESS, 125) * 1.0f / 255.0f;
    }

    public static BorealisHandler borealisHandler = null;

    public static void setAppScreenBrightness(Activity activity, float value) {
        Message message = Message.obtain();
        message.obj = activity;
        message.arg1 = (int)(value * 255);
        message.what = 0;
        if(borealisHandler != null) borealisHandler.sendMessage(message);
    }

    public static float getAppScreenBrightness(Activity activity) {
        Window window = activity.getWindow();
        WindowManager.LayoutParams lp = window.getAttributes();
        if (lp.screenBrightness < 0) return getSystemScreenBrightness(activity);
        return lp.screenBrightness;
    }
}
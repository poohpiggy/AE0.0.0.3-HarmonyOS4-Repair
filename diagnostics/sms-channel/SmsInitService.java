package org.codex.smsinit;

import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Handler;
import android.os.IBinder;
import android.provider.Settings;
import android.util.Log;
import java.io.BufferedInputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** Invoked once by the root first-boot guard; never exposes an activity. */
public final class SmsInitService extends Service {
    private static final String TAG = "CodexSmsInit";
    private static final String TARGET = "/system/media/audio/notifications/Bongo.ogg";
    private boolean bound, finished;
    private String uri, nonce;

    static boolean eligible(String message, String path, String candidate) {
        return (message == null || "null".equals(message)) && path == null && candidate != null
                && candidate.matches("content://media/internal/audio/media/[0-9]+");
    }
    private boolean stillEligible() {
        return eligible(Settings.System.getString(getContentResolver(), "message"),
                Settings.System.getString(getContentResolver(), "message_path"), uri);
    }
    static boolean streamsMatch(InputStream first, InputStream second) throws java.io.IOException {
        BufferedInputStream a = new BufferedInputStream(first), b = new BufferedInputStream(second);
        long total = 0;
        while (true) {
            int x = a.read(), y = b.read();
            if (x != y) return false;
            if (x < 0) return total > 0;
            total++;
        }
    }
    private boolean isExactBongo() {
        try (InputStream a = getContentResolver().openInputStream(Uri.parse(uri));
             InputStream b = new FileInputStream(TARGET)) {
            return a != null && streamsMatch(a,b);
        } catch (Exception failure) { Log.w(TAG, "BONGO_UNREADABLE", failure); return false; }
    }
    private void finish(String status) {
        if (finished) return;
        finished = true;
        try (FileOutputStream out = openFileOutput("result", MODE_PRIVATE)) {
            out.write((nonce + ":" + status + "\n").getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch (Exception failure) { Log.e(TAG, "RESULT_WRITE_FAILED", failure); }
        if (bound) { try { unbindService(connection); } catch (Exception ignored) { } bound = false; }
        stopSelf();
    }
    private final ServiceConnection connection = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            try {
                if (!stillEligible() || !isExactBongo()) { finish("SKIP"); return; }
                if (!"com.huawei.mms.service.IMessageRingtonInterface".equals(binder.getInterfaceDescriptor())) {
                    finish("BAD_BINDER"); return;
                }
                Class<?> loaderType = Class.forName("dalvik.system.PathClassLoader");
                ClassLoader loader = (ClassLoader) loaderType.getConstructor(String.class, ClassLoader.class)
                        .newInstance("/system/priv-app/Mms/Mms.apk", getClassLoader());
                Class<?> iface = Class.forName("com.huawei.mms.service.IMessageRingtonInterface", true, loader);
                Class<?> stub = Class.forName("com.huawei.mms.service.IMessageRingtonInterface$Stub", true, loader);
                Object remote = stub.getMethod("asInterface", IBinder.class).invoke(null, binder);
                int status = (Integer) iface.getMethod("setRingtone", String.class, String.class, boolean.class)
                        .invoke(remote, uri, "0", false);
                String actual = Settings.System.getString(getContentResolver(), "message");
                finish(status == 0 && uri.equals(actual) ? "OK" : "UNCONFIRMED");
            } catch (Exception error) { Log.e(TAG, "CALL_FAILED", error); finish("ERROR"); }
        }
        @Override public void onServiceDisconnected(ComponentName name) { finish("DISCONNECTED"); }
    };
    @Override public int onStartCommand(Intent input, int flags, int startId) {
        uri = input == null ? null : input.getStringExtra("uri");
        nonce = input == null ? null : input.getStringExtra("nonce");
        if (nonce == null || !nonce.matches("[0-9]{1,20}")) { stopSelf(); return START_NOT_STICKY; }
        if (getPackageManager().checkPermission("huawei.permmisons.mms.CHANGE_MMS_RINGTON_REQUEST",
                getPackageName()) != PackageManager.PERMISSION_GRANTED || !stillEligible() || !isExactBongo()) {
            finish("SKIP"); return START_NOT_STICKY;
        }
        Intent target = new Intent();
        target.setClassName("com.android.mms", "com.huawei.mms.service.MessageRingtonService");
        try { bound = bindService(target, connection, BIND_AUTO_CREATE); }
        catch (Exception error) { Log.e(TAG, "BIND_FAILED", error); }
        if (!bound) finish("BIND_FAILED");
        else new Handler().postDelayed(new Runnable() { @Override public void run() { finish("TIMEOUT"); } }, 12000);
        return START_NOT_STICKY;
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}

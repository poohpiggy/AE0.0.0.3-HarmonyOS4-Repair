package org.codex.locale;
import java.util.Locale;

/** One-shot system configuration helper, invoked only by the opt-in shell script. */
public final class Bootstrap {
    public static void main(String[] args) {
        try {
            Class<?> process = Class.forName("android.os.Process");
            Class.forName("android.system.Os").getMethod("setuid", int.class).invoke(null, 1000);
            int uid = (Integer) process.getMethod("myUid").invoke(null);
            if (uid != 1000) throw new SecurityException("not guest system UID");
            Class<?> am = Class.forName("android.app.ActivityManager");
            Class<?> iface = Class.forName("android.app.IActivityManager");
            Class<?> configClass = Class.forName("android.content.res.Configuration");
            Object service = am.getMethod("getService").invoke(null);
            Object config = iface.getMethod("getConfiguration").invoke(service);
            Object locales = configClass.getMethod("getLocales").invoke(config);
            String current = (String) locales.getClass().getMethod("toLanguageTags").invoke(locales);
            if (!current.isEmpty() && !"en-US".equals(current)) {
                System.out.println("Existing language preserved: " + current);
                System.exit(2);
            }
            configClass.getMethod("setLocale", Locale.class).invoke(config, Locale.SIMPLIFIED_CHINESE);
            configClass.getField("userSetLocale").setBoolean(config, true);
            iface.getMethod("updatePersistentConfigurationWithAttribution", configClass, String.class, String.class)
                .invoke(service, config, "android", null);
            System.exit(0);
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(1);
        }
    }
}

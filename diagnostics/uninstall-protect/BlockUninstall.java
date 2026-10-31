package org.codex.protect;

/** Sets the package manager's per-user uninstall block for two bundled apps. */
public final class BlockUninstall {
    public static void main(String[] args) {
        try {
            if (args.length != 1 || (!"com.iflytek.inputmethod".equals(args[0]) && !"mark.via".equals(args[0]))) {
                throw new SecurityException("package is outside the explicit allowlist");
            }
            Class.forName("android.system.Os").getMethod("setuid", int.class).invoke(null, 1000);
            int uid = (Integer) Class.forName("android.os.Process").getMethod("myUid").invoke(null);
            if (uid != 1000) throw new SecurityException("not guest system UID");
            Class<?> iface = Class.forName("android.content.pm.IPackageManager");
            Object pm = Class.forName("android.app.AppGlobals").getMethod("getPackageManager").invoke(null);
            java.lang.reflect.Method get = iface.getMethod("getBlockUninstallForUser", String.class, int.class);
            java.lang.reflect.Method set = iface.getMethod("setBlockUninstallForUser", String.class, boolean.class, int.class);
            if (!((Boolean) get.invoke(pm, args[0], 0))) {
                boolean updated = (Boolean) set.invoke(pm, args[0], true, 0);
                if (!updated) throw new IllegalStateException("package manager refused uninstall protection");
            }
            if (!((Boolean) get.invoke(pm, args[0], 0))) throw new IllegalStateException("uninstall protection not confirmed");
            System.out.println("Uninstall protection confirmed for " + args[0]);
        } catch (Throwable error) {
            error.printStackTrace();
            System.exit(1);
        }
    }
}

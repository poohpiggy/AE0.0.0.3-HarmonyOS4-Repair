package org.codex.privacy;

/** Fresh-ROM default only; never grants permissions or changes permission flags. */
public final class ViaLocationDefault {
    private static final String PACKAGE = "mark.via";
    private static final String[] PERMISSIONS = {
        "android.permission.ACCESS_FINE_LOCATION", "android.permission.ACCESS_COARSE_LOCATION"
    };
    private static final int GRANTED_BY_DEFAULT = 1 << 5;
    private static final int USER_OR_POLICY_CHOICE = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 16) | (1 << 17);
    public static void main(String[] args) {
        try {
            if (args.length != 0) throw new IllegalArgumentException("no arguments accepted");
            Class.forName("android.system.Os").getMethod("setuid", int.class).invoke(null, 1000);
            if ((Integer)Class.forName("android.os.Process").getMethod("myUid").invoke(null) != 1000) {
                throw new SecurityException("not guest system UID");
            }
            Class<?> binder = Class.forName("android.os.IBinder");
            Object raw = Class.forName("android.os.ServiceManager").getMethod("getService", String.class).invoke(null, "permissionmgr");
            if (raw == null) throw new IllegalStateException("permission manager unavailable");
            Object perm = Class.forName("android.permission.IPermissionManager$Stub").getMethod("asInterface", binder).invoke(null, raw);
            Class<?> permIface = Class.forName("android.permission.IPermissionManager");
            java.lang.reflect.Method getFlags = permIface.getMethod("getPermissionFlags", String.class, String.class, int.class);
            java.lang.reflect.Method revoke = permIface.getMethod("revokeRuntimePermission", String.class, String.class, int.class, String.class);
            Class<?> pmIface = Class.forName("android.content.pm.IPackageManager");
            Object pm = Class.forName("android.app.AppGlobals").getMethod("getPackageManager").invoke(null);
            java.lang.reflect.Method check = pmIface.getMethod("checkPermission", String.class, String.class, int.class);
            for (String toReview : PERMISSIONS) {
                // The two location permissions are linked. Preserve BOTH if
                // either has a user, enterprise, one-time or auto-revoke flag.
                for (String p : PERMISSIONS) {
                    int flags = (Integer)getFlags.invoke(perm, PACKAGE, p, 0);
                    if ((flags & USER_OR_POLICY_CHOICE) != 0) {
                        System.out.println("Via location has an existing decision; preserving it");
                        return;
                    }
                    if ((flags & GRANTED_BY_DEFAULT) == 0) {
                        System.err.println("default permission policy not ready");
                        System.exit(2);
                    }
                }
                if ((Integer)check.invoke(pm, toReview, PACKAGE, 0) == 0) {
                    revoke.invoke(perm, PACKAGE, toReview, 0, "Restore fresh-ROM location default");
                    if ((Integer)check.invoke(pm, toReview, PACKAGE, 0) == 0) {
                        throw new IllegalStateException("location permission still granted: " + toReview);
                    }
                }
            }
            // Read both again after the full group operation, not just the
            // permission just revoked. The two permissions may interact.
            for (String p : PERMISSIONS) {
                if ((Integer)check.invoke(pm, p, PACKAGE, 0) == 0) {
                    throw new IllegalStateException("location permission unexpectedly granted: " + p);
                }
            }
            System.out.println("Via default location permissions are not granted");
        } catch (Throwable error) {
            error.printStackTrace();
            System.exit(1);
        }
    }
}

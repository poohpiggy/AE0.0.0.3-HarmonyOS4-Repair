/** Read-only probe, scoped to the stock SIM-1 SMS notification channel. */
public final class SmsChannelStatus {
    public static void main(String[] args) {
        try {
            if (args.length > 1) throw new IllegalArgumentException();
            Class.forName("android.system.Os").getMethod("setuid", int.class).invoke(null, 1000);
            if (((Integer) Class.forName("android.os.Process").getMethod("myUid").invoke(null)) != 1000)
                throw new SecurityException("expected system uid");
            Object pm=Class.forName("android.app.AppGlobals").getMethod("getPackageManager").invoke(null);
            int uid=(Integer)Class.forName("android.content.pm.IPackageManager")
                    .getMethod("getPackageUid",String.class,int.class,int.class).invoke(pm,"com.android.mms",0,0);
            if (uid < 10000 || uid >= 20000) throw new SecurityException("unexpected package uid");
            Class<?> binder=Class.forName("android.os.IBinder");
            Object h=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"notification");
            if (h==null) throw new IllegalStateException("notification service unavailable");
            Object s=Class.forName("android.app.INotificationManager$Stub").getMethod("asInterface",binder).invoke(null,h);
            Class<?> i=Class.forName("android.app.INotificationManager"),c=Class.forName("android.app.NotificationChannel");
            Object channel=i.getMethod("getNotificationChannelForPackage",String.class,int.class,String.class,String.class,boolean.class)
                    .invoke(s,"com.android.mms",uid,"mms_sim1_channel",null,false);
            if (channel==null || (Boolean)c.getMethod("isDeleted").invoke(channel)) {System.out.println("ABSENT");return;}
            if (!"mms_sim1_channel".equals(c.getMethod("getId").invoke(channel))) throw new SecurityException("channel id mismatch");
            int locked=(Integer)c.getMethod("getUserLockedFields").invoke(channel);
            Object sound=c.getMethod("getSound").invoke(channel);
            if (args.length==0) System.out.println(sound==null && locked==0 ? "EMPTY_UNLOCKED" : "NOT_ELIGIBLE");
            else System.out.println(args[0].matches("content://media/internal/audio/media/[0-9]+")
                    && sound!=null && args[0].equals(sound.toString()) ? "MATCH" : "NOT_MATCH");
        } catch (Throwable error) {
            System.err.println("SMS status probe failed: "+error);
            System.exit(1);
        }
    }
}

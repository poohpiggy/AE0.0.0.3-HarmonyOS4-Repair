package org.codex.harmonybridge;
import android.app.Service;
import android.content.*;
import android.os.*;
import java.io.FileWriter;
public final class ProbeService extends Service {
 private static final java.util.concurrent.atomic.AtomicBoolean started = new java.util.concurrent.atomic.AtomicBoolean();
 private synchronized void note(String s) { try (FileWriter f=new FileWriter(new java.io.File(getFilesDir(),"status.txt"),true)) { f.write(s+"\n"); } catch(Exception ignored) {} android.util.Log.i("CodexFoundationProbe",s); }
 public IBinder onBind(Intent intent) { return null; }
 private final class LocalContext extends ContextWrapper {
  LocalContext(Context c) {super(c);}
  public Context getApplicationContext(){return this;}
  public void unregisterReceiver(BroadcastReceiver r) {
   try { getBaseContext().unregisterReceiver(r); } catch (IllegalArgumentException e) {
    if (r != null && r.getClass().getName().equals("ohos.event.commonevent.CesProxyReceiver") && e.getMessage() != null && e.getMessage().startsWith("Receiver not registered:")) { note("CES redundant unregister id="+System.identityHashCode(r)+" thread="+Thread.currentThread().getName()); return; }
    throw e;
   }
  }
  public Intent registerReceiverAsUser(BroadcastReceiver r, UserHandle u, IntentFilter f, String permission, Handler h) {
   try { int id=(Integer)UserHandle.class.getMethod("getIdentifier").invoke(u); if(id != -1) throw new SecurityException("unsupported user "+id); } catch (ReflectiveOperationException e) {throw new IllegalStateException(e);}
   note("registerReceiverAsUser ALL mapped to current user"); return getBaseContext().registerReceiver(r,f,permission,h);
  }
 }
 @Override public int onStartCommand(Intent intent,int flags,int startId) {
  if (!started.compareAndSet(false, true)) { note("already started"); return START_STICKY; }
  new Thread(() -> {try {
   note("start pid="+android.os.Process.myPid()+" uid="+android.os.Process.myUid());
   Class.forName("android.system.Os").getMethod("setuid",int.class).invoke(null,1000); note("after setUid="+android.os.Process.myUid());
   Class<?> b=Class.forName("ohos.bundlemgr.BundleMgrAdapter"); Class.forName("com.huawei.ohos.foundation.DmsProxyService"); note("libraries loaded");
   Context ctx=new LocalContext(this);
   Class.forName("ohos.event.CommonEventMgrAdapter").getMethod("init",Context.class).invoke(null,ctx); note("CES init returned");
   Object mgr=b.getMethod("getInstance").invoke(null); b.getMethod("init",Context.class).invoke(mgr,this); note("BMS init returned");
   Class<?> f=Class.forName("com.huawei.ohos.foundation.FoundationFramework");f.getMethod("start").invoke(f.getConstructor().newInstance());note("Foundation start returned");
  } catch(Throwable e) {started.set(false);java.io.StringWriter s=new java.io.StringWriter(); e.printStackTrace(new java.io.PrintWriter(s));note(s.toString());}},"foundation-probe").start();return START_STICKY;
 }
}

package com.mirefir.nativedev;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

/** Держит запись и таймер живыми, пока приложение свёрнуто. */
public class RecService extends Service {
  static final String ACTION_LIVE = "com.mirefir.nativedev.REC_LIVE";
  static final String ACTION_TIMER = "com.mirefir.nativedev.REC_TIMER";
  static final String ACTION_STOP = "com.mirefir.nativedev.REC_STOP";
  private static final String CH = "mirefir.rec";
  private PowerManager.WakeLock wake;
  private final Handler h = new Handler(Looper.getMainLooper());
  private Rec rec;

  static void startLive(Context ctx) {
    Intent i = new Intent(ctx, RecService.class).setAction(ACTION_LIVE);
    ContextCompat.startForegroundService(ctx, i);
  }

  static void startTimerWatch(Context ctx) {
    Intent i = new Intent(ctx, RecService.class).setAction(ACTION_TIMER);
    ContextCompat.startForegroundService(ctx, i);
  }

  static void stop(Context ctx) {
    ctx.stopService(new Intent(ctx, RecService.class));
  }

  @Override
  public void onCreate() {
    super.onCreate();
    ensureChannel();
    try {
      PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
      if (pm != null) {
        wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "mirefir:rec");
        wake.acquire(6 * 3600_000L);
      }
    } catch (Exception ignored) {}
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    String act = intent == null ? ACTION_LIVE : intent.getAction();
    Notification n = note(I18n.t("recOn"));
    if (Build.VERSION.SDK_INT >= 29) {
      startForeground(42, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    } else {
      startForeground(42, n);
    }
    rec = Rec.get();
    if (ACTION_STOP.equals(act)) {
      if (rec != null) rec.stopLive();
      stopSelf();
      return START_NOT_STICKY;
    }
    h.removeCallbacks(tick);
    h.post(tick);
    return START_STICKY;
  }

  private final Runnable tick = new Runnable() {
    @Override public void run() {
      Rec r = Rec.get();
      if (r != null) r.serviceTick();
      if (r != null && (r.liveActive() || r.timerStatus != Rec.OFF)) {
        h.postDelayed(this, 1000);
      } else {
        stopSelf();
      }
    }
  };

  private Notification note(String text) {
    Intent open = new Intent(this, MainActivity.class);
    PendingIntent pi = PendingIntent.getActivity(this, 0, open,
        Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0);
    NotificationCompat.Builder b = new NotificationCompat.Builder(this, CH)
        .setContentTitle("MirEfir")
        .setContentText(text == null ? I18n.t("recOn") : text)
        .setSmallIcon(android.R.drawable.ic_media_play)
        .setContentIntent(pi)
        .setOngoing(true)
        .setPriority(NotificationCompat.PRIORITY_LOW);
    if (Build.VERSION.SDK_INT >= 29) {
      b.setCategory(Notification.CATEGORY_SERVICE);
    }
    return b.build();
  }

  private void ensureChannel() {
    if (Build.VERSION.SDK_INT < 26) return;
    NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
    if (nm == null) return;
    NotificationChannel ch = new NotificationChannel(CH, "MirEfir", NotificationManager.IMPORTANCE_LOW);
    ch.setDescription("Recording");
    nm.createNotificationChannel(ch);
  }

  @Override
  public void onDestroy() {
    h.removeCallbacks(tick);
    if (wake != null && wake.isHeld()) try { wake.release(); } catch (Exception ignored) {}
    super.onDestroy();
  }

  @Override
  public IBinder onBind(Intent intent) { return null; }
}

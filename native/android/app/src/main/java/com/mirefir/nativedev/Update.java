package com.mirefir.nativedev;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import java.io.File;
import org.json.JSONArray;
import org.json.JSONObject;

final class Update {
  interface Listener { void onReady(); }

  private final Context ctx;
  private final File dir;
  private final Handler h = new Handler(Looper.getMainLooper());
  volatile boolean chips;
  String version = "";
  File apk;
  boolean applyOnStart;
  boolean pendingInstall;
  Listener listener;

  Update(Context ctx) {
    this.ctx = ctx;
    dir = new File(ctx.getFilesDir(), "updates");
    dir.mkdirs();
    loadPending();
  }

  void start() {
    if (applyOnStart && hasFile()) {
      apply();
      return;
    }
    if (hasFile() && !applyOnStart) {
      chips = true;
      if (listener != null) listener.onReady();
    }
    new Thread(this::check, "upd").start();
  }

  boolean hasFile() {
    return apk != null && apk.isFile() && apk.length() > 64 * 1024 && newer(version, App.VERSION);
  }

  void later() {
    chips = false;
    applyOnStart = true;
    savePending();
    if (listener != null) listener.onReady();
  }

  boolean apply() {
    return applyFrom(ctx instanceof Activity ? (Activity) ctx : null);
  }

  boolean applyFrom(Activity act) {
    if (!hasFile()) return false;
    chips = false;
    try {
      if (Build.VERSION.SDK_INT >= 26) {
        if (!ctx.getPackageManager().canRequestPackageInstalls()) {
          pendingInstall = true;
          Intent s = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES);
          s.setData(Uri.parse("package:" + ctx.getPackageName()));
          if (act == null) s.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
          (act != null ? act : ctx).startActivity(s);
          return false;
        }
      }
      pendingInstall = false;
      Uri uri = FileProvider.getUriForFile(ctx, ctx.getPackageName() + ".files", apk);
      Intent i = new Intent(Intent.ACTION_INSTALL_PACKAGE);
      i.setDataAndType(uri, "application/vnd.android.package-archive");
      i.putExtra(Intent.EXTRA_NOT_UNKNOWN_SOURCE, true);
      i.putExtra(Intent.EXTRA_RETURN_RESULT, true);
      i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
      if (act == null) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      (act != null ? act : ctx).startActivity(i);
      return true;
    } catch (Exception e) {
      try {
        Uri uri = FileProvider.getUriForFile(ctx, ctx.getPackageName() + ".files", apk);
        Intent i = new Intent(Intent.ACTION_VIEW);
        i.setDataAndType(uri, "application/vnd.android.package-archive");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        ctx.startActivity(i);
        return true;
      } catch (Exception ignored) {
        return false;
      }
    }
  }

  private void check() {
    try { Thread.sleep(4000); } catch (InterruptedException ignored) {}
    if (hasFile()) return;
    String body = Http.get(App.GITHUB_API, "MirEfir-Updater", 20000, 2 * 1024 * 1024);
    if (body == null) return;
    try {
      JSONObject o = new JSONObject(body);
      String tag = o.optString("tag_name", o.optString("name", ""));
      if (tag.startsWith("v") || tag.startsWith("V")) tag = tag.substring(1);
      if (!newer(tag, App.VERSION)) return;
      JSONArray assets = o.optJSONArray("assets");
      String url = "";
      if (assets != null) {
        for (int i = 0; i < assets.length(); i++) {
          JSONObject a = assets.optJSONObject(i);
          if (a == null) continue;
          String name = a.optString("name", "").toLowerCase();
          if (!name.endsWith(".apk")) continue;
          url = a.optString("browser_download_url", "");
          if (name.contains("apk")) break;
        }
      }
      if (url.isEmpty()) return;
      File dest = new File(dir, "MirEfir.apk");
      if (!Http.getFile(url, "MirEfir-Updater", dest)) return;
      version = tag;
      apk = dest;
      applyOnStart = false;
      savePending();
      chips = true;
      h.post(() -> { if (listener != null) listener.onReady(); });
    } catch (Exception ignored) {}
  }

  private void loadPending() {
    String json = Util.read(new File(dir, "pending.json"));
    if (json == null) return;
    try {
      JSONObject o = new JSONObject(json);
      version = o.optString("v", "");
      apk = new File(o.optString("file", ""));
      applyOnStart = o.optInt("apply", 0) == 1;
    } catch (Exception ignored) {}
  }

  private void savePending() {
    try {
      JSONObject o = new JSONObject();
      o.put("v", version);
      o.put("file", apk == null ? "" : apk.getAbsolutePath());
      o.put("apply", applyOnStart ? 1 : 0);
      Util.write(new File(dir, "pending.json"), o.toString());
    } catch (Exception ignored) {}
  }

  static boolean newer(String remote, String local) {
    int[] a = parts(remote), b = parts(local);
    for (int i = 0; i < 6; i++) {
      if (a[i] > b[i]) return true;
      if (a[i] < b[i]) return false;
    }
    return false;
  }

  private static int[] parts(String v) {
    int[] o = new int[6];
    if (v == null) return o;
    if (v.startsWith("v") || v.startsWith("V")) v = v.substring(1);
    String[] p = v.split("\\.");
    for (int i = 0; i < p.length && i < 6; i++) {
      try { o[i] = Integer.parseInt(p[i].replaceAll("[^0-9]", "")); } catch (Exception ignored) {}
    }
    return o;
  }
}

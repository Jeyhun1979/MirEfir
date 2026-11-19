package com.mirefir.nativedev;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Однократный перенос из старого Capacitor/Chromium APK (тот же applicationId)
 * в native-профиль: settings.json, sources.json, playlist.m3u.
 */
final class Migrate {
  static void fromCapacitor(Context ctx, Profile p) {
    File marker = p.file("migrated-capacitor");
    if (marker.isFile()) return;
    if (p.file("settings.json").isFile() && p.file("sources.json").isFile()) {
      Util.write(marker, "1");
      return;
    }
    Map<String, String> kv = new HashMap<>();
    readPrefs(ctx, kv);
    readLocalStorageSqlite(ctx, kv);
    scanWebViewFiles(ctx, kv);
    readPersistFiles(ctx, kv);
    boolean any = apply(p, kv);
    if (any || !kv.isEmpty()) Util.write(marker, "1");
  }

  private static void readPrefs(Context ctx, Map<String, String> kv) {
    try {
      SharedPreferences sp = ctx.getSharedPreferences("CapacitorStorage", Context.MODE_PRIVATE);
      Map<String, ?> all = sp.getAll();
      for (Map.Entry<String, ?> e : all.entrySet()) {
        if (e.getValue() != null) put(kv, e.getKey(), String.valueOf(e.getValue()));
      }
    } catch (Exception ignored) {}
    try {
      SharedPreferences sp = ctx.getSharedPreferences("CapacitorStorage.xml", Context.MODE_PRIVATE);
      for (Map.Entry<String, ?> e : sp.getAll().entrySet()) {
        if (e.getValue() != null) put(kv, e.getKey(), String.valueOf(e.getValue()));
      }
    } catch (Exception ignored) {}
  }

  private static void readLocalStorageSqlite(Context ctx, Map<String, String> kv) {
    File data = new File(ctx.getApplicationInfo().dataDir);
    File[] roots = {
        new File(data, "app_webview"),
        new File(data, "app_webview/Default/Local Storage"),
        new File(data, "app_webview/Local Storage")
    };
    for (File root : roots) walkSqlite(root, kv, 0);
  }

  private static void walkSqlite(File dir, Map<String, String> kv, int depth) {
    if (dir == null || !dir.isDirectory() || depth > 4) return;
    File[] files = dir.listFiles();
    if (files == null) return;
    for (File f : files) {
      if (f.isDirectory()) walkSqlite(f, kv, depth + 1);
      else if (f.getName().endsWith(".localstorage") || f.getName().contains("localstorage")) readSqlite(f, kv);
    }
  }

  private static void readSqlite(File f, Map<String, String> kv) {
    SQLiteDatabase db = null;
    try {
      db = SQLiteDatabase.openDatabase(f.getAbsolutePath(), null, SQLiteDatabase.OPEN_READONLY);
      try (Cursor c = db.rawQuery("SELECT key, value FROM ItemTable", null)) {
        while (c.moveToNext()) {
          String key = c.getString(0);
          String val;
          try { val = c.getString(1); } catch (Exception e) {
            byte[] b = c.getBlob(1);
            val = b == null ? "" : new String(b, StandardCharsets.UTF_16LE);
          }
          put(kv, key, val);
        }
      }
    } catch (Exception ignored) {
    } finally {
      if (db != null) try { db.close(); } catch (Exception ignored) {}
    }
  }

  private static void scanWebViewFiles(Context ctx, Map<String, String> kv) {
    File root = new File(ctx.getApplicationInfo().dataDir, "app_webview");
    walkBytes(root, kv, 0);
  }

  private static void walkBytes(File dir, Map<String, String> kv, int depth) {
    if (dir == null || !dir.isDirectory() || depth > 5) return;
    File[] files = dir.listFiles();
    if (files == null) return;
    for (File f : files) {
      if (f.isDirectory()) {
        walkBytes(f, kv, depth + 1);
        continue;
      }
      String n = f.getName();
      if (n.endsWith(".ldb") || n.endsWith(".log") || n.equals("CURRENT") || n.endsWith(".sst")) {
        extractKeys(f, kv);
      }
    }
  }

  private static void extractKeys(File f, Map<String, String> kv) {
    if (f.length() > 8 * 1024 * 1024) return;
    String raw = Util.read(f);
    if (raw == null) return;
    String[] names = {
        "mirefir.settings", "mirefir.playlistUrl", "mirefir.playlistText", "mirefir.epgUrl",
        "mirefir.favorites", "mirefir.history", "mirefir.session", "mirefir.volume", "mirefir.muted"
    };
    for (String name : names) {
      int i = raw.indexOf(name);
      if (i < 0) continue;
      int start = i + name.length();
      while (start < raw.length() && raw.charAt(start) < 32) start++;
      if (start >= raw.length()) continue;
      char c = raw.charAt(start);
      if (c == '{' || c == '[') {
        int end = findJsonEnd(raw, start);
        if (end > start) put(kv, name, raw.substring(start, end));
      } else if (c == '"' ) {
        int e = raw.indexOf('"', start + 1);
        if (e > start) put(kv, name, raw.substring(start + 1, e));
      } else {
        int e = start;
        while (e < raw.length() && raw.charAt(e) >= 32 && raw.charAt(e) != '\n') e++;
        String v = raw.substring(start, e).trim();
        if (!v.isEmpty() && v.length() < 8000) put(kv, name, v);
      }
    }
  }

  private static int findJsonEnd(String s, int start) {
    int depth = 0;
    boolean inStr = false, esc = false;
    for (int i = start; i < s.length() && i < start + 400000; i++) {
      char c = s.charAt(i);
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"') inStr = false;
        continue;
      }
      if (c == '"') inStr = true;
      else if (c == '{' || c == '[') depth++;
      else if (c == '}' || c == ']') {
        depth--;
        if (depth == 0) return i + 1;
      }
    }
    return -1;
  }

  private static void readPersistFiles(Context ctx, Map<String, String> kv) {
    File[] dirs = {
        ctx.getFilesDir(),
        new File(ctx.getFilesDir(), "profile"),
        ctx.getExternalFilesDir(null)
    };
    String[] names = {"mirefir-config.json", "persist.json", "backup.json"};
    for (File d : dirs) {
      if (d == null) continue;
      for (String n : names) {
        String json = Util.read(new File(d, n));
        if (json != null && json.contains("{")) put(kv, "persist:" + n, json);
      }
    }
  }

  private static void put(Map<String, String> kv, String key, String val) {
    if (key == null || val == null || val.isEmpty()) return;
    int i = key.lastIndexOf('\u0000');
    if (i >= 0 && i + 1 < key.length()) key = key.substring(i + 1);
    if (key.startsWith("\u0001")) key = key.substring(1);
    kv.put(key, val);
  }

  private static boolean apply(Profile p, Map<String, String> kv) {
    boolean any = false;
    String settings = first(kv, "mirefir.settings", "settings");
    if (settings != null && settings.startsWith("{")) {
      p.applySettings(settings);
      try {
        JSONObject o = new JSONObject(settings);
        if (o.has("playlists") || o.has("epgSources") || o.has("epgUrl")) {
          p.applySources(settings);
        }
      } catch (Exception ignored) {}
      any = true;
    }
    for (String k : kv.keySet()) {
      if (k.startsWith("persist:")) {
        p.importBackup(kv.get(k));
        any = true;
      }
    }
    String plUrl = first(kv, "mirefir.playlistUrl", "playlistUrl");
    String plText = first(kv, "mirefir.playlistText", "playlistText");
    if (plText != null && plText.contains("#EXT")) {
      p.addLocalM3u("migrated", plText);
      any = true;
    } else if (plUrl != null && Util.looksUrl(plUrl)) {
      p.addPlaylistUrl(plUrl);
      any = true;
    }
    String epg = first(kv, "mirefir.epgUrl", "epgUrl");
    if (epg != null && Util.looksUrl(epg)) {
      p.addEpgUrl(epg, 1);
      any = true;
    }
    String fav = first(kv, "mirefir.favorites", "favorites");
    if (fav != null) {
      try {
        JSONArray a = new JSONArray(fav);
        for (int i = 0; i < a.length(); i++) {
          String item = a.optString(i, "");
          if (!item.isEmpty() && !p.st.favIds.contains(item)) p.st.favIds.add(item);
        }
        any = true;
      } catch (Exception ignored) {}
    }
    String session = first(kv, "mirefir.session", "session");
    if (session != null && session.startsWith("{")) {
      p.applySession(session);
      any = true;
    }
    String vol = first(kv, "mirefir.volume", "volume");
    if (vol != null) {
      try {
        double v = Double.parseDouble(vol);
        p.st.volume = (v > 0 && v <= 1) ? (int) Math.round(v * 100) : (int) Math.round(v);
      } catch (Exception ignored) {}
    }
    p.clamp();
    p.normalizeEpg();
    p.saveSettings();
    p.saveSources();
    p.saveSession();
    return any;
  }

  private static String first(Map<String, String> kv, String... keys) {
    for (String k : keys) {
      String v = kv.get(k);
      if (v != null && !v.isEmpty()) return v;
    }
    return null;
  }
}

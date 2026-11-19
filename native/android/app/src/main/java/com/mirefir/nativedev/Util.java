package com.mirefir.nativedev;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class Util {
  static boolean starts(String s, String pfx) {
    return s != null && pfx != null && s.regionMatches(true, 0, pfx, 0, pfx.length());
  }

  static boolean has(String s, String part) {
    if (s == null || part == null) return false;
    return s.toLowerCase(Locale.ROOT).contains(part.toLowerCase(Locale.ROOT));
  }

  static String clip(String s, int max) {
    if (s == null) return "";
    return s.length() <= max ? s : s.substring(0, max);
  }

  static String others() {
    return I18n.t("others");
  }

  static String read(File f) {
    if (f == null || !f.isFile()) return null;
    try (FileInputStream in = new FileInputStream(f)) {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buf = new byte[16384];
      int n;
      while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
      return stripBom(out.toString("UTF-8"));
    } catch (Exception e) {
      return null;
    }
  }

  static String stripBom(String s) {
    if (s != null && !s.isEmpty() && s.charAt(0) == '\uFEFF') return s.substring(1);
    return s;
  }

  static boolean write(File f, String text) {
    if (f == null) return false;
    File parent = f.getParentFile();
    if (parent != null) parent.mkdirs();
    File tmp = new File(f.getAbsolutePath() + ".tmp");
    try (FileOutputStream out = new FileOutputStream(tmp)) {
      out.write((text == null ? "" : text).getBytes(StandardCharsets.UTF_8));
      out.flush();
    } catch (Exception e) {
      try { tmp.delete(); } catch (Exception ignored) {}
      return false;
    }
    if (f.exists() && !f.delete()) {
      try { tmp.delete(); } catch (Exception ignored) {}
      return false;
    }
    if (!tmp.renameTo(f)) {
      try { tmp.delete(); } catch (Exception ignored) {}
      return false;
    }
    return true;
  }

  static String clipboard(Context ctx) {
    try {
      ClipboardManager cm = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
      if (cm == null || !cm.hasPrimaryClip()) return "";
      ClipData data = cm.getPrimaryClip();
      if (data == null || data.getItemCount() < 1) return "";
      CharSequence t = data.getItemAt(0).coerceToText(ctx);
      return t == null ? "" : t.toString().trim();
    } catch (Exception e) {
      return "";
    }
  }

  static void setClipboard(Context ctx, String text) {
    ClipboardManager cm = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
    if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("MirEfir", text == null ? "" : text));
  }

  static boolean looksUrl(String s) {
    return starts(s, "http://") || starts(s, "https://") || starts(s, "file:");
  }

  static String urlHint(String url) {
    if (url == null) return "";
    String s = url.trim();
    if (s.length() <= 90) return s;
    return s.substring(s.length() - 90);
  }

  static String nameFromUrl(String url) {
    if (url == null || url.isEmpty()) return I18n.t("source");
    int end = url.length();
    int q = url.indexOf('?');
    int h = url.indexOf('#');
    if (q >= 0) end = Math.min(end, q);
    if (h >= 0) end = Math.min(end, h);
    int slash = 0;
    for (int i = 0; i < end; i++) {
      char c = url.charAt(i);
      if (c == '/' || c == '\\') slash = i + 1;
    }
    String name = slash < end ? url.substring(slash, end) : "";
    if (name.isEmpty() || "/".equals(name)) return I18n.t("source");
    return clip(name, Limits.MAX_NAME);
  }

  static String enc(String s) {
    if (s == null) return "";
    try {
      return java.net.URLEncoder.encode(s, "UTF-8").replace("+", "%20");
    } catch (Exception e) {
      return s;
    }
  }

  static boolean unzip(File zip, File dest) {
    if (zip == null || dest == null || !zip.isFile()) return false;
    dest.mkdirs();
    byte[] buf = new byte[65536];
    try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
      ZipEntry e;
      while ((e = in.getNextEntry()) != null) {
        String name = e.getName();
        if (name == null || name.contains("..")) continue;
        File out = new File(dest, name);
        if (e.isDirectory()) {
          out.mkdirs();
          continue;
        }
        File parent = out.getParentFile();
        if (parent != null) parent.mkdirs();
        try (FileOutputStream fos = new FileOutputStream(out)) {
          int n;
          while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
        }
      }
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  static void copyStream(InputStream in, File dest) throws Exception {
    File parent = dest.getParentFile();
    if (parent != null) parent.mkdirs();
    try (FileOutputStream out = new FileOutputStream(dest)) {
      byte[] buf = new byte[8192];
      int n;
      while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }
  }

  static int atoi(String s) {
    if (s == null) return 0;
    int i = 0, n = s.length(), sign = 1, v = 0;
    while (i < n && (s.charAt(i) == ' ' || s.charAt(i) == '\t')) i++;
    if (i < n && (s.charAt(i) == '+' || s.charAt(i) == '-')) {
      if (s.charAt(i) == '-') sign = -1;
      i++;
    }
    while (i < n) {
      char c = s.charAt(i);
      if (c < '0' || c > '9') break;
      v = v * 10 + (c - '0');
      i++;
    }
    return v * sign;
  }

  static boolean containsI(String hay, String needle) {
    if (hay == null || needle == null || needle.isEmpty()) return needle != null && needle.isEmpty();
    return hay.toLowerCase(Locale.ROOT).contains(needle.toLowerCase(Locale.ROOT));
  }

  static String jsonEsc(String s) {
    if (s == null) return "";
    StringBuilder b = new StringBuilder(s.length() + 8);
    for (int i = 0; i < s.length(); i++) {
      char c = s.charAt(i);
      if (c == '\\' || c == '"') b.append('\\');
      if (c == '\n') b.append("\\n");
      else if (c == '\r') b.append("\\r");
      else b.append(c);
    }
    return b.toString();
  }
}

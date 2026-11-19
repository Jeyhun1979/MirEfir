package com.mirefir.nativedev;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.zip.GZIPInputStream;
import java.util.zip.Inflater;
import java.util.zip.InflaterInputStream;

final class Http {
  static String get(String url, String ua, int timeoutMs, int maxBytes) {
    byte[] raw = getBytes(url, ua, timeoutMs, maxBytes);
    if (raw == null) return null;
    try {
      return new String(raw, "UTF-8");
    } catch (Exception e) {
      return null;
    }
  }

  static boolean getFile(String url, String ua, File dest) {
    return getFile(url, ua, dest, 20000, 64 * 1024);
  }

  interface Progress {
    void onProgress(long done, long total);
  }

  static boolean getFile(String url, String ua, File dest, int timeoutMs, long minBytes) {
    return getFile(url, ua, dest, timeoutMs, minBytes, null);
  }

  static boolean getFile(String url, String ua, File dest, int timeoutMs, long minBytes, Progress pr) {
    if (url != null && url.startsWith("http://")) {
      if (getFileOnce("https://" + url.substring(7), ua, dest, timeoutMs, minBytes, pr)) return true;
    }
    return getFileOnce(url, ua, dest, timeoutMs, minBytes, pr);
  }

  private static boolean getFileOnce(String url, String ua, File dest, int timeoutMs, long minBytes, Progress pr) {
    HttpURLConnection c = null;
    File part = null;
    try {
      c = openFollow(url, ua, timeoutMs);
      if (c == null || c.getResponseCode() != 200) return false;
      InputStream in = wrap(c);
      File parent = dest.getParentFile();
      if (parent != null) parent.mkdirs();
      part = new File(dest.getAbsolutePath() + ".part");
      try (FileOutputStream out = new FileOutputStream(part)) {
        byte[] buf = new byte[65536];
        int n;
        long total = c.getContentLengthLong(), got = 0;
        while ((n = in.read(buf)) > 0) {
          out.write(buf, 0, n);
          got += n;
          if (pr != null) pr.onProgress(got, total);
        }
      }
      if (dest.exists()) dest.delete();
      return part.renameTo(dest) && dest.length() >= minBytes;
    } catch (Exception e) {
      if (part != null) try { part.delete(); } catch (Exception ignored) {}
      return false;
    } finally {
      if (c != null) c.disconnect();
    }
  }

  interface StreamBody {
    boolean accept(InputStream in) throws Exception;
  }

  static boolean readStream(String url, String ua, int timeoutMs, StreamBody body) {
    HttpURLConnection c = null;
    try {
      c = openFollow(url, ua, timeoutMs);
      if (c == null || c.getResponseCode() != 200) return false;
      return body.accept(wrap(c));
    } catch (Exception e) {
      return false;
    } finally {
      if (c != null) c.disconnect();
    }
  }

  static byte[] getBytes(String url, String ua, int timeoutMs, int maxBytes) {
    if (maxBytes > 0 && maxBytes < 64 * 1024) maxBytes = 32 * 1024 * 1024;
    if (url != null && url.startsWith("http://")) {
      byte[] https = getBytesOnce("https://" + url.substring(7), ua, timeoutMs, maxBytes);
      if (https != null && https.length > 0) return https;
    }
    return getBytesOnce(url, ua, timeoutMs, maxBytes);
  }

  private static byte[] getBytesOnce(String url, String ua, int timeoutMs, int maxBytes) {
    HttpURLConnection c = null;
    try {
      c = openFollow(url, ua, timeoutMs);
      if (c == null || c.getResponseCode() != 200) return null;
      InputStream in = wrap(c);
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buf = new byte[16384];
      int n;
      int used = 0;
      while ((n = in.read(buf)) > 0) {
        used += n;
        if (maxBytes > 0 && used > maxBytes) return null;
        out.write(buf, 0, n);
      }
      byte[] raw = out.toByteArray();
      return raw.length > 0 ? raw : null;
    } catch (Exception e) {
      return null;
    } finally {
      if (c != null) c.disconnect();
    }
  }

  private static HttpURLConnection openFollow(String url, String ua, int timeoutMs) throws Exception {
    String cur = url;
    for (int hop = 0; hop < 5; hop++) {
      HttpURLConnection c = open(cur, ua, timeoutMs);
      c.setInstanceFollowRedirects(false);
      int code = c.getResponseCode();
      if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
        String loc = c.getHeaderField("Location");
        c.disconnect();
        if (loc == null || loc.isEmpty()) return null;
        cur = absUrl(cur, loc);
        continue;
      }
      return c;
    }
    return null;
  }

  private static String absUrl(String current, String loc) {
    try {
      return new URL(new URL(current), loc).toString();
    } catch (Exception e) {
      return loc;
    }
  }

  private static HttpURLConnection open(String url, String ua, int timeoutMs) throws Exception {
    int t = Math.max(3000, timeoutMs);
    HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
    c.setInstanceFollowRedirects(false);
    c.setConnectTimeout(t);
    c.setReadTimeout(t);
    c.setRequestProperty("User-Agent", ua == null || ua.isEmpty() ? "MirEfir/1.0" : ua);
    c.setRequestProperty("Accept-Encoding", "gzip, deflate");
    return c;
  }

  private static InputStream wrap(HttpURLConnection c) throws Exception {
    InputStream in = c.getInputStream();
    String enc = c.getContentEncoding();
    if (enc == null) enc = "";
    enc = enc.toLowerCase();
    if (enc.contains("gzip")) return new GZIPInputStream(in);
    if (enc.contains("deflate")) return new InflaterInputStream(in, new Inflater(true));
    return in;
  }
}

package com.mirefir.nativedev;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

final class Catchup {
  static boolean channelHas(Channel ch) {
    if (ch == null) return false;
    if (ch.catchupDays > 0) return true;
    if (ch.catchupSource != null && !ch.catchupSource.isEmpty()) return true;
    String t = ch.catchup == null ? "" : ch.catchup.trim().toLowerCase(Locale.ROOT);
    return !(t.isEmpty() || t.equals("0") || t.equals("false") || t.equals("none") || t.equals("off") || t.equals("no"));
  }

  static int catchupDays(Channel ch, Settings st) {
    int a = ch == null ? 0 : ch.catchupDays;
    int b = st == null ? 0 : st.archiveDays;
    if (a > 0 && b > 0) return Math.min(a, b);
    if (a > 0) return a;
    if (b > 0) return b;
    return 7;
  }

  static boolean programHasArchive(Settings st, long start, long end) {
    long now = System.currentTimeMillis();
    if (end <= 0) end = start + 3600_000L;
    if (end > now) return false;
    long oldest = now - catchupWindow(st);
    return end >= oldest;
  }

  static boolean programOk(Channel ch, Settings st, long start, long end) {
    if (st == null || !st.archiveEnabled || !channelHas(ch) || start <= 0) return false;
    long now = System.currentTimeMillis();
    if (start >= now) return false;
    long finish = end > start ? end : start + 3600_000L;
    long oldest = now - catchupWindow(st);
    return finish >= oldest;
  }

  private static long catchupWindow(Settings st) {
    int d = st == null ? 7 : Math.max(1, st.archiveDays);
    return d * 24L * 3600L * 1000L;
  }

  static List<String> urls(Channel ch, long startMs, long endMs) {
    List<String> out = new ArrayList<>();
    if (ch == null || !channelHas(ch) || startMs <= 0) return out;
    long now = System.currentTimeMillis();
    long finish = endMs > startMs ? endMs : startMs + 3600_000L;
    if (finish > now - 1500) finish = now - 1500;
    if (finish <= startMs) return out;
    String type = ch.catchup == null ? "" : ch.catchup.toLowerCase(Locale.ROOT);
    boolean index = Util.has(ch.url, "/index.m3u8");
    String[] xp = parseXtream(ch.url);
    boolean xtType = type.contains("xc") || type.contains("default") || type.contains("shift")
        || type.contains("timeshift") || type.isEmpty();
    boolean xtream = xtType && xp != null;
    boolean flussonic = type.contains("flussonic") || type.contains("fs") || index || Util.has(ch.url, "video.m3u8");
    boolean append = type.contains("append") || type.contains("shift") || type.contains("default")
        || flussonic || type.isEmpty();
    if (index) add(out, appendUtc(ch.url, startMs, now - 1500), ch.url);
    if (ch.catchupSource != null && !ch.catchupSource.isEmpty())
      add(out, fill(ch.catchupSource, startMs, finish), ch.url);
    if (flussonic || append) add(out, appendUtc(ch.url, startMs, finish), ch.url);
    if (xtream) {
      int duration = (int) ((finish - startMs + 30000) / 60000);
      if (duration < 1) duration = 1;
      String host = xp[0], user = xp[1], pass = xp[2], id = xp[3];
      String unix = Long.toString(startMs / 1000);
      String loc = stamp(startMs, false, true);
      String utc = stamp(startMs, true, true);
      String locNo = stamp(startMs, false, false);
      String utcNo = stamp(startMs, true, false);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + unix + "/" + id + ".m3u8", ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + loc + "/" + id + ".m3u8", ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + utc + "/" + id + ".m3u8", ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + locNo + "/" + id + ".m3u8", ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + utcNo + "/" + id + ".m3u8", ch.url);
      add(out, host + "/streaming/timeshift.php?username=" + Util.enc(user) + "&password=" + Util.enc(pass)
          + "&stream=" + Util.enc(id) + "&start=" + loc + "&duration=" + duration, ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + loc + "/" + id + ".ts", ch.url);
      add(out, host + "/timeshift/" + user + "/" + pass + "/" + duration + "/" + unix + "/" + id + ".ts", ch.url);
    }
    return out;
  }

  private static void add(List<String> out, String url, String live) {
    if (url == null || url.isEmpty() || url.equals(live) || out.contains(url)) return;
    if (out.size() >= 12) return;
    out.add(url);
  }

  private static String appendUtc(String url, long start, long end) {
    String u = url;
    int q = u.indexOf('?');
    if (q >= 0) {
      String kept = "";
      String rest = u.substring(q + 1);
      u = u.substring(0, q);
      for (String part : rest.split("&")) {
        String low = part.toLowerCase(Locale.ROOT);
        if (low.startsWith("utc=") || low.startsWith("lutc=") || low.startsWith("duration=")) continue;
        kept = kept.isEmpty() ? part : kept + "&" + part;
      }
      if (!kept.isEmpty()) u = u + "?" + kept;
    }
    u += (u.contains("?") ? "&" : "?") + "utc=" + (start / 1000) + "&lutc=" + (end / 1000);
    return u;
  }

  private static String fill(String tmpl, long start, long end) {
    String out = tmpl;
    long utc = start / 1000, lutc = end / 1000;
    int durationSec = (int) Math.max(1, lutc - utc);
    int durationMin = (int) Math.max(1, (end - start + 30000) / 60000);
    boolean xt = tmpl.contains("timeshift");
    String stamp = stamp(start, false, false);
    String utcS = Long.toString(utc);
    String lutcS = Long.toString(lutc);
    String durS = Integer.toString(xt ? durationMin : durationSec);
    String minS = Integer.toString(durationMin);
    long offset = Math.max(0, (System.currentTimeMillis() - start) / 1000);
    out = out.replace("${utc}", utcS);
    out = out.replace("${timestamp}", utcS);
    out = out.replace("${start}", stamp);
    out = out.replace("${duration}", durS);
    out = out.replace("${end}", lutcS);
    out = out.replace("${lutc}", lutcS);
    out = out.replace("${utcend}", lutcS);
    out = out.replace("${minutes}", minS);
    out = out.replace("{utc}", utcS);
    out = out.replace("{start}", xt ? stamp : utcS);
    out = out.replace("{lutc}", lutcS);
    out = out.replace("{utcend}", lutcS);
    out = out.replace("{end}", lutcS);
    out = out.replace("{duration}", durS);
    out = out.replace("{minutes}", minS);
    out = out.replace("{offset}", Long.toString(offset));
    Calendar loc = Calendar.getInstance();
    loc.setTimeInMillis(start);
    Calendar u = Calendar.getInstance(TimeZone.getTimeZone("UTC"));
    u.setTimeInMillis(start);
    out = out.replace("{Y}", String.format(Locale.US, "%04d", loc.get(Calendar.YEAR)));
    out = out.replace("{m}", String.format(Locale.US, "%02d", loc.get(Calendar.MONTH) + 1));
    out = out.replace("{d}", String.format(Locale.US, "%02d", loc.get(Calendar.DAY_OF_MONTH)));
    out = out.replace("{H}", String.format(Locale.US, "%02d", loc.get(Calendar.HOUR_OF_DAY)));
    out = out.replace("{M}", String.format(Locale.US, "%02d", loc.get(Calendar.MINUTE)));
    out = out.replace("{S}", String.format(Locale.US, "%02d", loc.get(Calendar.SECOND)));
    out = out.replace("{utcY}", String.format(Locale.US, "%04d", u.get(Calendar.YEAR)));
    out = out.replace("{utcm}", String.format(Locale.US, "%02d", u.get(Calendar.MONTH) + 1));
    out = out.replace("{utcd}", String.format(Locale.US, "%02d", u.get(Calendar.DAY_OF_MONTH)));
    out = out.replace("{utcH}", String.format(Locale.US, "%02d", u.get(Calendar.HOUR_OF_DAY)));
    out = out.replace("{utcM}", String.format(Locale.US, "%02d", u.get(Calendar.MINUTE)));
    return out;
  }

  private static String stamp(long ms, boolean utc, boolean withSec) {
    Calendar c = Calendar.getInstance(utc ? TimeZone.getTimeZone("UTC") : TimeZone.getDefault());
    c.setTimeInMillis(ms);
    if (withSec)
      return String.format(Locale.US, "%04d-%02d-%02d:%02d-%02d-%02d",
          c.get(Calendar.YEAR), c.get(Calendar.MONTH) + 1, c.get(Calendar.DAY_OF_MONTH),
          c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE), c.get(Calendar.SECOND));
    return String.format(Locale.US, "%04d-%02d-%02d:%02d-%02d",
        c.get(Calendar.YEAR), c.get(Calendar.MONTH) + 1, c.get(Calendar.DAY_OF_MONTH),
        c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE));
  }

  private static String[] parseXtream(String url) {
    if (url == null) return null;
    String p = url;
    String scheme;
    if (Util.starts(p, "https://")) { scheme = "https://"; p = p.substring(8); }
    else if (Util.starts(p, "http://")) { scheme = "http://"; p = p.substring(7); }
    else return null;
    int slash = p.indexOf('/');
    if (slash < 0) return null;
    String host = scheme + p.substring(0, slash);
    String rest = p.substring(slash + 1);
    int q = rest.indexOf('?');
    if (q >= 0) rest = rest.substring(0, q);
    String[] parts = rest.split("/");
    if (parts.length < 3) return null;
    int i0 = 0;
    String first = parts[0].toLowerCase(Locale.ROOT);
    if (first.equals("timeshift") || first.equals("hls") || first.equals("streaming")
        || first.equals("xmltv") || first.equals("player_api") || first.equals("panel_api")) return null;
    if (parts[0].equalsIgnoreCase("live") || parts[0].equalsIgnoreCase("play")) i0 = 1;
    if (parts.length - i0 < 3) return null;
    String user = parts[i0], pass = parts[i0 + 1], id = parts[i0 + 2];
    int dot = id.lastIndexOf('.');
    if (dot > 0) id = id.substring(0, dot);
    if (user.isEmpty() || pass.isEmpty() || id.isEmpty()) return null;
    return new String[] {host, user, pass, id};
  }
}

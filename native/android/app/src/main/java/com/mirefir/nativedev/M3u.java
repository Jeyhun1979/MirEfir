package com.mirefir.nativedev;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class M3u {
  static int qualityOf(String name, String url) {
    String t = ((name == null ? "" : name) + " " + (url == null ? "" : url)).toLowerCase(Locale.ROOT);
    if (t.contains("4k") || t.contains("uhd") || t.contains("2160") || t.contains("hevc")
        || t.contains("h265") || t.contains("h.265")) return 4;
    if (t.contains("fhd") || t.contains("1080")) return 3;
    if (t.contains("720") || t.contains(" hd") || t.contains("hd ") || t.equals("hd")) return 2;
    if (t.contains("sd") || t.contains("480") || t.contains("576")) return 1;
    return 3;
  }

  static String qualityLabel(int q) {
    if (q >= 4) return "4K";
    if (q == 3) return "FHD";
    if (q == 2) return "HD";
    return "SD";
  }

  static String foldFamily(String name) {
    if (name == null) return "";
    String lower = name.toLowerCase(Locale.ROOT).replace('ё', 'е');
    StringBuilder b = new StringBuilder();
    for (int i = 0; i < lower.length(); i++) {
      char c = lower.charAt(i);
      boolean ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'а' && c <= 'я');
      if (!ok) {
        if (b.length() > 0 && b.charAt(b.length() - 1) != ' ') b.append(' ');
      } else b.append(c);
    }
    StringBuilder out = new StringBuilder();
    for (String tok : b.toString().trim().split(" +")) {
      if (tok.isEmpty() || isQualityToken(tok) || isFamilyNoise(tok)) continue;
      if (out.length() > 0) out.append(' ');
      out.append(tok);
    }
    return out.toString();
  }

  /** Служебные слова имени канала (как в fold_family на Windows). */
  private static boolean isFamilyNoise(String tok) {
    return tok.equals("tv") || tok.equals("тв") || tok.equals("канал") || tok.equals("телеканал")
        || tok.equals("программа");
  }

  /**
   * Суффикс качества / кадровой частоты / кодека: UHD, 4K, 8K, FHD, HD, SD, HEVC, HDR, H264/H265,
   * 2160p, 1080p, 720p, 576p, 50fps, 60fps, а также слитные «uhd4k», «hd50». Токен уже в нижнем регистре.
   */
  static boolean isQualityToken(String tok) {
    if (tok == null || tok.isEmpty()) return false;
    switch (tok) {
      case "uhd": case "fhd": case "qhd": case "hd": case "sd": case "4k": case "8k": case "2k":
      case "hevc": case "hdr": case "hdr10": case "sdr": case "h264": case "h265": case "x264": case "x265":
      case "avc": case "lq": case "hq": case "ultrahd":
        return true;
      default:
        break;
    }
    /* 1080p / 1080i / 720p / 2160p / 576i */
    if (tok.length() >= 4 && tok.length() <= 5) {
      char last = tok.charAt(tok.length() - 1);
      if (last == 'p' || last == 'i') {
        boolean digits = true;
        for (int i = 0; i < tok.length() - 1; i++) if (tok.charAt(i) < '0' || tok.charAt(i) > '9') digits = false;
        if (digits) return true;
      }
    }
    /* 25fps / 50fps / 60fps / 50hz */
    if (tok.endsWith("fps") || tok.endsWith("hz")) {
      String num = tok.substring(0, tok.length() - (tok.endsWith("fps") ? 3 : 2));
      if (!num.isEmpty()) {
        boolean digits = true;
        for (int i = 0; i < num.length(); i++) if (num.charAt(i) < '0' || num.charAt(i) > '9') digits = false;
        if (digits) return true;
      }
    }
    return false;
  }

  static List<String> playUrls(List<Channel> pl, int index, int startQ) {
    List<String> urls = new ArrayList<>();
    if (pl == null || index < 0 || index >= pl.size()) return urls;
    addPair(urls, pl.get(index).url);
    return urls;
  }

  /**
   * Для соседних окон мультиэкрана: тот же канал в HD, иначе SD, иначе исходный индекс.
   * Другой FHD/UHD не берём — фокусное окно само играет полный поток.
   */
  static int lighterIndex(List<Channel> pl, int index) {
    if (pl == null || index < 0 || index >= pl.size()) return index;
    Channel ch = pl.get(index);
    if (ch.quality > 0 && ch.quality <= 2) return index;
    String fam = ch.family;
    if (fam == null || fam.isEmpty()) return index;
    int hd = -1, sd = -1;
    for (int i = 0; i < pl.size(); i++) {
      Channel c = pl.get(i);
      if (c.family == null || !fam.equals(c.family)) continue;
      if (c.quality == 2 && hd < 0) hd = i;
      else if (c.quality == 1 && sd < 0) sd = i;
    }
    if (hd >= 0) return hd;
    if (sd >= 0) return sd;
    return index;
  }

  private static void addPair(List<String> urls, String url) {
    if (url == null || url.isEmpty() || urls.contains(url)) return;
    urls.add(url);
    if (url.contains("/live/")) {
      String alt = swapExt(url);
      if (alt != null && !urls.contains(alt)) urls.add(alt);
    }
  }

  private static String swapExt(String url) {
    int q = url.indexOf('?');
    String base = q >= 0 ? url.substring(0, q) : url;
    String query = q >= 0 ? url.substring(q) : "";
    String low = base.toLowerCase(Locale.ROOT);
    if (low.endsWith(".m3u8")) return base.substring(0, base.length() - 5) + ".ts" + query;
    if (low.endsWith(".ts")) return base.substring(0, base.length() - 3) + ".m3u8" + query;
    return null;
  }

  static List<Channel> parse(String text) {
    List<Channel> out = new ArrayList<>();
    if (text == null || text.isEmpty()) return out;
    Channel pending = null;
    boolean haveGroupTitle = false;
    String ua = "", ref = "", lastGroup = "", listCatchup = "", listSrc = "";
    int listDays = 0;
    String[] lines = text.split("\n");
    for (String raw : lines) {
      if (raw.length() > Limits.MAX_URL + 64) raw = raw.substring(0, Limits.MAX_URL + 64);
      String s = raw.trim();
      if (s.isEmpty()) continue;
      if (Util.starts(s, "#EXTM3U")) {
        listCatchup = attr(s, "catchup=");
        if (listCatchup.isEmpty()) listCatchup = attr(s, "catchup-type=");
        listSrc = attr(s, "catchup-source=");
        String days = attr(s, "catchup-days=");
        if (days.isEmpty()) days = attr(s, "tvg-rec=");
        if (days.isEmpty()) days = attr(s, "timeshift=");
        int d = Util.atoi(days);
        if (d > 0 && d <= 40) listDays = d;
        continue;
      }
      if (Util.starts(s, "#EXTVLCOPT:")) {
        String body = s.substring(11);
        if (Util.starts(body, "http-user-agent=")) ua = Util.clip(body.substring(16).trim(), Limits.MAX_UA);
        else if (Util.starts(body, "http-referrer=")) ref = Util.clip(body.substring(14).trim(), Limits.MAX_REF);
        else if (Util.starts(body, "http-referer=")) ref = Util.clip(body.substring(13).trim(), Limits.MAX_REF);
        continue;
      }
      if (Util.starts(s, "#EXTGRP:")) {
        lastGroup = Util.clip(s.substring(8).trim(), Limits.MAX_NAME);
        if (pending != null && !haveGroupTitle) pending.group = lastGroup;
        continue;
      }
      if (Util.starts(s, "#EXTINF:")) {
        pending = new Channel();
        haveGroupTitle = false;
        int comma = s.lastIndexOf(',');
        pending.name = Util.clip((comma >= 0 ? s.substring(comma + 1) : I18n.t("channel")).trim(), Limits.MAX_NAME);
        if (pending.name.isEmpty()) pending.name = I18n.t("channel");
        pending.ua = ua;
        pending.referer = ref;
        pending.group = attr(s, "group-title=");
        if (!pending.group.isEmpty()) haveGroupTitle = true;
        if (pending.group.isEmpty()) pending.group = lastGroup;
        pending.number = Util.atoi(attr(s, "tvg-chno="));
        pending.tvgId = Util.clip(attr(s, "tvg-id="), 96);
        pending.tvgName = Util.clip(attr(s, "tvg-name="), Limits.MAX_NAME);
        pending.logo = Util.clip(attr(s, "tvg-logo="), Limits.MAX_URL);
        pending.catchup = attr(s, "catchup=");
        if (pending.catchup.isEmpty()) pending.catchup = attr(s, "catchup-type=");
        pending.catchupSource = Util.clip(attr(s, "catchup-source="), 768);
        String days = attr(s, "catchup-days=");
        if (days.isEmpty()) days = attr(s, "tvg-rec=");
        if (days.isEmpty()) days = attr(s, "timeshift=");
        int d = Util.atoi(days);
        if (d > 0 && d <= 40) pending.catchupDays = d;
        if (pending.catchup.isEmpty()) pending.catchup = listCatchup;
        if (pending.catchupSource.isEmpty()) pending.catchupSource = listSrc;
        if (pending.catchupDays == 0) pending.catchupDays = listDays;
        continue;
      }
      if (s.startsWith("#") || pending == null) continue;
      pending.url = Util.clip(s, Limits.MAX_URL);
      if (pending.url.isEmpty()) {
        pending = null;
        haveGroupTitle = false;
        continue;
      }
      if (pending.group == null || pending.group.isEmpty()) pending.group = Util.others();
      pending.family = foldFamily(pending.name);
      pending.quality = qualityOf(pending.name, pending.url);
      pending.id = makeId(out.size() + 1, pending.url, pending.tvgName.isEmpty() ? pending.name : pending.tvgName);
      out.add(pending);
      ua = "";
      ref = "";
      pending = null;
      haveGroupTitle = false;
    }
    return out;
  }

  /** Искать key= только на границе токена (начало, пробел, таб). */
  private static String attr(String s, String key) {
    if (s == null || key == null) return "";
    int from = 0;
    while (from < s.length()) {
      int i = s.indexOf(key, from);
      if (i < 0) return "";
      if (i == 0 || s.charAt(i - 1) == ' ' || s.charAt(i - 1) == '\t') {
        i += key.length();
        if (i < s.length() && s.charAt(i) == '"') {
          int e = s.indexOf('"', i + 1);
          return e > i ? s.substring(i + 1, e) : "";
        }
        int e = i;
        while (e < s.length() && s.charAt(e) != ' ' && s.charAt(e) != '\t' && s.charAt(e) != ',') e++;
        return s.substring(i, e);
      }
      from = i + 1;
    }
    return "";
  }

  private static String makeId(int index1, String url, String name) {
    String src = (url == null ? "" : url) + (name == null ? "" : name);
    int h = 0;
    for (int i = 0; i < src.length(); i++) h = (h << 5) - h + src.charAt(i);
    long v = h < 0 ? (h == Integer.MIN_VALUE ? 0x80000000L : -h) : h;
    return index1 + "-" + Long.toString(v, 36);
  }

  static int findName(List<Channel> pl, String name) {
    if (pl == null || name == null || name.isEmpty()) return -1;
    for (int i = 0; i < pl.size(); i++) if (name.equals(pl.get(i).name)) return i;
    return -1;
  }

  static int findId(List<Channel> pl, String id) {
    if (pl == null || id == null || id.isEmpty()) return -1;
    for (int i = 0; i < pl.size(); i++) if (id.equals(pl.get(i).id)) return i;
    return -1;
  }
}

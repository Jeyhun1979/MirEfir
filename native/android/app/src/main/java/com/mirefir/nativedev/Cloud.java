package com.mirefir.nativedev;

import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.util.zip.GZIPInputStream;
import java.util.zip.GZIPOutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import org.json.JSONArray;
import org.json.JSONObject;

final class Cloud {
  static String make(Profile p) {
    try {
      JSONObject o = new JSONObject();
      o.put("v", 2);
      JSONArray fav = new JSONArray();
      for (int i = 0; i < p.st.fav.size(); i++) {
        int idx = p.st.fav.get(i);
        if (idx < 0 || idx >= p.pl.size()) continue;
        String name = p.pl.get(idx).name;
        if (name == null || name.isEmpty()) continue;
        fav.put(name);
      }
      o.put("f", fav);
      JSONArray pls = new JSONArray();
      for (SavedLink s : p.st.playlists) {
        if (s.url == null || s.url.isEmpty() || "local".equals(s.id) || "local".equals(s.url)) continue;
        pls.put(s.url);
      }
      o.put("p", pls);
      JSONArray epg = new JSONArray();
      for (SavedLink s : p.st.epgSrc) {
        if (s.url == null || s.url.isEmpty()) continue;
        if (!contains(epg, s.url)) epg.put(s.url);
      }
      o.put("e", epg);
      JSONObject s = new JSONObject();
      s.put("language", p.st.language);
      s.put("decoder", p.st.decoder);
      s.put("aspect", p.st.aspect);
      s.put("defaultAudio", p.st.audio);
      s.put("bufferSec", p.st.bufferSec);
      s.put("clockEnabled", p.st.clockEnabled);
      s.put("clockPosition", p.st.clockPos);
      s.put("clockSize", p.st.clockSize);
      s.put("clockOpacity", p.st.clockOpacity);
      s.put("theme", p.st.theme);
      s.put("highlight", p.st.highlight);
      s.put("fontSize", p.st.fontSize);
      s.put("logoSize", p.st.logoSize);
      s.put("twoLineTitles", p.st.twoLineTitles);
      s.put("showChannelNumbers", p.st.showNumbers);
      s.put("showEpgProgress", p.st.showEpgProgress);
      s.put("userAgent", p.st.userAgent);
      s.put("epgDays", p.st.epgDays);
      s.put("epgOffsetHours", p.st.epgOffsetHours);
      s.put("archiveEnabled", p.st.archiveEnabled);
      s.put("archiveDays", p.st.archiveDays);
      o.put("s", s);
      String json = o.toString();
      ByteArrayOutputStream gz = new ByteArrayOutputStream();
      GZIPOutputStream go = new GZIPOutputStream(gz);
      go.write(json.getBytes(StandardCharsets.UTF_8));
      go.close();
      String b64 = Base64.encodeToString(gz.toByteArray(), Base64.NO_WRAP);
      return "ME2." + b64;
    } catch (Exception e) {
      return "";
    }
  }

  static boolean looksLike(String code) {
    if (code == null) return false;
    String t = code.trim().replace("\n", "").replace("\r", "").replace(" ", "");
    if (t.length() < 8) return false;
    char a = Character.toUpperCase(t.charAt(0));
    return (a == 'M' || a == 'O') && t.charAt(3) == '.';
  }

  static boolean apply(Profile p, String code) {
    if (code == null) return false;
    code = code.trim().replace("\n", "").replace("\r", "").replace(" ", "");
    String packed = "ME1";
    String body = code;
    if (code.length() > 4 && code.charAt(3) == '.') {
      packed = code.substring(0, 3).toUpperCase();
      body = code.substring(4);
    }
    try {
      byte[] raw = Base64.decode(body, Base64.DEFAULT);
      String json;
      if ("ME2".equals(packed)) {
        if (raw.length > 2 * 1024 * 1024) return false;
        int len = raw.length;
        if (len >= 4) {
          int declared = (raw[len - 4] & 0xff)
              | ((raw[len - 3] & 0xff) << 8)
              | ((raw[len - 2] & 0xff) << 16)
              | ((raw[len - 1] & 0xff) << 24);
          if (declared > 0 && declared <= 2 * 1024 * 1024) { /* gzip footer size */ }
        }
        GZIPInputStream gz = new GZIPInputStream(new ByteArrayInputStream(raw));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n, used = 0;
        while ((n = gz.read(buf)) > 0) {
          used += n;
          if (used > 2 * 1024 * 1024) return false;
          out.write(buf, 0, n);
        }
        json = out.toString("UTF-8");
      } else {
        json = new String(raw, StandardCharsets.UTF_8);
      }
      JSONObject o = new JSONObject(json);
      JSONArray pArr = o.optJSONArray("p");
      if (pArr != null) {
        for (int i = 0; i < pArr.length(); i++) {
          String url = pArr.optString(i, "");
          if (!url.isEmpty()) p.addPlaylistUrl(url);
        }
      }
      JSONArray eArr = o.optJSONArray("e");
      if (eArr != null) {
        for (int i = 0; i < eArr.length(); i++) {
          String url = eArr.optString(i, "");
          if (!url.isEmpty()) p.addEpgUrl(url, i == 0 ? 1 : i == 1 ? 2 : 0);
        }
      }
      JSONArray fArr = o.optJSONArray("f");
      JSONObject s = o.optJSONObject("s");
      if (s != null) {
        p.st.language = s.optString("language", p.st.language);
        p.st.decoder = s.optString("decoder", p.st.decoder);
        p.st.aspect = s.optString("aspect", p.st.aspect);
        p.st.audio = s.optString("defaultAudio", p.st.audio);
        p.st.bufferSec = s.optInt("bufferSec", p.st.bufferSec);
        p.st.clockEnabled = s.optBoolean("clockEnabled", p.st.clockEnabled);
        p.st.clockPos = s.optString("clockPosition", p.st.clockPos);
        p.st.clockSize = s.optString("clockSize", p.st.clockSize);
        p.st.clockOpacity = s.optInt("clockOpacity", p.st.clockOpacity);
        p.st.theme = s.optString("theme", p.st.theme);
        p.st.highlight = s.optString("highlight", p.st.highlight);
        p.st.fontSize = s.optString("fontSize", p.st.fontSize);
        p.st.logoSize = s.optString("logoSize", p.st.logoSize);
        p.st.twoLineTitles = s.optBoolean("twoLineTitles", p.st.twoLineTitles);
        p.st.showNumbers = s.optBoolean("showChannelNumbers", p.st.showNumbers);
        p.st.showEpgProgress = s.optBoolean("showEpgProgress", p.st.showEpgProgress);
        p.st.archiveEnabled = s.optBoolean("archiveEnabled", p.st.archiveEnabled);
        p.st.archiveDays = s.optInt("archiveDays", p.st.archiveDays);
        p.st.epgDays = s.optInt("epgDays", p.st.epgDays);
        p.st.epgOffsetHours = s.optInt("epgOffsetHours", p.st.epgOffsetHours);
        p.st.userAgent = s.optString("userAgent", p.st.userAgent);
        I18n.set(p.st.language);
      }
      boolean ok = true;
      if (p.pl.isEmpty() && !p.st.playlists.isEmpty()) {
        p.st.activePl = p.st.playlists.get(0).id;
        ok = p.fetchPlaylist(p.st.playlists.get(0).url);
      }
      if (fArr != null && ok && !p.pl.isEmpty()) {
        p.st.favIds.clear();
        for (int i = 0; i < fArr.length(); i++) {
          String item = fArr.optString(i, "");
          if (item.isEmpty()) continue;
          int idx = M3u.findName(p.pl, item);
          if (idx < 0) idx = M3u.findId(p.pl, item);
          if (idx < 0) continue;
          String id = p.pl.get(idx).id;
          if (!p.st.favIds.contains(id)) p.st.favIds.add(id);
        }
        p.resolveLists();
      }
      p.clamp();
      p.saveSettings();
      p.saveSources();
      return ok;
    } catch (Exception e) {
      return false;
    }
  }

  private static boolean contains(JSONArray a, String s) {
    for (int i = 0; i < a.length(); i++) if (s.equals(a.optString(i))) return true;
    return false;
  }
}

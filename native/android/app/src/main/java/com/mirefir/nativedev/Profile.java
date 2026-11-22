package com.mirefir.nativedev;

import android.content.Context;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

final class Profile {
  final Context app;
  final File dir;
  final Settings st = new Settings();
  List<Channel> pl = new ArrayList<>();
  private final Handler io = new Handler(Looper.getMainLooper());
  private Runnable delayedSave;
  private long lastRefreshAt;

  Profile(Context ctx) {
    app = ctx.getApplicationContext();
    dir = new File(app.getFilesDir(), "profile");
    dir.mkdirs();
    load();
  }

  File file(String name) {
    return new File(dir, name);
  }

  void load() {
    String json = Util.read(file("settings.json"));
    if (json != null) applySettings(json);
    json = Util.read(file("sources.json"));
    if (json != null) applySources(json);
    json = Util.read(file("session.json"));
    if (json != null) applySession(json);
    if (st.playlists.isEmpty() && st.epgSrc.isEmpty()) {
      Migrate.fromCapacitor(app, this);
    }
    ensureLocalSource();
    String m3u = Util.read(file("playlist.m3u"));
    if (m3u != null) pl = M3u.parse(m3u);
    normalizeEpg();
    resolveLists();
    I18n.set(st.language);
    clamp();
  }

  void clamp() {
    if (st.epgUpdateHours < 1 || st.epgUpdateHours > 24) st.epgUpdateHours = 6;
    if (st.epgOffsetHours < -12) st.epgOffsetHours = -12;
    if (st.epgOffsetHours > 12) st.epgOffsetHours = 12;
    if (st.epgDays < 1 || st.epgDays > 14) st.epgDays = st.epgDays < 1 ? 7 : 14;
    if (st.archiveDays < 1 || st.archiveDays > 14) st.archiveDays = st.archiveDays < 1 ? 7 : 14;
    if (st.guideRows < 4) st.guideRows = 6;
    if (st.guideRows > 9) st.guideRows = 9;
    if (st.bufferSec < 3) st.bufferSec = 15;
    if (st.bufferSec > 90) st.bufferSec = 90;
    if (st.clockOpacity < 40) st.clockOpacity = 90;
    if (st.clockOpacity > 100) st.clockOpacity = 100;
    if (st.panelOpacity < 50) st.panelOpacity = 90;
    if (st.panelOpacity > 100) st.panelOpacity = 100;
    if (st.playlistUpdateHours < 1) st.playlistUpdateHours = 6;
    if (st.volume < 5) st.volume = 95;
    if (st.volume > 100) st.volume = 100;
    if (st.parentalPin == null || st.parentalPin.isEmpty()) st.parentalPin = "0000";
    if (empty(st.theme)) st.theme = "dark";
    if (empty(st.clockPos)) st.clockPos = "top-right";
    if (empty(st.fontSize)) st.fontSize = "md";
    if (empty(st.channelSort)) st.channelSort = "playlist";
    if (empty(st.language)) st.language = "ru";
    if (empty(st.decoder)) st.decoder = "auto";
    if (empty(st.aspect)) st.aspect = "auto";
    if (empty(st.audio)) st.audio = "ru";
    if (empty(st.highlight)) st.highlight = "blue";
    if (empty(st.clockSize)) st.clockSize = "md";
    if (empty(st.logoSize)) st.logoSize = "md";
    if (empty(st.keyMenu)) st.keyMenu = "F2";
    if (empty(st.keyMute)) st.keyMute = "m";
    if (empty(st.keyFav)) st.keyFav = " ";
    if (empty(st.keyFs)) st.keyFs = "F11";
    if (empty(st.keyGuide)) st.keyGuide = "g";
    if (empty(st.keySearch)) st.keySearch = "s";
    if (empty(st.keyRec)) st.keyRec = "r";
    if (empty(st.keyPip)) st.keyPip = "p";
    if (empty(st.keyVoice)) st.keyVoice = "v";
    if (empty(st.keyLive)) st.keyLive = "ArrowLeft";
    applyUaRule();
    trimList(st.hidden, Limits.MAX_HIDDEN);
    trimList(st.locked, Limits.MAX_HIDDEN);
    trimList(st.favIds, Limits.MAX_FAV);
    trimList(st.recentIds, Limits.MAX_RECENT);
    while (st.playlists.size() > Limits.MAX_SAVED) st.playlists.remove(st.playlists.size() - 1);
    while (st.epgSrc.size() > Limits.MAX_SAVED) st.epgSrc.remove(st.epgSrc.size() - 1);
  }

  private static boolean empty(String s) {
    return s == null || s.isEmpty();
  }

  private static void trimList(List<String> list, int max) {
    while (list.size() > max) list.remove(list.size() - 1);
  }

  void applyUaRule() {
    if ("MirEfir/1.0".equals(st.userAgent)) {
      st.userAgent = "Mozilla/5.0 (Linux; Android 7) MirEfir/1.0";
    }
  }

  void saveSettings() {
    try {
      JSONObject o = settingsObj();
      Util.write(file("settings.json"), o.toString());
    } catch (Exception ignored) {}
  }

  void saveSources() {
    try {
      JSONObject o = sourcesObj();
      Util.write(file("sources.json"), o.toString());
    } catch (Exception ignored) {}
  }

  void saveSession() {
    try {
      JSONObject o = new JSONObject();
      o.put("index", st.lastIndex);
      o.put("name", st.lastName);
      o.put("groupId", st.lastGroup);
      o.put("volume", st.volume);
      o.put("muted", st.muted ? 1 : 0);
      o.put("weak", st.weak ? 1 : 0);
      o.put("playClass", st.playClass);
      Util.write(file("session.json"), o.toString());
    } catch (Exception ignored) {}
  }

  void saveSessionLater() {
    if (delayedSave != null) io.removeCallbacks(delayedSave);
    delayedSave = () -> {
      saveSession();
      saveSettings();
    };
    io.postDelayed(delayedSave, 800);
  }

  private JSONObject sourcesObj() throws Exception {
    JSONObject o = new JSONObject();
    o.put("activePlaylistId", st.activePl);
    JSONArray pls = new JSONArray();
    for (SavedLink s : st.playlists) {
      JSONObject x = new JSONObject();
      x.put("id", s.id);
      x.put("name", s.name);
      x.put("url", s.url);
      pls.put(x);
    }
    o.put("playlists", pls);
    JSONArray epg = new JSONArray();
    for (SavedLink s : st.epgSrc) {
      JSONObject x = new JSONObject();
      x.put("id", s.id);
      x.put("url", s.url);
      x.put("enabled", s.enabled != 0);
      x.put("slot", s.enabled);
      epg.put(x);
    }
    o.put("epgSources", epg);
    return o;
  }

  boolean fetchPlaylist(String url) {
    if (url == null || url.isEmpty()) return false;
    String body;
    if (Util.looksUrl(url) && !Util.starts(url, "file:")) {
      body = Http.get(url, st.userAgent, 60000, 16 * 1024 * 1024);
    } else {
      body = Util.read(new File(url.replace("file://", "")));
    }
    if (body == null) return false;
    List<Channel> next = M3u.parse(body);
    if (next.isEmpty()) return false;
    String keepName = st.lastName;
    int keepIndex = st.lastIndex;
    pl = next;
    Util.write(file("playlist.m3u"), body);
    st.plFetchedMs = System.currentTimeMillis();
    resolveLists();
    if (keepName != null && !keepName.isEmpty()) {
      int found = M3u.findName(pl, keepName);
      if (found >= 0) st.lastIndex = found;
      else if (keepIndex >= 0 && keepIndex < pl.size()) st.lastIndex = keepIndex;
      else st.lastIndex = 0;
    }
    saveSettings();
    return true;
  }

  boolean addPlaylistUrl(String url) {
    if (url == null) return false;
    url = url.trim();
    if (!Util.looksUrl(url) && !url.contains(".m3u")) return false;
    url = Util.clip(url, Limits.MAX_URL);
    for (SavedLink e : st.playlists) {
      if (url.equals(e.url)) return false;
    }
    if (st.playlists.size() >= Limits.MAX_SAVED) return false;
    SavedLink s = new SavedLink();
    s.id = nextPlId();
    s.name = Util.nameFromUrl(url);
    s.url = url;
    s.enabled = 1;
    st.playlists.add(s);
    if (st.activePl == null || st.activePl.isEmpty()) st.activePl = s.id;
    saveSources();
    if (s.id.equals(st.activePl)) return fetchPlaylist(url);
    return true;
  }

  boolean addLocalM3u(String path, String text) {
    if (text == null || M3u.parse(text).isEmpty()) return false;
    Util.write(file("playlist.m3u"), text);
    pl = M3u.parse(text);
    boolean haveRemote = false;
    for (SavedLink s : st.playlists) {
      if (!"local".equals(s.id)) { haveRemote = true; break; }
    }
    if (!haveRemote) {
      SavedLink s = findById(st.playlists, "local");
      if (s == null) {
        if (st.playlists.size() >= Limits.MAX_SAVED) return !pl.isEmpty();
        s = new SavedLink();
        s.id = "local";
        st.playlists.add(s);
      }
      s.name = path != null && !path.isEmpty() ? Util.nameFromUrl(path) : "playlist.m3u";
      s.url = path == null ? "local" : path;
      s.enabled = 1;
      if (st.activePl == null || st.activePl.isEmpty()) st.activePl = "local";
      saveSources();
    }
    st.plFetchedMs = System.currentTimeMillis();
    resolveLists();
    saveSettings();
    return true;
  }

  boolean setActivePlaylist(int index) {
    if (index < 0 || index >= st.playlists.size()) return false;
    SavedLink s = st.playlists.get(index);
    st.activePl = s.id;
    saveSources();
    if (s.url != null && !s.url.isEmpty() && !"local".equals(s.id)) return fetchPlaylist(s.url);
    if ("local".equals(s.id)) {
      String m3u = Util.read(file("playlist.m3u"));
      if (m3u != null) pl = M3u.parse(m3u);
      resolveLists();
      return !pl.isEmpty();
    }
    return false;
  }

  boolean replacePlaylistFromClip(int index, String url) {
    if (index < 0 || index >= st.playlists.size()) return false;
    if (url == null || url.trim().isEmpty()) return false;
    url = Util.clip(url.trim(), Limits.MAX_URL);
    SavedLink s = st.playlists.get(index);
    s.url = url;
    s.name = Util.nameFromUrl(url);
    saveSources();
    if (s.id.equals(st.activePl)) return fetchPlaylist(url);
    return true;
  }

  boolean removePlaylist(int index) {
    if (index < 0 || index >= st.playlists.size()) return false;
    SavedLink gone = st.playlists.remove(index);
    if (gone.id.equals(st.activePl)) {
      st.activePl = st.playlists.isEmpty() ? "" : st.playlists.get(0).id;
      if (!st.activePl.isEmpty()) {
        SavedLink n = findById(st.playlists, st.activePl);
        if (n != null && n.url != null && !n.url.isEmpty() && !"local".equals(n.id)) fetchPlaylist(n.url);
      }
    }
    saveSources();
    return true;
  }

  boolean addEpgUrl(String url) {
    return addEpgUrl(url, -1);
  }

  boolean addEpgUrl(String url, int slot) {
    if (url == null) return false;
    url = Util.clip(url.trim(), Limits.MAX_URL);
    if (!Util.looksUrl(url)) return false;
    int at = -1;
    for (int i = 0; i < st.epgSrc.size(); i++) {
      if (url.equals(st.epgSrc.get(i).url)) { at = i; break; }
    }
    if (at < 0) {
      if (st.epgSrc.size() >= Limits.MAX_SAVED) return false;
      SavedLink s = new SavedLink();
      s.id = nextEpgId();
      s.name = Util.nameFromUrl(url);
      s.url = url;
      s.enabled = slot > 0 ? 0 : (st.epgSrc.isEmpty() ? 1 : 0);
      st.epgSrc.add(s);
      at = st.epgSrc.size() - 1;
    }
    if (slot == 1 || slot == 2) {
      /* роль «основной»/«дополнительный» переходит к этой ссылке, прежний владелец роли становится запасным */
      setEpgSlot(at, slot);
      return true;
    }
    normalizeEpg();
    saveSources();
    return true;
  }

  boolean removeEpg(int index) {
    if (index < 0 || index >= st.epgSrc.size()) return false;
    SavedLink gone = st.epgSrc.remove(index);
    if (gone.enabled == 1) {
      for (SavedLink s : st.epgSrc) {
        if (s.enabled == 2) { s.enabled = 1; break; }
      }
    }
    normalizeEpg();
    saveSources();
    return true;
  }

  void setEpgSlot(int index, int slot) {
    if (index < 0 || index >= st.epgSrc.size()) return;
    if (slot == 1 || slot == 2) {
      for (SavedLink s : st.epgSrc) {
        if (s.enabled == slot) s.enabled = 0;
      }
    }
    st.epgSrc.get(index).enabled = slot;
    if (slot == 2) {
      /* дополнительный без основного — это основной (как sources_set_epg_role на Windows) */
      boolean pri = false;
      for (int k = 0; k < st.epgSrc.size(); k++) if (k != index && st.epgSrc.get(k).enabled == 1) pri = true;
      if (!pri) st.epgSrc.get(index).enabled = 1;
    }
    normalizeEpg();
    saveSources();
  }

  void normalizeEpg() {
    boolean has2 = false;
    for (SavedLink s : st.epgSrc) if (s.enabled == 2) has2 = true;
    if (!has2) {
      int n = 0;
      for (SavedLink s : st.epgSrc) {
        if (s.url == null || s.url.isEmpty() || s.enabled == 0) {
          if (s.enabled != 1 && s.enabled != 2) s.enabled = 0;
          if (s.enabled == 0) continue;
        }
        if (s.enabled == 0) continue;
        n++;
        s.enabled = n == 1 ? 1 : n == 2 ? 2 : 0;
      }
      if (n == 0) {
        int k = 0;
        for (SavedLink s : st.epgSrc) {
          if (s.url == null || s.url.isEmpty()) continue;
          k++;
          s.enabled = k == 1 ? 1 : k == 2 ? 2 : 0;
        }
      }
    } else {
      boolean pri = false, fb = false;
      for (SavedLink s : st.epgSrc) {
        if (s.enabled == 1) {
          if (pri) s.enabled = 0;
          else pri = true;
        } else if (s.enabled == 2) {
          if (fb) s.enabled = 0;
          else fb = true;
        } else {
          s.enabled = 0;
        }
      }
    }
  }

  List<String> epgChain() {
    List<String> out = new ArrayList<>();
    addChain(out, 1);
    addChain(out, 2);
    for (SavedLink s : st.epgSrc) {
      if (s.enabled == 0 && s.url != null && !s.url.isEmpty() && !out.contains(s.url)) out.add(s.url);
    }
    return out;
  }

  private void addChain(List<String> out, int slot) {
    for (SavedLink s : st.epgSrc) {
      if (s.enabled == slot && s.url != null && !s.url.isEmpty() && !out.contains(s.url)) out.add(s.url);
    }
  }

  String xtreamM3u(String server, String user, String pass) {
    if (server == null || user == null || pass == null) return "";
    server = server.trim();
    while (server.endsWith("/")) server = server.substring(0, server.length() - 1);
    if (!Util.starts(server, "http")) server = "http://" + server;
    return server + "/get.php?username=" + Util.enc(user) + "&password=" + Util.enc(pass) + "&type=m3u_plus";
  }

  void refreshIfDue(boolean force) {
    if (st.playlists.isEmpty()) return;
    long due = st.playlistUpdateHours * 3600L * 1000L;
    if (!force && st.plFetchedMs > 0 && System.currentTimeMillis() - st.plFetchedMs < due) return;
    String url = activeUrl();
    if (url == null || url.isEmpty()) return;
    if (!Util.starts(url, "http://") && !Util.starts(url, "https://")) return;
    lastRefreshAt = System.currentTimeMillis();
    fetchPlaylist(url);
  }

  boolean playlistRefreshDue() {
    if (st.playlists.isEmpty()) return false;
    String url = activeUrl();
    if (url == null || !(Util.starts(url, "http://") || Util.starts(url, "https://"))) return false;
    long due = st.playlistUpdateHours * 3600L * 1000L;
    if (st.plFetchedMs > 0 && System.currentTimeMillis() - st.plFetchedMs < due) return false;
    return lastRefreshAt == 0 || System.currentTimeMillis() - lastRefreshAt > 60_000;
  }

  String activeUrl() {
    for (SavedLink s : st.playlists) {
      if (s.id.equals(st.activePl) && s.url != null) return s.url;
    }
    return st.playlists.isEmpty() ? "" : st.playlists.get(0).url;
  }

  void resolveLists() {
    st.fav.clear();
    for (String id : st.favIds) {
      int i = M3u.findId(pl, id);
      if (i < 0) i = M3u.findName(pl, id);
      if (i >= 0 && !st.fav.contains(i)) st.fav.add(i);
    }
    st.recent.clear();
    for (String id : st.recentIds) {
      int i = M3u.findId(pl, id);
      if (i < 0) i = M3u.findName(pl, id);
      if (i >= 0 && !st.recent.contains(i)) st.recent.add(i);
    }
  }

  void rememberChannel(int index) {
    if (index < 0 || index >= pl.size()) return;
    Channel ch = pl.get(index);
    st.lastIndex = index;
    st.lastName = ch.name;
    if (!st.recentIds.contains(ch.id)) {
      st.recentIds.add(0, ch.id);
      while (st.recentIds.size() > Limits.MAX_RECENT) st.recentIds.remove(st.recentIds.size() - 1);
    } else {
      st.recentIds.remove(ch.id);
      st.recentIds.add(0, ch.id);
    }
    resolveLists();
    saveSessionLater();
  }

  void toggleFav(int index) {
    if (index < 0 || index >= pl.size()) return;
    String id = pl.get(index).id;
    if (st.favIds.contains(id)) st.favIds.remove(id);
    else if (st.favIds.size() < Limits.MAX_FAV) st.favIds.add(id);
    resolveLists();
    saveSettings();
  }

  boolean isFav(int index) {
    return index >= 0 && index < pl.size() && st.favIds.contains(pl.get(index).id);
  }

  void toggleHidden(String name) {
    if (name == null || name.isEmpty()) return;
    if (st.hidden.contains(name)) st.hidden.remove(name);
    else if (st.hidden.size() < Limits.MAX_HIDDEN) st.hidden.add(name);
    saveSettings();
  }

  void toggleLocked(String name) {
    if (name == null || name.isEmpty()) return;
    if (st.locked.contains(name)) st.locked.remove(name);
    else if (st.locked.size() < Limits.MAX_HIDDEN) st.locked.add(name);
    saveSettings();
  }

  boolean groupLocked(String name) {
    return st.parentalEnabled && name != null && st.locked.contains(name);
  }

  void resetSettings() {
    List<SavedLink> plinks = new ArrayList<>(st.playlists);
    List<SavedLink> elinks = new ArrayList<>(st.epgSrc);
    String active = st.activePl;
    List<String> fav = new ArrayList<>(st.favIds);
    List<String> rec = new ArrayList<>(st.recentIds);
    List<Channel> keepPl = pl;
    String last = st.lastName;
    int lastI = st.lastIndex;
    String lastG = st.lastGroup;
    Settings fresh = Settings.defaults();
    copyPrefs(fresh, st);
    st.playlists.clear();
    st.playlists.addAll(plinks);
    st.epgSrc.clear();
    st.epgSrc.addAll(elinks);
    st.activePl = active;
    st.favIds.clear();
    st.favIds.addAll(fav);
    st.recentIds.clear();
    st.recentIds.addAll(rec);
    st.lastName = last;
    st.lastIndex = lastI;
    st.lastGroup = lastG;
    pl = keepPl;
    clamp();
    resolveLists();
    I18n.set(st.language);
    saveSettings();
  }

  private static void copyPrefs(Settings from, Settings to) {
    to.userAgent = from.userAgent;
    to.decoder = from.decoder;
    to.aspect = from.aspect;
    to.audio = from.audio;
    to.bufferSec = from.bufferSec;
    to.rememberVolume = from.rememberVolume;
    to.autoplay = from.autoplay;
    to.clockEnabled = from.clockEnabled;
    to.clockPos = from.clockPos;
    to.clockSize = from.clockSize;
    to.clockOpacity = from.clockOpacity;
    to.confirmExit = from.confirmExit;
    to.voiceEnabled = from.voiceEnabled;
    to.showNumbers = from.showNumbers;
    to.showEpgProgress = from.showEpgProgress;
    to.twoLineTitles = from.twoLineTitles;
    to.theme = from.theme;
    to.highlight = from.highlight;
    to.panelOpacity = from.panelOpacity;
    to.fontSize = from.fontSize;
    to.logoSize = from.logoSize;
    to.channelSort = from.channelSort;
    to.playlistUpdateHours = from.playlistUpdateHours;
    to.externalPlayer = from.externalPlayer;
    to.epgOffsetHours = from.epgOffsetHours;
    to.epgDays = from.epgDays;
    to.archiveEnabled = from.archiveEnabled;
    to.archiveDays = from.archiveDays;
    to.showProgramDesc = from.showProgramDesc;
    to.guideRows = from.guideRows;
    to.epgUpdateHours = from.epgUpdateHours;
    to.epgAutoUpdate = from.epgAutoUpdate;
    to.recordingsEnabled = from.recordingsEnabled;
    to.recPath = from.recPath;
    to.parentalEnabled = from.parentalEnabled;
    to.parentalPin = from.parentalPin;
    to.lockSettings = from.lockSettings;
    to.language = from.language;
    to.systemPip = from.systemPip;
    to.keyMenu = from.keyMenu;
    to.keyMute = from.keyMute;
    to.keyFav = from.keyFav;
    to.keyFs = from.keyFs;
    to.keyGuide = from.keyGuide;
    to.keySearch = from.keySearch;
    to.keyRec = from.keyRec;
    to.keyPip = from.keyPip;
    to.keyVoice = from.keyVoice;
    to.keyLive = from.keyLive;
    to.hidden.clear();
    to.locked.clear();
  }

  File backupsDir() {
    File d = new File(dir, "Backups");
    d.mkdirs();
    return d;
  }

  File defaultRecordingsDir() {
    try {
      File pub = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES);
      File d = new File(pub, "MirEfir");
      if (d.isDirectory() || d.mkdirs()) return d;
    } catch (Exception ignored) {}
    try {
      File ext = app.getExternalFilesDir(Environment.DIRECTORY_MOVIES);
      if (ext != null) {
        File d = new File(ext, "MirEfir");
        if (d.isDirectory() || d.mkdirs()) return d;
      }
    } catch (Exception ignored) {}
    File d = new File(dir, "Recordings");
    d.mkdirs();
    return d;
  }

  File recordingsDir() {
    if (st.recPath.isEmpty() || Rec.isSaf(st.recPath)) {
      File d = defaultRecordingsDir();
      d.mkdirs();
      return d;
    }
    File d = new File(st.recPath);
    if (d.isDirectory() || d.mkdirs()) return d;
    return defaultRecordingsDir();
  }

  String exportBackup(File dest) {
    try {
      JSONObject o = new JSONObject();
      o.put("v", 1);
      o.put("version", 1);
      o.put("createdAt", new java.util.Date().toString());
      o.put("settings", settingsObj());
      JSONObject src = sourcesObj();
      o.put("playlists", src.getJSONArray("playlists"));
      o.put("epgSources", src.getJSONArray("epgSources"));
      o.put("activePlaylistId", st.activePl);
      JSONArray fav = new JSONArray();
      for (String id : st.favIds) {
        int i = M3u.findId(pl, id);
        fav.put(i >= 0 && i < pl.size() ? pl.get(i).name : id);
      }
      o.put("favorites", fav);
      o.put("favoriteIds", jsonStr(st.favIds));
      o.put("recentIds", jsonStr(st.recentIds));
      JSONObject session = new JSONObject();
      session.put("index", st.lastIndex);
      session.put("name", st.lastName);
      session.put("groupId", st.lastGroup);
      session.put("volume", st.volume);
      session.put("muted", st.muted ? 1 : 0);
      o.put("session", session);
      String m3u = Util.read(file("playlist.m3u"));
      if (m3u != null) o.put("playlistText", m3u);
      String url = activeUrl();
      if (url != null) o.put("playlistUrl", url);
      if (dest.getParentFile() != null) dest.getParentFile().mkdirs();
      Util.write(dest, o.toString(2));
      return dest.getAbsolutePath();
    } catch (Exception e) {
      return null;
    }
  }

  boolean importBackup(String json) {
    if (json == null || json.isEmpty()) return false;
    applySettings(json);
    applySources(json);
    try {
      JSONObject o = new JSONObject(json);
      JSONObject inner = o.optJSONObject("settings");
      if (inner != null) {
        if (st.playlists.isEmpty()) applyLinks(inner.optJSONArray("playlists"), st.playlists, false);
        if (st.epgSrc.isEmpty()) applyLinks(inner.optJSONArray("epgSources"), st.epgSrc, true);
      }
      String text = o.optString("playlistText", "");
      if (!text.isEmpty()) {
        Util.write(file("playlist.m3u"), text);
        pl = M3u.parse(text);
      }
      JSONObject session = o.optJSONObject("session");
      if (session != null) applySession(session.toString());
      JSONArray favNames = o.optJSONArray("favorites");
      if (favNames != null) {
        for (int i = 0; i < favNames.length(); i++) {
          String v = favNames.optString(i, "");
          if (v.isEmpty()) continue;
          int idx = M3u.findId(pl, v);
          if (idx < 0) idx = M3u.findName(pl, v);
          if (idx >= 0 && !st.favIds.contains(pl.get(idx).id)) st.favIds.add(pl.get(idx).id);
        }
      }
    } catch (Exception ignored) {}
    clamp();
    normalizeEpg();
    saveSettings();
    saveSources();
    saveSession();
    if (pl.isEmpty()) {
      String url = activeUrl();
      if (url != null && !url.isEmpty() && !"local".equals(url)) fetchPlaylist(url);
    }
    resolveLists();
    return !pl.isEmpty() || !st.playlists.isEmpty();
  }

  private JSONObject settingsObj() throws Exception {
    JSONObject o = new JSONObject();
    o.put("decoder", st.decoder);
    o.put("aspect", st.aspect);
    o.put("defaultAudio", st.audio);
    o.put("bufferSec", st.bufferSec);
    o.put("rememberVolume", st.rememberVolume);
    o.put("autoplay", st.autoplay);
    o.put("clockEnabled", st.clockEnabled);
    o.put("clockPosition", st.clockPos);
    o.put("clockSize", st.clockSize);
    o.put("clockOpacity", st.clockOpacity);
    o.put("confirmExit", st.confirmExit);
    o.put("voiceEnabled", st.voiceEnabled);
    o.put("showChannelNumbers", st.showNumbers);
    o.put("showEpgProgress", st.showEpgProgress);
    o.put("showProgramDesc", st.showProgramDesc);
    o.put("twoLineTitles", st.twoLineTitles);
    o.put("theme", st.theme);
    o.put("highlight", st.highlight);
    o.put("panelOpacity", st.panelOpacity);
    o.put("fontSize", st.fontSize);
    o.put("logoSize", st.logoSize);
    o.put("channelSort", st.channelSort);
    o.put("playlistUpdateHours", st.playlistUpdateHours);
    o.put("playlistFetchedAt", st.plFetchedMs);
    o.put("externalPlayer", st.externalPlayer);
    o.put("userAgent", st.userAgent);
    o.put("epgOffsetHours", st.epgOffsetHours);
    o.put("epgDays", st.epgDays);
    o.put("archiveEnabled", st.archiveEnabled);
    o.put("archiveDays", st.archiveDays);
    o.put("guideRows", st.guideRows);
    o.put("epgUpdateHours", st.epgUpdateHours);
    o.put("epgAutoUpdate", st.epgAutoUpdate);
    o.put("recordingsEnabled", st.recordingsEnabled);
    o.put("recordingPath", st.recPath);
    o.put("parentalEnabled", st.parentalEnabled);
    o.put("parentalPin", st.parentalPin);
    o.put("lockSettings", st.lockSettings);
    o.put("language", st.language);
    o.put("systemPip", st.systemPip);
    JSONObject keys = new JSONObject();
    keys.put("menu", st.keyMenu);
    keys.put("mute", st.keyMute);
    keys.put("favorite", st.keyFav);
    keys.put("fullscreen", st.keyFs);
    keys.put("guide", st.keyGuide);
    keys.put("search", st.keySearch);
    keys.put("record", st.keyRec);
    keys.put("pip", st.keyPip);
    keys.put("voice", st.keyVoice);
    keys.put("liveGuide", st.keyLive);
    o.put("keys", keys);
    o.put("hiddenGroups", jsonStr(st.hidden));
    o.put("lockedGroups", jsonStr(st.locked));
    o.put("favoriteIds", jsonStr(st.favIds));
    o.put("recentIds", jsonStr(st.recentIds));
    return o;
  }

  private static JSONArray jsonStr(List<String> list) {
    JSONArray a = new JSONArray();
    for (String s : list) a.put(s);
    return a;
  }

  void applySettings(String json) {
    try {
      JSONObject root = new JSONObject(json);
      JSONObject o = root.optJSONObject("settings");
      if (o == null) o = root;
      st.decoder = o.optString("decoder", st.decoder);
      st.aspect = o.optString("aspect", st.aspect);
      st.audio = o.optString("defaultAudio", st.audio);
      st.bufferSec = o.optInt("bufferSec", st.bufferSec);
      st.rememberVolume = o.optBoolean("rememberVolume", st.rememberVolume);
      st.autoplay = o.optBoolean("autoplay", st.autoplay);
      st.clockEnabled = o.optBoolean("clockEnabled", st.clockEnabled);
      st.clockPos = o.optString("clockPosition", st.clockPos);
      st.clockSize = o.optString("clockSize", st.clockSize);
      st.clockOpacity = o.optInt("clockOpacity", st.clockOpacity);
      st.confirmExit = o.optBoolean("confirmExit", st.confirmExit);
      st.voiceEnabled = o.optBoolean("voiceEnabled", st.voiceEnabled);
      st.showNumbers = o.optBoolean("showChannelNumbers", st.showNumbers);
      st.showEpgProgress = o.optBoolean("showEpgProgress", st.showEpgProgress);
      st.showProgramDesc = o.optBoolean("showProgramDesc", st.showProgramDesc);
      st.twoLineTitles = o.optBoolean("twoLineTitles", st.twoLineTitles);
      st.theme = o.optString("theme", st.theme);
      st.highlight = o.optString("highlight", st.highlight);
      st.panelOpacity = o.optInt("panelOpacity", st.panelOpacity);
      st.fontSize = o.optString("fontSize", st.fontSize);
      st.logoSize = o.optString("logoSize", st.logoSize);
      st.channelSort = o.optString("channelSort", st.channelSort);
      st.playlistUpdateHours = o.optInt("playlistUpdateHours", st.playlistUpdateHours);
      st.plFetchedMs = o.optLong("playlistFetchedAt", st.plFetchedMs);
      st.externalPlayer = o.optBoolean("externalPlayer", st.externalPlayer);
      String ua = o.optString("userAgent", st.userAgent);
      if (!ua.isEmpty()) st.userAgent = ua;
      st.epgOffsetHours = o.optInt("epgOffsetHours", st.epgOffsetHours);
      st.epgDays = o.optInt("epgDays", st.epgDays);
      st.archiveEnabled = o.optBoolean("archiveEnabled", st.archiveEnabled);
      st.archiveDays = o.optInt("archiveDays", st.archiveDays);
      st.guideRows = o.optInt("guideRows", st.guideRows);
      st.epgUpdateHours = o.optInt("epgUpdateHours", st.epgUpdateHours);
      st.epgAutoUpdate = o.optBoolean("epgAutoUpdate", st.epgAutoUpdate);
      st.recordingsEnabled = o.optBoolean("recordingsEnabled", st.recordingsEnabled);
      st.recPath = o.optString("recordingPath", st.recPath);
      st.parentalEnabled = o.optBoolean("parentalEnabled", st.parentalEnabled);
      st.parentalPin = o.optString("parentalPin", st.parentalPin);
      st.lockSettings = o.optBoolean("lockSettings", st.lockSettings);
      st.language = o.optString("language", st.language);
      st.systemPip = o.optBoolean("systemPip", st.systemPip);
      JSONObject keys = o.optJSONObject("keys");
      if (keys != null) {
        st.keyMenu = keys.optString("menu", st.keyMenu);
        st.keyMute = keys.optString("mute", st.keyMute);
        st.keyFav = keys.optString("favorite", st.keyFav);
        st.keyFs = keys.optString("fullscreen", st.keyFs);
        st.keyGuide = keys.optString("guide", st.keyGuide);
        st.keySearch = keys.optString("search", st.keySearch);
        st.keyRec = keys.optString("record", st.keyRec);
        st.keyPip = keys.optString("pip", st.keyPip);
        st.keyVoice = keys.optString("voice", st.keyVoice);
        st.keyLive = keys.optString("liveGuide", st.keyLive);
      }
      fillStr(o.optJSONArray("hiddenGroups"), st.hidden);
      fillStr(o.optJSONArray("lockedGroups"), st.locked);
      fillStr(o.optJSONArray("favoriteIds"), st.favIds);
      fillStr(o.optJSONArray("recentIds"), st.recentIds);
      I18n.set(st.language);
    } catch (Exception ignored) {}
  }

  void applySources(String json) {
    try {
      JSONObject o = new JSONObject(json);
      JSONObject inner = o.optJSONObject("settings");
      if (o.has("playlists") || o.has("epgSources") || o.has("activePlaylistId")) {
        st.activePl = o.optString("activePlaylistId", st.activePl);
        applyLinks(o.optJSONArray("playlists"), st.playlists, false);
        applyLinks(o.optJSONArray("epgSources"), st.epgSrc, true);
      } else if (inner != null) {
        st.activePl = inner.optString("activePlaylistId", st.activePl);
        applyLinks(inner.optJSONArray("playlists"), st.playlists, false);
        applyLinks(inner.optJSONArray("epgSources"), st.epgSrc, true);
      }
    } catch (Exception ignored) {}
  }

  void applySession(String json) {
    try {
      JSONObject o = new JSONObject(json);
      JSONObject s = o.optJSONObject("session");
      if (s == null) s = o;
      st.lastName = s.optString("name", "");
      if (st.lastName.isEmpty()) st.lastName = s.optString("channelName", st.lastName);
      st.lastGroup = s.optString("groupId", st.lastGroup);
      st.lastIndex = s.optInt("index", st.lastIndex);
      if (s.has("volume")) {
        double v = s.optDouble("volume", st.volume);
        st.volume = (v > 0 && v <= 1) ? (int) Math.round(v * 100) : (int) Math.round(v);
        if (st.volume < 0) st.volume = 0;
        if (st.volume > 100) st.volume = 100;
      }
      Object muted = s.opt("muted");
      if (muted instanceof Boolean) st.muted = (Boolean) muted;
      else st.muted = s.optInt("muted", st.muted ? 1 : 0) != 0;
      Object weak = s.opt("weak");
      if (weak instanceof Boolean) st.weak = (Boolean) weak;
      else st.weak = s.optInt("weak", 0) != 0;
      st.playClass = s.optInt("playClass", st.playClass);
    } catch (Exception ignored) {}
  }

  private void ensureLocalSource() {
    if (!st.playlists.isEmpty()) return;
    File m3u = file("playlist.m3u");
    if (!m3u.isFile() || m3u.length() < 8) return;
    SavedLink s = new SavedLink();
    s.id = "local";
    s.name = "playlist.m3u";
    s.url = "local";
    s.enabled = 1;
    st.playlists.add(s);
    st.activePl = "local";
  }

  private String nextPlId() {
    int n = 1;
    while (findById(st.playlists, "pl-" + n) != null) n++;
    return "pl-" + n;
  }

  private String nextEpgId() {
    int n = 1;
    while (findById(st.epgSrc, "epg-" + n) != null) n++;
    return "epg-" + n;
  }

  private static SavedLink findById(List<SavedLink> list, String id) {
    for (SavedLink s : list) if (id.equals(s.id)) return s;
    return null;
  }

  private static void fillStr(JSONArray a, List<String> dest) {
    if (a == null) return;
    dest.clear();
    for (int i = 0; i < a.length(); i++) {
      String s = a.optString(i, "");
      if (!s.isEmpty()) dest.add(s);
    }
  }

  static void applyLinks(JSONArray a, List<SavedLink> dest, boolean epg) {
    if (a == null) return;
    dest.clear();
    for (int i = 0; i < a.length(); i++) {
      JSONObject o = a.optJSONObject(i);
      if (o == null) {
        String url = a.optString(i, "");
        if (url.isEmpty()) continue;
        SavedLink s = new SavedLink();
        s.id = epg ? "epg-" + (i + 1) : "pl-" + (i + 1);
        s.url = url;
        s.name = Util.nameFromUrl(url);
        s.enabled = epg ? (i == 0 ? 1 : i == 1 ? 2 : 0) : 1;
        dest.add(s);
        continue;
      }
      SavedLink s = new SavedLink();
      s.id = o.optString("id", epg ? "epg-" + (i + 1) : "pl-" + (i + 1));
      s.name = o.optString("name", "");
      s.url = o.optString("url", "");
      if (s.name.isEmpty()) s.name = Util.nameFromUrl(s.url);
      if (o.has("slot")) s.enabled = o.optInt("slot", 0);
      else if (epg) {
        Object en = o.opt("enabled");
        if (en instanceof Integer) s.enabled = (Integer) en;
        else if (en instanceof Boolean) s.enabled = ((Boolean) en) ? (dest.isEmpty() ? 1 : 0) : 0;
        else s.enabled = o.optInt("enabled", dest.isEmpty() ? 1 : 0);
      } else {
        s.enabled = 1;
      }
      dest.add(s);
    }
  }
}

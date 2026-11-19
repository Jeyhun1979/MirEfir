package com.mirefir.nativedev;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

final class Rec {
  static final int OFF = 0, SCHED = 1, RUN = 2;
  static final long MIN_FREE = 50L * 1024 * 1024;
  private static Rec inst;
  final Profile profile;
  volatile boolean live;
  private Thread dump;
  private volatile boolean stop;
  File liveFile;
  Uri liveUri;
  int timerCh = -1;
  String timerTitle = "";
  long timerStart, timerEnd;
  int timerStatus;
  private boolean timerDump;

  Rec(Profile profile) {
    this.profile = profile;
    inst = this;
    loadTimer();
  }

  static Rec get() { return inst; }

  boolean liveActive() { return live; }

  static boolean isSaf(String path) {
    return path != null && path.startsWith("content:");
  }

  String startLive(int index) {
    return startDump(index, false, 0, 0);
  }

  String startDump(int index, boolean hidden, long startMs, long endMs) {
    if (!profile.st.recordingsEnabled) return I18n.t("recOff");
    if (index < 0 || index >= profile.pl.size()) return I18n.t("recFail");
    if (live && !hidden) {
      stopLive();
      return I18n.t("recStopped");
    }
    if (live && hidden) stopLive();
    Channel ch = profile.pl.get(index);
    String warn = prepareDir();
    RecSink sink = openSink(safe(ch.name) + "_" +
        new SimpleDateFormat("yyyyMMdd_HHmm", Locale.US).format(new Date()) + ".ts");
    if (sink == null) return warn != null ? warn : I18n.t("recNoPerm");
    liveFile = sink.file;
    liveUri = sink.uri;
    stop = false;
    live = true;
    timerDump = hidden;
    String url = ch.url;
    if (hidden && startMs > 0 && endMs > 0 && startMs < System.currentTimeMillis() - 2000) {
      java.util.List<String> cand = Catchup.urls(ch, startMs, endMs);
      if (!cand.isEmpty()) url = cand.get(0);
    } else {
      java.util.List<String> u = M3u.playUrls(profile.pl, index, 4);
      if (!u.isEmpty()) url = u.get(0);
    }
    final String dumpUrl = url;
    final String ua = ch.ua;
    final String ref = ch.referer;
    dump = new Thread(() -> dumpUrl(dumpUrl, ua, ref, sink.out), "rec");
    dump.start();
    try { RecService.startLive(profile.app); } catch (Exception ignored) {}
    return warn != null ? warn : I18n.t("recOn");
  }

  void stopLive() {
    stop = true;
    live = false;
    Thread t = dump;
    dump = null;
    if (t != null) t.interrupt();
    File f = liveFile;
    Uri u = liveUri;
    new Thread(() -> remux(f, u), "remux").start();
    if (!timerDump) {
      try { RecService.stop(profile.app); } catch (Exception ignored) {}
    }
    timerDump = false;
  }

  void onZap() {
    if (live && !timerDump) stopLive();
  }

  void toggleTimer(int ch, String title, long start, long end) {
    if (timerCh == ch && Math.abs(timerStart - start) < 2000) {
      cancelTimer();
      return;
    }
    timerCh = ch;
    timerTitle = title == null ? "" : title;
    timerStart = start;
    timerEnd = end;
    timerStatus = SCHED;
    saveTimer();
    try { RecService.startTimerWatch(profile.app); } catch (Exception ignored) {}
  }

  void cancelTimer() {
    if (timerStatus == RUN && timerDump) stopLive();
    timerCh = -1;
    timerStatus = OFF;
    saveTimer();
  }

  boolean hasTimer(int ch, long start, long end) {
    return timerCh == ch && Math.abs(timerStart - start) < 2000;
  }

  void tick(int playingIndex) { serviceTick(); }

  void serviceTick() {
    long now = System.currentTimeMillis();
    if (timerStatus == SCHED && timerCh >= 0 && now >= timerStart - 2000) {
      timerStatus = RUN;
      saveTimer();
      startDump(timerCh, true, timerStart, timerEnd);
    }
    if (timerStatus == RUN && timerEnd > 0 && now >= timerEnd) {
      stopLive();
      timerStatus = OFF;
      timerCh = -1;
      saveTimer();
    }
  }

  private void saveTimer() {
    try {
      org.json.JSONObject o = new org.json.JSONObject();
      o.put("ch", timerCh);
      o.put("title", timerTitle);
      o.put("start", timerStart);
      o.put("end", timerEnd);
      o.put("status", timerStatus);
      Util.write(profile.file("rec-timer.json"), o.toString());
    } catch (Exception ignored) {}
  }

  private void loadTimer() {
    String json = Util.read(profile.file("rec-timer.json"));
    if (json == null) return;
    try {
      org.json.JSONObject o = new org.json.JSONObject(json);
      timerCh = o.optInt("ch", -1);
      timerTitle = o.optString("title", "");
      timerStart = o.optLong("start", 0);
      timerEnd = o.optLong("end", 0);
      timerStatus = o.optInt("status", OFF);
      if (timerStatus == RUN) timerStatus = SCHED;
      if (timerEnd > 0 && timerEnd < System.currentTimeMillis()) {
        timerCh = -1;
        timerStatus = OFF;
      }
    } catch (Exception ignored) {}
  }

  static final class Item {
    final String name;
    final Uri uri;
    final File file;
    final long size;
    final long modified;
    Item(String name, Uri uri, File file, long size, long modified) {
      this.name = name;
      this.uri = uri;
      this.file = file;
      this.size = size;
      this.modified = modified;
    }
    boolean delete() {
      if (file != null) return file.delete();
      try {
        return DocumentsContract.deleteDocument(App.get().getContentResolver(), uri);
      } catch (Exception e) {
        return false;
      }
    }
  }

  List<Item> files() {
    List<Item> out = new ArrayList<>();
    if (isSaf(profile.st.recPath)) {
      if (!treeOk()) {
        fallbackInternal();
        return filesFromDir(profile.recordingsDir());
      }
      out.addAll(filesFromSaf());
    } else {
      out.addAll(filesFromDir(profile.recordingsDir()));
    }
    out.sort((a, b) -> Long.compare(b.modified, a.modified));
    if (out.size() > 96) out = new ArrayList<>(out.subList(0, 96));
    return out;
  }

  String recPathLabel() {
    String p = profile.st.recPath;
    if (p == null || p.isEmpty()) return I18n.t("setPickFolder");
    if (isSaf(p)) {
      String name = treeName();
      return name != null && !name.isEmpty() ? name : I18n.t("askDisk");
    }
    return p;
  }

  String pathError() {
    if (isSaf(profile.st.recPath)) {
      if (!treeOk()) return I18n.t("recNoDisk");
      if (!treeWritable()) return I18n.t("recNoPerm");
      return null;
    }
    File dir = profile.recordingsDir();
    if (dir == null || (!dir.exists() && !dir.mkdirs())) return I18n.t("recNoPerm");
    if (!dir.canWrite()) return I18n.t("recNoPerm");
    long free = dir.getFreeSpace();
    if (free > 0 && free < MIN_FREE) return I18n.t("recNoSpace");
    return null;
  }

  private String prepareDir() {
    if (isSaf(profile.st.recPath)) {
      if (!treeOk() || !treeWritable()) {
        fallbackInternal();
        return I18n.t("recNoDisk");
      }
      return null;
    }
    File dir = profile.recordingsDir();
    if (dir == null || (!dir.exists() && !dir.mkdirs())) return I18n.t("recNoPerm");
    if (!dir.canWrite()) return I18n.t("recNoPerm");
    long free = dir.getFreeSpace();
    if (free > 0 && free < MIN_FREE) return I18n.t("recNoSpace");
    return null;
  }

  private void fallbackInternal() {
    File d = profile.defaultRecordingsDir();
    profile.st.recPath = d.getAbsolutePath();
    profile.st.recordingsEnabled = true;
    profile.saveSettings();
  }

  private Uri treeUri() {
    try { return Uri.parse(profile.st.recPath); } catch (Exception e) { return null; }
  }

  private Uri treeDocUri() {
    Uri tree = treeUri();
    if (tree == null) return null;
    try {
      String id = DocumentsContract.getTreeDocumentId(tree);
      return DocumentsContract.buildDocumentUriUsingTree(tree, id);
    } catch (Exception e) {
      return null;
    }
  }

  private boolean treeOk() {
    Uri doc = treeDocUri();
    if (doc == null) return false;
    try (Cursor c = resolver().query(doc, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID }, null, null, null)) {
      return c != null && c.moveToFirst();
    } catch (Exception e) {
      return false;
    }
  }

  private boolean treeWritable() {
    Uri doc = treeDocUri();
    if (doc == null) return false;
    try (Cursor c = resolver().query(doc, new String[] { DocumentsContract.Document.COLUMN_FLAGS }, null, null, null)) {
      if (c == null || !c.moveToFirst()) return false;
      int flags = c.getInt(0);
      if (flags == 0) return true;
      return (flags & DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE) != 0
          || (flags & DocumentsContract.Document.FLAG_SUPPORTS_WRITE) != 0;
    } catch (Exception e) {
      return true;
    }
  }

  private String treeName() {
    Uri doc = treeDocUri();
    if (doc == null) return null;
    try (Cursor c = resolver().query(doc, new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
      if (c != null && c.moveToFirst()) return c.getString(0);
    } catch (Exception ignored) {}
    return null;
  }

  private ContentResolver resolver() {
    return profile.app.getContentResolver();
  }

  private static class RecSink {
    OutputStream out;
    File file;
    Uri uri;
  }

  private RecSink openSink(String name) {
    RecSink s = new RecSink();
    try {
      if (isSaf(profile.st.recPath)) {
        Uri dir = treeDocUri();
        if (dir == null) return null;
        Uri created = DocumentsContract.createDocument(resolver(), dir, "video/mp2t", name);
        if (created == null) return null;
        s.out = resolver().openOutputStream(created, "w");
        s.uri = created;
        if (s.out == null) return null;
        return s;
      }
      File dir = profile.recordingsDir();
      if (dir == null) return null;
      s.file = new File(dir, name);
      s.uri = Uri.fromFile(s.file);
      s.out = new FileOutputStream(s.file);
      return s;
    } catch (Exception e) {
      return null;
    }
  }

  private List<Item> filesFromSaf() {
    List<Item> out = new ArrayList<>();
    Uri tree = treeUri();
    if (tree == null) return out;
    Uri children;
    try {
      children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
    } catch (Exception e) {
      return out;
    }
    String[] cols = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED,
        DocumentsContract.Document.COLUMN_SIZE
    };
    try (Cursor c = resolver().query(children, cols, null, null, null)) {
      if (c == null) return out;
      while (c.moveToNext()) {
        String name = c.getString(1);
        if (name == null || !isRecName(name)) continue;
        String id = c.getString(0);
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, id);
        long mod = c.isNull(2) ? 0 : c.getLong(2);
        long size = c.isNull(3) ? 0 : c.getLong(3);
        out.add(new Item(name, uri, null, size, mod));
      }
    } catch (Exception ignored) {}
    return out;
  }

  private List<Item> filesFromDir(File dir) {
    List<Item> out = new ArrayList<>();
    if (dir == null) return out;
    File[] all = dir.listFiles();
    if (all == null) return out;
    for (File f : all) {
      if (!f.isFile()) continue;
      if (!isRecName(f.getName())) continue;
      out.add(new Item(f.getName(), Uri.fromFile(f), f, f.length(), f.lastModified()));
    }
    return out;
  }

  private static boolean isRecName(String n) {
    String low = n.toLowerCase(Locale.ROOT);
    return low.endsWith(".ts") || low.endsWith(".mp4") || low.endsWith(".mkv") || low.endsWith(".m4v");
  }

  private void dumpUrl(String url, String ua, String ref, OutputStream dest) {
    HttpURLConnection c = null;
    try {
      c = (HttpURLConnection) new URL(url).openConnection();
      c.setConnectTimeout(8000);
      c.setReadTimeout(15000);
      c.setInstanceFollowRedirects(true);
      c.setRequestProperty("User-Agent", ua == null || ua.isEmpty() ? profile.st.userAgent : ua);
      if (ref != null && !ref.isEmpty()) c.setRequestProperty("Referer", ref);
      try (InputStream in = c.getInputStream(); OutputStream out = dest) {
        byte[] buf = new byte[16384];
        int n;
        while (!stop && (n = in.read(buf)) > 0) out.write(buf, 0, n);
      }
    } catch (Exception ignored) {
    } finally {
      if (c != null) c.disconnect();
      live = false;
    }
  }

  private void remux(File ts, Uri uri) {
    File src = ts;
    if (src == null && uri != null && "file".equals(uri.getScheme())) {
      src = new File(uri.getPath());
    }
    if (src == null || !src.isFile() || src.length() < 1024) return;
    File mp4 = new File(src.getAbsolutePath().replaceAll("\\.ts$", "") + ".mp4");
    if (copyMux(src, mp4, android.media.MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)) {
      src.delete();
      return;
    }
    try { if (mp4.exists() && mp4.length() < 1024) mp4.delete(); } catch (Exception ignored) {}
    File mkv = new File(src.getAbsolutePath().replaceAll("\\.ts$", "") + ".mkv");
    /* MediaMuxer не умеет MKV: оставляем исходный .ts */
    try { if (mkv.exists() && mkv.length() < 1024) mkv.delete(); } catch (Exception ignored) {}
  }

  private static boolean copyMux(File src, File dest, int format) {
    android.media.MediaExtractor ex = new android.media.MediaExtractor();
    android.media.MediaMuxer mux = null;
    try {
      ex.setDataSource(src.getAbsolutePath());
      mux = new android.media.MediaMuxer(dest.getAbsolutePath(), format);
      int[] map = new int[ex.getTrackCount()];
      boolean any = false;
      for (int i = 0; i < ex.getTrackCount(); i++) {
        android.media.MediaFormat f = ex.getTrackFormat(i);
        map[i] = mux.addTrack(f);
        any = true;
      }
      if (!any) return false;
      mux.start();
      java.nio.ByteBuffer buf = java.nio.ByteBuffer.allocate(1 << 20);
      android.media.MediaCodec.BufferInfo info = new android.media.MediaCodec.BufferInfo();
      long t0 = System.currentTimeMillis();
      for (int i = 0; i < ex.getTrackCount(); i++) {
        ex.selectTrack(i);
        while (System.currentTimeMillis() - t0 < 120_000) {
          info.offset = 0;
          int n = ex.readSampleData(buf, 0);
          if (n < 0) break;
          info.size = n;
          info.presentationTimeUs = ex.getSampleTime();
          info.flags = ex.getSampleFlags();
          mux.writeSampleData(map[i], buf, info);
          if (!ex.advance()) break;
        }
        ex.unselectTrack(i);
      }
      mux.stop();
      return dest.isFile() && dest.length() > 1024;
    } catch (Exception e) {
      try { dest.delete(); } catch (Exception ignored) {}
      return false;
    } finally {
      try { ex.release(); } catch (Exception ignored) {}
      if (mux != null) try { mux.release(); } catch (Exception ignored) {}
    }
  }

  private static String safe(String s) {
    return s.replaceAll("[\\\\/:*?\"<>|]", "_");
  }
}

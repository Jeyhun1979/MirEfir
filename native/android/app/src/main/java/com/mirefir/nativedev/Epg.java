package com.mirefir.nativedev;

import android.content.ContentValues;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteStatement;
import android.util.Log;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.GZIPInputStream;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserFactory;

final class Epg {
  private static final String TAG = "MirEfirEpg";
  private final Profile profile;
  private SQLiteDatabase db;
  private long lastOk;
  private long lastFail;
  private long fetchStarted;
  private int fileBack = -1, fileAhead = -1;
  private volatile boolean ingesting;
  private volatile boolean fetching;
  /** prune/VACUUM: UI не должен ждать SQLite. */
  private volatile boolean locking;
  /** Мультиэкран: не качать гид, пока на экране несколько эфиров. */
  private volatile boolean held;
  private volatile Map<String, Set<String>> bindExact = new HashMap<>();
  private volatile Map<String, Set<String>> bindFold = new HashMap<>();
  private final Map<String, Set<String>> xmlKeys = new HashMap<>();

  Epg(Profile profile) {
    this.profile = profile;
    File f = profile.file("epg.db");
    db = SQLiteDatabase.openOrCreateDatabase(f, null);
    try { db.disableWriteAheadLogging(); } catch (Exception ignored) {}
    try { db.execSQL("PRAGMA journal_mode=DELETE"); } catch (Exception ignored) {}
    try { profile.file("epg.stat").delete(); } catch (Exception ignored) {}
    db.execSQL("CREATE TABLE IF NOT EXISTS p (k TEXT, s INTEGER, e INTEGER, t TEXT, d TEXT)");
    db.execSQL("CREATE TABLE IF NOT EXISTS m (k TEXT PRIMARY KEY, v TEXT)");
    db.execSQL("CREATE INDEX IF NOT EXISTS i_k ON p(k,s)");
    try { db.execSQL("CREATE UNIQUE INDEX IF NOT EXISTS i_ks ON p(k,s)"); } catch (Exception ignored) {}
    /* UI никогда не ждёт писателя: 5 с busy_timeout замораживало пульт на приставке. */
    try { db.execSQL("PRAGMA busy_timeout=50"); } catch (Exception ignored) {}
    /* гид — перекачиваемый кэш: без fsync на каждый коммит (на приставках с eMMC это минуты) */
    try { db.execSQL("PRAGMA synchronous=OFF"); } catch (Exception ignored) {}
    db.execSQL("CREATE TABLE IF NOT EXISTS i (k TEXT PRIMARY KEY, u TEXT)");
    loadMeta();
    loadIcons();
  }

  private void loadIcons() {
    Map<String, String> m = new ConcurrentHashMap<>();
    try (Cursor c = db.rawQuery("SELECT k,u FROM i", null)) {
      while (c.moveToNext()) {
        String k = c.getString(0), u = c.getString(1);
        if (k != null && u != null && !u.isEmpty()) m.put(k, u);
      }
    } catch (Exception ignored) {}
    iconUrl = m;
    iconVer++;
  }

  /** Иконка канала из XMLTV (как epg_channel_icon на Windows) или null. */
  String icon(int index) {
    if (index < 0 || index >= profile.pl.size()) return null;
    return iconUrl.get(keyFor(profile.pl.get(index)));
  }

  /** Результат закачки: UI перерисовывает гид и показывает итог. */
  interface Listener {
    void onEpgDone(boolean ok, boolean forced);
  }

  private volatile Listener listener;
  /** Номер «поколения»: ручная загрузка увеличивает его и обрывает текущий разбор. */
  private volatile int gen;
  private volatile int runGen;
  private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
    Thread t = new Thread(r, "epg");
    t.setDaemon(true);
    return t;
  });
  private volatile Map<String, String> iconUrl = new ConcurrentHashMap<>();
  private volatile int iconVer;
  private final Map<String, String> pendIcon = new HashMap<>();
  private final Map<String, Integer> pendRank = new HashMap<>();
  private volatile Set<String> shiftedKeys = new HashSet<>();
  private boolean replaceMode;
  private final Set<String> touched = new HashSet<>();
  private long winFrom, winTo;
  private String fetchBase = "";

  void setListener(Listener l) { listener = l; }

  boolean busy() { return fetching || ingesting; }

  /** Писатель держит epg.db — UI читает только кэш. */
  boolean writerBusy() { return ingesting || locking; }

  /** Приостановить закачку гида (мультиэкран), чтобы не спорить с декодерами. */
  void hold(boolean on) {
    if (held == on) return;
    held = on;
    if (on) {
      gen++;
      fetching = false;
    }
  }

  /** Версия набора иконок: растёт, когда после закачки появились новые логотипы. */
  int iconVersion() { return iconVer; }

  void start() { start(false); }

  /**
   * force = «Загрузить телепрограмму» / смена источника: сбросить флаг «свежо», паузу после ошибки,
   * кэш «сейчас в эфире», оборвать идущий разбор и качать полным окном.
   */
  void start(boolean force) {
    if (held && !force) return;
    if (!force && fetching && System.currentTimeMillis() - fetchStarted < 12L * 60L * 1000L) return;
    if (force) {
      gen++;
      resetState();
    }
    final int my = gen;
    fetching = true;
    fetchStarted = System.currentTimeMillis();
    worker.execute(() -> {
      boolean ok = false, ran = false;
      try {
        if (my != gen) return;
        runGen = my;
        rebuildKeys();
        if (force || !fresh()) {
          ran = true;
          ok = refresh(rowCount() > 0 && !force);
        }
      } catch (Throwable t) {
        Log.w(TAG, "epg job failed: " + t.getClass().getSimpleName());
      } finally {
        if (my == gen) fetching = false;
      }
      if (ran && my == gen) done(ok, force);
    });
  }

  private void resetState() {
    lastFail = 0;
    lastOk = 0;
    dropCaches();
  }

  private void done(boolean ok, boolean forced) {
    dropCaches();
    Listener l = listener;
    if (l != null) l.onEpgDone(ok, forced);
  }

  private void dropCaches() {
    nowMap.clear();
    nowMapAt = 0;
    listIdx = Integer.MIN_VALUE;
    listCached = Collections.emptyList();
  }

  private boolean cancelled() { return runGen != gen; }

  void applySettings() {
    worker.execute(() -> {
      rebuildKeys();
      int gone = prune();
      compact(gone > 0);
    });
  }

  void tick() {
    if (held) return;
    if (!profile.st.epgAutoUpdate) return;
    if (fetching) return;
    if (profile.pl.isEmpty()) return;
    if (fresh()) return;
    if (lastFail > 0 && System.currentTimeMillis() - lastFail < 30L * 60L * 1000L) return;
    fetching = true;
    fetchStarted = System.currentTimeMillis();
    final int my = gen;
    worker.execute(() -> {
      boolean ok = false, ran = false;
      try {
        if (my != gen) return;
        runGen = my;
        ran = true;
        ok = refresh(rowCount() > 0);
      } catch (Throwable t) {
        Log.w(TAG, "epg tick failed: " + t.getClass().getSimpleName());
      } finally {
        if (my == gen) fetching = false;
      }
      if (ran && my == gen) done(ok, false);
    });
  }

  private void cleanupTmp() {
    File[] junk = {
      profile.file("epg.tmp"),
      profile.file("epg.tmp.part"),
      new File(profile.file("epg.tmp").getAbsolutePath() + ".part")
    };
    for (File f : junk) {
      try { if (f.exists()) f.delete(); } catch (Exception ignored) {}
    }
  }

  private void loadMeta() {
    lastOk = longMeta("lastOk");
    fileBack = (int) longMeta("back");
    fileAhead = (int) longMeta("ahead");
  }

  private long longMeta(String key) {
    try (Cursor c = db.rawQuery("SELECT v FROM m WHERE k=?", new String[] {key})) {
      if (c.moveToFirst()) return Long.parseLong(c.getString(0));
    } catch (Exception ignored) {}
    return 0;
  }

  private void saveMeta() {
    putMeta("lastOk", "" + lastOk);
    putMeta("back", "" + pastDays());
    putMeta("ahead", "" + profile.st.epgDays);
    fileBack = pastDays();
    fileAhead = profile.st.epgDays;
  }

  private void putMeta(String k, String v) {
    ContentValues row = new ContentValues();
    row.put("k", k);
    row.put("v", v);
    db.insertWithOnConflict("m", null, row, SQLiteDatabase.CONFLICT_REPLACE);
  }

  /**
   * Только поля в памяти. SELECT COUNT(*) по epg.db (сотни МБ) на главном потоке
   * держал пульт 1,5–10 с на каждом tick.
   */
  private boolean fresh() {
    if (lastOk <= 0) return false;
    if (fileBack != pastDays() || fileAhead != profile.st.epgDays) return false;
    long due = Math.max(1, profile.st.epgUpdateHours) * 3600L * 1000L;
    return System.currentTimeMillis() - lastOk < due;
  }

  /** Новые карты собираются отдельно и подменяются целиком: UI и закачка не делят HashMap. */
  private synchronized void rebuildKeys() {
    Map<String, Set<String>> ex = new HashMap<>();
    Map<String, Set<String>> fo = new HashMap<>();
    Set<String> sh = new HashSet<>();
    for (Channel ch : new ArrayList<>(profile.pl)) {
      String k = keyFor(ch);
      if (shifted(ch.name) || shifted(ch.tvgName)) sh.add(k);
      addBind(ex, norm(ch.tvgId), k);
      addBind(ex, norm(ch.tvgName), k);
      addBind(ex, norm(ch.name), k);
      addBind(fo, foldKey(ch.tvgId), k);
      addBind(fo, foldKey(ch.tvgName), k);
      addBind(fo, foldKey(ch.name), k);
    }
    bindExact = ex;
    bindFold = fo;
    shiftedKeys = sh;
  }

  private void addBind(Map<String, Set<String>> into, String id, String k) {
    if (id == null || id.isEmpty() || k == null || k.isEmpty()) return;
    Set<String> s = into.get(id);
    if (s == null) {
      s = new HashSet<>();
      into.put(id, s);
    }
    s.add(k);
  }

  private int pastDays() {
    int back = Math.max(1, profile.st.epgDays);
    if (profile.st.archiveEnabled && profile.st.archiveDays > back) back = profile.st.archiveDays;
    return back;
  }

  private long todayMidnight() {
    Calendar c = Calendar.getInstance();
    c.set(Calendar.HOUR_OF_DAY, 0);
    c.set(Calendar.MINUTE, 0);
    c.set(Calendar.SECOND, 0);
    c.set(Calendar.MILLISECOND, 0);
    return c.getTimeInMillis();
  }

  private long windowStart() {
    return todayMidnight() - pastDays() * 24L * 3600L * 1000L;
  }

  private long windowEnd() {
    return todayMidnight() + (Math.max(1, profile.st.epgDays) + 1) * 24L * 3600L * 1000L;
  }

  private int prune() {
    int n = 0;
    locking = true;
    try {
      n = db.delete("p", "e<? OR s>?", new String[] {"" + windowStart(), "" + windowEnd()});
      db.execSQL("PRAGMA journal_mode=DELETE");
    } catch (Exception ignored) {
    } finally {
      locking = false;
    }
    return n;
  }

  /** Убрать дыры SQLite. Не режет окно гида. На большой базе VACUUM держит eMMC минутами — пульт мёртв. */
  private void compact(boolean deleted) {
    if (!deleted) return;
    File f = profile.file("epg.db");
    if (f == null || !f.isFile()) return;
    long bytes = f.length();
    if (bytes >= 80L * 1024 * 1024) {
      Log.i(TAG, "vacuum skip bytes=" + bytes);
      return;
    }
    if (cancelled()) return;
    locking = true;
    Log.i(TAG, "vacuum start bytes=" + bytes);
    try {
      db.execSQL("VACUUM");
    } catch (Exception e) {
      Log.w(TAG, "vacuum: " + e.getClass().getSimpleName());
    } finally {
      locking = false;
    }
    Log.i(TAG, "vacuum done bytes=" + f.length());
  }

  private int rowCount() {
    try (Cursor c = db.rawQuery("SELECT COUNT(*) FROM p", null)) {
      if (c.moveToFirst()) return c.getInt(0);
    } catch (Exception ignored) {}
    return 0;
  }

  private long coverageEnd() {
    try (Cursor c = db.rawQuery("SELECT MAX(e) FROM p", null)) {
      if (c.moveToFirst()) return c.getLong(0);
    } catch (Exception ignored) {}
    return 0;
  }

  /**
   * Цепочка как на Windows: основной → дополнительный → остальные; первый годный источник
   * останавливает цепочку, остальные не качаются. Ссылки в журнал не пишутся.
   */
  private boolean refresh(boolean slice) {
    rebuildKeys();
    if (bindExact.isEmpty() && bindFold.isEmpty()) {
      Log.w(TAG, "no channels to bind");
      return false;
    }
    List<String> chain = profile.epgChain();
    Log.i(TAG, "refresh start: sources=" + chain.size() + " slice=" + slice);
    boolean ok = false;
    int n = 0;
    for (String url : chain) {
      if (url == null || url.isEmpty()) continue;
      if (cancelled()) return false;
      n++;
      boolean got = ingest(url, slice);
      Log.i(TAG, "source #" + n + (got ? " ok" : " empty/failed"));
      if (got) {
        ok = true;
        break;
      }
    }
    if (cancelled()) return false;
    if (ok) {
      lastOk = System.currentTimeMillis();
      lastFail = 0;
      int gone = prune();
      compact(gone > 0);
      saveMeta();
    } else {
      lastFail = System.currentTimeMillis();
    }
    return ok;
  }

  private boolean ingest(String url, boolean slice) {
    xmlKeys.clear();
    pendIcon.clear();
    pendRank.clear();
    touched.clear();
    fetchBase = url;
    long from = windowStart();
    long to = windowEnd();
    winFrom = from;
    winTo = to;
    replaceMode = !slice;
    if (slice) {
      long have = coverageEnd();
      long chunk = Math.max(1, profile.st.epgUpdateHours) * 3600L * 1000L;
      from = Math.max(from, have - 30L * 60L * 1000L);
      to = Math.min(to, have + chunk);
      if (from >= to) return true;
    }
    final long keepFrom = from;
    final long keepTo = to;
    File tmp = profile.file("epg.tmp");
    cleanupTmp();
    try {
      if (!Http.getFile(url, profile.st.userAgent, tmp, 20000, 32)) return false;
      if (tmp.length() < 32 || cancelled()) return false;
      boolean ok;
      try (InputStream in = new FileInputStream(tmp)) {
        if (looksJson(tmp)) ok = parseJson(tmp, keepFrom, keepTo);
        else ok = parseXml(in, keepFrom, keepTo);
      }
      if (ok) commitIcons();
      return ok;
    } catch (Throwable t) {
      Log.w(TAG, "ingest failed: " + t.getClass().getSimpleName());
      ingesting = false;
      return false;
    } finally {
      replaceMode = false;
      cleanupTmp();
    }
  }

  /** Иконки из канала XMLTV → таблица i (ключ канала плейлиста → URL), без затирания прежних. */
  private void commitIcons() {
    if (pendIcon.isEmpty()) return;
    Map<String, String> next = new ConcurrentHashMap<>(iconUrl);
    db.beginTransactionNonExclusive();
    try {
      for (Map.Entry<String, String> e : pendIcon.entrySet()) {
        String k = e.getKey(), u = e.getValue();
        if (k == null || k.isEmpty() || u == null || u.isEmpty()) continue;
        ContentValues row = new ContentValues();
        row.put("k", k);
        row.put("u", u);
        db.insertWithOnConflict("i", null, row, SQLiteDatabase.CONFLICT_REPLACE);
        next.put(k, u);
      }
      db.setTransactionSuccessful();
    } catch (Exception e) {
      Log.w(TAG, "icons save failed");
    } finally {
      try { db.endTransaction(); } catch (Exception ignored) {}
    }
    iconUrl = next;
    iconVer++;
    Log.i(TAG, "icons stored: " + pendIcon.size());
    pendIcon.clear();
    pendRank.clear();
  }

  private void noteIcon(Set<String> keys, String src, int rank) {
    if (keys == null || keys.isEmpty() || src == null || src.isEmpty()) return;
    String url = resolveIcon(src);
    if (url == null) return;
    for (String k : keys) {
      Integer old = pendRank.get(k);
      if (old == null || rank < old) {
        pendIcon.put(k, url);
        pendRank.put(k, rank);
      }
    }
  }

  private String resolveIcon(String src) {
    String s = src.trim();
    if (s.isEmpty()) return null;
    String low = s.toLowerCase(Locale.ROOT);
    if (low.startsWith("http://") || low.startsWith("https://")) return s;
    String base = fetchBase == null ? "" : fetchBase;
    String bl = base.toLowerCase(Locale.ROOT);
    if (!(bl.startsWith("http://") || bl.startsWith("https://"))) return null;
    try {
      return new java.net.URL(new java.net.URL(base), s).toString();
    } catch (Exception e) {
      return null;
    }
  }

  private boolean parseXml(InputStream raw, long from, long to) throws Exception {
    InputStream in = openXml(raw);
    if (in == null) return false;
    XmlPullParserFactory f = XmlPullParserFactory.newInstance();
    f.setNamespaceAware(false);
    XmlPullParser x = f.newPullParser();
    x.setInput(in, "UTF-8");
    boolean any = false;
    String ch = "";
    Programme cur = null;
    boolean inChannel = false;
    String xmlId = "";
    String iconSrc = "";
    String capture = "";
    StringBuilder textBuf = new StringBuilder();
    List<String> names = new ArrayList<>();
    ingesting = true;
    int batch = 0;
    int statCh = 0, statMap = 0, statProg = 0, statStore = 0;
    SQLiteStatement ins = db.compileStatement("INSERT OR REPLACE INTO p(k,s,e,t,d) VALUES(?,?,?,?,?)");
    db.beginTransactionNonExclusive();
    try {
      int ev = x.getEventType();
      while (ev != XmlPullParser.END_DOCUMENT) {
        if (cancelled()) throw new java.util.concurrent.CancellationException("epg superseded");
        if (ev == XmlPullParser.START_TAG) {
          String n = local(x.getName());
          textBuf.setLength(0);
          capture = "";
          if ("channel".equals(n) && cur == null) {
            inChannel = true;
            xmlId = attr(x, "id");
            names.clear();
            iconSrc = "";
          } else if (inChannel && "icon".equals(n)) {
            if (iconSrc.isEmpty()) iconSrc = attr(x, "src");
          } else if (inChannel && "display-name".equals(n)) {
            capture = "dn";
          } else if ("programme".equals(n)) {
            cur = new Programme();
            ch = attr(x, "channel");
            cur.start = parseXmltv(attr(x, "start"));
            cur.end = parseXmltv(attr(x, "stop"));
          } else if (cur != null && "title".equals(n)) {
            capture = "title";
          } else if (cur != null && "desc".equals(n)) {
            capture = "desc";
          }
        } else if (ev == XmlPullParser.TEXT || ev == XmlPullParser.CDSECT) {
          if (capture.length() > 0) {
            String t = x.getText();
            if (t != null) textBuf.append(t);
          }
        } else if (ev == XmlPullParser.END_TAG) {
          String n = local(x.getName());
          if ("display-name".equals(n) && inChannel) {
            String dn = textBuf.toString().trim();
            if (!dn.isEmpty()) names.add(dn);
          } else if ("title".equals(n) && cur != null) {
            cur.title = clip(textBuf.toString(), Limits.TITLE_MAX);
            if (cur.title.isEmpty()) cur.title = I18n.t("programme");
          } else if ("desc".equals(n) && cur != null) {
            cur.desc = clip(textBuf.toString(), 400);
          } else if ("channel".equals(n) && inChannel) {
            statCh++;
            if (mapChannel(xmlId, names, iconSrc) != null) statMap++;
            inChannel = false;
          } else if ("programme".equals(n) && cur != null) {
            statProg++;
            if (statProg % 50000 == 0) Log.i(TAG, "xmltv: programmes=" + statProg + " stored=" + statStore);
            if (store(ins, ch, cur, from, to)) {
              any = true;
              statStore++;
              if (++batch >= 4000) {
                db.setTransactionSuccessful();
                db.endTransaction();
                db.beginTransactionNonExclusive();
                batch = 0;
              }
            }
            cur = null;
          }
          capture = "";
          textBuf.setLength(0);
        }
        ev = x.next();
      }
      if (any) db.setTransactionSuccessful();
    } catch (Throwable t) {
      throw t;
    } finally {
      try { ins.close(); } catch (Exception ignored) {}
      try { db.endTransaction(); } catch (Exception ignored) {}
      ingesting = false;
      try { in.close(); } catch (Exception ignored) {}
    }
    Log.i(TAG, "xmltv: channels=" + statCh + " bound=" + statMap + " programmes=" + statProg + " stored=" + statStore
        + " icons=" + pendIcon.size());
    return any;
  }

  private InputStream openXml(InputStream raw) throws Exception {
    BufferedInputStream buf = new BufferedInputStream(raw, 16384);
    buf.mark(32);
    int a = buf.read();
    int b = buf.read();
    buf.reset();
    if (a < 0) return null;
    if (a == '<') {
      byte[] head = new byte[24];
      buf.mark(24);
      int n = buf.read(head);
      buf.reset();
      String s = n > 0 ? new String(head, 0, n).toLowerCase(Locale.ROOT) : "";
      if (s.contains("html")) return null;
      return buf;
    }
    if ((a & 0xff) == 0x1f && (b & 0xff) == 0x8b) {
      return new BufferedInputStream(new GZIPInputStream(buf, 65536), 65536);
    }
    return buf;
  }

  private boolean mapChannel(String xmlId, List<String> display) {
    return mapChannel(xmlId, display, null) != null;
  }

  /**
   * Как bind_channel_obj на Windows: id и каждое display-name сопоставляются с каналами плейлиста
   * и точно, и по «семейству» (без UHD / 4K / FHD / HD / SD / 50fps…). Раньше свёртка включалась
   * только когда точных совпадений не было, и UHD-канал рядом с HD-каналом оставался без гида.
   */
  private Set<String> mapChannel(String xmlId, List<String> display, String icon) {
    Set<String> keys = new HashSet<>();
    boolean sh = shifted(xmlId);
    addResolved(keys, xmlId);
    if (display != null) {
      for (String dn : display) {
        addResolved(keys, dn);
        if (shifted(dn)) sh = true;
      }
    }
    if (keys.isEmpty()) return null;
    if (xmlId != null && !xmlId.isEmpty()) xmlKeys.put(norm(xmlId), keys);
    if (icon != null && !icon.isEmpty()) noteIcon(keys, icon, sh ? 2 : 0);
    return keys;
  }

  private void addResolved(Set<String> keys, String raw) {
    if (raw == null || raw.isEmpty()) return;
    Set<String> hit = bindExact.get(norm(raw));
    if (hit != null) keys.addAll(hit);
    String f = foldKey(raw);
    if (f.isEmpty()) return;
    Set<String> fh = bindFold.get(f);
    if (fh == null || fh.isEmpty()) return;
    Set<String> sk = shiftedKeys;
    if (shifted(raw)) {
      /* «Первый +2» в XMLTV — только каналам со сдвигом */
      for (String k : fh) if (sk.contains(k)) keys.add(k);
    } else {
      /* обычное имя — обычным каналам; сдвинутым только если других нет */
      boolean plain = false;
      for (String k : fh) {
        if (!sk.contains(k)) { keys.add(k); plain = true; }
      }
      if (!plain) keys.addAll(fh);
    }
  }

  private boolean store(SQLiteStatement ins, String ch, Programme p, long from, long to) {
    if (ch == null || ch.isEmpty() || p.start <= 0) return false;
    if (p.end > 0 && p.end <= p.start) return false;
    if (p.end > 0 && p.end < from) return false;
    if (p.start > to) return false;
    Set<String> keys = xmlKeys.get(norm(ch));
    if (keys == null || keys.isEmpty()) {
      keys = new HashSet<>();
      addResolved(keys, ch);
      if (!keys.isEmpty()) xmlKeys.put(norm(ch), keys);
    }
    if (keys == null || keys.isEmpty()) return false;
    boolean any = false;
    for (String k : keys) {
      /* полная перезагрузка: у канала сначала убираем старые передачи окна (другой источник — другие start) */
      if (replaceMode && touched.add(k)) {
        try {
          db.delete("p", "k=? AND e>? AND s<?", new String[] {k, "" + winFrom, "" + winTo});
        } catch (Exception ignored) {}
      }
      ins.clearBindings();
      ins.bindString(1, k);
      ins.bindLong(2, p.start);
      ins.bindLong(3, p.end);
      String title = p.title == null || p.title.isEmpty() ? I18n.t("programme") : p.title;
      ins.bindString(4, title);
      ins.bindString(5, p.desc == null ? "" : p.desc);
      ins.executeInsert();
      any = true;
    }
    return any;
  }

  private static String local(String n) {
    if (n == null) return "";
    int i = n.indexOf(':');
    return i >= 0 ? n.substring(i + 1) : n;
  }

  private static String clip(String s, int max) {
    if (s == null) return "";
    String t = s.trim();
    return t.length() <= max ? t : t.substring(0, max);
  }

  private static String norm(String s) {
    return s == null ? "" : s.trim().toLowerCase(Locale.ROOT);
  }

  private static String foldKey(String name) {
    if (name == null || name.isEmpty()) return "";
    String lower = name.toLowerCase(Locale.ROOT).replace('ё', 'е');
    StringBuilder b = new StringBuilder();
    boolean skipParen = false;
    for (int i = 0; i < lower.length(); ) {
      char c = lower.charAt(i);
      if (c == '+') {
        int j = i + 1;
        if (j < lower.length() && cDigit(lower.charAt(j))) {
          while (j < lower.length() && cDigit(lower.charAt(j))) j++;
          i = j;
          space(b);
          continue;
        }
      }
      if (c == '(') {
        skipParen = true;
        space(b);
        i++;
        continue;
      }
      if (c == ')') {
        skipParen = false;
        i++;
        continue;
      }
      if (skipParen) {
        i++;
        continue;
      }
      boolean ok = cDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'а' && c <= 'я');
      if (!ok) space(b);
      else b.append(c);
      i++;
    }
    StringBuilder out = new StringBuilder();
    for (String tok : b.toString().trim().split(" +")) {
      if (tok.isEmpty()) continue;
      if (M3u.isQualityToken(tok) || tok.equals("50") || tok.equals("60")) continue;
      if (out.length() > 0) out.append(' ');
      out.append(tok);
    }
    return out.toString();
  }

  /** «+2», «+4» и т.п. в имени: передачи со сдвигом по времени. */
  private static boolean shifted(String s) {
    if (s == null) return false;
    for (int i = 0; i + 1 < s.length(); i++) {
      if (s.charAt(i) == '+' && cDigit(s.charAt(i + 1))) return true;
    }
    return false;
  }

  private static boolean cDigit(char c) { return c >= '0' && c <= '9'; }

  private static void space(StringBuilder b) {
    if (b.length() > 0 && b.charAt(b.length() - 1) != ' ') b.append(' ');
  }

  private static String attr(XmlPullParser x, String name) {
    int n = x.getAttributeCount();
    for (int i = 0; i < n; i++) {
      String an = local(x.getAttributeName(i));
      if (name.equals(an)) {
        String v = x.getAttributeValue(i);
        return v == null ? "" : v;
      }
    }
    String v = x.getAttributeValue(null, name);
    if (v == null) v = x.getAttributeValue("", name);
    return v == null ? "" : v;
  }

  static long parseXmltv(String s) {
    if (s == null || s.length() < 12) return 0;
    try {
      int Y = Integer.parseInt(s.substring(0, 4));
      int M = Integer.parseInt(s.substring(4, 6));
      int D = Integer.parseInt(s.substring(6, 8));
      int h = Integer.parseInt(s.substring(8, 10));
      int m = Integer.parseInt(s.substring(10, 12));
      int sec = s.length() >= 14 ? Integer.parseInt(s.substring(12, 14)) : 0;
      Calendar c = Calendar.getInstance();
      int offMin = 0;
      int plus = s.indexOf('+');
      int minus = s.indexOf('-', 14);
      int tz = plus >= 0 ? plus : minus;
      if (tz > 0 && s.length() >= tz + 5) {
        int hh = Integer.parseInt(s.substring(tz + 1, tz + 3));
        int mm = Integer.parseInt(s.substring(tz + 3, tz + 5));
        offMin = (hh * 60 + mm) * (plus >= 0 ? 1 : -1);
        c.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
      }
      c.set(Y, M - 1, D, h, m, sec);
      c.set(Calendar.MILLISECOND, 0);
      return c.getTimeInMillis() - offMin * 60L * 1000L;
    } catch (Exception e) {
      return 0;
    }
  }

  String keyFor(Channel ch) {
    if (ch.tvgId != null && !ch.tvgId.isEmpty()) return ch.tvgId.toLowerCase(Locale.ROOT);
    return ch.name.toLowerCase(Locale.ROOT);
  }

  private static boolean looksJson(File f) {
    try (FileInputStream in = new FileInputStream(f)) {
      int a = in.read();
      while (a == ' ' || a == '\n' || a == '\r' || a == '\t') a = in.read();
      return a == '{' || a == '[';
    } catch (Exception e) {
      return false;
    }
  }

  private boolean parseJson(File f, long from, long to) {
    String body = Util.read(f);
    if (body == null || body.length() < 32) return false;
    try {
      org.json.JSONObject o = new org.json.JSONObject(body);
      org.json.JSONArray chs = o.optJSONArray("channels");
      org.json.JSONArray progs = o.optJSONArray("programs");
      if (progs == null) progs = o.optJSONArray("programmes");
      if (chs != null) {
        for (int i = 0; i < chs.length(); i++) {
          org.json.JSONObject c = chs.optJSONObject(i);
          if (c == null) continue;
          String id = c.optString("id", c.optString("tvg_id", ""));
          java.util.List<String> names = new ArrayList<>();
          names.add(c.optString("name", c.optString("display-name", "")));
          mapChannel(id, names, c.optString("icon", c.optString("logo", "")));
        }
      }
      if (progs == null) return false;
      SQLiteStatement ins = db.compileStatement("INSERT OR REPLACE INTO p(k,s,e,t,d) VALUES(?,?,?,?,?)");
      db.beginTransactionNonExclusive();
      boolean any = false;
      try {
        for (int i = 0; i < progs.length(); i++) {
          org.json.JSONObject p = progs.optJSONObject(i);
          if (p == null) continue;
          Programme cur = new Programme();
          cur.start = p.optLong("start", 0);
          cur.end = p.optLong("end", 0);
          if (cur.start < 1_000_000_000_000L) cur.start *= 1000;
          if (cur.end < 1_000_000_000_000L) cur.end *= 1000;
          cur.title = p.optString("title", I18n.t("programme"));
          cur.desc = p.optString("desc", p.optString("description", ""));
          String ch = p.optString("channel", p.optString("channel_id", ""));
          if (store(ins, ch, cur, from, to)) any = true;
        }
        if (any) db.setTransactionSuccessful();
      } finally {
        try { ins.close(); } catch (Exception ignored) {}
        try { db.endTransaction(); } catch (Exception ignored) {}
      }
      return any;
    } catch (Exception e) {
      return false;
    }
  }

  private long offsetMs() {
    return profile.st.epgOffsetHours * 3600L * 1000L;
  }

  private void applyOffset(Programme p) {
    if (p == null) return;
    long o = offsetMs();
    if (o == 0) return;
    if (p.start > 0) p.start += o;
    if (p.end > 0) p.end += o;
  }

  private static final Programme NOW_NONE = new Programme();
  private final Map<Integer, Programme> nowMap = new ConcurrentHashMap<>();
  private volatile long nowMapAt;
  private volatile int listIdx = Integer.MIN_VALUE;
  private volatile List<Programme> listCached = Collections.emptyList();

  private static Programme nowOrNull(Programme p) {
    return p == null || p == NOW_NONE ? null : p;
  }

  Programme now(int index) {
    if (index < 0 || index >= profile.pl.size()) return null;
    long t = System.currentTimeMillis();
    /* во время записи epg.db UI не трогает SQLite — иначе пульт ждёт lock */
    if (writerBusy()) return nowOrNull(nowMap.get(index));
    if (t - nowMapAt < 800 && nowMap.containsKey(index)) return nowOrNull(nowMap.get(index));
    if (t - nowMapAt >= 800) {
      nowMap.clear();
      nowMapAt = t;
    }
    String k = keyFor(profile.pl.get(index));
    long q = t - offsetMs();
    try (Cursor c = db.query("p", new String[] {"s", "e", "t", "d"}, "k=? AND s<=? AND e>?",
            new String[] {k, "" + q, "" + q}, null, null, "s DESC", "1")) {
      if (!c.moveToFirst()) {
        nowMap.put(index, NOW_NONE);
        return null;
      }
      Programme p = new Programme();
      p.start = c.getLong(0);
      p.end = c.getLong(1);
      p.title = c.getString(2);
      p.desc = c.getString(3);
      applyOffset(p);
      nowMap.put(index, p);
      return p;
    } catch (Exception e) {
      return nowOrNull(nowMap.get(index));
    }
  }

  List<Programme> list(int index) {
    if (index < 0 || index >= profile.pl.size()) return Collections.emptyList();
    if (writerBusy()) {
      return index == listIdx ? listCached : Collections.emptyList();
    }
    List<Programme> out = new ArrayList<>();
    String k = keyFor(profile.pl.get(index));
    long from = windowStart();
    long to = windowEnd();
    try (Cursor c = db.query("p", new String[] {"s", "e", "t", "d"}, "k=? AND e>? AND s<?",
            new String[] {k, "" + from, "" + to}, null, null, "s ASC", null)) {
      while (c.moveToNext()) {
        Programme p = new Programme();
        p.start = c.getLong(0);
        p.end = c.getLong(1);
        p.title = c.getString(2);
        p.desc = c.getString(3);
        applyOffset(p);
        out.add(p);
      }
    } catch (Exception ignored) {}
    listIdx = index;
    listCached = out;
    return out;
  }

  int liveIndex(int ch) {
    List<Programme> all = list(ch);
    long now = System.currentTimeMillis();
    for (int i = 0; i < all.size(); i++) {
      if (all.get(i).start <= now && all.get(i).end > now) return i;
    }
    return Math.max(0, all.size() - 1);
  }
}

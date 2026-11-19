package com.mirefir.nativedev;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.SystemClock;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** Кэш логотипов каналов: память + диск, очередь по тику, старт через 6 с после картинки. */
final class Logo {
  private static final int MAX_MEM = 96;
  private final Profile profile;
  private final File cacheDir;
  private final Map<String, Bitmap> mem = new LinkedHashMap<String, Bitmap>(64, 0.75f, true) {
    @Override protected boolean removeEldestEntry(Map.Entry<String, Bitmap> e) {
      return size() > MAX_MEM;
    }
  };
  private final ConcurrentHashMap<String, Long> fail = new ConcurrentHashMap<>();
  private final List<String> queue = new ArrayList<>();
  private final Object lock = new Object();
  private volatile boolean busy;
  private long notBefore;
  private Runnable onReady;
  private Epg epg;
  private List<Channel> famRef;
  private int famSize = -1, famVer = -1;
  private Map<String, String> famUrl = new HashMap<>();

  Logo(Profile profile) {
    this.profile = profile;
    cacheDir = new File(profile.dir, "logos");
    cacheDir.mkdirs();
  }

  void setListener(Runnable r) { onReady = r; }

  void setEpg(Epg e) { epg = e; }

  /** Ошибка загрузки помнится 60 с, потом повтор (на Windows 20 с): сеть могла быть не готова. */
  private boolean failed(String url) {
    Long t = fail.get(url);
    if (t == null) return false;
    if (SystemClock.uptimeMillis() - t > 60_000L) {
      fail.remove(url);
      return false;
    }
    return true;
  }

  private static boolean web(String u) {
    return u != null && (Util.starts(u, "http://") || Util.starts(u, "https://"));
  }

  /**
   * URL логотипа канала: свой tvg-logo → иконка канала из XMLTV (как epg_channel_icon на Windows) →
   * логотип «родственного» канала (то же имя без UHD / 4K / FHD / HD / SD / 50fps).
   * Если один источник уже не загрузился, берётся следующий.
   */
  String urlFor(int index) {
    List<Channel> pl = profile.pl;
    if (index < 0 || index >= pl.size()) return null;
    Channel ch = pl.get(index);
    String own = web(ch.logo) ? ch.logo : null;
    String ic = epg != null ? epg.icon(index) : null;
    if (!web(ic)) ic = null;
    if (own != null && !failed(own)) return own;
    if (ic != null && !failed(ic)) return ic;
    String fam = familyUrl(ch);
    if (fam != null && !failed(fam)) return fam;
    return own != null ? own : ic;
  }

  private String familyUrl(Channel ch) {
    if (ch == null || ch.family == null || ch.family.isEmpty()) return null;
    List<Channel> pl = profile.pl;
    int ver = epg != null ? epg.iconVersion() : 0;
    if (famRef != pl || famSize != pl.size() || famVer != ver) {
      Map<String, String> m = new HashMap<>();
      for (int i = 0; i < pl.size(); i++) {
        Channel c = pl.get(i);
        if (c.family == null || c.family.isEmpty() || m.containsKey(c.family)) continue;
        String u = web(c.logo) && !failed(c.logo) ? c.logo : null;
        if (u == null && epg != null) {
          String e = epg.icon(i);
          if (web(e) && !failed(e)) u = e;
        }
        if (u != null) m.put(c.family, u);
      }
      famUrl = m;
      famRef = pl;
      famSize = pl.size();
      famVer = ver;
    }
    return famUrl.get(ch.family);
  }

  void hold(long ms) {
    notBefore = SystemClock.uptimeMillis() + Math.max(0, ms);
  }

  int px() {
    String s = profile.st.logoSize;
    if ("sm".equals(s)) return 22;
    if ("lg".equals(s)) return 40;
    return 30;
  }

  Bitmap get(String url) {
    if (url == null || url.isEmpty()) return null;
    if (!Util.starts(url, "http://") && !Util.starts(url, "https://")) return null;
    synchronized (mem) {
      Bitmap b = mem.get(url);
      if (b != null && !b.isRecycled()) return b;
    }
    if (failed(url)) return null;
    File disk = diskFile(url);
    if (disk.isFile() && disk.length() > 32) {
      Bitmap b = decode(disk);
      if (b != null) {
        synchronized (mem) { mem.put(url, b); }
        return b;
      }
    }
    enqueue(url);
    return null;
  }

  /** Поставить в очередь видимые URL, снять до 32 за тик. */
  void tick(List<String> visible) {
    if (SystemClock.uptimeMillis() < notBefore) return;
    if (visible != null) {
      for (String u : visible) {
        if (u == null || u.isEmpty()) continue;
        if (get(u) == null && !failed(u)) enqueue(u);
      }
    }
    List<String> batch = new ArrayList<>();
    synchronized (lock) {
      if (busy) return;
      Iterator<String> it = queue.iterator();
      while (it.hasNext() && batch.size() < 32) {
        String u = it.next();
        it.remove();
        if (!failed(u)) {
          synchronized (mem) {
            if (mem.containsKey(u)) continue;
          }
          batch.add(u);
        }
      }
      if (batch.isEmpty()) return;
      busy = true;
    }
    new Thread(() -> {
      try {
        for (String url : batch) fetch(url);
      } finally {
        busy = false;
      }
    }, "logo").start();
  }

  private void enqueue(String url) {
    if (SystemClock.uptimeMillis() < notBefore) return;
    synchronized (lock) {
      if (queue.contains(url) || queue.size() > 400) return;
      queue.add(url);
    }
  }

  private void fetch(String url) {
    File dest = diskFile(url);
    String ua = profile.st.userAgent;
    if (!Http.getFile(url, ua, dest, 12000, 32)) {
      fail.put(url, SystemClock.uptimeMillis());
      try { dest.delete(); } catch (Exception ignored) {}
      return;
    }
    Bitmap b = decode(dest);
    if (b == null) {
      fail.put(url, SystemClock.uptimeMillis());
      return;
    }
    synchronized (mem) { mem.put(url, b); }
    Runnable r = onReady;
    if (r != null) r.run();
  }

  private Bitmap decode(File f) {
    try {
      BitmapFactory.Options o = new BitmapFactory.Options();
      o.inJustDecodeBounds = true;
      BitmapFactory.decodeFile(f.getAbsolutePath(), o);
      int side = Math.max(1, o.outWidth);
      int sample = 1;
      int want = 96;
      while (side / sample > want * 2) sample *= 2;
      BitmapFactory.Options d = new BitmapFactory.Options();
      d.inSampleSize = sample;
      return BitmapFactory.decodeFile(f.getAbsolutePath(), d);
    } catch (Exception e) {
      return null;
    }
  }

  private File diskFile(String url) {
    int h = url.hashCode();
    String name = String.format(Locale.US, "%08x", h);
    return new File(cacheDir, name);
  }
}

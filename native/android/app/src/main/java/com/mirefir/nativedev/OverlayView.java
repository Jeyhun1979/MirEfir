package com.mirefir.nativedev;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Build;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;
import androidx.media3.common.text.Cue;
import java.io.File;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class OverlayView extends View {
  static final int LIVE = 0, PAD = 1, MENU = 2, SETTINGS = 3, GUIDE = 4, EXIT = 5, SEARCH = 6,
      HISTORY = 7, RECS = 8, MULTI = 9, PIN = 10, SETUP = 11, SHARE = 12, ASK = 13, CHMENU = 14, URL = 15,
      FILEBROWSE = 16, MICPERM = 17;
  static final String[] MENU_ID = {"live", "guide", "recordings", "multiview", "search", "microphone",
      "playlists", "history", "settings", "exit"};
  static final String[] MENU_HINT = {"liveHint", "guideHint", "recordingsHint", "multiviewHint", "searchHint",
      "", "playlistsHint", "historyHint", "settingsHint", "exitHint"};
  static final String[] TAB_ID = {"tabPlaylists", "tabEpg", "tabAppearance", "tabPlayer", "tabRecordings",
      "tabArchive", "tabParental", "tabRemote", "tabLanguage", "tabData", "tabAbout"};
  static final String[] PAD_ID = {"padMenu", "padGuide", "padVoice", "padBack30", "padPause", "padFwd30",
      "padLive", "padMute", "padPip", "padRec"};
  static final int GV_CHANNELS = 0, GV_CATEGORIES = 1, GV_GROUPS = 2, GV_SCHEDULE = 3;
  static final int COL_G = 0, COL_C = 1, COL_P = 2, COL_D = 3, COL_DET = 4;
  static final int PAD_NONE = 0, PAD_BUTTONS = 1, PAD_BAR = 2;

  final Profile profile;
  PlayerEngine player;
  Epg epg;
  Rec rec;
  Voice voice;
  Update update;
  MainActivity host;
  final Logo logos;

  int mode = LIVE;
  int menuI, setTab, setPane, setI, setScroll;
  int groupI, chI, guideView, focusCol, guideI, dayI;
  boolean daysOpen, overlayPip;
  final List<Long> guideDays = new ArrayList<>();
  int searchI, histI, recI, mvI, setupI, shareI, askI, pinI, chmI;
  boolean shareFile;
  int padI = 4, padFocus = PAD_NONE;
  String note = "";
  long noteUntil, padUntil, holdAt;
  boolean holdPad, holdSub, holdChan;
  /* касания */
  boolean touching, dragging, longFired, scrubbing, burgerOpen;
  int gesture, downKind, downIdx, pressKind;
  float downX, downY, lastX, lastY, rawX, rawY, scrubRatio;
  final RectF pressRect = new RectF();
  Lst dragList, flingList;
  float flingVel;
  long flingT;
  VelocityTracker vt;
  final RectF padRow = new RectF();
  final RectF padBar = new RectF();
  final RectF padTimer = new RectF();
  final RectF[] padBtn = new RectF[11]; /* 10 кнопок + 10-й индекс: бургер в тач-раскладке */
  String searchQ = "";
  int searchBtn; /* фокус в верхней строке поиска: 0 поле, 1 «Вставить», 2 «Копировать», 3 микрофон */
  String voiceNote = ""; /* строка статуса голоса / скачивания модели в окне поиска */
  boolean voiceDl; /* идёт скачивание модели Vosk */
  String urlText = "", urlTitle = "", urlHint = "";
  int urlKind, urlI, urlBack = SETTINGS;
  boolean kbSuppress; /* не открывать экранную клавиатуру (результат голосового поиска) */
  String pinBuf = "";
  String xtSrv = "", xtUser = "", xtPass = "";
  int askWhy; /* 0 rec 1 backup 2 restore */
  final List<String> groups = new ArrayList<>();
  final List<Integer> inGroup = new ArrayList<>();
  final List<Integer> searchHits = new ArrayList<>();
  final List<Row> rows = new ArrayList<>();
  final List<Programme> guideList = new ArrayList<>();
  int[] mvCh = new int[] {-1, -1, -1, -1};
  int mvN = 1, mvFocus = 0, mvMenu, mvPick;
  boolean pinOk, resumeGuide, favMoving, startRecAfterFolder;
  int waitKey, chmCh = -1, chmBack = LIVE, chmN = 1, pendingSetTab, pendingGroup = -1, plRepI = -1;
  boolean chmOn; /* опции поверх гида, как g_chm на Windows — режим GUIDE не менять */
  int plMenu = -1, epgMenu = -1, histX, browseI, browseWhy, micI;
  long exitAt;
  File browseDir;
  final List<File> browseList = new ArrayList<>();
  final List<String> favBak = new ArrayList<>();
  int lastHatX, lastHatY, exitI;
  long lastBack, lastClick, volUntil, lastGuidePulse;
  int volPct;
  private final ExecutorService netExec = Executors.newSingleThreadExecutor();

  final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
  final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
  final RectF rf = new RectF();

  OverlayView(Context c, Profile p) {
    super(c);
    profile = p;
    logos = new Logo(p);
    logos.setListener(() -> post(this::invalidate));
    logos.hold(6000);
    setClickable(true);
    setFocusable(false);
    setFocusableInTouchMode(false);
    setSoundEffectsEnabled(false);
    rebuildGroups();
    if (profile.pl.isEmpty()) mode = SETUP;
    for (int i = 0; i < padBtn.length; i++) padBtn[i] = new RectF();
  }

  void bind(MainActivity a, PlayerEngine pl, Epg e, Rec r, Voice v, Update u) {
    host = a;
    player = pl;
    epg = e;
    rec = r;
    voice = v;
    update = u;
    if (epg != null) {
      epg.setListener((ok, forced) -> post(() -> onEpgDone(ok, forced)));
      logos.setEpg(epg);
    }
  }

  /** Закачка гида закончилась: перечитать базу в открытый экран гида и показать итог. */
  private void onEpgDone(boolean ok, boolean forced) {
    if (forced || !ok) note(I18n.t(ok ? "epgDone" : "epgFail"), 2200);
    if (mode == GUIDE) {
      if (focusCol == COL_C) syncProgToChannel();
      else fillGuide();
    }
    invalidate();
  }

  int padLift() {
    if (mode != PAD) return 0;
    int h = getHeight();
    if (padRow.top > 0 && h > 0) return Math.max(0, h - (int) padRow.top + 12);
    return (int) dp(88);
  }

  void tick() {
    long now = SystemClock.uptimeMillis();
    if (mode == PAD && padUntil > 0 && now > padUntil && !touching) setMode(LIVE);
    if (mode == EXIT && exitAt > 0 && now > exitAt) { exitAt = 0; setMode(LIVE); }
    if (note.length() > 0 && now > noteUntil) { note = ""; invalidate(); }
    if (volUntil > 0 && now > volUntil) { volUntil = 0; invalidate(); }
    if (mode == GUIDE && guideList.isEmpty() && epg != null && !epg.busy()) {
      if (focusCol == COL_C) syncProgToChannel();
      else fillGuide();
    }
    /* LIVE и GUIDE не гоняем полный кадр каждые 500 мс: на Amlogic это плюс SQLite = мёртвый пульт.
       Гид: полоса «сейчас» раз в 2 с; бегущая строка сама шлёт postInvalidateDelayed. */
    boolean needDraw = (mode != LIVE && mode != GUIDE)
        || note.length() > 0
        || volUntil > now
        || (update != null && update.chips)
        || textMode();
    if (mode == GUIDE && now - lastGuidePulse >= 2000) {
      lastGuidePulse = now;
      needDraw = true;
    }
    if (needDraw) invalidate();
    if (rec != null && player != null) rec.tick(player.index);
    if (epg != null && mode != MULTI) epg.tick();
    if (mode == GUIDE) logos.tick(visibleLogoUrls());
    if (profile.playlistRefreshDue()) {
      netExec.execute(() -> {
        String before = profile.activeUrl();
        profile.refreshIfDue(false);
        post(() -> {
          if (before != null) note(I18n.t("plUpdated"), 1600);
        });
      });
    }
  }

  void note(String s, int ms) {
    note = s == null ? "" : s;
    noteUntil = SystemClock.uptimeMillis() + Math.max(800, ms);
    invalidate();
  }

  void setMode(int m) {
    int old = mode;
    if (m != GUIDE) chmOn = false;
    if (old == GUIDE && m != GUIDE && m != CHMENU) restoreFolderCursor();
    mode = m;
    if (m == PAD) {
      padUntil = SystemClock.uptimeMillis() + 6500;
      padFocus = PAD_NONE;
      padI = 4;
      burgerOpen = false;
    }
    if (m == MENU) menuI = 0;
    if (m == SEARCH) {
      if (mvPick == 0) searchQ = "";
      searchBtn = 0;
      if (old != SEARCH && !voiceDl) voiceNote = "";
      fillSearch();
    }
    if (m == PIN) pinBuf = "";
    if (m == EXIT) exitAt = SystemClock.uptimeMillis() + 4000;
    if (m == HISTORY) { histI = 0; histX = 0; }
    if (m == MICPERM) micI = 0;
    modeChanged(old);
    if (m == SEARCH && old == SEARCH) syncKb();
    if (host != null && (m == PAD || old == PAD)) host.placeClock();
    invalidate();
  }

  /** Сменить режим без сброса состояния (поиск из мультиэкрана и т.п.). */
  private void switchTo(int m) {
    int old = mode;
    if (m != GUIDE && m != CHMENU) chmOn = false;
    mode = m;
    modeChanged(old);
    invalidate();
  }

  int accent() {
    String h = profile.st.highlight;
    if ("orange".equals(h)) return Color.rgb(234, 88, 12);
    if ("teal".equals(h)) return Color.rgb(13, 148, 136);
    return Color.rgb(37, 99, 235);
  }

  static int padBlue() { return Color.rgb(47, 124, 246); }

  int themeRgb() {
    String t = profile.st.theme;
    if ("oled".equals(t)) return Color.rgb(0, 0, 0);
    if ("graphite".equals(t)) return Color.rgb(28, 32, 38);
    return Color.rgb(18, 22, 32);
  }

  int panelBg() {
    int rgb = themeRgb();
    return Color.argb(panelA(), Color.red(rgb), Color.green(rgb), Color.blue(rgb));
  }

  int panelBg(int a) {
    int rgb = themeRgb();
    return Color.argb(a, Color.red(rgb), Color.green(rgb), Color.blue(rgb));
  }

  int panelA() { return Math.round(255 * profile.st.panelOpacity / 100f); }

  float font() {
    if ("sm".equals(profile.st.fontSize)) return ts(15);
    if ("lg".equals(profile.st.fontSize)) return ts(20);
    return ts(17);
  }

  float dp(float v) { return v * getResources().getDisplayMetrics().density; }

  // ===================================================================================
  //  Касания: области попадания, списки с прокруткой
  // ===================================================================================
  static final int H_PANEL = 1, H_CLOSE = 2, H_MENU = 3, H_TAB = 4, H_ROW = 5, H_SET_BACK = 6, H_SET_BACK2 = 7,
      H_GGRP = 8, H_GCH = 9, H_GSCHED = 10, H_GPROG = 11, H_GDAY = 12, H_LIST = 13, H_REC = 14, H_MVOPT = 15,
      H_MVBAR = 16, H_PIN = 17, H_EXIT = 18, H_SETUP = 19, H_SHARE = 20, H_ASK = 21, H_CHM = 22, H_PAD = 23,
      H_BAR = 24, H_BURGER = 25, H_BURGER_ITEM = 26, H_CHIP_UP = 27, H_CHIP_LATER = 28, H_PASTE = 29,
      H_COPY = 30, H_FIELD = 31, H_URL_OK = 32, H_GDET = 33, H_CRUMB_BACK = 34, H_CRUMB = 35,
      H_MIC = 36;
  /** Пункты списка «бургер»: коды кнопок pad; 100 — субтитры. */
  static final int[] BURGER = {0, 1, 2, 6, 7, 100, 8, 9};
  static final int BURGER_AUDIO = 7, BURGER_SUB = 100;

  static final class Hit {
    float l, t, r, b;
    int kind, idx;
    boolean has(float x, float y) { return x >= l && x <= r && y >= t && y <= b; }
  }

  static final class Lst {
    float off, rowH = 1, viewH = 1;
    int total, lastSel = -2;
    long touchedAt;
    void reset() { off = 0; lastSel = -2; }
    float max() { return Math.max(0f, total - viewH / rowH); }
    void clamp() {
      float m = max();
      if (off > m) off = m;
      if (off < 0) off = 0;
    }
  }

  static final class SReg {
    float l, t, r, b;
    Lst lst;
    boolean has(float x, float y) { return x >= l && x <= r && y >= t && y <= b; }
  }

  final ArrayList<Hit> hitPool = new ArrayList<>();
  int nHits;
  final ArrayList<SReg> sregPool = new ArrayList<>();
  int nSregs;
  final Lst lMenu = new Lst(), lTabs = new Lst(), lRows = new Lst(), lGroups = new Lst(), lChan = new Lst(),
      lProg = new Lst(), lDays = new Lst(), lSearch = new Lst(), lHist = new Lst(), lRecs = new Lst(),
      lBurger = new Lst();
  final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
  final Paint bmpPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
  final TextPaint subPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
  final RectF padBarHit = new RectF();
  final Path tmpPath = new Path();

  /** Логический размер после отступов (безопасная зона / вырез). */
  int lw, lh;
  int padL, padT, padR, padB;
  int imeH;

  private void hit(float l, float t, float r, float b, int kind, int idx) {
    Hit hh;
    if (nHits < hitPool.size()) hh = hitPool.get(nHits);
    else { hh = new Hit(); hitPool.add(hh); }
    nHits++;
    hh.l = l; hh.t = t; hh.r = r; hh.b = b; hh.kind = kind; hh.idx = idx;
  }

  private void hit(RectF r, int kind, int idx) { hit(r.left, r.top, r.right, r.bottom, kind, idx); }

  private Hit hitAt(float x, float y) {
    for (int i = nHits - 1; i >= 0; i--) {
      Hit hh = hitPool.get(i);
      if (hh.has(x, y)) return hh;
    }
    return null;
  }

  private void sreg(float l, float t, float r, float b, Lst lst) {
    SReg s;
    if (nSregs < sregPool.size()) s = sregPool.get(nSregs);
    else { s = new SReg(); sregPool.add(s); }
    nSregs++;
    s.l = l; s.t = t; s.r = r; s.b = b; s.lst = lst;
  }

  private Lst scrollAt(float x, float y) {
    for (int i = nSregs - 1; i >= 0; i--) {
      SReg s = sregPool.get(i);
      if (s.has(x, y) && s.lst.max() > 0) return s.lst;
    }
    return null;
  }

  private boolean fv() { return !Ui.touch(); }

  float sp(float v) {
    return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, getResources().getDisplayMetrics());
  }

  /** Размер текста: на приставке не мельче 16–18 sp, на телефоне как задано в dp. */
  float ts(float v) {
    float mul = "sm".equals(profile.st.fontSize) ? 0.9f : "lg".equals(profile.st.fontSize) ? 1.15f : 1f;
    float s = dp(v) * mul;
    if (Ui.touch()) return s;
    return Math.max(s * 1.1f, sp(v < 14 ? 16 : 18) * mul);
  }

  /** Окно оверлея перерисовывается при смене вырезов, жестовых зон и системных полос. */
  @Override
  public WindowInsets onApplyWindowInsets(WindowInsets insets) {
    invalidate();
    return super.onApplyWindowInsets(insets);
  }

  /**
   * Безопасная область: 5% на приставке (overscan), вырез камеры (DisplayCutout), видимые системные полосы
   * и жестовые зоны у краёв (system gesture insets, не шире 32 dp). Фон панелей продлевается под эту область
   * (см. {@link #bg}), а текст и кнопки остаются внутри неё.
   */
  private void computeInsets(int W, int H) {
    int l = 0, t = 0, r = 0, b = 0;
    if (Ui.overscan()) {
      if (mode == GUIDE) {
        /* гид: колонка и описание ~15 px от края экрана, не 5% overscan */
        l = r = 15;
        t = b = Math.round(H * 0.05f);
      } else {
        l = r = Math.round(W * 0.05f);
        t = b = Math.round(H * 0.05f);
      }
    }
    if (Build.VERSION.SDK_INT >= 28) {
      try {
        WindowInsets wi = getRootWindowInsets();
        if (wi != null) {
          DisplayCutout dc = wi.getDisplayCutout();
          if (dc != null) {
            l = Math.max(l, dc.getSafeInsetLeft());
            t = Math.max(t, dc.getSafeInsetTop());
            r = Math.max(r, dc.getSafeInsetRight());
            b = Math.max(b, dc.getSafeInsetBottom());
          }
          if (!Ui.overscan()) {
            int cap = Math.round(dp(32));
            int[] sys = new int[4], gest = new int[4];
            if (Build.VERSION.SDK_INT >= 30) {
              Insets bars = wi.getInsets(WindowInsets.Type.systemBars());
              Insets g = wi.getInsets(WindowInsets.Type.systemGestures());
              sys[0] = bars.left; sys[1] = bars.top; sys[2] = bars.right; sys[3] = bars.bottom;
              gest[0] = g.left; gest[1] = g.top; gest[2] = g.right; gest[3] = g.bottom;
            } else {
              sys[0] = wi.getSystemWindowInsetLeft(); sys[1] = wi.getSystemWindowInsetTop();
              sys[2] = wi.getSystemWindowInsetRight(); sys[3] = wi.getSystemWindowInsetBottom();
              if (Build.VERSION.SDK_INT >= 29) {
                Insets g = wi.getSystemGestureInsets();
                gest[0] = g.left; gest[1] = g.top; gest[2] = g.right; gest[3] = g.bottom;
              }
            }
            /* верхняя жестовая зона (шторка) в полноэкранном плеере не нужна: только полосы */
            l = Math.max(l, Math.max(sys[0], Math.min(gest[0], cap)));
            t = Math.max(t, sys[1]);
            r = Math.max(r, Math.max(sys[2], Math.min(gest[2], cap)));
            b = Math.max(b, Math.max(sys[3], Math.min(gest[3], cap)));
          }
        }
      } catch (Throwable ignored) {}
    }
    padL = l; padT = t; padR = r; padB = b;
    lw = Math.max(8, W - l - r);
    lh = Math.max(8, H - t - b);
  }

  /** Фон панели: прижатые к краю экрана панели продлеваются под безопасную зону. */
  private void bg(Canvas c, float l, float t, float r, float b) {
    /* гид: колонки и описание не растягивать под край — отступ 15 px должен быть виден */
    if (mode != GUIDE) {
      if (l <= 0) l -= padL;
      if (r >= lw - 0.5f) r += padR;
    }
    if (t <= 0) t -= padT;
    if (b >= lh - 0.5f) b += padB;
    c.drawRect(l, t, r, b, fill);
  }

  private void rr(Canvas c, float l, float t, float r, float b, float rad) {
    rf.set(l, t, r, b);
    c.drawRoundRect(rf, rad, rad, fill);
  }

  /** Старт списка с прокруткой: клип, регистрация зоны, автопрокрутка к выбранной строке. */
  private void listBegin(Canvas c, Lst L, float l, float t, float r, float b, float rowH, int total, int sel) {
    listBegin(c, L, l, t, r, b, rowH, total, sel, false);
  }

  /** center: как Windows — выбранная строка по середине колонки (пульт; на касании только при смене sel). */
  private void listBegin(Canvas c, Lst L, float l, float t, float r, float b, float rowH, int total, int sel,
      boolean center) {
    L.rowH = rowH;
    L.viewH = Math.max(1f, b - t);
    L.total = total;
    float vis = L.viewH / rowH;
    if (center) {
      if (sel >= 0 && (!Ui.touch() || sel != L.lastSel)) L.off = sel - vis / 2f;
      L.lastSel = sel;
    } else if (sel != L.lastSel) {
      if (sel >= 0) {
        if (L.lastSel < 0) L.off = sel - (vis - 1f) / 2f;
        else if (sel < L.off) L.off = sel;
        else if (sel + 1 > L.off + vis) L.off = sel + 1 - vis;
      }
      L.lastSel = sel;
    }
    L.clamp();
    sreg(l, t, r, b, L);
    c.save();
    c.clipRect(l, t, r, b);
  }

  private void listEnd(Canvas c, Lst L, float r, float t, float b) {
    c.restore();
    if (!Ui.touch()) return;
    float vis = L.viewH / L.rowH;
    if (L.total <= vis + 0.01f) return;
    long age = SystemClock.uptimeMillis() - L.touchedAt;
    int a = age < 800 ? 150 : age < 1200 ? (int) (150 * (1200 - age) / 400f) : 0;
    if (a <= 0) return;
    float barH = Math.max(dp(28), L.viewH * vis / L.total);
    float m = L.max();
    float y = t + (L.viewH - barH) * (m <= 0 ? 0 : L.off / m);
    fill.setColor(Color.argb(a, 255, 255, 255));
    rr(c, r - dp(6), y, r - dp(3), y + barH, dp(2));
    postInvalidateDelayed(200);
  }

  private int firstRow(Lst L) { return Math.max(0, (int) Math.floor(L.off)); }

  // ===================================================================================
  //  Отрисовка
  // ===================================================================================
  @Override
  protected void onDraw(Canvas c) {
    int W = getWidth(), H = getHeight();
    if (W < 8 || H < 8) return;
    nHits = 0;
    nSregs = 0;
    marqAny = false;
    for (int i = 0; i < marqUsed.length; i++) marqUsed[i] = false;
    computeInsets(W, H);
    int w = lw, h = lh;
    c.save();
    c.translate(padL, padT);
    boolean air = mode == LIVE || mode == PAD || mode == GUIDE;
    if (mode == PAD) layoutPad(w, h);
    if (profile.st.clockEnabled && (host == null || !host.clockOnTop()) && air) drawClock(c, w, h);
    if (air) drawSubs(c, w, h);
    if (update != null && update.chips && (mode == LIVE || mode == PAD)) drawChips(c, w, h);
    switch (mode) {
      case PAD: drawPad(c, w, h); break;
      case MENU: drawMenu(c, w, h); break;
      case SETTINGS: drawSettings(c, w, h); break;
      case GUIDE:
        drawGuide(c, w, h);
        if (chmOn) drawChMenu(c, w, h);
        break;
      case EXIT: drawExit(c, w, h); break;
      case SEARCH: drawSearch(c, w, h); break;
      case HISTORY: drawHistory(c, w, h); break;
      case RECS: drawRecs(c, w, h); break;
      case MULTI: drawMulti(c, w, h); break;
      case PIN: drawPin(c, w, h); break;
      case SETUP: drawSetup(c, w, h); break;
      case SHARE: drawShare(c, w, h); break;
      case ASK: drawAsk(c, w, h); break;
      case CHMENU: drawChMenu(c, w, h); break;
      case URL: drawUrl(c, w, h); break;
      case FILEBROWSE: drawBrowse(c, w, h); break;
      case MICPERM: drawMicPerm(c, w, h); break;
      default: break;
    }
    if (note.length() > 0) drawNote(c, w, h);
    if (volUntil > SystemClock.uptimeMillis()) drawVol(c, w, h);
    drawPressed(c);
    c.restore();
    for (int i = 0; i < marqUsed.length; i++) if (!marqUsed[i]) marqKey[i] = null;
    /* кадр бегущей строки: перерисовываем только её полосу, ~25 кадров/с */
    if (marqAny && mode == GUIDE) {
      postInvalidateDelayed(40, (int) (padL + marqRect.left) - 1, (int) (padT + marqRect.top) - 1,
          (int) (padL + marqRect.right) + 2, (int) (padT + marqRect.bottom) + 2);
    }
  }

  private void drawPressed(Canvas c) {
    if (!touching || dragging || pressKind <= 0 || pressKind == H_PANEL || pressKind == H_BAR || pressKind == H_GDET) return;
    fill.setColor(Color.argb(46, 255, 255, 255));
    float rad = pressKind == H_PAD || pressKind == H_BURGER ? Math.min(pressRect.width(), pressRect.height()) / 2f : dp(8);
    c.drawRoundRect(pressRect, rad, rad, fill);
  }

  private void drawClock(Canvas c, int w, int h) {
    Calendar cal = Calendar.getInstance();
    String t = String.format(Locale.US, "%02d:%02d",
        cal.get(Calendar.HOUR_OF_DAY), cal.get(Calendar.MINUTE));
    text.setTextSize("lg".equals(profile.st.clockSize) ? dp(28) : "sm".equals(profile.st.clockSize) ? dp(16) : dp(22));
    text.setColor(Color.argb(Math.round(255 * profile.st.clockOpacity / 100f), 255, 255, 255));
    float tw = text.measureText(t);
    float gap = 30;
    float nudge = 15; /* правый верхний угол: на 15 px выше и правее */
    float x = w - tw - (gap - nudge), y = (gap - nudge) + text.getTextSize();
    if ("top-left".equals(profile.st.clockPos)) { x = gap; y = gap + text.getTextSize(); }
    else if ("bottom-left".equals(profile.st.clockPos)) { x = gap; y = h - gap; }
    else if ("bottom-right".equals(profile.st.clockPos)) { x = w - tw - gap; y = h - gap; }
    c.drawText(t, x, y, text);
  }

  private void drawChips(Canvas c, int w, int h) {
    text.setTextSize(ts(15));
    String a = I18n.t("update"), b = I18n.t("updateLater");
    float aw = text.measureText(a) + dp(28), bw = text.measureText(b) + dp(28);
    float ch = Math.max(dp(44), ts(15) + dp(22));
    float y = h - dp(56) - (ch - dp(40));
    if (mode == PAD && padRow.top > dp(60)) y = padRow.top - ch - dp(10);
    rf.set(dp(16), y, dp(16) + aw, y + ch);
    fill.setColor(accent());
    c.drawRoundRect(rf, dp(12), dp(12), fill);
    hit(rf, H_CHIP_UP, 0);
    text.setColor(Color.WHITE);
    c.drawText(a, rf.left + dp(14), rf.centerY() + ts(15) * 0.35f, text);
    rf.set(dp(16) + aw + dp(8), y, dp(16) + aw + dp(8) + bw, y + ch);
    fill.setColor(Color.argb(230, 26, 34, 48));
    c.drawRoundRect(rf, dp(12), dp(12), fill);
    hit(rf, H_CHIP_LATER, 0);
    text.setColor(Color.WHITE);
    c.drawText(b, rf.left + dp(14), rf.centerY() + ts(15) * 0.35f, text);
  }

  private void drawNote(Canvas c, int w, int h) {
    float sz = ts(16);
    text.setTextSize(sz);
    String s = note;
    float maxW = w - dp(32);
    float tw = text.measureText(s) + dp(32);
    if (tw > maxW) {
      s = TextUtils.ellipsize(s, text, maxW - dp(32), TextUtils.TruncateAt.END).toString();
      tw = text.measureText(s) + dp(32);
    }
    float bh = sz + dp(24);
    float top;
    if (Ui.touch()) top = dp(24);
    else if (mode == PAD && padRow.top > 0) top = Math.max(dp(8), padRow.top - bh - dp(10));
    else top = h - dp(110);
    rf.set((w - tw) / 2, top, (w + tw) / 2, top + bh);
    fill.setColor(Color.argb(220, 20, 26, 36));
    c.drawRoundRect(rf, dp(12), dp(12), fill);
    text.setColor(Color.WHITE);
    c.drawText(s, rf.left + dp(16), rf.centerY() + sz * 0.35f, text);
  }

  // ---------- субтитры ----------
  private void drawSubs(Canvas c, int w, int h) {
    if (player == null) return;
    List<Cue> cs = player.cues;
    if (cs == null || cs.isEmpty()) return;
    float bottom = h - dp(28);
    if (mode == PAD && padRow.top > 0) bottom = Math.min(bottom, padRow.top - dp(10));
    StringBuilder sb = new StringBuilder();
    for (Cue q : cs) {
      if (q.bitmap != null) { drawCueBitmap(c, q, w, h, bottom); continue; }
      if (q.text != null && q.text.length() > 0) {
        if (sb.length() > 0) sb.append('\n');
        sb.append(q.text);
      }
    }
    if (sb.length() == 0) return;
    subPaint.setTextSize(ts(Ui.touch() ? 17 : 20));
    subPaint.setColor(Color.WHITE);
    int maxW = (int) Math.max(dp(120), w * 0.9f);
    StaticLayout sl = StaticLayout.Builder.obtain(sb, 0, sb.length(), subPaint, maxW)
        .setAlignment(Layout.Alignment.ALIGN_CENTER)
        .build();
    float lineMax = 0;
    for (int i = 0; i < sl.getLineCount(); i++) lineMax = Math.max(lineMax, sl.getLineWidth(i));
    float x = (w - maxW) / 2f, y = bottom - sl.getHeight();
    fill.setColor(Color.argb(150, 0, 0, 0));
    rr(c, w / 2f - lineMax / 2f - dp(8), y - dp(4), w / 2f + lineMax / 2f + dp(8), y + sl.getHeight() + dp(4), dp(6));
    c.save();
    c.translate(x, y);
    sl.draw(c);
    c.restore();
  }

  private void drawCueBitmap(Canvas c, Cue q, int w, int h, float bottom) {
    Bitmap bm = q.bitmap;
    if (bm == null || bm.isRecycled() || bm.getWidth() <= 0) return;
    float dw = q.size != Cue.DIMEN_UNSET && q.size > 0 ? q.size * w : Math.min(w * 0.8f, bm.getWidth() * (w / 720f));
    float dh = dw * bm.getHeight() / bm.getWidth();
    float x = (w - dw) / 2f, y = bottom - dh;
    if (q.position != Cue.DIMEN_UNSET) {
      x = q.position * w;
      if (q.positionAnchor == Cue.ANCHOR_TYPE_MIDDLE) x -= dw / 2f;
      else if (q.positionAnchor == Cue.ANCHOR_TYPE_END) x -= dw;
    }
    if (q.lineType == Cue.LINE_TYPE_FRACTION && q.line != Cue.DIMEN_UNSET) {
      y = q.line * h;
      if (q.lineAnchor == Cue.ANCHOR_TYPE_END) y -= dh;
    }
    x = Math.max(0, Math.min(w - dw, x));
    y = Math.max(0, Math.min(h - dh, y));
    rf.set(x, y, x + dw, y + dh);
    c.drawBitmap(bm, null, rf, bmpPaint);
  }

  // ---------- общие элементы ----------
  private void drawClose(Canvas c, float right, float top) {
    if (!Ui.touch()) return;
    float s = dp(48);
    float l = right - s - dp(4), t = top + dp(2);
    float cx = l + s / 2, cy = t + s / 2, k = dp(8);
    stroke.setStyle(Paint.Style.STROKE);
    stroke.setStrokeWidth(dp(2.2f));
    stroke.setStrokeCap(Paint.Cap.ROUND);
    stroke.setColor(Color.argb(220, 255, 255, 255));
    c.drawLine(cx - k, cy - k, cx + k, cy + k, stroke);
    c.drawLine(cx - k, cy + k, cx + k, cy - k, stroke);
    hit(l, t, l + s, t + s, H_CLOSE, 0);
  }

  private void drawChip(Canvas c, RectF b, String lab, boolean focus, boolean primary) {
    fill.setColor(primary ? accent() : Color.rgb(44, 52, 68));
    c.drawRoundRect(b, dp(10), dp(10), fill);
    if (focus) {
      stroke.setStyle(Paint.Style.STROKE);
      stroke.setStrokeWidth(dp(2.5f));
      stroke.setColor(Color.WHITE);
      c.drawRoundRect(b, dp(10), dp(10), stroke);
    }
    drawFit(c, lab, b.centerX(), b.centerY(), b.width() - dp(10), ts(15), Color.WHITE);
  }

  private void drawFit(Canvas c, String s, float cx, float cy, float maxW, float size, int color) {
    if (s == null) return;
    text.setTextSize(size);
    float tw = text.measureText(s);
    float min = size * 0.85f;
    while (tw > maxW && size > min) {
      size -= dp(0.5f);
      text.setTextSize(size);
      tw = text.measureText(s);
    }
    text.setColor(color);
    if (tw > maxW && s.indexOf(' ') > 0) {
      int sp = s.indexOf(' ', s.length() / 2 - 2);
      if (sp < 0) sp = s.indexOf(' ');
      String a = s.substring(0, sp), b2 = s.substring(sp + 1);
      float sz2 = size * 0.9f;
      text.setTextSize(sz2);
      String la = TextUtils.ellipsize(a, text, maxW, TextUtils.TruncateAt.END).toString();
      String lb = TextUtils.ellipsize(b2, text, maxW, TextUtils.TruncateAt.END).toString();
      c.drawText(la, cx - text.measureText(la) / 2, cy - sz2 * 0.12f, text);
      c.drawText(lb, cx - text.measureText(lb) / 2, cy + sz2 * 0.95f, text);
      return;
    }
    if (tw > maxW) {
      s = TextUtils.ellipsize(s, text, maxW, TextUtils.TruncateAt.END).toString();
      tw = text.measureText(s);
    }
    c.drawText(s, cx - tw / 2, cy + size * 0.35f, text);
  }

  private final String[] marqKey = new String[3];
  private final long[] marqT0 = new long[3];
  private final boolean[] marqUsed = new boolean[3];
  private final RectF marqRect = new RectF();
  private boolean marqAny;

  /**
   * Одна строка в ширину maxW. Не влезает и scroll = true — плавная бегущая строка (две копии текста со
   * швом dp(40), скорость dp(40)/с, пауза 0,9 с на старте, клип по прямоугольнику строки), как argb_marquee
   * в Windows. Иначе — обычное «…» в конце. Кисть text (размер, цвет) настраивает вызывающий.
   * slot: 0 имя канала, 1 передача под именем, 2 строка расписания.
   */
  private void drawMarquee(Canvas c, String s, float x, float base, float maxW, boolean scroll, int slot) {
    if (s == null || s.isEmpty() || maxW < 4) return;
    float tw = text.measureText(s);
    if (tw <= maxW) {
      c.drawText(s, x, base, text);
      return;
    }
    if (!scroll) {
      c.drawText(TextUtils.ellipsize(s, text, maxW, TextUtils.TruncateAt.END).toString(), x, base, text);
      return;
    }
    long now = SystemClock.uptimeMillis();
    if (!s.equals(marqKey[slot])) {
      marqKey[slot] = s;
      marqT0[slot] = now;
    }
    marqUsed[slot] = true;
    float gap = dp(40), span = tw + gap;
    long t = now - marqT0[slot] - 900;
    float shift = t <= 0 ? 0 : (t * dp(40) / 1000f) % span;
    float top = base + text.ascent() - dp(1), bot = base + text.descent() + dp(1);
    c.save();
    c.clipRect(x, top, x + maxW, bot);
    c.drawText(s, x - shift, base, text);
    if (shift > 0) c.drawText(s, x - shift + span, base, text);
    c.restore();
    if (!marqAny) marqRect.set(x, top, x + maxW, bot);
    else marqRect.union(x, top, x + maxW, bot);
    marqAny = true;
  }

  private boolean caretOn() { return (SystemClock.uptimeMillis() / 500) % 2 == 0; }

  /** Однострочное поле: показывается хвост текста, чтобы был виден курсор. */
  private void drawField(Canvas c, RectF r, String txt, String hint, boolean focus, boolean editing) {
    fill.setColor(Color.rgb(26, 32, 44));
    c.drawRoundRect(r, dp(10), dp(10), fill);
    if (focus || editing) {
      stroke.setStyle(Paint.Style.STROKE);
      stroke.setStrokeWidth(dp(2));
      stroke.setColor(focus ? Color.WHITE : accent());
      c.drawRoundRect(r, dp(10), dp(10), stroke);
    }
    float sz = ts(17);
    text.setTextSize(sz);
    float avail = r.width() - dp(28);
    float base = r.centerY() + sz * 0.35f;
    if (txt == null || txt.isEmpty()) {
      text.setColor(Color.rgb(130, 140, 155));
      String s = TextUtils.ellipsize(hint, text, avail, TextUtils.TruncateAt.END).toString();
      c.drawText(s, r.left + dp(14), base, text);
      if (editing && caretOn()) {
        fill.setColor(Color.WHITE);
        c.drawRect(r.left + dp(14), r.centerY() - sz * 0.6f, r.left + dp(15.5f), r.centerY() + sz * 0.6f, fill);
      }
      return;
    }
    String s = txt;
    while (s.length() > 1 && text.measureText(s) > avail) s = s.substring(1);
    text.setColor(Color.WHITE);
    c.drawText(s, r.left + dp(14), base, text);
    if (editing && caretOn()) {
      float cx = r.left + dp(14) + text.measureText(s) + dp(1);
      fill.setColor(Color.WHITE);
      c.drawRect(cx, r.centerY() - sz * 0.6f, cx + dp(1.5f), r.centerY() + sz * 0.6f, fill);
    }
  }

  /** Многострочное поле для ссылок. */
  private void drawFieldMulti(Canvas c, RectF r, String txt, boolean mask, boolean focus, boolean editing, String hint) {
    fill.setColor(Color.rgb(26, 32, 44));
    c.drawRoundRect(r, dp(10), dp(10), fill);
    if (focus || editing) {
      stroke.setStyle(Paint.Style.STROKE);
      stroke.setStrokeWidth(dp(2));
      stroke.setColor(focus ? Color.WHITE : accent());
      c.drawRoundRect(r, dp(10), dp(10), stroke);
    }
    float sz = ts(16);
    TextPaint tp = text;
    tp.setTextSize(sz);
    int iw = (int) Math.max(dp(40), r.width() - dp(28));
    if (txt == null || txt.isEmpty()) {
      tp.setColor(Color.rgb(130, 140, 155));
      String s = TextUtils.ellipsize(hint, tp, iw, TextUtils.TruncateAt.END).toString();
      c.drawText(s, r.left + dp(14), r.top + dp(14) + sz, tp);
      if (editing && caretOn()) {
        fill.setColor(Color.WHITE);
        c.drawRect(r.left + dp(14), r.top + dp(14), r.left + dp(15.5f), r.top + dp(14) + sz * 1.2f, fill);
      }
      return;
    }
    String shown = mask ? txt.replaceAll("(?s).", "•") : txt;
    StaticLayout sl = StaticLayout.Builder.obtain(shown, 0, shown.length(), tp, iw)
        .setAlignment(Layout.Alignment.ALIGN_NORMAL).build();
    tp.setColor(Color.WHITE);
    float viewH = r.height() - dp(24);
    float shift = Math.max(0, sl.getHeight() - viewH);
    c.save();
    c.clipRect(r.left + dp(8), r.top + dp(8), r.right - dp(8), r.bottom - dp(8));
    c.translate(r.left + dp(14), r.top + dp(12) - shift);
    sl.draw(c);
    if (editing && caretOn()) {
      int last = Math.max(0, sl.getLineCount() - 1);
      float cx = sl.getLineRight(last) + dp(1);
      fill.setColor(Color.WHITE);
      c.drawRect(cx, sl.getLineTop(last), cx + dp(1.5f), sl.getLineBottom(last), fill);
    }
    c.restore();
  }

  // ---------- нижняя панель ----------
  private void layoutPad(int w, int h) {
    if (Ui.touch()) layoutPadTouch(w, h);
    else layoutPadRemote(w, h);
  }

  /** Приставка: десять кнопок в ряд (как в Windows), при нехватке ширины зазор и кнопки сжимаются. */
  private void layoutPadRemote(int w, int h) {
    if (wc(w) == Ui.W_NARROW) { layoutPadRemoteTwoRows(w, h); return; }
    float sc = Math.max(1f, Math.min(1.5f, w / dp(900)));
    float playW = dp(60) * sc, btnW = dp(64) * sc, btnH = dp(52) * sc, gap = dp(6), inset = dp(8), rowH = dp(84) * sc;
    float availW = w - dp(16);
    float need = inset * 2 + playW + btnW * 9 + gap * 9;
    if (need > availW) gap = dp(3);
    need = inset * 2 + playW + btnW * 9 + gap * 9;
    if (need > availW) {
      float k = (availW - inset * 2 - gap * 9) / (playW + btnW * 9);
      playW = Math.max(dp(20), playW * k);
      btnW = Math.max(dp(16), btnW * k);
    }
    float total = inset * 2 + playW + btnW * 9 + gap * 9;
    float x0 = (w - total) / 2f;
    float barC = h - dp(34);
    padBar.set(dp(24), barC - dp(2), w - dp(24), barC + dp(2));
    padBarHit.set(0, barC - dp(18), w, barC + dp(18));
    padTimer.set(dp(24), barC - dp(30), w - dp(24), barC - dp(10));
    float top = padTimer.top - dp(8) - rowH;
    padRow.set(x0, top, x0 + total, top + rowH);
    float x = x0 + inset;
    for (int i = 0; i < 10; i++) {
      float bw = i == 4 ? playW : btnW;
      float bh = i == 4 ? Math.min(playW, rowH - dp(8)) : btnH;
      float y = top + (rowH - bh) / 2f;
      padBtn[i].set(x, y, x + bw, y + bh);
      x += bw + gap;
    }
  }

  /**
   * Узкий экран (&lt; 600 dp, портрет телефона) с клавиатурой или геймпадом: десять кнопок в две строки по пять
   * (первая — меню…пауза, вторая — вперёд…запись), полоса прогресса под ними.
   */
  /** Кнопки pad в две строки: фокусный вид на узком экране. */
  private boolean padTwoRows() { return !Ui.touch() && wc(lw) == Ui.W_NARROW; }

  private void layoutPadRemoteTwoRows(int w, int h) {
    float gap = dp(6), inset = dp(8);
    float bw = Math.min(dp(64), (w - dp(16) - inset * 2 - gap * 4) / 5f);
    float bh = Math.min(dp(52), bw);
    float lineH = bh + dp(12);
    float total = inset * 2 + bw * 5 + gap * 4;
    float x0 = (w - total) / 2f;
    float barC = h - dp(34);
    padBar.set(dp(24), barC - dp(2), w - dp(24), barC + dp(2));
    padBarHit.set(0, barC - dp(18), w, barC + dp(18));
    padTimer.set(dp(24), barC - dp(30), w - dp(24), barC - dp(10));
    float top = padTimer.top - dp(8) - lineH * 2;
    padRow.set(x0, top, x0 + total, top + lineH * 2);
    for (int i = 0; i < 10; i++) {
      int r = i / 5, col = i % 5;
      float x = x0 + inset + col * (bw + gap);
      float y = top + r * lineH + (lineH - bh) / 2f;
      padBtn[i].set(x, y, x + bw, y + bh);
    }
  }

  /** Телефон и планшет: три крупные кнопки по центру, полоса прогресса снизу, бургер над ней справа. */
  private void layoutPadTouch(int w, int h) {
    boolean small = h < dp(340);
    float k = Math.min(1f, w / dp(320));
    float playD = (small ? dp(60) : dp(76)) * k, sideD = (small ? dp(48) : dp(60)) * k, gap = (small ? dp(24) : dp(36)) * k;
    float cx = w / 2f, cy = h / 2f - dp(8);
    padBtn[4].set(cx - playD / 2, cy - playD / 2, cx + playD / 2, cy + playD / 2);
    padBtn[3].set(cx - playD / 2 - gap - sideD, cy - sideD / 2, cx - playD / 2 - gap, cy + sideD / 2);
    padBtn[5].set(cx + playD / 2 + gap, cy - sideD / 2, cx + playD / 2 + gap + sideD, cy + sideD / 2);
    float side = dp(20);
    float barC = h - dp(30);
    padBar.set(side, barC - dp(2), w - side, barC + dp(2));
    padBarHit.set(0, barC - dp(28), w, barC + dp(28) + padB);
    padTimer.set(side, barC - dp(32), w - side - dp(64), barC - dp(10));
    float bs = dp(48);
    padBtn[10].set(w - side - bs + dp(4), padBar.top - dp(14) - bs, w - side + dp(4), padBar.top - dp(14));
    padRow.set(0, padBtn[10].top - dp(8), w, h);
  }

  private void drawPad(Canvas c, int w, int h) {
    layoutPad(w, h);
    long age = SystemClock.uptimeMillis() - Ui.changedAt;
    boolean fade = age < 220;
    int layer = 0;
    if (fade) {
      layer = c.saveLayerAlpha(0, 0, w, h, (int) (255 * Math.max(0.05f, age / 220f)));
      invalidate();
    }
    if (Ui.touch()) drawPadTouch(c, w, h);
    else drawPadRemote(c, w, h);
    if (fade) c.restoreToCount(layer);
  }

  private void padBarData(boolean[] hasOut, double[] ratioOut, long[] bnds) {
    boolean has = padBounds(bnds);
    double ratio = has && bnds[1] > 0 ? Math.max(0, Math.min(1, (double) bnds[0] / (double) bnds[1])) : 0;
    if (scrubbing && has) ratio = Math.max(0, Math.min(1, scrubRatio));
    hasOut[0] = has;
    ratioOut[0] = ratio;
  }

  private void drawPadClock(Canvas c, boolean has, double ratio, long[] bnds, boolean left) {
    if (!has) return;
    long pos = scrubbing ? (long) (ratio * bnds[1]) : bnds[0];
    String clock = formatHms(pos) + "/" + formatHms(bnds[1]);
    text.setTextSize(ts(12));
    text.setColor(Color.argb(240, 235, 235, 235));
    float tw = text.measureText(clock);
    float x = left ? padTimer.left : padTimer.right - tw;
    c.drawText(clock, x, padTimer.centerY() + ts(12) * 0.35f, text);
  }

  private void drawPadBar(Canvas c, boolean has, double ratio, boolean knobAlways) {
    if (padFocus == PAD_BAR && !Ui.touch()) {
      fill.setColor(Color.argb(70, 255, 255, 255));
      rr(c, padBar.left - 2, padBar.top - 2, padBar.right + 2, padBar.bottom + 2, dp(6));
    }
    fill.setColor(Color.argb(Ui.touch() ? 80 : 38, 255, 255, 255));
    c.drawRoundRect(padBar, dp(6), dp(6), fill);
    if (has && ratio > 0) {
      fill.setColor(Color.rgb(47, 124, 246));
      rr(c, padBar.left, padBar.top, padBar.left + (float) (padBar.width() * ratio), padBar.bottom, dp(6));
    }
    if ((padFocus == PAD_BAR && !Ui.touch()) || (knobAlways && has)) {
      float cx = padBar.left + (float) (padBar.width() * ratio);
      float cy = padBar.centerY();
      float r = scrubbing ? dp(11) : dp(8);
      fill.setColor(Color.WHITE);
      c.drawCircle(cx, cy, r, fill);
      fill.setColor(Color.rgb(47, 124, 246));
      c.drawCircle(cx, cy, r - dp(2), fill);
    }
  }

  private void drawPadRemote(Canvas c, int w, int h) {
    int rowA = padFocus == PAD_BUTTONS ? 178 : 140;
    fill.setColor(Color.argb(rowA, 0, 0, 0));
    c.drawRoundRect(padRow, dp(16), dp(16), fill);
    hit(padRow, H_PANEL, 0);
    if (padFocus == PAD_BUTTONS) {
      fill.setColor(Color.argb(90, 47, 124, 246));
      c.drawRoundRect(new RectF(padRow.left + 1, padRow.top + 1, padRow.right - 1, padRow.bottom - 1), dp(16), dp(16), fill);
      fill.setColor(Color.argb(rowA, 0, 0, 0));
      c.drawRoundRect(new RectF(padRow.left + 3, padRow.top + 3, padRow.right - 3, padRow.bottom - 3), dp(14), dp(14), fill);
    }
    boolean archive = player != null && player.isArchive();
    for (int i = 0; i < 10; i++) {
      RectF b = padBtn[i];
      boolean hot = padFocus == PAD_BUTTONS && i == padI;
      boolean dim = i == 5 && !archive;
      hit(b, H_PAD, i);
      if (i == 4) {
        drawPadPlay(c, b, hot);
        continue;
      }
      if (hot && !dim) fill.setColor(padBlue());
      else if (dim) fill.setColor(Color.rgb(70, 74, 82));
      else fill.setColor(Color.rgb(96, 102, 112));
      c.drawRoundRect(b, dp(10), dp(10), fill);
      String lab = i == 9 && rec != null && rec.liveActive() ? I18n.t("padStop") : I18n.t(PAD_ID[i]);
      drawFit(c, lab, b.centerX(), b.centerY(), b.width() - dp(6), ts(11), Color.argb(dim ? 190 : 255, 255, 255, 255));
    }
    boolean[] hasA = new boolean[1];
    double[] ratioA = new double[1];
    long[] bnds = new long[2];
    padBarData(hasA, ratioA, bnds);
    drawPadClock(c, hasA[0], ratioA[0], bnds, false);
    hit(padBarHit, H_BAR, 0);
    drawPadBar(c, hasA[0], ratioA[0], false);
  }

  private void drawPadTouch(Canvas c, int w, int h) {
    boolean archive = player != null && player.isArchive();
    // затемнение снизу, чтобы читались время и бургер
    fill.setColor(Color.argb(105, 0, 0, 0));
    bg(c, 0, padRow.top - dp(4), w, h);
    hit(0, padRow.top - dp(4), w, h, H_PANEL, 0);
    // три кнопки по центру
    drawTouchSkip(c, padBtn[3], true, false);
    hit(padBtn[3], H_PAD, 3);
    drawPadPlay(c, padBtn[4], false);
    hit(padBtn[4], H_PAD, 4);
    drawTouchSkip(c, padBtn[5], false, !archive);
    hit(padBtn[5], H_PAD, 5);
    // полоса и время
    boolean[] hasA = new boolean[1];
    double[] ratioA = new double[1];
    long[] bnds = new long[2];
    padBarData(hasA, ratioA, bnds);
    drawPadClock(c, hasA[0], ratioA[0], bnds, true);
    hit(padBarHit, H_BAR, 0);
    drawPadBar(c, hasA[0], ratioA[0], true);
    // бургер
    RectF b = padBtn[10];
    fill.setColor(Color.argb(burgerOpen ? 235 : 170, burgerOpen ? 47 : 0, burgerOpen ? 124 : 0, burgerOpen ? 246 : 0));
    c.drawCircle(b.centerX(), b.centerY(), b.width() / 2f, fill);
    stroke.setStyle(Paint.Style.STROKE);
    stroke.setStrokeWidth(dp(2.4f));
    stroke.setStrokeCap(Paint.Cap.ROUND);
    stroke.setColor(Color.WHITE);
    float k = dp(9);
    if (burgerOpen) {
      c.drawLine(b.centerX() - k * 0.8f, b.centerY() - k * 0.8f, b.centerX() + k * 0.8f, b.centerY() + k * 0.8f, stroke);
      c.drawLine(b.centerX() - k * 0.8f, b.centerY() + k * 0.8f, b.centerX() + k * 0.8f, b.centerY() - k * 0.8f, stroke);
    } else {
      for (int i = -1; i <= 1; i++) {
        float y = b.centerY() + i * dp(6);
        c.drawLine(b.centerX() - k, y, b.centerX() + k, y, stroke);
      }
    }
    hit(b, H_BURGER, 0);
    if (burgerOpen) drawBurgerPopup(c, w, h);
  }

  private void drawTouchSkip(Canvas c, RectF b, boolean back, boolean dim) {
    float r = b.width() / 2f, cx = b.centerX(), cy = b.centerY();
    fill.setColor(Color.argb(dim ? 110 : 170, 0, 0, 0));
    c.drawCircle(cx, cy, r, fill);
    int a = dim ? 120 : 255;
    fill.setColor(Color.argb(a, 255, 255, 255));
    float tw = r * 0.34f, th = r * 0.3f, ay = cy - r * 0.1f;
    for (int k = 0; k < 2; k++) {
      tmpPath.reset();
      if (back) {
        float base = cx + k * tw, tip = base - tw;
        tmpPath.moveTo(tip, ay);
        tmpPath.lineTo(base, ay - th);
        tmpPath.lineTo(base, ay + th);
      } else {
        float base = cx - k * tw, tip = base + tw;
        tmpPath.moveTo(tip, ay);
        tmpPath.lineTo(base, ay - th);
        tmpPath.lineTo(base, ay + th);
      }
      tmpPath.close();
      c.drawPath(tmpPath, fill);
    }
    text.setTextSize(r * 0.42f);
    text.setColor(Color.argb(a, 255, 255, 255));
    String s = "30";
    c.drawText(s, cx - text.measureText(s) / 2f, cy + r * 0.62f, text);
  }

  private String burgerLabel(int code) {
    switch (code) {
      case 0: return I18n.t("padMenu");
      case 1: return I18n.t("padGuide");
      case 2: return I18n.t("padVoice");
      case 6: return I18n.t("padLive");
      case BURGER_AUDIO: return I18n.t("popAudio");
      case BURGER_SUB: return I18n.t("padSub");
      case 8: return I18n.t("multiview");
      case 9: return rec != null && rec.liveActive() ? I18n.t("popRecStop") : I18n.t("popRec");
      default: return "";
    }
  }

  private void drawBurgerPopup(Canvas c, int w, int h) {
    int n = BURGER.length;
    float itemH = dp(50), pw = Math.min(dp(250), w - dp(24));
    RectF bb = padBtn[10];
    float bottom = bb.top - dp(8), topMin = dp(8);
    float ph = Math.min(n * itemH + dp(8), bottom - topMin);
    float r = bb.right, l = r - pw, t = bottom - ph;
    fill.setColor(Color.argb(240, 22, 28, 40));
    rr(c, l, t, r, bottom, dp(14));
    hit(l, t, r, bottom, H_PANEL, 0);
    listBegin(c, lBurger, l, t + dp(4), r, bottom - dp(4), itemH, n, -1);
    boolean archive = player != null && player.isArchive();
    for (int i = firstRow(lBurger); i < n; i++) {
      float y = t + dp(4) + (i - lBurger.off) * itemH;
      if (y >= bottom) break;
      int code = BURGER[i];
      text.setTextSize(ts(16));
      text.setColor(code == 6 && !archive ? Color.rgb(150, 156, 168) : Color.WHITE);
      c.drawText(burgerLabel(code), l + dp(20), y + itemH / 2f + ts(16) * 0.35f, text);
      hit(l, Math.max(y, t + dp(4)), r, Math.min(y + itemH, bottom - dp(4)), H_BURGER_ITEM, i);
    }
    listEnd(c, lBurger, r, t + dp(4), bottom - dp(4));
  }

  private void drawPadPlay(Canvas c, RectF b, boolean hot) {
    float cx = b.centerX(), cy = b.centerY();
    float r = Math.min(b.width(), b.height()) / 2f;
    if (hot) {
      fill.setColor(accent());
      c.drawCircle(cx, cy, r + dp(3), fill);
    }
    fill.setColor(Color.WHITE);
    c.drawCircle(cx, cy, r, fill);
    fill.setColor(Color.BLACK);
    if (player != null && player.paused()) {
      tmpPath.reset();
      tmpPath.moveTo(cx - r * 0.18f, cy - r * 0.32f);
      tmpPath.lineTo(cx + r * 0.38f, cy);
      tmpPath.lineTo(cx - r * 0.18f, cy + r * 0.32f);
      tmpPath.close();
      c.drawPath(tmpPath, fill);
    } else {
      float bar = Math.max(2f, r * 0.12f);
      float hh = r * 0.42f;
      float g = r * 0.14f;
      c.drawRect(cx - g - bar, cy - hh, cx - g, cy + hh, fill);
      c.drawRect(cx + g, cy - hh, cx + g + bar, cy + hh, fill);
    }
  }

  private void keepPad() {
    padUntil = SystemClock.uptimeMillis() + 6500;
    invalidate();
  }

  private long nowMs() {
    return System.currentTimeMillis();
  }

  private String formatHms(long ms) {
    if (ms < 0) ms = 0;
    int sec = (int) (ms / 1000);
    return String.format(Locale.US, "%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
  }

  private boolean padBounds(long[] out) {
    if (player == null) return false;
    if (player.isArchive()) {
      long[] b = new long[3];
      if (!player.archiveBounds(b) || b[1] <= b[0]) return false;
      out[0] = Math.max(0, b[2] - b[0]);
      out[1] = b[1] - b[0];
      return true;
    }
    if (epg == null) return false;
    Programme p = epg.now(player.index);
    if (p == null || p.end <= p.start) return false;
    long now = nowMs();
    if (now < p.start) now = p.start;
    if (now > p.end) now = p.end;
    out[0] = now - p.start;
    out[1] = p.end - p.start;
    return true;
  }

  private boolean padSeekTo(long at) {
    if (player == null || profile == null) return false;
    int ch = player.index;
    if (ch < 0 || ch >= profile.pl.size()) return false;
    if (player.isArchive()) {
      long[] b = new long[3];
      if (!player.archiveBounds(b)) return false;
      long lo = b[0], hi = b[1] - 400;
      if (at < lo) at = lo;
      if (at > hi) at = hi;
      if (at >= nowMs() - 1200) {
        player.goLive();
        rec.onZap();
        return true;
      }
      return playArchiveAt(ch, at, b[0], b[1]);
    }
    Programme p = epg != null ? epg.now(ch) : null;
    if (p == null || p.end <= p.start) return false;
    if (at > nowMs() - 1200) {
      note(I18n.t("liveAir"), 1200);
      return true;
    }
    if (at < p.start) at = p.start;
    if (!playArchiveAt(ch, at, p.start, p.end)) {
      note(I18n.t("archiveOff"), 1600);
      return false;
    }
    return true;
  }

  private boolean playArchiveAt(int ch, long at, long start, long end) {
    Channel c = profile.pl.get(ch);
    if (!Catchup.programOk(c, profile.st, start, end)) return false;
    List<String> urls = Catchup.urls(c, at, end);
    if (urls.isEmpty()) return false;
    player.playArchive(ch, urls, start, end, at);
    return true;
  }

  private void padNudge(int seconds) {
    if (player != null && player.isArchive()) {
      long[] b = new long[3];
      if (!player.archiveBounds(b)) {
        note(I18n.t("archiveOff"), 1600);
        return;
      }
      if (!padSeekTo(b[2] + seconds * 1000L)) note(I18n.t("archiveOff"), 1600);
      return;
    }
    if (seconds > 0) {
      note(I18n.t("liveAir"), 1200);
      return;
    }
    long[] bnds = new long[2];
    if (!padBounds(bnds)) {
      note(I18n.t("archiveOff"), 1600);
      return;
    }
    Programme p = epg != null ? epg.now(player.index) : null;
    if (p == null || p.end <= p.start) {
      note(I18n.t("archiveOff"), 1600);
      return;
    }
    if (!padSeekTo(nowMs() + seconds * 1000L)) note(I18n.t("archiveOff"), 1600);
  }

  private void padSeekRatio(double ratio) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    if (player != null && player.isArchive()) {
      long[] b = new long[3];
      if (!player.archiveBounds(b) || b[1] <= b[0]) return;
      padSeekTo(b[0] + (long) (ratio * (b[1] - b[0])));
      return;
    }
    Programme p = epg != null && player != null ? epg.now(player.index) : null;
    if (p == null || p.end <= p.start) return;
    padSeekTo(p.start + (long) (ratio * (p.end - p.start)));
  }

  // ---------- меню ----------
  /** Класс ширины рабочей области: Ui.W_NARROW (&lt; 600 dp), Ui.W_MID (600–840), Ui.W_WIDE (≥ 840). */
  private int wc(float w) {
    return Ui.widthClass(w / getResources().getDisplayMetrics().density);
  }

  /** Масштаб колонок и панелей от ширины: 960 dp = 1, не меньше 0,8 и не больше 1,35. */
  private float wsc(float w) {
    return Math.max(0.8f, Math.min(1.35f, w / dp(960)));
  }

  /**
   * Ширина боковой панели от ширины экрана: на узком — весь экран, на среднем — до 85%, на широком — до 60%,
   * желаемая ширина pref растёт с экраном.
   */
  private float panelW(int w, float pref) {
    int c = wc(w);
    if (c == Ui.W_NARROW) return w;
    return Math.min(pref * wsc(w), w * (c == Ui.W_MID ? 0.85f : 0.6f));
  }

  /** Ширина диалога по центру: от ширины экрана, не шире экрана с полем 12 dp. */
  private float dialogW(int w, float pref) {
    return Math.min(pref * wsc(w), w - dp(24));
  }

  private void drawMenu(Canvas c, int w, int h) {
    float mw = panelW(w, dp(520));
    boolean narrow = mw < dp(560);
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(22));
    text.setColor(Color.WHITE);
    c.drawText(I18n.t("menu"), dp(24), dp(36), text);
    if (!Ui.touch()) {
      text.setTextSize(ts(13));
      text.setColor(Color.rgb(140, 150, 165));
      c.drawText(I18n.t("menuHint"), dp(24), dp(58), text);
    }
    drawClose(c, mw, 0);
    float rowH = dp(56), top = dp(70);
    listBegin(c, lMenu, 0, top, mw, h, rowH, 10, menuI);
    for (int i = firstRow(lMenu); i < 10; i++) {
      float y = top + (i - lMenu.off) * rowH;
      if (y >= h) break;
      if (i == menuI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(2), fill);
      }
      String title = I18n.t(MENU_ID[i]);
      String hint;
      if (i == 5) hint = profile.st.voiceEnabled ? I18n.t("on") : I18n.t("off");
      else hint = MENU_HINT[i].isEmpty() ? "" : I18n.t(MENU_HINT[i]);
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      if (narrow) {
        c.drawText(title, dp(24), y + rowH * (hint.isEmpty() ? 0.62f : 0.4f), text);
        if (!hint.isEmpty()) {
          text.setTextSize(ts(13));
          text.setColor(i == menuI && fv() ? Color.rgb(220, 230, 255) : Color.rgb(140, 150, 165));
          c.drawText(TextUtils.ellipsize(hint, text, mw - dp(40), TextUtils.TruncateAt.END).toString(),
              dp(24), y + rowH * 0.78f, text);
        }
      } else {
        c.drawText(title, dp(24), y + rowH * 0.62f, text);
        if (!hint.isEmpty()) {
          text.setTextSize(ts(13));
          text.setColor(i == menuI && fv() ? Color.rgb(220, 230, 255) : Color.rgb(140, 150, 165));
          String htxt = TextUtils.ellipsize(hint, text, mw * 0.42f, TextUtils.TruncateAt.END).toString();
          c.drawText(htxt, mw - dp(24) - text.measureText(htxt), y + rowH * 0.62f, text);
        }
      }
      hit(0, Math.max(y, top), mw, Math.min(y + rowH, h), H_MENU, i);
    }
    listEnd(c, lMenu, mw, top, h);
  }

  /** Узкий экран (&lt; 600 dp): настройки идут двумя ступенями «список вкладок → экран настроек». */
  boolean settingsNarrow() { return wc(getWidth() - padL - padR) == Ui.W_NARROW; }

  /** Высота шапки «хлебной крошки» на узком экране. */
  private float crumbH() { return dp(52); }

  /**
   * Узкий экран: «хлебная крошка» сверху («Меню › Настройки › Плеер») и кнопка «‹ Назад» (в тач-виде).
   * Справа остаётся место под крестик. Кнопка работает как системное «Назад» (goBack).
   */
  private void drawCrumb(Canvas c, float l, float r, String... parts) {
    float hh = crumbH();
    float x = l + dp(8);
    hit(l, 0, r - dp(56), hh, H_CRUMB, 0);
    if (Ui.touch()) {
      String lab = "‹ " + I18n.t("back");
      text.setTextSize(font());
      float bw = text.measureText(lab) + dp(28);
      RectF b = new RectF(x, dp(6), x + bw, hh - dp(6));
      drawChip(c, b, lab, false, false);
      hit(b, H_CRUMB_BACK, 0);
      x = b.right + dp(10);
    } else {
      x += dp(8);
    }
    float maxW = r - dp(56) - x;
    if (maxW < dp(40)) return;
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < parts.length; i++) {
      if (i > 0) sb.append("  ›  ");
      sb.append(parts[i]);
    }
    text.setTextSize(ts(15));
    String all = sb.toString();
    float tw = text.measureText(all);
    if (tw > maxW && parts.length > 1) {
      /* не помещается: оставляем только последние два звена */
      all = "…  ›  " + parts[parts.length - 1];
    }
    String fit = TextUtils.ellipsize(all, text, maxW, TextUtils.TruncateAt.START).toString();
    float ty = hh / 2f + ts(15) * 0.35f;
    int split = fit.lastIndexOf('›');
    if (split > 0 && split < fit.length() - 1) {
      String head = fit.substring(0, split + 1), tail = fit.substring(split + 1);
      text.setColor(Color.rgb(140, 150, 165));
      c.drawText(head, x, ty, text);
      text.setColor(Color.WHITE);
      c.drawText(tail, x + text.measureText(head), ty, text);
    } else {
      text.setColor(Color.WHITE);
      c.drawText(fit, x, ty, text);
    }
  }

  private void drawSettings(Canvas c, int w, int h) {
    int cls = wc(w);
    boolean narrow = cls == Ui.W_NARROW;
    boolean showTabs = !narrow || setPane == 0;
    boolean showRows = !narrow || setPane == 1;
    /* ≥ 840 dp: вкладки и настройки рядом на 86% ширины; 600–840: те же две колонки на весь экран */
    float nav = narrow ? w : Math.min(dp(220) * wsc(w), w * 0.34f);
    float right = narrow || cls == Ui.W_MID ? w : w * 0.86f;
    fill.setColor(panelBg());
    bg(c, 0, 0, right, h);
    hit(0, 0, right, h, H_PANEL, 0);
    if (showTabs) {
      fill.setColor(themeRgb());
      bg(c, 0, 0, nav, h);
      if (narrow) {
        drawCrumb(c, 0, w, I18n.t("menu"), I18n.t("settings"));
      } else {
        text.setTextSize(ts(16));
        text.setColor(Color.WHITE);
        c.drawText(I18n.t("backMenu"), dp(16), dp(36), text);
        hit(0, 0, nav, dp(52), H_SET_BACK, 0);
      }
      float rowH = Ui.touch() ? dp(50) : dp(40), top = dp(56);
      listBegin(c, lTabs, 0, top, nav, h, rowH, TAB_ID.length, setTab);
      for (int i = firstRow(lTabs); i < TAB_ID.length; i++) {
        float y = top + (i - lTabs.off) * rowH;
        if (y >= h) break;
        if (i == setTab) {
          fill.setColor(setPane == 0 && fv() ? accent() : Color.argb(setPane == 0 ? 110 : 70, 37, 99, 235));
          c.drawRect(0, y, nav, y + rowH - dp(2), fill);
        }
        text.setTextSize(font());
        text.setColor(i == setTab || Ui.touch() ? Color.WHITE : Color.rgb(180, 188, 200));
        c.drawText(I18n.t(TAB_ID[i]), dp(16), y + rowH * 0.64f, text);
        hit(0, Math.max(y, top), nav, Math.min(y + rowH, h), H_TAB, i);
      }
      listEnd(c, lTabs, nav, top, h);
    }
    if (showRows) {
      float rl = narrow ? 0 : nav;
      fillRows();
      text.setTextSize(ts(20));
      text.setColor(Color.WHITE);
      String head = I18n.t(TAB_ID[setTab]);
      float rowH = dp(58), top = dp(70);
      if (narrow) {
        drawCrumb(c, 0, w, I18n.t("settings"), head);
        top = dp(56);
      } else {
        c.drawText(head, rl + dp(24), dp(48), text);
      }
      listBegin(c, lRows, rl, top, right, h, rowH, rows.size(), setI);
      for (int i = firstRow(lRows); i < rows.size(); i++) {
        float y = top + (i - lRows.off) * rowH;
        if (y >= h) break;
        Row r = rows.get(i);
        if (setPane == 1 && setI == i && fv()) {
          fill.setColor(accent());
          c.drawRect(rl, y, right, y + rowH - dp(2), fill);
        }
        text.setTextSize(font());
        text.setColor(Color.WHITE);
        c.drawText(TextUtils.ellipsize(r.title, text, right - rl - dp(40), TextUtils.TruncateAt.END).toString(),
            rl + dp(24), y + rowH * 0.42f, text);
        text.setTextSize(ts(13));
        text.setColor(Color.rgb(160, 170, 185));
        c.drawText(TextUtils.ellipsize(r.hint == null ? "" : r.hint, text, right - rl - dp(40), TextUtils.TruncateAt.END).toString(),
            rl + dp(24), y + rowH * 0.8f, text);
        hit(rl, Math.max(y, top), right, Math.min(y + rowH, h), H_ROW, i);
      }
      listEnd(c, lRows, right, top, h);
    }
    drawClose(c, right, 0);
  }

  // ---------- гид ----------
  private void drawGuide(Canvas c, int w, int h) {
    int cls = wc(w);
    boolean narrow = cls == Ui.W_NARROW;
    float sc = wsc(w);
    /* какие колонки есть в этом виде гида (порядок: группы, каналы, передачи, дни) */
    boolean hasG = guideView == GV_CATEGORIES || guideView == GV_GROUPS;
    boolean hasP = guideView == GV_CHANNELS || guideView == GV_SCHEDULE;
    boolean hasD = daysOpen && hasP;
    int[] order = new int[4];
    int n = 0;
    if (hasG) order[n++] = COL_G;
    order[n++] = COL_C;
    if (hasP) order[n++] = COL_P;
    if (hasD) order[n++] = COL_D;
    int fcol = focusCol == COL_DET ? COL_D : focusCol;
    int fi = n - 1;
    for (int i = 0; i < n; i++) if (order[i] == fcol) { fi = i; break; }
    if (fcol != order[fi]) for (int i = 0; i < n; i++) if (order[i] == COL_C) { fi = i; break; }
    /* окно видимых колонок: ≥ 840 dp все; 600–840 две со сдвигом за фокусом; < 600 одна на экран */
    int from = 0, to = n;
    if (narrow) { from = fi; to = fi + 1; }
    else if (cls == Ui.W_MID && n > 2) { from = Math.max(0, Math.min(fi - 1, n - 2)); to = from + 2; }
    boolean showGroups = false, showChannels = false, showPrograms = false, showDays = false;
    for (int i = from; i < to; i++) {
      if (order[i] == COL_G) showGroups = true;
      else if (order[i] == COL_C) showChannels = true;
      else if (order[i] == COL_P) showPrograms = true;
      else showDays = true;
    }
    /* описание передачи: на широком рядом с днями; на среднем и узком отдельным экраном по «вправо» на деталь */
    boolean showDesc = hasD && profile.st.showProgramDesc && (cls == Ui.W_WIDE || focusCol == COL_DET);
    if (narrow && focusCol == COL_DET) {
      showGroups = showChannels = showPrograms = showDays = false;
    }
    float crumbTop = narrow ? crumbH() + dp(4) : dp(50);
    float gw0 = dp(200) * sc, cw0 = dp(280) * sc, pw0 = dp(260) * sc, dw0 = dp(72) * sc;
    float detW = showDesc ? (narrow ? w - dp(40) : Math.min(dp(340) * sc, Math.max(dp(220), w / 3f))) : 0;
    float avail = narrow ? w : w - (showDesc ? detW + dp(24) : 0);
    float gw = showGroups ? gw0 : 0, cw = showChannels ? cw0 : 0, pw = showPrograms ? pw0 : 0, dw = showDays ? dw0 : 0;
    if (cls == Ui.W_WIDE) {
      float tot = gw + cw + pw + dw;
      if (tot > avail && tot > 0) {
        float k = avail / tot;
        gw *= k; cw *= k; pw *= k; dw *= k;
      }
    } else {
      /* узкий и средний: видимые колонки заполняют ширину, колонка дней остаётся узкой */
      float flex = gw + cw + pw;
      if (flex <= 0) dw = avail;
      else {
        float k = (avail - dw) / flex;
        gw *= k; cw *= k; pw *= k;
      }
    }
    int colA = Math.min(200, panelA());
    if (narrow && !showGroups && !showChannels && !showPrograms && !showDays) {
      fill.setColor(Color.argb(Math.max(colA, 190), 0, 0, 0));
      bg(c, 0, 0, w, h);
      hit(0, 0, w, h, H_PANEL, 0);
    }
    if (showGroups) {
      fill.setColor(Color.argb(colA, 0, 0, 0));
      bg(c, 0, 0, gw, h);
      hit(0, 0, gw, h, H_PANEL, 0);
    }
    if (cw > 0) {
      fill.setColor(Color.argb(colA, 0, 0, 0));
      bg(c, gw, 0, gw + cw, h);
      hit(gw, 0, gw + cw, h, H_PANEL, 0);
    }
    if (showPrograms) {
      fill.setColor(Color.argb(Math.max(160, colA - 20), 0, 0, 0));
      bg(c, gw + cw, 0, gw + cw + pw, h);
      hit(gw + cw, 0, gw + cw + pw, h, H_PANEL, 0);
    }
    if (showDays) {
      fill.setColor(Color.argb(Math.max(150, colA - 30), 0, 0, 0));
      bg(c, gw + cw + pw, 0, gw + cw + pw + dw, h);
      hit(gw + cw + pw, 0, gw + cw + pw + dw, h, H_PANEL, 0);
    }
    int rowsN = profile.st.guideRows < 4 ? 6 : profile.st.guideRows;
    float y0 = crumbTop;
    float row = Math.max(dp(Ui.touch() ? 56 : 52), (h - y0) / rowsN);
    /* портрет и узкий экран: высота строки не растёт вместе с экраном, помещается больше строк */
    if (narrow || Ui.portrait(w, h)) row = Math.min(row, dp(76));
    if (narrow) {
      String gnm = groups.isEmpty() ? I18n.t("channels") : groups.get(Math.min(groupI, groups.size() - 1));
      int cha = channelAt();
      String chn = cha >= 0 && cha < profile.pl.size() ? profile.pl.get(cha).name : "";
      String root = I18n.t("guide");
      if (focusCol == COL_DET) drawCrumb(c, 0, w, root, chn, I18n.t("crumbDesc"));
      else if (showGroups) drawCrumb(c, 0, w, root, I18n.t("folders"));
      else if (showChannels) drawCrumb(c, 0, w, root, gnm);
      else if (showPrograms) drawCrumb(c, 0, w, gnm, chn);
      else drawCrumb(c, 0, w, chn, I18n.t("daysCol"));
    }
    if (showGroups) {
      if (!narrow) {
        text.setTextSize(ts(14));
        text.setColor(Color.rgb(125, 200, 255));
        c.drawText(I18n.t("folders"), dp(16), dp(28), text);
      }
      listBegin(c, lGroups, 0, y0, gw, h, row, groups.size(), groupI, true);
      for (int idx = firstRow(lGroups); idx < groups.size(); idx++) {
        float y = y0 + (idx - lGroups.off) * row;
        if (y >= h) break;
        boolean hot = focusCol == COL_G && idx == groupI;
        boolean active = idx == groupI;
        RectF item = new RectF(dp(8), y, gw - dp(8), y + row - dp(4));
        if (hot) {
          fill.setColor(accent());
          c.drawRoundRect(item, dp(8), dp(8), fill);
        }
        text.setTextSize(ts(15));
        text.setColor(hot ? Color.WHITE : (active ? Color.rgb(125, 211, 252) : Color.WHITE));
        String cnt = String.valueOf(groupCount(idx));
        text.setTextSize(ts(13));
        float cntW = text.measureText(cnt);
        text.setTextSize(ts(15));
        String gn = TextUtils.ellipsize(groups.get(idx), text, gw - dp(60) - cntW, TextUtils.TruncateAt.END).toString();
        c.drawText(gn, dp(16), y + row * 0.5f, text);
        text.setTextSize(ts(13));
        text.setColor(Color.rgb(180, 185, 195));
        c.drawText(cnt, gw - dp(14) - cntW, y + row * 0.5f, text);
        hit(0, Math.max(y, y0), gw, Math.min(y + row, h), H_GGRP, idx);
      }
      listEnd(c, lGroups, gw, y0, h);
    }
    int watching = player.index;
    long now = System.currentTimeMillis();
    if (cw > 0) {
      if (!narrow) {
        String gname = groups.isEmpty() ? I18n.t("channels") : groups.get(Math.min(groupI, groups.size() - 1));
        text.setTextSize(ts(16));
        text.setColor(Color.WHITE);
        c.drawText(TextUtils.ellipsize(gname, text, cw - dp(60), TextUtils.TruncateAt.END).toString(), gw + dp(16), dp(28), text);
      }
      if (inGroup.isEmpty()) {
        text.setTextSize(ts(14));
        text.setColor(Color.rgb(170, 180, 190));
        c.drawText(TextUtils.ellipsize(I18n.t("noChannels"), text, cw - dp(24), TextUtils.TruncateAt.END).toString(),
            gw + dp(12), y0 + dp(26), text);
      }
      listBegin(c, lChan, gw, y0, gw + cw, h, row, inGroup.size(), chI, true);
      for (int idx = firstRow(lChan); idx < inGroup.size(); idx++) {
        float y = y0 + (idx - lChan.off) * row;
        if (y >= h) break;
        int chIdx = inGroup.get(idx);
        if (chIdx < 0 || chIdx >= profile.pl.size()) continue;
        Channel ch = profile.pl.get(chIdx);
        boolean selected = idx == chI;
        boolean on = chIdx == watching;
        RectF item = new RectF(gw + dp(8), y, gw + cw - dp(8), y + row - dp(4));
        if (selected) {
          fill.setColor(accent());
          c.drawRoundRect(item, dp(8), dp(8), fill);
        } else if (on) {
          fill.setColor(Color.argb(26, 255, 255, 255));
          c.drawRoundRect(item, dp(8), dp(8), fill);
        }
        int num = groupI == 1 ? idx + 1 : (ch.number > 0 ? ch.number : chIdx + 1);
        String nm = (on ? "> " : "") + (profile.st.showNumbers ? num + "  " : "") + ch.name;
        float rightPad = Ui.touch() ? dp(88) : dp(36);
        /* как на Windows: в гиде всегда название + текущая передача */
        float blockTop = item.top + dp(3);
        float blockH = Math.min(dp(56), Math.max(dp(16), item.height() - dp(6) - dp(8)));
        float nameH = blockH * 0.52f;
        float logoS = blockH;
        Bitmap logoBm = logos.get(logos.urlFor(chIdx));
        float textL = item.left + dp(10);
        if (logoBm != null && !logoBm.isRecycled()) {
          float ly = blockTop;
          float bmW = logoBm.getWidth(), bmH = logoBm.getHeight();
          float k = Math.min(logoS / Math.max(1f, bmW), logoS / Math.max(1f, bmH));
          float lw = bmW * k, lh = bmH * k;
          rf.set(textL + (logoS - lw) / 2f, ly + (logoS - lh) / 2f, textL + (logoS + lw) / 2f, ly + (logoS + lh) / 2f);
          c.drawBitmap(logoBm, null, rf, bmpPaint);
          textL += logoS + dp(8);
        }
        text.setTextSize(ts(15));
        text.setColor(Color.WHITE);
        float nameY = blockTop + nameH * 0.5f + ts(15) * 0.35f;
        boolean scrollRow = selected && focusCol == COL_C;
        drawMarquee(c, nm, textL, nameY, item.right - textL - rightPad, scrollRow, 0);
        Programme live = epg != null ? epg.now(chIdx) : null;
        String sub = live != null && live.title.length() > 0 ? live.title : I18n.t("liveAir");
        text.setTextSize(ts(13));
        text.setColor(Color.rgb(200, 205, 215));
        float subY = blockTop + nameH + (blockH - nameH) * 0.5f + ts(13) * 0.35f;
        drawMarquee(c, sub, textL, subY, item.right - textL - (Ui.touch() ? dp(40) : dp(8)), scrollRow, 1);
        if (profile.st.showEpgProgress && (selected || on) && live != null && live.end > live.start
            && now >= live.start && now < live.end) {
          /* полоса начинается от левого края строки, под логотипом (как на Windows) */
          float tr = item.left + dp(10), tw = item.right - tr - dp(12);
          fill.setColor(Color.argb(38, 255, 255, 255));
          c.drawRect(tr, item.bottom - dp(7), tr + tw, item.bottom - dp(4), fill);
          fill.setColor(Color.argb(204, 255, 255, 255));
          c.drawRect(tr, item.bottom - dp(7), tr + tw * (now - live.start) / (float) (live.end - live.start),
              item.bottom - dp(4), fill);
        }
        if (profile.isFav(chIdx)) {
          text.setTextSize(ts(16));
          text.setColor(Color.rgb(252, 211, 77));
          c.drawText("★", item.right - (Ui.touch() ? dp(54) : dp(20)), y + row * 0.45f, text);
        }
        hit(gw, Math.max(y, y0), gw + cw, Math.min(y + row, h), H_GCH, idx);
        if (Ui.touch()) {
          text.setTextSize(dp(26));
          text.setColor(Color.argb(210, 255, 255, 255));
          c.drawText("›", item.right - dp(26), y + row * 0.55f, text);
          hit(item.right - dp(46), Math.max(y, y0), gw + cw, Math.min(y + row, h), H_GSCHED, idx);
        }
      }
      listEnd(c, lChan, gw + cw, y0, h);
    }
    drawClose(c, w, 0);
    float px = gw + cw;
    int pHeadCh = channelAt();
    if (showPrograms && !narrow && pHeadCh >= 0 && pHeadCh < profile.pl.size()) {
      Channel ch = profile.pl.get(pHeadCh);
      text.setTextSize(ts(15));
      text.setColor(Color.WHITE);
      int num = groupI == 1 ? chI + 1 : (ch.number > 0 ? ch.number : pHeadCh + 1);
      c.drawText(TextUtils.ellipsize(num + "  " + ch.name, text, pw - dp(24), TextUtils.TruncateAt.END).toString(),
          px + dp(12), dp(28), text);
    }
    float pRow = Ui.touch() ? dp(48) : dp(36);
    if (!showPrograms) {
      /* колонка передач не на экране */
    } else if (guideList.isEmpty()) {
      text.setTextSize(ts(14));
      text.setColor(Color.rgb(170, 180, 190));
      c.drawText(TextUtils.ellipsize(I18n.t(epg != null && epg.busy() ? "epgWait" : "noProgram"), text, pw - dp(24),
          TextUtils.TruncateAt.END).toString(), px + dp(12), y0 + dp(26), text);
    } else {
      listBegin(c, lProg, px, y0, px + pw, h, pRow, guideList.size(), guideI, true);
      Channel pCh = pHeadCh >= 0 && pHeadCh < profile.pl.size() ? profile.pl.get(pHeadCh) : null;
      for (int i = firstRow(lProg); i < guideList.size(); i++) {
        float y = y0 + (i - lProg.off) * pRow;
        if (y >= h) break;
        Programme p = guideList.get(i);
        boolean hot = focusCol == COL_P && i == guideI;
        boolean selected = i == guideI;
        boolean current = p.start <= now && now < p.end;
        RectF item = new RectF(px + dp(8), y, px + pw - dp(8), y + pRow - dp(3));
        if (hot) {
          fill.setColor(accent());
          c.drawRoundRect(item, dp(6), dp(6), fill);
        } else if (selected) {
          fill.setColor(Color.argb(31, 255, 255, 255));
          c.drawRoundRect(item, dp(6), dp(6), fill);
        }
        int tr, tg, tb;
        if (current && !hot) { tr = 125; tg = 211; tb = 252; }
        else if (!hot && !selected) { tr = 220; tg = 220; tb = 220; }
        else { tr = 255; tg = 255; tb = 255; }
        text.setTextSize(ts(14));
        String tm = fmtTime(p.start);
        float timeX = item.left + dp(8);
        float timeW = text.measureText(tm);
        float titleX = timeX + timeW + dp(12);
        text.setColor(current && !hot ? Color.rgb(125, 211, 252) : Color.rgb(180, 185, 195));
        c.drawText(tm, timeX, y + pRow * 0.64f, text);
        boolean archive = pCh != null && p.end <= now && Catchup.programOk(pCh, profile.st, p.start, p.end);
        boolean timed = rec != null && rec.hasTimer(pHeadCh, p.start, p.end);
        float titleR = item.right - dp(8) - (archive || timed ? dp(22) : 0);
        text.setColor(Color.rgb(tr, tg, tb));
        drawMarquee(c, p.title, titleX, y + pRow * 0.64f, Math.max(dp(8), titleR - titleX), hot, 2);
        if (timed) {
          text.setColor(Color.rgb(248, 113, 113));
          c.drawText("●", item.right - dp(20), y + pRow * 0.64f, text);
        } else if (archive) {
          text.setColor(Color.argb(140, 220, 220, 220));
          c.drawText("↺", item.right - dp(20), y + pRow * 0.64f, text);
        }
        hit(px, Math.max(y, y0), px + pw, Math.min(y + pRow, h), H_GPROG, i);
      }
      listEnd(c, lProg, px + pw, y0, h);
    }
    if (showDays) drawGuideDays(c, h, px + pw, dw, narrow ? y0 : dp(40), !narrow);
    if (showDesc) drawGuideDesc(c, w, h, now, detW, narrow ? y0 + dp(8) : dp(48));
    if (favMoving) {
      text.setTextSize(ts(14));
      text.setColor(Color.WHITE);
      String hm = I18n.t("favMoveHint");
      float tw = text.measureText(hm) + dp(24);
      fill.setColor(Color.argb(220, 0, 0, 0));
      rr(c, dp(16), h - dp(48), dp(16) + tw, h - dp(12), dp(8));
      c.drawText(hm, dp(28), h - dp(24), text);
    }
  }

  private void drawGuideDays(Canvas c, int h, float x, float dw, float dy, boolean title) {
    if (title) {
      text.setTextSize(dp(11));
      text.setColor(Color.argb(90, 180, 185, 195));
      String dc = I18n.t("daysCol");
      c.drawText(dc, x + (dw - text.measureText(dc)) / 2, dp(28), text);
    }
    float drow = Ui.touch() ? dp(62) : dp(66);
    listBegin(c, lDays, x, dy, x + dw, h - dp(8), drow, guideDays.size(), dayI, true);
    Calendar cal = Calendar.getInstance();
    for (int idx = firstRow(lDays); idx < guideDays.size(); idx++) {
      float y = dy + (idx - lDays.off) * drow;
      if (y >= h) break;
      cal.setTimeInMillis(guideDays.get(idx));
      RectF item = new RectF(x + dp(4), y, x + dw - dp(4), y + drow - dp(4));
      boolean hot = focusCol == COL_D && idx == dayI;
      boolean selected = idx == dayI;
      if (hot) {
        fill.setColor(accent());
        c.drawRoundRect(item, dp(6), dp(6), fill);
      }
      int wd = cal.get(Calendar.DAY_OF_WEEK) - 1;
      int mo = cal.get(Calendar.MONTH);
      int num = cal.get(Calendar.DAY_OF_MONTH);
      int cr = hot ? Color.WHITE : (selected ? Color.rgb(125, 211, 252) : Color.WHITE);
      float gap = 2f;
      float numSize = dp(18), moSize = dp(11), wdSize = dp(13);
      float ty = item.top + dp(4);
      text.setTextSize(numSize);
      text.setColor(cr);
      String ns = String.valueOf(num);
      ty += numSize;
      c.drawText(ns, item.centerX() - text.measureText(ns) / 2, ty, text);
      ty += gap;
      text.setTextSize(moSize);
      text.setColor(Color.argb(idx == dayI && !hot ? 128 : 90, 255, 255, 255));
      String ms = I18n.t("ms" + mo);
      ty += moSize;
      c.drawText(ms, item.centerX() - text.measureText(ms) / 2, ty, text);
      ty += gap;
      text.setTextSize(wdSize);
      text.setColor(cr);
      String ws = I18n.t("ws" + wd);
      ty += wdSize;
      c.drawText(ws, item.centerX() - text.measureText(ws) / 2, ty, text);
      hit(x, Math.max(y, dy), x + dw, Math.min(y + drow, h - dp(8)), H_GDAY, idx);
    }
    listEnd(c, lDays, x + dw, dy, h - dp(8));
  }

  private void drawGuideDesc(Canvas c, int w, int h, long now, float cardw, float top) {
    Programme p = null;
    if (guideI >= 0 && guideI < guideList.size()) p = guideList.get(guideI);
    else p = epg != null ? epg.now(channelAt()) : null;
    float left = w - cardw;
    float bottom = Math.min(top + dp(300), h - dp(8));
    RectF card = new RectF(left, top, w, bottom);
    hit(card, H_GDET, 0);
    if (focusCol == COL_DET) {
      fill.setColor(Color.WHITE);
      c.drawRoundRect(new RectF(card.left - 2, card.top - 2, card.right + 2, card.bottom + 2), dp(18), dp(18), fill);
    }
    fill.setColor(Color.argb(Ui.touch() ? 215 : 140, 0, 0, 0));
    c.drawRoundRect(card, dp(16), dp(16), fill);
    if (p == null || p.title.isEmpty()) {
      text.setTextSize(ts(16));
      text.setColor(Color.rgb(200, 205, 215));
      c.drawText(I18n.t("noDesc"), card.left + dp(16), card.top + dp(32), text);
      return;
    }
    text.setTextSize(ts(17));
    text.setColor(Color.WHITE);
    drawWrapped(c, p.title, card.left + dp(16), card.top + dp(12), card.width() - dp(32), dp(48), 2);
    if (p.start > 0 && p.end > 0) {
      text.setTextSize(ts(14));
      text.setColor(Color.argb(200, 200, 205, 215));
      c.drawText(fmtTime(p.start) + " – " + fmtTime(p.end), card.left + dp(16), card.top + dp(74), text);
      if (now >= p.start && now < p.end && p.end > p.start) {
        float tr = card.left + dp(16), tw = card.width() - dp(32);
        fill.setColor(Color.argb(40, 255, 255, 255));
        c.drawRect(tr, card.top + dp(88), tr + tw, card.top + dp(91), fill);
        fill.setColor(Color.WHITE);
        c.drawRect(tr, card.top + dp(88), tr + tw * (now - p.start) / (float) (p.end - p.start),
            card.top + dp(91), fill);
      }
    }
    String desc = p.desc == null || p.desc.isEmpty() ? I18n.t("noDesc") : p.desc;
    text.setTextSize(ts(14));
    text.setColor(Color.argb(200, 210, 215, 220));
    float maxH = card.bottom - (card.top + dp(100)) - dp(8);
    int lines = Math.max(1, (int) (maxH / (ts(14) * 1.3f)));
    drawWrapped(c, desc, card.left + dp(16), card.top + dp(100), card.width() - dp(32), maxH, lines);
  }

  private void drawWrapped(Canvas c, String s, float x, float y, float maxW, float maxH, int lines) {
    StaticLayout sl = StaticLayout.Builder.obtain(s, 0, s.length(), text, (int) maxW)
        .setAlignment(Layout.Alignment.ALIGN_NORMAL)
        .setMaxLines(lines)
        .setEllipsize(TextUtils.TruncateAt.END)
        .build();
    c.save();
    c.translate(x, y);
    c.clipRect(0, 0, maxW, maxH);
    sl.draw(c);
    c.restore();
  }

  // ---------- диалоги и списки ----------
  private void drawExit(Canvas c, int w, int h) {
    float dw = dialogW(w, dp(420)), dh = dp(180);
    float l = (w - dw) / 2f, t = (h - dh) / 2f;
    fill.setColor(panelBg(240));
    rr(c, l, t, l + dw, t + dh, dp(16));
    hit(l, t, l + dw, t + dh, H_PANEL, 0);
    text.setTextSize(ts(17));
    text.setColor(Color.WHITE);
    String s = TextUtils.ellipsize(I18n.t("exitAgain"), text, dw - dp(24), TextUtils.TruncateAt.END).toString();
    c.drawText(s, l + dw / 2 - text.measureText(s) / 2, t + dp(48), text);
    float mid = l + dw / 2;
    float bh = Math.max(dp(46), ts(17) + dp(24));
    RectF a = new RectF(l + dp(24), t + dh - dp(20) - bh, mid - dp(8), t + dh - dp(20));
    RectF b = new RectF(mid + dp(8), t + dh - dp(20) - bh, l + dw - dp(24), t + dh - dp(20));
    fill.setColor(exitI == 0 && fv() ? accent() : Color.rgb(40, 46, 60));
    c.drawRoundRect(a, dp(10), dp(10), fill);
    fill.setColor(exitI == 1 && fv() ? accent() : Color.rgb(40, 46, 60));
    c.drawRoundRect(b, dp(10), dp(10), fill);
    drawFit(c, I18n.t("quit"), a.centerX(), a.centerY(), a.width() - dp(8), ts(17), Color.WHITE);
    drawFit(c, I18n.t("cancel"), b.centerX(), b.centerY(), b.width() - dp(8), ts(17), Color.WHITE);
    hit(a, H_EXIT, 0);
    hit(b, H_EXIT, 1);
  }

  /** Вставить / Копировать рядом с полем. */
  private void drawFieldChips(Canvas c, float l, float t, float r, boolean focusPaste, boolean focusCopy) {
    float bh = Math.max(dp(46), ts(15) + dp(22));
    String a = I18n.t("paste"), b = I18n.t("copy");
    text.setTextSize(ts(15));
    float aw = text.measureText(a) + dp(32), bw = text.measureText(b) + dp(32);
    float avail = r - l - dp(8);
    if (aw + bw > avail) {
      float k = avail / (aw + bw);
      aw *= k; bw *= k;
    }
    RectF ra = new RectF(l, t, l + aw, t + bh);
    RectF rb = new RectF(l + aw + dp(8), t, l + aw + dp(8) + bw, t + bh);
    drawChip(c, ra, a, focusPaste, false);
    drawChip(c, rb, b, focusCopy, false);
    hit(ra, H_PASTE, 0);
    hit(rb, H_COPY, 0);
  }

  private void drawSearch(Canvas c, int w, int h) {
    float mw = panelW(w, dp(440));
    float bottom = h - Math.max(0, imeH - padB);
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    boolean compact = bottom < dp(330);
    float y;
    if (!compact) {
      text.setTextSize(ts(22));
      text.setColor(Color.WHITE);
      String t = mvPick != 0 ? I18n.t("mvPick") : I18n.t("searchTitle");
      c.drawText(TextUtils.ellipsize(t, text, mw - dp(80), TextUtils.TruncateAt.END).toString(), dp(24), dp(46), text);
      y = dp(62);
    } else {
      y = dp(8);
    }
    drawClose(c, mw, 0);
    float fh = Math.max(dp(46), ts(17) + dp(24));
    /* строка ввода + справа кнопка микрофона (как в окне поиска Windows); в compact справа ещё крестик */
    float micW = fh;
    float rightEdge = mw - dp(16) - (compact ? dp(46) : 0);
    RectF f = new RectF(dp(16), y, rightEdge - micW - dp(8), y + fh);
    RectF mic = new RectF(rightEdge - micW, y, rightEdge, y + fh);
    boolean topFocus = searchI < 0 && fv();
    drawField(c, f, searchQ, I18n.t("searchField"), topFocus && searchBtn == 0, imeH > 0);
    hit(f, H_FIELD, 0);
    drawMicButton(c, mic, topFocus && searchBtn == 3);
    hit(mic, H_MIC, 0);
    y += fh + dp(8);
    float chipH = Math.max(dp(46), ts(15) + dp(22));
    drawFieldChips(c, dp(16), y, mw - dp(16), topFocus && searchBtn == 1, topFocus && searchBtn == 2);
    y += chipH + dp(8);
    float rowH = Ui.touch() ? dp(50) : dp(44);
    text.setTextSize(ts(13));
    boolean noteOn = voiceNote != null && !voiceNote.isEmpty();
    text.setColor(noteOn ? Color.rgb(160, 190, 230) : Color.argb(180, 255, 255, 255));
    c.drawText(TextUtils.ellipsize(noteOn ? voiceNote : I18n.t("searchVoiceHint"), text, mw - dp(40),
        TextUtils.TruncateAt.END).toString(), dp(24), y + dp(18), text);
    y += dp(28);
    if (searchHits.isEmpty()) {
      text.setTextSize(ts(16));
      text.setColor(Color.WHITE);
      c.drawText(I18n.t(searchQ.isEmpty() ? "noMatches" : "searchNoVoice"), dp(24), y + dp(30), text);
      return;
    }
    listBegin(c, lSearch, 0, y, mw, bottom, rowH, searchHits.size(), searchI);
    for (int i = firstRow(lSearch); i < searchHits.size(); i++) {
      float yy = y + (i - lSearch.off) * rowH;
      if (yy >= bottom) break;
      if (i == searchI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, yy, mw, yy + rowH - dp(2), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      String nm = profile.pl.get(searchHits.get(i)).name;
      c.drawText(TextUtils.ellipsize(nm, text, mw - dp(40), TextUtils.TruncateAt.END).toString(), dp(24), yy + rowH * 0.64f, text);
      hit(0, Math.max(yy, y), mw, Math.min(yy + rowH, bottom), H_LIST, i);
    }
    listEnd(c, lSearch, mw, y, bottom);
  }

  private boolean micBusy() {
    return voiceDl || (voice != null && (voice.downloading() || voice.state() == Voice.BUSY));
  }

  /** Кнопка микрофона в строке поиска: красная при слушании/скачивании, акцент при фокусе пульта. */
  private void drawMicButton(Canvas c, RectF r, boolean focus) {
    int bg = micBusy() ? Color.rgb(200, 48, 48) : focus ? accent() : Color.rgb(44, 52, 68);
    fill.setColor(bg);
    c.drawRoundRect(r, dp(10), dp(10), fill);
    if (focus) {
      stroke.setStyle(Paint.Style.STROKE);
      stroke.setStrokeWidth(dp(2.5f));
      stroke.setColor(Color.WHITE);
      c.drawRoundRect(r, dp(10), dp(10), stroke);
    }
    float cx = r.centerX(), cy = r.centerY() - dp(1);
    float u = Math.min(r.width(), r.height()) / 48f;
    fill.setColor(Color.WHITE);
    rf.set(cx - 5 * u, cy - 13 * u, cx + 5 * u, cy + 3 * u);
    c.drawRoundRect(rf, 5 * u, 5 * u, fill);
    stroke.setStyle(Paint.Style.STROKE);
    stroke.setStrokeWidth(Math.max(dp(1.6f), 2 * u));
    stroke.setStrokeCap(Paint.Cap.ROUND);
    stroke.setColor(Color.WHITE);
    rf.set(cx - 9 * u, cy - 9 * u, cx + 9 * u, cy + 7 * u);
    c.drawArc(rf, 0, 180, false, stroke);
    c.drawLine(cx, cy + 7 * u, cx, cy + 13 * u, stroke);
    c.drawLine(cx - 6 * u, cy + 13 * u, cx + 6 * u, cy + 13 * u, stroke);
  }

  /** Порядок фокуса в верхней строке поиска: поле → микрофон → «Вставить» → «Копировать». */
  private void searchStep(int dir) {
    int[] ord = {0, 3, 1, 2};
    int p = 0;
    for (int i = 0; i < ord.length; i++) if (ord[i] == searchBtn) p = i;
    searchBtn = ord[(p + dir + ord.length) % ord.length];
    invalidate();
  }

  /**
   * Микрофон в поиске — тот же слух, что кнопка пульта и «Голос» на pad.
   * Vosk, если модель уже на диске; иначе системное распознавание.
   * Качать модель только если слушать вообще нечем.
   */
  void searchMic() {
    searchI = -1;
    searchBtn = 3;
    if (voice == null) return;
    if (voiceDl || voice.downloading()) {
      note(I18n.t("voiceDlModel"), 1500);
      return;
    }
    if (!profile.st.voiceEnabled) {
      voiceNote = I18n.t("voiceOffMenu");
      note(voiceNote, 2200);
      invalidate();
      return;
    }
    if (host != null && !host.hasMicPermission()) {
      setMode(MICPERM);
      return;
    }
    if (voice.available()) {
      listenNow();
      return;
    }
    downloadVosk();
  }

  private void listenNow() {
    voiceNote = I18n.t("voicePrep");
    invalidate();
    if (host != null) host.startVoice();
  }

  private void downloadVosk() {
    if (voiceDl || voice == null) return;
    voiceDl = true;
    voiceNote = I18n.t("voiceDlModel") + " 0%";
    note(I18n.t("voiceDlModel"), 2500);
    invalidate();
    voice.downloadModel(() -> {
      voiceDl = false;
      boolean ok = voice.voskReady();
      voiceNote = I18n.t(ok ? "voskReady" : "voskFail");
      note(voiceNote, 2000);
      invalidate();
      /* модель готова (или есть системное распознавание как запас) — слушаем сразу */
      if (mode == SEARCH && voice.available()) listenNow();
    });
  }

  /** Статус от Voice (в том числе проценты скачивания модели): строка в окне поиска и тост вне поиска. */
  void voiceStatus(String t) {
    voiceNote = t == null ? "" : t;
    if (mode == SEARCH) invalidate();
    else note(voiceNote, 2000);
  }

  private void drawHistory(Canvas c, int w, int h) {
    float mw = panelW(w, dp(420));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(22));
    text.setColor(Color.WHITE);
    c.drawText(I18n.t("history"), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    text.setTextSize(ts(13));
    text.setColor(Color.argb(180, 255, 255, 255));
    c.drawText(I18n.t("histHint"), dp(24), dp(70), text);
    List<String> items = histLabels();
    items.add(I18n.t("histClear"));
    float rowH = Ui.touch() ? dp(50) : dp(44), top = dp(86);
    listBegin(c, lHist, 0, top, mw, h, rowH, items.size(), histI);
    for (int i = firstRow(lHist); i < items.size(); i++) {
      float y = top + (i - lHist.off) * rowH;
      if (y >= h) break;
      if (i == histI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(2), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      String lab = items.get(i);
      if (i == histI && histX == 1 && i < items.size() - 1) lab = "✕  " + lab;
      c.drawText(TextUtils.ellipsize(lab, text, mw - dp(40), TextUtils.TruncateAt.END).toString(), dp(24), y + rowH * 0.64f, text);
      hit(0, Math.max(y, top), mw, Math.min(y + rowH, h), H_LIST, i);
    }
    listEnd(c, lHist, mw, top, h);
  }

  private void drawRecs(Canvas c, int w, int h) {
    float mw = panelW(w, dp(480));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(22));
    text.setColor(Color.WHITE);
    c.drawText(I18n.t("recordings"), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    List<Rec.Item> files = rec.files();
    if (files.isEmpty()) {
      text.setTextSize(ts(16));
      c.drawText(TextUtils.ellipsize(I18n.t("recsEmpty"), text, mw - dp(48), TextUtils.TruncateAt.END).toString(),
          dp(24), dp(100), text);
      return;
    }
    if (recI >= files.size()) recI = files.size() - 1;
    float rowH = Ui.touch() ? dp(56) : dp(48), top = dp(70);
    listBegin(c, lRecs, 0, top, mw, h, rowH, files.size(), recI);
    for (int i = firstRow(lRecs); i < files.size(); i++) {
      float y = top + (i - lRecs.off) * rowH;
      if (y >= h) break;
      if (i == recI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(2), fill);
      }
      Rec.Item it = files.get(i);
      String title = recTitle(it.name);
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(TextUtils.ellipsize(title, text, mw - dp(40), TextUtils.TruncateAt.END).toString(),
          dp(24), y + rowH * 0.42f, text);
      text.setTextSize(ts(13));
      text.setColor(Color.rgb(160, 170, 185));
      c.drawText(recMeta(it), dp(24), y + rowH * 0.78f, text);
      hit(0, Math.max(y, top), mw, Math.min(y + rowH, h), H_REC, i);
    }
    listEnd(c, lRecs, mw, top, h);
  }

  private static String recTitle(String name) {
    if (name == null) return "";
    int dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }

  private String recMeta(Rec.Item it) {
    long mb = Math.max(1, it.size / (1024 * 1024));
    String when = it.modified > 0
        ? new SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.US).format(new Date(it.modified)) : "";
    return mb + " " + I18n.t("mb") + (when.isEmpty() ? "" : "  ·  " + when);
  }

  private void drawMulti(Canvas c, int w, int h) {
    drawClose(c, w, 0);
    if (mvMenu == 1) {
      String[] opt = {I18n.t("mvAdd"), I18n.t("mvFind"), I18n.t("mvChange"), I18n.t("mvDel")};
      float aw = dialogW(w, dp(300)), ah = dp(16) + opt.length * dp(52);
      float l = w - aw - dp(12), t = Math.max(dp(8), (h - ah) / 2);
      fill.setColor(panelBg(245));
      rr(c, l, t, l + aw, t + ah, dp(12));
      hit(l, t, l + aw, t + ah, H_PANEL, 0);
      for (int i = 0; i < opt.length; i++) {
        float y = t + dp(8) + i * dp(52);
        if (i == mvI && fv()) {
          fill.setColor(accent());
          c.drawRect(l, y, l + aw, y + dp(50), fill);
        }
        text.setColor(Color.WHITE);
        text.setTextSize(font());
        c.drawText(opt[i], l + dp(16), y + dp(32), text);
        hit(l, y, l + aw, y + dp(50), H_MVOPT, i);
      }
    } else {
      fill.setColor(Color.argb(230, 16, 21, 30));
      bg(c, 0, h - dp(52), w, h);
      hit(0, h - dp(52), w, h, H_MVBAR, 0);
      text.setTextSize(ts(15));
      text.setColor(Color.rgb(230, 235, 245));
      String hint = TextUtils.ellipsize(I18n.t(Ui.touch() ? "mvTapHint" : "mvHint"), text, w - dp(24),
          TextUtils.TruncateAt.END).toString();
      c.drawText(hint, (w - text.measureText(hint)) / 2, h - dp(52) / 2f + ts(15) * 0.35f, text);
    }
  }

  private static final String[] PIN_KEYS = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "←", "0", "OK"};

  private void drawPin(Canvas c, int w, int h) {
    boolean pad = Ui.touch();
    float dw = dialogW(w, dp(320));
    float keyH = pad ? Math.max(dp(34), Math.min(dp(52), (h - dp(150)) / 4f)) : 0;
    float dh = pad ? dp(112) + keyH * 4 + dp(16) : dp(180);
    float l = (w - dw) / 2f, t = Math.max(dp(6), (h - dh) / 2f);
    fill.setColor(panelBg(244));
    rr(c, l, t, l + dw, t + dh, dp(16));
    hit(l, t, l + dw, t + dh, H_PANEL, 0);
    text.setTextSize(ts(20));
    text.setColor(Color.WHITE);
    String ttl = I18n.t("pinTitle");
    c.drawText(ttl, l + dw / 2 - text.measureText(ttl) / 2, t + dp(36), text);
    String dots = pinBuf.replaceAll(".", "•");
    text.setTextSize(ts(24));
    c.drawText(dots, l + dw / 2 - text.measureText(dots) / 2, t + dp(78), text);
    if (!pad) return;
    float gx = l + dp(12), gw = (dw - dp(24)) / 3f;
    float gy = t + dp(96);
    for (int i = 0; i < 12; i++) {
      int rI = i / 3, cI = i % 3;
      RectF b = new RectF(gx + cI * gw + dp(4), gy + rI * keyH + dp(2), gx + (cI + 1) * gw - dp(4), gy + (rI + 1) * keyH - dp(2));
      fill.setColor(i == 11 ? accent() : Color.rgb(42, 50, 66));
      c.drawRoundRect(b, dp(10), dp(10), fill);
      drawFit(c, PIN_KEYS[i], b.centerX(), b.centerY(), b.width() - dp(8), dp(20), Color.WHITE);
      hit(b, H_PIN, i);
    }
  }

  private void drawSetup(Canvas c, int w, int h) {
    float mw = panelW(w, dp(460));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(22));
    text.setColor(Color.WHITE);
    c.drawText(TextUtils.ellipsize(I18n.t("welcome"), text, mw - dp(48), TextUtils.TruncateAt.END).toString(), dp(24), dp(48), text);
    float hintH = dp(44);
    text.setTextSize(ts(15));
    drawWrapped(c, I18n.t("setupHint"), dp(24), dp(62), mw - dp(48), hintH, 2);
    String[] rows = {I18n.t("setupOpen"), I18n.t("setupFile"), I18n.t("setupEpg"), I18n.t("setupClose")};
    float rowH = dp(56), top = dp(116);
    for (int i = 0; i < rows.length; i++) {
      float y = top + i * rowH;
      if (y + rowH > h + dp(40)) break;
      if (i == setupI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(4), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(TextUtils.ellipsize(rows[i], text, mw - dp(48), TextUtils.TruncateAt.END).toString(), dp(24), y + rowH * 0.6f, text);
      hit(0, y, mw, y + rowH - dp(4), H_SETUP, i);
    }
  }

  private void drawShare(Canvas c, int w, int h) {
    String[] rows = {I18n.t("shareCopy"), I18n.t("shareTg"), I18n.t("shareWa"), I18n.t("shareMax"), I18n.t("shareMail")};
    float mw = panelW(w, dp(360));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(20));
    text.setColor(Color.WHITE);
    String title = I18n.t(shareFile ? "shareTitleFile" : "shareTitle");
    c.drawText(TextUtils.ellipsize(title, text, mw - dp(80), TextUtils.TruncateAt.END).toString(), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    float rowH = Math.min(dp(52), (h - dp(80)) / rows.length);
    for (int i = 0; i < rows.length; i++) {
      float y = dp(70) + i * rowH;
      if (i == shareI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(2), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(rows[i], dp(24), y + rowH * 0.62f, text);
      hit(0, y, mw, y + rowH, H_SHARE, i);
    }
  }

  private void drawAsk(Canvas c, int w, int h) {
    String title = askWhy == 0 ? I18n.t("askRec") : askWhy == 1 ? I18n.t("askBackup") : I18n.t("askRestore");
    String[] rows = {I18n.t("askInternal"), I18n.t("askDisk")};
    float mw = panelW(w, dp(400));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(20));
    text.setColor(Color.WHITE);
    c.drawText(TextUtils.ellipsize(title, text, mw - dp(80), TextUtils.TruncateAt.END).toString(), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    for (int i = 0; i < 2; i++) {
      float y = dp(80) + i * dp(58);
      if (i == askI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + dp(54), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(rows[i], dp(24), y + dp(34), text);
      hit(0, y, mw, y + dp(54), H_ASK, i);
    }
  }

  private void drawChMenu(Canvas c, int w, int h) {
    int n = Math.max(1, chmN);
    int ch = chmCh >= 0 ? chmCh : (player != null ? player.index : -1);
    boolean fav = ch >= 0 && profile.isFav(ch);
    float dw = dialogW(w, dp(320)), dh = dp(24) + n * dp(54);
    float l = (w - dw) / 2f, t = (h - dh) / 2f;
    fill.setColor(panelBg(240));
    rr(c, l, t, l + dw, t + dh, dp(12));
    hit(l, t, l + dw, t + dh, H_PANEL, 0);
    for (int i = 0; i < n; i++) {
      float y = t + dp(12) + i * dp(54);
      if (i == chmI && fv()) {
        fill.setColor(accent());
        c.drawRect(l, y, l + dw, y + dp(52), fill);
      }
      String lab = i == 0
          ? (fav ? I18n.t("favDelAction") : I18n.t("favAddAction"))
          : I18n.t("favMove");
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(lab, l + dp(20), y + dp(33), text);
      hit(l, y, l + dw, y + dp(52), H_CHM, i);
    }
  }

  // ---------- редактор ссылки / кода (MENU_URL) ----------
  private void drawUrl(Canvas c, int w, int h) {
    float mw = panelW(w, dp(520));
    float bottom = h - Math.max(0, imeH - padB);
    fill.setColor(panelBg(Math.max(panelA(), 235)));
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    boolean compact = bottom < dp(300);
    float y = dp(12);
    text.setTextSize(ts(compact ? 18 : 22));
    text.setColor(Color.WHITE);
    float ty = y + ts(compact ? 18 : 22);
    c.drawText(TextUtils.ellipsize(urlTitle, text, mw - dp(80), TextUtils.TruncateAt.END).toString(), dp(20), ty, text);
    drawClose(c, mw, 0);
    y += compact ? dp(38) : dp(52);
    if (!compact) {
      text.setTextSize(ts(14));
      text.setColor(Color.rgb(160, 170, 185));
      drawWrapped(c, urlHint, dp(20), y, mw - dp(40), dp(44), 2);
      y += dp(50);
    }
    float fh = compact ? dp(56) : Math.min(dp(104), Math.max(dp(64), bottom - y - dp(80)));
    RectF f = new RectF(dp(16), y, mw - dp(16), y + fh);
    boolean mask = urlKind == U_XTP;
    drawFieldMulti(c, f, urlText, mask, urlI == 0 && fv(), imeH > 0, I18n.t("urlField"));
    hit(f, H_FIELD, 0);
    y += fh + dp(10);
    String[] labs = {I18n.t("paste"), I18n.t("copy"), I18n.t("ok")};
    float bh = Math.max(dp(46), ts(15) + dp(24));
    text.setTextSize(ts(15));
    float[] bw = new float[3];
    float sum = 0;
    for (int i = 0; i < 3; i++) { bw[i] = text.measureText(labs[i]) + dp(32); sum += bw[i]; }
    float avail = mw - dp(32) - dp(16);
    if (sum > avail) for (int i = 0; i < 3; i++) bw[i] *= avail / sum;
    float x = dp(16);
    for (int i = 0; i < 3; i++) {
      RectF b = new RectF(x, y, x + bw[i], y + bh);
      drawChip(c, b, labs[i], urlI == i + 1 && fv(), i == 2);
      hit(b, i == 0 ? H_PASTE : i == 1 ? H_COPY : H_URL_OK, 0);
      x += bw[i] + dp(8);
    }
  }

  boolean onKey(int code, KeyEvent e) {
    if (e == null) return false;
    if (e.getAction() == KeyEvent.ACTION_UP) {
      if (holdSub && isOk(code)) {
        holdSub = false;
        if (player != null) {
          if (SystemClock.uptimeMillis() - holdAt >= 550) player.cycleSubs();
          else player.cycleAudio();
        }
        if (mode == PAD) keepPad();
        return true;
      }
      if (holdChan && isOk(code)) {
        holdChan = false;
        if (mode == GUIDE) {
          if (SystemClock.uptimeMillis() - holdAt >= 550) openChMenu();
          else activateChannel();
        }
        return true;
      }
      if (holdPad && isOk(code)) {
        holdPad = false;
        if (SystemClock.uptimeMillis() - holdAt >= 550 && mode == LIVE) {
          openChMenu();
          return true;
        }
        if (mode == LIVE) {
          setMode(PAD);
          return true;
        }
      }
      return isOk(code) || isNav(code) || isVol(code) || isVoiceKey(code);
    }
    if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_MULTIPLE) return false;
    if (isRemoteKey(code, e) && Ui.onKeyInput()) invalidate();
    if (waitKey > 0) {
      if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) {
        waitKey = 0;
        note(I18n.t("cancel"), 1200);
        invalidate();
        return true;
      }
      assignWaitKey(code);
      return true;
    }
    /* поиск и ввод ссылки: буква/цифра/пробел без скрытого поля ввода (физическая клавиатура, нумпад) */
    if (textMode() && textKey(e)) return true;
    if (handleBoundKey(code, e)) return true;
    if (e.getRepeatCount() > 0 && isOk(code)) return true;
    if (isVoiceKey(code) && mode != URL && mode != PIN) {
      if (e.getRepeatCount() == 0) startVoiceSearch();
      return true;
    }
    if (isVolUp(code)) { player.addVolume(5); return true; }
    if (isVolDown(code)) { player.addVolume(-5); return true; }
    if (code == KeyEvent.KEYCODE_VOLUME_MUTE) { player.toggleMute(); return true; }
    if (code == KeyEvent.KEYCODE_CHANNEL_UP || code == KeyEvent.KEYCODE_PAGE_UP
        || (code == KeyEvent.KEYCODE_DPAD_UP && mode == LIVE)) {
      zap(-1); return true;
    }
    if (code == KeyEvent.KEYCODE_CHANNEL_DOWN || code == KeyEvent.KEYCODE_PAGE_DOWN
        || (code == KeyEvent.KEYCODE_DPAD_DOWN && mode == LIVE)) {
      zap(1); return true;
    }
    if (code == KeyEvent.KEYCODE_COMMA || code == KeyEvent.KEYCODE_MEDIA_REWIND
        || code == KeyEvent.KEYCODE_MEDIA_PREVIOUS) {
      if (mode == LIVE || mode == PAD) { padNudge(-30); if (mode == PAD) keepPad(); return true; }
    }
    if (code == KeyEvent.KEYCODE_PERIOD || code == KeyEvent.KEYCODE_MEDIA_FAST_FORWARD
        || code == KeyEvent.KEYCODE_MEDIA_NEXT) {
      if (mode == LIVE || mode == PAD) { padNudge(30); if (mode == PAD) keepPad(); return true; }
    }
    if (code == KeyEvent.KEYCODE_FORWARD_DEL && mode == MULTI) {
      if (mvFocus > 0) { mvCh[mvFocus] = -1; applyMultiLayout(); }
      return true;
    }
    if (code == KeyEvent.KEYCODE_D && mode == RECS) {
      askWhy = 0; setMode(ASK); return true;
    }
    if (isOk(code)) return onOk(e);
    if (code == KeyEvent.KEYCODE_DEL) {
      if (mode == PIN) {
        if (pinBuf.length() > 0) pinBuf = pinBuf.substring(0, pinBuf.length() - 1);
        invalidate();
        return true;
      }
      if (textMode()) {
        String s = currentText();
        if (!s.isEmpty()) onKbText(s.substring(0, s.length() - 1));
        syncKb();
        return true;
      }
      if (mode == RECS) {
        List<Rec.Item> files = rec.files();
        if (recI >= 0 && recI < files.size()) {
          files.get(recI).delete();
          invalidate();
          return true;
        }
      }
      return goBack();
    }
    if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE
        || code == KeyEvent.KEYCODE_BUTTON_B || code == KeyEvent.KEYCODE_BUTTON_Y) return goBack();
    if (code == KeyEvent.KEYCODE_DPAD_LEFT) return onLeft();
    if (code == KeyEvent.KEYCODE_DPAD_RIGHT) return onRight();
    if (code == KeyEvent.KEYCODE_DPAD_UP) return onUp();
    if (code == KeyEvent.KEYCODE_DPAD_DOWN) return onDown();
    if (code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_INFO || code == KeyEvent.KEYCODE_F2
        || code == KeyEvent.KEYCODE_BUTTON_START || code == KeyEvent.KEYCODE_BUTTON_MODE
        || code == KeyEvent.KEYCODE_SETTINGS || code == KeyEvent.KEYCODE_TV_CONTENTS_MENU) {
      if (chmOpen()) return true;
      openMainMenuFromKey();
      return true;
    }
    if (code == KeyEvent.KEYCODE_GUIDE || code == KeyEvent.KEYCODE_TV) {
      if (mode == GUIDE) closeGuide();
      else openGuide();
      return true;
    }
    if (code == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
        || code == KeyEvent.KEYCODE_MEDIA_PLAY || code == KeyEvent.KEYCODE_MEDIA_PAUSE) {
      if (mode == LIVE || mode == PAD) player.pauseToggle();
      return true;
    }
    if (code == KeyEvent.KEYCODE_A && (mode == LIVE || mode == PAD) && player != null) {
      player.cycleAudio();
      if (mode == PAD) keepPad();
      return true;
    }
    int dig = digitOf(code);
    if (dig >= 0) {
      if (mode == PIN) {
        if (pinBuf.length() < 8) pinBuf += dig;
        invalidate();
        return true;
      }
      if (mode == SEARCH) {
        onKbText(searchQ + dig);
        syncKb();
        return true;
      }
    }
    return false;
  }

  private static int digitOf(int code) {
    if (code >= KeyEvent.KEYCODE_0 && code <= KeyEvent.KEYCODE_9) return code - KeyEvent.KEYCODE_0;
    if (code >= KeyEvent.KEYCODE_NUMPAD_0 && code <= KeyEvent.KEYCODE_NUMPAD_9) return code - KeyEvent.KEYCODE_NUMPAD_0;
    return -1;
  }

  /** Клавиша пульта, D-pad, геймпада или физической клавиатуры (не экранной и не громкость). */
  private static boolean isRemoteKey(int code, KeyEvent e) {
    if (e.getDeviceId() == KeyCharacterMap.VIRTUAL_KEYBOARD) return false;
    switch (code) {
      case KeyEvent.KEYCODE_DPAD_UP:
      case KeyEvent.KEYCODE_DPAD_DOWN:
      case KeyEvent.KEYCODE_DPAD_LEFT:
      case KeyEvent.KEYCODE_DPAD_RIGHT:
      case KeyEvent.KEYCODE_DPAD_CENTER:
      case KeyEvent.KEYCODE_BUTTON_A:
      case KeyEvent.KEYCODE_BUTTON_B:
      case KeyEvent.KEYCODE_BUTTON_X:
      case KeyEvent.KEYCODE_BUTTON_Y:
      case KeyEvent.KEYCODE_BUTTON_START:
      case KeyEvent.KEYCODE_BUTTON_SELECT:
      case KeyEvent.KEYCODE_CHANNEL_UP:
      case KeyEvent.KEYCODE_CHANNEL_DOWN:
      case KeyEvent.KEYCODE_GUIDE:
      case KeyEvent.KEYCODE_INFO:
        return true;
      case KeyEvent.KEYCODE_ENTER:
      case KeyEvent.KEYCODE_NUMPAD_ENTER:
        return true;
      default:
        return false;
    }
  }

  private boolean isOk(int code) {
    return code == KeyEvent.KEYCODE_DPAD_CENTER || code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_NUMPAD_ENTER
        || code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_BUTTON_SELECT;
  }

  private static boolean isVolUp(int code) {
    return code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_PLUS
        || code == KeyEvent.KEYCODE_NUMPAD_ADD || code == KeyEvent.KEYCODE_EQUALS;
  }

  private static boolean isVolDown(int code) {
    return code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_MINUS
        || code == KeyEvent.KEYCODE_NUMPAD_SUBTRACT;
  }

  private static boolean isVol(int code) {
    return isVolUp(code) || isVolDown(code) || code == KeyEvent.KEYCODE_VOLUME_MUTE;
  }

  private static boolean isNav(int code) {
    return code == KeyEvent.KEYCODE_DPAD_UP || code == KeyEvent.KEYCODE_DPAD_DOWN
        || code == KeyEvent.KEYCODE_DPAD_LEFT || code == KeyEvent.KEYCODE_DPAD_RIGHT
        || code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE
        || code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_CHANNEL_UP
        || code == KeyEvent.KEYCODE_CHANNEL_DOWN;
  }

  static boolean isVoiceKey(int code) {
    return code == KeyEvent.KEYCODE_VOICE_ASSIST
        || code == KeyEvent.KEYCODE_ASSIST
        || code == KeyEvent.KEYCODE_SEARCH;
  }

  private void assignWaitKey(int code) {
    String name = Keys.store(code);
    Settings s = profile.st;
    switch (waitKey) {
      case 1: s.keyMenu = name; break;
      case 2: s.keyMute = name; break;
      case 3: s.keyFav = name; break;
      case 4: s.keyFs = name; break;
      case 5: s.keyGuide = name; break;
      case 6: s.keySearch = name; break;
      case 7: s.keyRec = name; break;
      case 8: s.keyPip = name; break;
      case 9: s.keyVoice = name; break;
      case 10: s.keyLive = name; break;
      default: break;
    }
    waitKey = 0;
    profile.saveSettings();
    note(I18n.t("keyAssigned"), 1600);
    invalidate();
  }

  private boolean handleBoundKey(int code, KeyEvent e) {
    if (mode == PIN || mode == URL || textMode()) return false;
    Settings s = profile.st;
    if (Keys.is(s.keyMenu, code) && !isOk(code)) {
      openMainMenuFromKey();
      return true;
    }
    if (mode == GUIDE && (Keys.is(s.keyGuide, code) || code == KeyEvent.KEYCODE_GUIDE
        || code == KeyEvent.KEYCODE_F1 || code == KeyEvent.KEYCODE_G)) {
      closeGuide();
      return true;
    }
    if (mode == LIVE || mode == PAD) {
      if (Keys.is(s.keyMute, code) || code == KeyEvent.KEYCODE_M) {
        if (player != null) player.toggleMute();
        return true;
      }
      if (Keys.is(s.keyFav, code) && !isOk(code)) {
        if (player != null) {
          profile.toggleFav(player.index);
          note(profile.isFav(player.index) ? I18n.t("favAdd") : I18n.t("favDel"), 1200);
        }
        return true;
      }
      if (Keys.is(s.keyFs, code) && !isOk(code)) {
        if (host != null) host.toggleFullscreen();
        return true;
      }
      if (Keys.is(s.keyGuide, code)) {
        if (mode == GUIDE) closeGuide();
        else openGuide();
        return true;
      }
      if (Keys.is(s.keySearch, code) && !isVoiceKey(code)) {
        setMode(SEARCH);
        return true;
      }
      if (Keys.is(s.keyRec, code)) {
        runPad(9);
        return true;
      }
      if (Keys.is(s.keyPip, code)) {
        doPip();
        return true;
      }
      if (Keys.is(s.keyVoice, code)) {
        if (host != null) host.startVoice();
        return true;
      }
    }
    if (mode == GUIDE && Keys.is(s.keyRec, code)) {
      guideOk();
      return true;
    }
    return false;
  }

  private void openMainMenuFromKey() {
    if (mode == GUIDE && groupI == 1) { openChMenu(); return; }
    resumeGuide = mode == GUIDE;
    menuI = 0;
    setMode(MENU);
  }

  private void openChMenu() {
    chmBack = mode;
    chmI = 0;
    chmCh = mode == GUIDE ? channelAt() : (player != null ? player.index : -1);
    chmN = "favorites".equals(groupIdOf(groupI)) ? 2 : 1;
    if (mode == GUIDE) {
      chmOn = true;
      invalidate();
      return;
    }
    switchTo(CHMENU);
  }

  private void closeChMenu() {
    chmOn = false;
    if (mode == GUIDE) {
      invalidate();
      return;
    }
    int back = chmBack;
    chmBack = LIVE;
    if (back == GUIDE) switchTo(GUIDE);
    else if (back == MULTI) switchTo(MULTI);
    else if (back == PAD) setMode(PAD);
    else setMode(LIVE);
  }

  private boolean chmOpen() {
    return mode == CHMENU || (mode == GUIDE && chmOn);
  }

  private void runChMenu() {
    int ch = chmCh >= 0 ? chmCh : (player != null ? player.index : -1);
    if (chmI == 0) {
      profile.toggleFav(ch);
      note(profile.isFav(ch) ? I18n.t("favAdd") : I18n.t("favDel"), 1200);
      closeChMenu();
    } else {
      chmOn = false;
      startFavMove(ch);
    }
  }

  /** Правая кнопка мыши / гиропульта: те же опции, что удержание ОК (избранное, копировать/вставить). */
  private boolean openContext(Hit hh) {
    int k = hh == null ? 0 : hh.kind;
    int i = hh == null ? -1 : hh.idx;
    if (mode == URL) {
      urlI = urlText == null || urlText.isEmpty() ? 1 : 2;
      invalidate();
      return true;
    }
    if (mode == SEARCH) {
      searchI = -1;
      searchBtn = (searchQ == null || searchQ.isEmpty()) ? 1 : 2;
      invalidate();
      return true;
    }
    if (mode == SETTINGS && k == H_ROW) {
      setPane = 1;
      setI = i;
      lRows.lastSel = i;
      fillRows();
      int id = (setI >= 0 && setI < rows.size()) ? rows.get(setI).id : -1;
      if ((id >= 1000 && id < 1132) || (id >= 3000 && id < 3400) || (id >= 6000 && id < 6500)) activateRow();
      invalidate();
      return true;
    }
    if (mode == GUIDE) {
      if (k == H_GCH) {
        focusCol = COL_C;
        chI = i;
        lChan.lastSel = i;
      }
      openChMenu();
      return true;
    }
    if (mode == LIVE || mode == PAD) {
      openChMenu();
      return true;
    }
    return true;
  }

  private void volNote() {
    volPct = player == null ? 0 : player.volume();
    volUntil = SystemClock.uptimeMillis() + 1800;
    invalidate();
  }

  private void drawVol(Canvas c, int w, int h) {
    int pct = Math.max(0, Math.min(100, volPct));
    String lab = pct + "%";
    float pad = 6f;
    float barW = 2f;
    float trackH = dp(144);
    text.setTextSize(dp(14));
    float tw = text.measureText(lab);
    float th = dp(18);
    float boxW = Math.max(barW + pad * 2, tw + pad * 2);
    float boxH = pad + trackH + pad + th + pad;
    float x = w - boxW - dp(24);
    float y = (h - boxH) / 2f;
    if (y < dp(48)) y = dp(48);
    rf.set(x, y, x + boxW, y + boxH);
    fill.setColor(Color.WHITE);
    c.drawRoundRect(rf, 3, 3, fill);
    float trackX = x + (boxW - barW) / 2f;
    float trackTop = y + pad;
    float fillH = trackH * pct / 100f;
    fill.setColor(accent());
    c.drawRect(trackX, trackTop + trackH - fillH, trackX + barW, trackTop + trackH, fill);
    text.setColor(Color.rgb(30, 34, 42));
    c.drawText(lab, x + (boxW - tw) / 2f, y + pad + trackH + pad + th - dp(4), text);
  }

  boolean onMotion(MotionEvent ev) {
    if (ev == null) return false;
    int src = ev.getSource();
    int act = ev.getActionMasked();
    if (act == MotionEvent.ACTION_BUTTON_PRESS) return onTouchEvent(ev);
    if (act == MotionEvent.ACTION_SCROLL && (src & InputDevice.SOURCE_CLASS_POINTER) != 0) {
      float v = ev.getAxisValue(MotionEvent.AXIS_VSCROLL), hs = ev.getAxisValue(MotionEvent.AXIS_HSCROLL);
      /* на сенсорном устройстве колёсико мыши прокручивает список под курсором */
      if (v != 0 && Ui.touch() && wheel(ev)) return true;
      /* иначе колёсико — стрелки D-pad: вращение вверх = «вверх», вниз = «вниз»; горизонтальное — влево/вправо */
      if (v != 0) return dpad(0, v > 0 ? -1 : 1);
      if (hs != 0) return dpad(hs < 0 ? -1 : 1, 0);
      return false;
    }
    boolean stick = (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        || (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        || (src & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
    if (!stick) return false;
    if (act != MotionEvent.ACTION_MOVE) return false;
    int hx = Math.round(Math.signum(ev.getAxisValue(MotionEvent.AXIS_HAT_X)));
    int hy = Math.round(Math.signum(ev.getAxisValue(MotionEvent.AXIS_HAT_Y)));
    boolean used = false;
    if (hx != lastHatX && hx != 0) used |= dpad(hx, 0);
    if (hy != lastHatY && hy != 0) used |= dpad(0, hy);
    lastHatX = hx;
    lastHatY = hy;
    /* левый стик (AXIS_X / AXIS_Y): отклонение за порог — шаг, удержание — повтор */
    int sx = stickDir(ev.getAxisValue(MotionEvent.AXIS_X), stickX);
    int sy = stickDir(ev.getAxisValue(MotionEvent.AXIS_Y), stickY);
    long now = SystemClock.uptimeMillis();
    boolean horizontal = Math.abs(ev.getAxisValue(MotionEvent.AXIS_X)) > Math.abs(ev.getAxisValue(MotionEvent.AXIS_Y));
    int dx = horizontal ? sx : 0, dy = horizontal ? 0 : sy;
    if (dx == 0 && dy == 0) {
      stickNext = 0;
    } else {
      boolean changed = dx != stickX || dy != stickY;
      if (changed || now >= stickNext) {
        used |= dpad(dx, dy);
        stickNext = now + (changed ? 380 : 150);
      }
    }
    stickX = dx;
    stickY = dy;
    return used;
  }

  private int stickX, stickY;
  private long stickNext;

  /** Направление оси с гистерезисом: включается за 0,6, отпускается ниже 0,35. */
  private static int stickDir(float v, int prev) {
    float a = Math.abs(v);
    if (a >= 0.6f) return v < 0 ? -1 : 1;
    if (a < 0.35f) return 0;
    return prev;
  }

  /**
   * Шаг как у стрелок D-pad (dx, dy = −1/0/1). В эфире вверх/вниз переключают канал, влево — гид, вправо — прошлый канал.
   * Ввод колёсиком, стиком и крестовиной переводит интерфейс в фокусный вид (выделена строка, а не касание).
   */
  private boolean dpad(int dx, int dy) {
    if (Ui.onKeyInput()) invalidate();
    boolean used = true;
    if (dx < 0) onLeft();
    else if (dx > 0) onRight();
    else if (dy < 0) { if (mode == LIVE) zap(-1); else onUp(); }
    else if (dy > 0) { if (mode == LIVE) zap(1); else onDown(); }
    else used = false;
    return used;
  }

  /**
   * Запасной ввод символа, когда скрытое поле не в фокусе: буква через {@link KeyEvent#getUnicodeChar},
   * цифра нумпада, пробел. Пин-код принимает только цифры. Backspace (KEYCODE_DEL) — выше, в onKey.
   */
  private boolean textKey(KeyEvent e) {
    if (e.getAction() != KeyEvent.ACTION_DOWN) return false;
    if (e.isCtrlPressed() || e.isAltPressed() || e.isMetaPressed()) return false;
    int code = e.getKeyCode();
    int ch = 0;
    if (code >= KeyEvent.KEYCODE_NUMPAD_0 && code <= KeyEvent.KEYCODE_NUMPAD_9) ch = '0' + (code - KeyEvent.KEYCODE_NUMPAD_0);
    else if (code == KeyEvent.KEYCODE_NUMPAD_DOT) ch = '.';
    else if (e.isPrintingKey()) ch = e.getUnicodeChar(e.getMetaState());
    else if (code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z) {
      ch = 'a' + (code - KeyEvent.KEYCODE_A);
      if (e.isShiftPressed()) ch = Character.toUpperCase(ch);
    } else if (code == KeyEvent.KEYCODE_SPACE) ch = ' ';
    if (ch <= 0 || (ch & KeyCharacterMap.COMBINING_ACCENT) != 0) return false;
    boolean digit = ch >= '0' && ch <= '9';
    boolean pin = mode == URL && urlKind == U_PIN;
    if (pin && !digit) return true;
    String cur = currentText();
    int max = mode == SEARCH ? 120 : pin ? 8 : (mode == URL && urlKind == U_CLOUD) ? 65536 : 4096;
    if (cur == null) cur = "";
    if (cur.length() >= max) return true;
    onKbText(cur + new String(Character.toChars(ch)));
    syncKb();
    return true;
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) {
    return onKey(keyCode, event) || super.onKeyDown(keyCode, event);
  }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) {
    return onKey(keyCode, event) || super.onKeyUp(keyCode, event);
  }

  private boolean onOk(KeyEvent e) {
    if (mode == LIVE) {
      holdPad = true;
      holdAt = SystemClock.uptimeMillis();
      return true;
    }
    if (mode == PAD) {
      if (padFocus == PAD_BUTTONS) {
        if (padI == 7) {
          /* «Звук»: короткое ОК — дорожка звука, удержание 550 мс — субтитры (решается на отпускании) */
          holdSub = true;
          holdAt = SystemClock.uptimeMillis();
          return true;
        }
        runPad(padI);
        return true;
      }
      setMode(LIVE);
      return true;
    }
    if (mode == MENU) { runMenu(menuI); return true; }
    if (mode == SETTINGS) {
      if (setPane == 0) { setPane = 1; setI = 0; invalidate(); return true; }
      activateRow(); return true;
    }
    if (chmOpen()) {
      runChMenu();
      return true;
    }
    if (mode == GUIDE) {
      if (favMoving) { commitFavMove(); return true; }
      if (focusCol == COL_C && !inGroup.isEmpty()) {
        holdChan = true;
        holdAt = SystemClock.uptimeMillis();
        return true;
      }
      return guideOk();
    }
    if (mode == EXIT) {
      if (exitI == 0) host.finish();
      else setMode(LIVE);
      return true;
    }
    if (mode == URL) {
      switch (urlI) {
        case 0:
          if (imeH > 0) urlSubmit();
          else kbOpen();
          break;
        case 1: doPaste(); break;
        case 2: doCopy(); break;
        default: urlSubmit(); break;
      }
      return true;
    }
    if (mode == SEARCH) {
      if (searchI < 0) {
        if (searchBtn == 0) kbOpen();
        else if (searchBtn == 3) searchMic();
        else if (searchBtn == 1) doPaste();
        else doCopy();
        return true;
      }
      if (searchI >= 0 && searchI < searchHits.size()) {
        int ch = searchHits.get(searchI);
        if (mvPick != 0) applyMvPick(ch);
        else zapTo(ch);
      }
      return true;
    }
    if (mode == HISTORY) {
      int n = profile.st.recent.size();
      if (histI == n) {
        profile.st.recent.clear();
        profile.st.recentIds.clear();
        profile.saveSession();
        histI = 0;
        invalidate();
        return true;
      }
      if (histI >= 0 && histI < n) {
        if (histX == 1) {
          profile.st.recent.remove(histI);
          if (histI < profile.st.recentIds.size()) profile.st.recentIds.remove(histI);
          profile.saveSession();
          if (histI >= profile.st.recent.size()) histI = Math.max(0, profile.st.recent.size() - 1);
          histX = 0;
          invalidate();
        } else zapTo(profile.st.recent.get(histI));
      }
      return true;
    }
    if (mode == RECS) {
      List<Rec.Item> files = rec.files();
      if (recI >= 0 && recI < files.size()) host.playFile(files.get(recI).uri, true);
      return true;
    }
    if (mode == MICPERM) return micPermOk();
    if (mode == FILEBROWSE) return browseOk();
    if (mode == MULTI) return multiOk();
    if (mode == PIN) { submitPin(); return true; }
    if (mode == SETUP) return setupOk();
    if (mode == SHARE) return shareOk();
    if (mode == ASK) return askOk();
    return true;
  }

  private boolean onLeft() {
    if (mode == EXIT) { exitI = 1 - exitI; invalidate(); return true; }
    if (mode == LIVE) { openGuide(); return true; }
    if (mode == PAD) {
      if (padFocus == PAD_BAR) { padNudge(-15); keepPad(); return true; }
      if (padFocus == PAD_NONE) return true;
      padI = (padI + 9) % 10;
      keepPad();
      return true;
    }
    if (mode == MENU) { exitI = 0; setMode(EXIT); return true; }
    if (mode == SETTINGS && setPane == 1) { setPane = 0; invalidate(); return true; }
    if (mode == SETTINGS) { setMode(MENU); return true; }
    if (chmOpen()) return true;
    if (mode == GUIDE) return guideLeft();
    if (mode == MULTI && mvMenu == 1) { mvMenu = 0; invalidate(); return true; }
    if (mode == MULTI) { mvMove(-1); return true; }
    if (mode == HISTORY) { histX = 0; setMode(MENU); return true; }
    if (mode == SEARCH && searchI < 0) { searchStep(-1); return true; }
    if (mode == URL) {
      if (urlI >= 1) { urlI = urlI == 1 ? 3 : urlI - 1; invalidate(); return true; }
      return goBack();
    }
    return goBack();
  }

  private boolean onRight() {
    if (mode == EXIT) { exitI = 1 - exitI; invalidate(); return true; }
    if (mode == LIVE) { if (player.prev >= 0) zapTo(player.prev); return true; }
    if (mode == PAD) {
      if (padFocus == PAD_BAR) { padNudge(15); keepPad(); return true; }
      if (padFocus == PAD_NONE) return true;
      padI = (padI + 1) % 10;
      keepPad();
      return true;
    }
    if (mode == MENU) return goBack();
    if (mode == SETTINGS && setPane == 0) { setPane = 1; invalidate(); return true; }
    if (chmOpen()) return onOk(null);
    if (mode == GUIDE) return guideRight();
    if (mode == MULTI && mvMenu == 0) { mvMove(1); return true; }
    if (mode == HISTORY) { histX = 1; invalidate(); return true; }
    if (mode == SEARCH && searchI < 0) { searchStep(1); return true; }
    if (mode == URL) {
      if (urlI >= 1) { urlI = urlI == 3 ? 1 : urlI + 1; invalidate(); return true; }
      return true;
    }
    return true;
  }

  private boolean onUp() {
    if (mode == MENU) { menuI = (menuI + 9) % 10; invalidate(); return true; }
    if (mode == SETTINGS && setPane == 0) { setTab = (setTab + TAB_ID.length - 1) % TAB_ID.length; invalidate(); return true; }
    if (mode == SETTINGS) { setI = Math.max(0, setI - 1); invalidate(); return true; }
    if (chmOpen()) {
      int n = Math.max(1, chmN);
      chmI = (chmI + n - 1) % n;
      invalidate();
      return true;
    }
    if (mode == GUIDE && favMoving) { moveFav(-1); return true; }
    if (mode == GUIDE) return guideMove(-1);
    if (mode == PAD) {
      if (padFocus == PAD_BUTTONS && padTwoRows() && padI >= 5) padI -= 5;
      else if (padFocus == PAD_BAR) padFocus = PAD_BUTTONS;
      else if (padFocus == PAD_BUTTONS) padFocus = PAD_NONE;
      keepPad();
      return true;
    }
    if (mode == SEARCH) {
      if (searchI > 0) searchI--;
      else if (searchI == 0) { searchI = -1; searchBtn = 0; }
      invalidate();
      return true;
    }
    if (mode == URL) { urlI = 0; invalidate(); return true; }
    if (mode == HISTORY) { histI = Math.max(0, histI - 1); invalidate(); return true; }
    if (mode == RECS) { recI = Math.max(0, recI - 1); invalidate(); return true; }
    if (mode == SETUP) { setupI = (setupI + 3) % 4; invalidate(); return true; }
    if (mode == SHARE) { shareI = (shareI + 4) % 5; invalidate(); return true; }
    if (mode == ASK) { askI = 1 - askI; invalidate(); return true; }
    if (mode == MICPERM) { micI = (micI + 2) % 3; invalidate(); return true; }
    if (mode == FILEBROWSE) { browseI = Math.max(0, browseI - 1); invalidate(); return true; }
    if (mode == MULTI) {
      if (mvMenu == 1) { mvI = (mvI + 3) % 4; invalidate(); return true; }
      mvFocus = (mvFocus + mvN - 1) % Math.max(1, mvN);
      player.focusPane(mvFocus);
      host.layoutTiles(mvN, mvFocus, (int) dp(52));
      invalidate();
      return true;
    }
    return true;
  }

  private boolean onDown() {
    if (mode == MENU) { menuI = (menuI + 1) % 10; invalidate(); return true; }
    if (mode == SETTINGS && setPane == 0) { setTab = (setTab + 1) % TAB_ID.length; invalidate(); return true; }
    if (mode == SETTINGS) { fillRows(); setI = Math.min(rows.size() - 1, setI + 1); invalidate(); return true; }
    if (chmOpen()) {
      int n = Math.max(1, chmN);
      chmI = (chmI + 1) % n;
      invalidate();
      return true;
    }
    if (mode == GUIDE && favMoving) { moveFav(1); return true; }
    if (mode == GUIDE) return guideMove(1);
    if (mode == PAD) {
      if (padFocus == PAD_NONE) padFocus = PAD_BUTTONS;
      else if (padFocus == PAD_BUTTONS && padTwoRows() && padI < 5) padI += 5;
      else if (padFocus == PAD_BUTTONS) padFocus = PAD_BAR;
      keepPad();
      return true;
    }
    if (mode == SEARCH) {
      if (searchI < 0) { if (!searchHits.isEmpty()) searchI = 0; }
      else searchI = Math.min(searchHits.size() - 1, searchI + 1);
      invalidate();
      return true;
    }
    if (mode == URL) { if (urlI == 0) urlI = 1; invalidate(); return true; }
    if (mode == HISTORY) {
      histI = Math.min(profile.st.recent.size(), histI + 1);
      invalidate();
      return true;
    }
    if (mode == RECS) { recI = Math.min(Math.max(0, rec.files().size() - 1), recI + 1); invalidate(); return true; }
    if (mode == SETUP) { setupI = (setupI + 1) % 4; invalidate(); return true; }
    if (mode == SHARE) { shareI = (shareI + 1) % 5; invalidate(); return true; }
    if (mode == ASK) { askI = 1 - askI; invalidate(); return true; }
    if (mode == MICPERM) { micI = (micI + 1) % 3; invalidate(); return true; }
    if (mode == FILEBROWSE) { browseI = Math.min(browseList.size() - 1, browseI + 1); invalidate(); return true; }
    if (mode == MULTI) {
      if (mvMenu == 1) { mvI = (mvI + 1) % 4; invalidate(); return true; }
      mvFocus = (mvFocus + 1) % Math.max(1, mvN);
      player.focusPane(mvFocus);
      host.layoutTiles(mvN, mvFocus, (int) dp(52));
      invalidate();
      return true;
    }
    return true;
  }

  boolean goBack() {
    long now = SystemClock.uptimeMillis();
    if (now - lastBack < 250) return true;
    lastBack = now;
    if (waitKey > 0) {
      waitKey = 0;
      note(I18n.t("cancel"), 1200);
      invalidate();
      return true;
    }
    if (favMoving) {
      cancelFavMove();
      return true;
    }
    if (note.length() > 0 && mode != GUIDE && mode != MENU) { note = ""; invalidate(); return true; }
    if (mode == LIVE && overlayPip) {
      overlayPip = false;
      host.layoutPip(false);
      note(I18n.t("pipOff"), 1200);
      return true;
    }
    if (mode == EXIT) { host.finish(); return true; }
    if (chmOpen()) {
      closeChMenu();
      return true;
    }
    if (mode == GUIDE) {
      holdChan = false;
      return guideBack();
    }
    if (mode == PAD && burgerOpen) { burgerOpen = false; invalidate(); return true; }
    if (mode == URL) { setMode(urlBack); return true; }
    if (mode == SEARCH && mvPick != 0) {
      mvPick = 0;
      switchTo(MULTI);
      return true;
    }
    if (mode == MULTI) { closeMulti(); return true; }
    if (mode == PAD || mode == SEARCH || mode == HISTORY
        || mode == RECS || mode == SHARE || mode == ASK || mode == PIN) {
      setMode(LIVE);
      return true;
    }
    if (mode == SETTINGS) {
      if (setPane == 1 && settingsNarrow()) { setPane = 0; invalidate(); return true; }
      setMode(MENU);
      return true;
    }
    if (mode == MENU) {
      if (resumeGuide) {
        resumeGuide = false;
        guideView = GV_GROUPS;
        focusCol = COL_G;
        setMode(GUIDE);
        return true;
      }
      setMode(LIVE);
      return true;
    }
    if (mode == SETUP) {
      if (profile.pl.isEmpty()) { setMode(MENU); return true; }
      setMode(LIVE);
      return true;
    }
    if (mode == FILEBROWSE) { setMode(SETTINGS); return true; }
    if (mode == MICPERM) { setMode(LIVE); return true; }
    if (player != null && player.playingFile) {
      if (player.fromRecs) { player.goLive(); setMode(RECS); }
      else { player.goLive(); setMode(LIVE); }
      return true;
    }
    if (profile.st.confirmExit) { exitI = 0; setMode(EXIT); return true; }
    host.finish();
    return true;
  }

  private void openSettingsTab(int tab) {
    pendingSetTab = tab;
    if (profile.st.parentalEnabled && profile.st.lockSettings && !pinOk) {
      setMode(PIN);
      return;
    }
    setTab = tab;
    setI = 0;
    if (settingsNarrow()) setPane = 1;
    setMode(SETTINGS);
  }

  private void runMenu(int i) {
    switch (i) {
      case 0: resumeGuide = false; setMode(LIVE); break;
      case 1: openSettingsTab(1); break;
      case 2: setMode(RECS); break;
      case 3: openMulti(); break;
      case 4: setMode(SEARCH); break;
      case 5:
        profile.st.voiceEnabled = !profile.st.voiceEnabled;
        profile.saveSettings();
        note(I18n.t(profile.st.voiceEnabled ? "micOn" : "micOff"), 1600);
        invalidate();
        break;
      case 6: openSettingsTab(0); break;
      case 7: setMode(HISTORY); break;
      case 8: openSettingsTab(2); break;
      case 9: host.finish(); break;
    }
  }

  private void runPad(int i) {
    keepPad();
    switch (i) {
      case 0: resumeGuide = false; setMode(MENU); break;
      case 1: openGuide(); break;
      case 2: startVoiceSearch(); break;
      case 3: padNudge(-30); keepPad(); break;
      case 4: player.pauseToggle(); keepPad(); break;
      case 5: padNudge(30); keepPad(); break;
      case 6:
        if (player.isArchive()) { rec.onZap(); player.goLive(); setMode(LIVE); }
        else note(I18n.t("liveAir"), 1200);
        break;
      case 7: player.cycleAudio(); break;
      case 8: doPip(); break;
      case 9:
        if (!profile.st.recordingsEnabled) { note(I18n.t("recOff"), 1600); break; }
        if (rec.liveActive()) { rec.stopLive(); note(I18n.t("recStopped"), 1600); break; }
        if (profile.st.recPath.isEmpty()) { askWhy = 0; setMode(ASK); }
        else note(rec.startLive(player.index), 1600);
        break;
    }
  }

  void startVoiceSearch() {
    if (!profile.st.voiceEnabled) {
      note(I18n.t("voiceOffMenu"), 2200);
      searchQ = I18n.t("voiceOffMenu");
      setMode(SEARCH);
      return;
    }
    if (host != null && !host.hasMicPermission()) {
      setMode(MICPERM);
      return;
    }
    if (mode == SEARCH) {
      searchMic();
      return;
    }
    if (mode == MULTI) {
      mvPick = 1;
      searchQ = "";
      fillSearch();
    } else {
      searchQ = "";
      setMode(SEARCH);
    }
    if (host != null) host.startVoice();
  }

  private boolean micPermOk() {
    if (micI == 0) {
      if (host != null) host.requestMic();
    } else if (micI == 1) {
      if (host != null) host.openAppSettings();
    } else {
      profile.st.voiceEnabled = false;
      profile.saveSettings();
      note(I18n.t("micOff"), 1600);
      setMode(LIVE);
    }
    return true;
  }

  void openBrowse(int why, File dir) {
    browseWhy = why;
    browseDir = dir == null ? new File("/storage") : dir;
    browseList.clear();
    if (why == 1) browseList.add(browseDir);
    File[] kids = browseDir.listFiles();
    if (browseDir.getParentFile() != null) browseList.add(browseDir.getParentFile());
    if (kids != null) {
      java.util.Arrays.sort(kids, (a, b) -> {
        if (a.isDirectory() != b.isDirectory()) return a.isDirectory() ? -1 : 1;
        return a.getName().compareToIgnoreCase(b.getName());
      });
      for (File f : kids) {
        if (f.getName().startsWith(".")) continue;
        if (why == 0 && f.isFile() && !f.getName().toLowerCase(Locale.ROOT).endsWith(".m3u")
            && !f.getName().toLowerCase(Locale.ROOT).endsWith(".m3u8")) continue;
        if (why == 1 && f.isFile()) continue;
        if (why == 2 && f.isFile() && !f.getName().toLowerCase(Locale.ROOT).endsWith(".json")) continue;
        browseList.add(f);
      }
    }
    browseI = 0;
    setMode(FILEBROWSE);
  }

  private boolean browseOk() {
    if (browseI < 0 || browseI >= browseList.size()) return true;
    File f = browseList.get(browseI);
    if (browseWhy == 1 && f.equals(browseDir)) {
      File out = new File(browseDir, "mirefir-backup.json");
      boolean ok = profile.exportBackup(out) != null;
      note(ok ? I18n.t("backupSaved") + ": " + out.getAbsolutePath() : I18n.t("backupFail"), 2200);
      setMode(SETTINGS);
      return true;
    }
    if (f.isDirectory()) {
      openBrowse(browseWhy, f);
      return true;
    }
    if (browseWhy == 2) {
      if (host != null) host.importBackupFile(f);
      else {
        note(profile.importBackup(Util.read(f)) ? I18n.t("backupLoaded") : I18n.t("backupFail"), 1600);
        if (profile.pl != null && !profile.pl.isEmpty()) bootLive();
      }
      return true;
    }
    if (browseWhy == 0) {
      String text = Util.read(f);
      if (text == null || !profile.addLocalM3u(f.getAbsolutePath(), text)) note(I18n.t("plFail"), 1600);
      else {
        note(I18n.t("plAdded"), 1600);
        bootLive();
      }
    }
    return true;
  }

  private void drawBrowse(Canvas c, int w, int h) {
    float mw = panelW(w, dp(480));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(20));
    text.setColor(Color.WHITE);
    String title = browseDir == null ? I18n.t("setAddM3u") : browseDir.getAbsolutePath();
    c.drawText(TextUtils.ellipsize(title, text, mw - dp(80), TextUtils.TruncateAt.END).toString(), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    float rowH = Ui.touch() ? dp(50) : dp(44), top = dp(70);
    if (browseList.isEmpty()) {
      text.setTextSize(ts(16));
      c.drawText(I18n.t("fileNone"), dp(24), dp(100), text);
      return;
    }
    listBegin(c, lHist, 0, top, mw, h, rowH, browseList.size(), browseI);
    for (int i = firstRow(lHist); i < browseList.size(); i++) {
      float y = top + (i - lHist.off) * rowH;
      if (y >= h) break;
      if (i == browseI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + rowH - dp(2), fill);
      }
      File f = browseList.get(i);
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      String lab;
      if (browseWhy == 1 && f.equals(browseDir)) lab = I18n.t("browseSaveHere");
      else if (f.isDirectory() && f.equals(browseDir.getParentFile())) lab = "▸ ..";
      else lab = (f.isDirectory() ? "▸ " : "") + f.getName();
      c.drawText(TextUtils.ellipsize(lab, text, mw - dp(40), TextUtils.TruncateAt.END).toString(), dp(24), y + rowH * 0.64f, text);
      hit(0, Math.max(y, top), mw, Math.min(y + rowH, h), H_LIST, i);
    }
    listEnd(c, lHist, mw, top, h);
  }

  private void drawMicPerm(Canvas c, int w, int h) {
    String[] rows = {I18n.t("micAllow"), I18n.t("micSettings"), I18n.t("micDisable")};
    float mw = panelW(w, dp(420));
    fill.setColor(panelBg());
    bg(c, 0, 0, mw, h);
    hit(0, 0, mw, h, H_PANEL, 0);
    text.setTextSize(ts(20));
    text.setColor(Color.WHITE);
    c.drawText(I18n.t("voiceNeedMic"), dp(24), dp(48), text);
    drawClose(c, mw, 0);
    for (int i = 0; i < rows.length; i++) {
      float y = dp(80) + i * dp(58);
      if (i == micI && fv()) {
        fill.setColor(accent());
        c.drawRect(0, y, mw, y + dp(54), fill);
      }
      text.setTextSize(font());
      text.setColor(Color.WHITE);
      c.drawText(rows[i], dp(24), y + dp(34), text);
      hit(0, y, mw, y + dp(54), H_ASK, i);
    }
  }

  private void doPip() {
    if (mode == MULTI) {
      closeMulti();
      return;
    }
    openMulti();
  }

  private void openMulti() {
    int cur = player != null ? player.index : -1;
    if (cur < 0) {
      note(I18n.t("pipFail"), 1600);
      return;
    }
    overlayPip = false;
    host.layoutPip(false);
    mvN = 1;
    mvI = 0;
    mvFocus = 0;
    mvMenu = 0;
    mvPick = 0;
    mvCh[0] = cur;
    mvCh[1] = mvCh[2] = mvCh[3] = -1;
    player.setPaused(false);
    player.setMultiview(1, mvCh);
    host.layoutTiles(1, 0, (int) dp(52));
    setMode(MULTI);
  }

  private void closeMulti() {
    int idx = mvCh[0] >= 0 ? mvCh[0] : (player != null ? player.index : -1);
    if (epg != null) epg.hold(false);
    player.endMultiview();
    mvN = 1;
    mvMenu = 0;
    mvPick = 0;
    mvFocus = 0;
    host.layoutTiles(1, 0, 0);
    player.setPaused(false);
    if (idx >= 0) player.play(idx, 4);
    setMode(LIVE);
  }

  private boolean playlistUrlOk(String url) {
    if (url == null) return false;
    url = url.trim();
    return Util.looksUrl(url) || url.contains(".m3u");
  }

  private void addPlaylistInBg(String url, Runnable onOk) {
    if (!playlistUrlOk(url)) {
      note(I18n.t("noClipUrl"), 1600);
      return;
    }
    note(I18n.t("plLoading"), 2500);
    final String fetch = url.trim();
    netExec.execute(() -> {
      boolean ok = profile.addPlaylistUrl(fetch);
      post(() -> {
        if (!ok) note(I18n.t("plFail"), 1600);
        else {
          note(I18n.t("plOn"), 1600);
          if (onOk != null) onOk.run();
        }
      });
    });
  }

  /**
   * Ссылка пришла из другого приложения («Поделиться», открыть http(s)/.m3u): открывается редактор
   * ссылки плейлиста с готовым адресом, ОК подтверждает. Адрес в журнал не пишется.
   */
  void incomingUrl(String url) {
    if (url == null || url.trim().isEmpty()) return;
    openUrlEditor(U_PL, profile.pl.isEmpty() ? SETUP : SETTINGS);
    urlText = Util.clip(url.trim(), 4096);
    syncKb();
    invalidate();
  }

  /** Поворот, складной экран, DeX: пересчитать окна мультиэкрана и перерисовать с новыми отступами. */
  void onConfig() {
    if (mvN > 1 && host != null) host.layoutTiles(mvN, mvFocus, (int) dp(52));
    requestApplyInsets();
    invalidate();
  }

  private void applyCloudInBg(String code) {
    if (code == null || code.trim().isEmpty()) {
      note(I18n.t("codeBad"), 1600);
      return;
    }
    note(I18n.t("plLoading"), 2500);
    netExec.execute(() -> {
      boolean ok = Cloud.apply(profile, code);
      post(() -> {
        if (ok) {
          note(I18n.t("codeOk"), 1600);
          bootLive();
        } else note(I18n.t("codeBad"), 1600);
      });
    });
  }

  private boolean setupOk() {
    if (setupI == 0) {
      note(I18n.t("setupLoading"), 2500);
      openUrlEditor(U_PL, SETUP);
    } else if (setupI == 1) host.pickM3uOrBrowse();
    else if (setupI == 2) {
      if (profile.pl.isEmpty()) { note(I18n.t("setupNeed"), 1600); return true; }
      openUrlEditor(U_EPG1, SETUP);
    } else setMode(MENU);
    return true;
  }

  void bootLive() {
    rebuildGroups();
    int i = M3u.findName(profile.pl, profile.st.lastName);
    if (i < 0) i = profile.st.lastIndex;
    if (i < 0 || i >= profile.pl.size()) i = 0;
    applyPlayingGroup(i);
    setMode(LIVE);
    if (!profile.pl.isEmpty() && profile.st.autoplay) zapTo(i);
  }

  private boolean shareOk() {
    if (host == null) return true;
    if (shareFile) host.shareBackup(shareI);
    else host.shareCode(shareI);
    setMode(SETTINGS);
    return true;
  }

  private boolean askOk() {
    if (askI == 0) {
      if (askWhy == 0) {
        profile.st.recPath = profile.defaultRecordingsDir().getAbsolutePath();
        profile.st.recordingsEnabled = true;
        profile.saveSettings();
        String err = rec.pathError();
        if (err != null) note(err, 1800);
        else note(rec.startLive(player.index), 1600);
        setMode(LIVE);
      } else if (askWhy == 1) {
        File f = new File(profile.backupsDir(), "mirefir-" + System.currentTimeMillis() + ".json");
        note(profile.exportBackup(f) != null ? I18n.t("backupSaved") : I18n.t("backupFail"), 1600);
        setMode(SETTINGS);
      } else {
        File last = newestJson(profile.backupsDir());
        if (last == null) last = newestJson(new File("/storage/emulated/0/Download/MAX"));
        if (last == null) last = newestJson(new File("/storage/emulated/0/Download"));
        if (last != null && profile.importBackup(Util.read(last))) {
          note(I18n.t("backupLoaded"), 1600);
          bootLive();
        } else note(I18n.t("backupFail"), 1600);
      }
    } else {
      startRecAfterFolder = askWhy == 0;
      host.pickFolder(askWhy);
    }
    return true;
  }

  private static File newestJson(File dir) {
    if (dir == null || !dir.isDirectory()) return null;
    File[] files = dir.listFiles();
    if (files == null) return null;
    File best = null;
    long t = -1;
    for (File f : files) {
      String n = f.getName().toLowerCase();
      if (!n.endsWith(".json")) continue;
      if (!n.contains("mirefir") && !n.contains("backup")) continue;
      if (f.lastModified() >= t) {
        t = f.lastModified();
        best = f;
      }
    }
    return best;
  }

  void onFolderPicked(boolean recFolder) {
    boolean start = recFolder && startRecAfterFolder;
    startRecAfterFolder = false;
    if (start && rec != null && player != null) {
      note(rec.startLive(player.index), 1600);
      setMode(LIVE);
      return;
    }
    setMode(SETTINGS);
  }

  void onFolderCancel(int why) {
    startRecAfterFolder = false;
    if (why == 0) {
      if (profile.st.recPath != null && !profile.st.recPath.isEmpty()) {
        profile.st.recPath = "";
        profile.saveSettings();
        note(I18n.t("folderReset"), 1600);
      } else note(I18n.t("folderNone"), 1600);
      setMode(SETTINGS);
    } else {
      note(I18n.t("folderNone"), 1600);
      setMode(SETTINGS);
    }
  }

  private boolean guideOk() {
    if (focusCol == COL_G) {
      if (!enterFolder()) return true;
      invalidate();
      return true;
    }
    if (focusCol == COL_C) {
      activateChannel();
      return true;
    }
    if (guideI < 0 || guideI >= guideList.size()) {
      int ch0 = channelAt();
      if (ch0 >= 0 && ch0 != player.index) zapTo(ch0);
      else setMode(LIVE);
      return true;
    }
    Programme p = guideList.get(guideI);
    int ch = channelAt();
    if (ch < 0 || ch >= profile.pl.size()) return true;
    Channel c = profile.pl.get(ch);
    long now = System.currentTimeMillis();
    if (guideList.isEmpty()) {
      if (ch != player.index) zapTo(ch);
      else setMode(LIVE);
      return true;
    }
    if (p.start <= now && now < p.end) {
      if (ch != player.index) zapTo(ch);
      else setMode(LIVE);
      return true;
    }
    if (p.start > now - 2000) {
      rec.toggleTimer(ch, p.title, p.start, p.end);
      note(rec.hasTimer(ch, p.start, p.end) ? I18n.t("recTimerOn") : I18n.t("recTimerOff"), 1600);
      return true;
    }
    if (!Catchup.programOk(c, profile.st, p.start, p.end)) { note(I18n.t("archiveOff"), 1600); return true; }
    List<String> urls = Catchup.urls(c, p.start, p.end);
    if (urls.isEmpty()) { note(I18n.t("archiveOff"), 1600); return true; }
    player.playArchive(ch, urls, p.start, p.end);
    setMode(LIVE);
    return true;
  }

  private boolean guideLeft() {
    if (guideView == GV_CHANNELS || guideView == GV_SCHEDULE) {
      if (focusCol == COL_DET) focusCol = COL_D;
      else if (focusCol == COL_D) focusCol = COL_P;
      else if (focusCol == COL_P) {
        focusCol = COL_C;
        guideView = GV_CHANNELS;
        daysOpen = false;
      } else {
        guideView = GV_GROUPS;
        focusCol = COL_G;
        daysOpen = false;
      }
      invalidate();
      return true;
    }
    if (guideView == GV_CATEGORIES) {
      guideView = GV_GROUPS;
      focusCol = COL_G;
      invalidate();
      return true;
    }
    if (mvPick != 0) {
      closeGuide();
      return true;
    }
    resumeGuide = true;
    menuI = 0;
    setMode(MENU);
    return true;
  }

  private boolean guideRight() {
    if (guideView == GV_GROUPS) {
      if (!enterFolder()) return true;
    } else if (guideView == GV_CATEGORIES) {
      guideView = GV_CHANNELS;
      focusCol = COL_C;
    } else if (focusCol == COL_C) {
      guideView = GV_SCHEDULE;
      focusCol = COL_P;
      daysOpen = true;
      rebuildDays();
      syncProgToChannel();
    } else if (focusCol == COL_P && daysOpen) {
      focusCol = COL_D;
    } else if (focusCol == COL_D && profile.st.showProgramDesc) {
      focusCol = COL_DET;
    }
    invalidate();
    return true;
  }

  private boolean guideBack() {
    if (focusCol == COL_DET) {
      focusCol = COL_D;
      invalidate();
      return true;
    }
    if (focusCol == COL_D) {
      focusCol = COL_P;
      invalidate();
      return true;
    }
    if (guideView == GV_GROUPS) {
      restoreFolderCursor();
      guideView = GV_CHANNELS;
      focusCol = COL_C;
      daysOpen = false;
      invalidate();
      return true;
    }
    if (guideView == GV_SCHEDULE || focusCol == COL_P) {
      guideView = GV_CHANNELS;
      focusCol = COL_C;
      daysOpen = false;
      invalidate();
      return true;
    }
    if (guideView == GV_CATEGORIES) {
      guideView = GV_GROUPS;
      focusCol = COL_G;
      invalidate();
      return true;
    }
    closeGuide();
    return true;
  }

  private boolean guideMove(int d) {
    if (focusCol == COL_G && !groups.isEmpty()) {
      groupI = (groupI + groups.size() + d) % groups.size();
      previewFolder();
    } else if (focusCol == COL_C && !inGroup.isEmpty()) {
      chI = (chI + inGroup.size() + d) % inGroup.size();
      rebuildDays();
      syncProgToChannel();
    } else if (focusCol == COL_P && !guideList.isEmpty()) {
      guideI = Math.max(0, Math.min(guideList.size() - 1, guideI + d));
      syncDayFromProgram();
    } else if (focusCol == COL_D && !guideDays.isEmpty()) {
      dayI = Math.max(0, Math.min(guideDays.size() - 1, dayI + d));
      jumpToDay();
    }
    invalidate();
    return true;
  }

  private boolean multiOk() {
    if (mvMenu == 0) { mvMenu = 1; mvI = 0; invalidate(); return true; }
    int opt = mvI;
    mvMenu = 0;
    if (opt == 0) {
      mvAddCh(-1);
      applyMultiLayout();
      return true;
    }
    if (opt == 1) {
      boolean empty = false;
      for (int i = 0; i < mvN; i++) if (mvCh[i] < 0) empty = true;
      if (!empty && mvN >= 4) { note(I18n.t("mvMax"), 1200); invalidate(); return true; }
      mvOpenSearch(empty ? 2 : 1);
      if (host != null) host.startVoice();
      return true;
    }
    if (opt == 2) {
      mvOpenGuidePick();
      return true;
    }
    mvRemove(mvFocus);
    if (mode != MULTI) return true;
    applyMultiLayout();
    return true;
  }

  private void mvOpenSearch(int pick) {
    mvPick = pick;
    mvMenu = 0;
    searchQ = "";
    fillSearch();
    switchTo(SEARCH);
  }

  private void mvOpenGuidePick() {
    mvPick = 2;
    mvMenu = 0;
    openGuide();
  }

  private void applyMvPick(int ch) {
    if (ch < 0 || ch >= profile.pl.size()) return;
    if (mvPick == 2) mvSetCh(mvFocus, ch);
    else mvPlaceCh(ch);
    mvPick = 0;
    switchTo(MULTI);
    applyMultiLayout();
  }

  private void mvPlaceCh(int ch) {
    for (int i = 0; i < mvN; i++) {
      if (mvCh[i] < 0) {
        mvSetCh(i, ch);
        return;
      }
    }
    mvAddCh(ch);
  }

  private void mvAddCh(int ch) {
    if (mvN >= 4) {
      note(I18n.t("mvMax"), 1200);
      return;
    }
    if (ch >= 0 && ch >= profile.pl.size()) return;
    mvCh[mvN] = ch;
    mvN++;
    mvFocus = mvN - 1;
    mvMenu = 0;
    mvPick = 0;
  }

  private void mvSetCh(int slot, int ch) {
    if (slot < 0 || slot >= mvN) return;
    if (ch >= 0 && ch >= profile.pl.size()) return;
    mvCh[slot] = ch;
    mvFocus = slot;
    mvMenu = 0;
    if (mode != GUIDE) mvPick = 0;
  }

  private void mvRemove(int slot) {
    if (mvN <= 1) {
      closeMulti();
      return;
    }
    if (slot < 0 || slot >= mvN) return;
    for (int i = slot; i < mvN - 1; i++) mvCh[i] = mvCh[i + 1];
    mvCh[mvN - 1] = -1;
    mvN--;
    if (mvFocus >= mvN) mvFocus = mvN - 1;
    mvMenu = 0;
    mvPick = 0;
  }

  private void mvMove(int step) {
    if (mvN <= 1 || step == 0) return;
    mvFocus = (mvFocus + mvN + step) % mvN;
    player.focusPane(mvFocus);
    host.layoutTiles(mvN, mvFocus, (int) dp(52));
    invalidate();
  }

  private void applyMultiLayout() {
    player.setMultiview(mvN, mvCh);
    player.focusPane(mvFocus);
    if (epg != null) epg.hold(mvN >= 2);
    host.layoutTiles(mvN, mvFocus, (int) dp(52));
    invalidate();
  }

  void onHeard(String text, int channel, boolean zap) {
    voiceNote = "";
    if (mvPick != 0) {
      if (zap && channel >= 0) {
        applyMvPick(channel);
        return;
      }
      searchQ = text == null ? "" : text;
      fillSearch();
      kbSuppress = true;
      switchTo(SEARCH);
      kbSuppress = false;
      syncKb();
      return;
    }
    if (zap && channel >= 0) {
      note(text, 1600);
      zapTo(channel);
      return;
    }
    searchQ = text == null ? "" : text;
    fillSearch();
    kbSuppress = true;
    switchTo(SEARCH);
    kbSuppress = false;
    syncKb();
  }

  private void submitPin() {
    if (pinBuf.equals(profile.st.parentalPin)) {
      pinOk = true;
      pinBuf = "";
      if (pendingGroup >= 0) {
        int g = pendingGroup;
        pendingGroup = -1;
        groupI = g;
        previewFolder();
        guideView = GV_CATEGORIES;
        focusCol = COL_C;
        setMode(GUIDE);
        return;
      }
      openSettingsTab(pendingSetTab);
    } else { note(I18n.t("pinWrong"), 1200); pinBuf = ""; }
  }

  private void openGuide() {
    rebuildGroups();
    int sel = player.index;
    if (mvPick != 0 && mvFocus >= 0 && mvFocus < mvN && mvCh[mvFocus] >= 0) sel = mvCh[mvFocus];
    applyPlayingGroup(sel);
    if (inGroup.isEmpty()) {
      rememberGroup(0);
      fillGroup();
    }
    selectCurrentInGuide();
    rebuildDays();
    guideView = GV_CHANNELS;
    focusCol = COL_C;
    daysOpen = false;
    resumeGuide = false;
    syncProgToChannel();
    setMode(GUIDE);
  }

  private void rememberGroup(int gi) {
    if (gi < 0 || gi >= groups.size()) return;
    groupI = gi;
    profile.st.lastGroup = groupIdOf(gi);
    profile.saveSession();
  }

  private boolean folderLocked(int gi) {
    String gid = groupIdOf(gi);
    String name = gi >= 0 && gi < groups.size() ? groups.get(gi) : "";
    return profile.groupLocked(name) || profile.groupLocked(gid);
  }

  /** Показать каналы папки без подтверждения (lastGroup не трогаем). */
  private void previewFolder() {
    if (folderLocked(groupI)) {
      inGroup.clear();
      guideList.clear();
      return;
    }
    fillGroup();
    lChan.reset();
    int home = groupIndexOfId(profile.st.lastGroup);
    if (groupI == home) selectCurrentInGuide();
    else chI = 0;
    rebuildDays();
    syncProgToChannel();
  }

  /** Вправо/ОК на папке: войти в список каналов, папку не подтверждать. */
  private boolean enterFolder() {
    if (folderLocked(groupI)) {
      pendingGroup = groupI;
      pendingSetTab = -1;
      note(I18n.t("pinGroup"), 1800);
      setMode(PIN);
      return false;
    }
    previewFolder();
    guideView = GV_CATEGORIES;
    focusCol = COL_C;
    return true;
  }

  /** Вернуть папку и канал к подтверждённым (откуда открыли гид / lastGroup). */
  private void restoreFolderCursor() {
    int gi = groupIndexOfId(profile.st.lastGroup);
    if (gi < 0) gi = 0;
    groupI = gi;
    fillGroup();
    selectCurrentInGuide();
    rebuildDays();
    syncProgToChannel();
  }

  private String groupIdOf(int gi) {
    if (gi < 0 || gi >= groups.size()) return "all";
    String n = groups.get(gi);
    if (I18n.t("allChannels").equals(n)) return "all";
    if (I18n.t("favorites").equals(n)) return "favorites";
    if (I18n.t("recent").equals(n)) return "recent";
    return n;
  }

  private int groupIndexOfId(String id) {
    if (id == null || id.isEmpty()) return 0;
    for (int i = 0; i < groups.size(); i++) {
      String n = groups.get(i);
      if (id.equals(n)) return i;
      if ("all".equals(id) && I18n.t("allChannels").equals(n)) return i;
      if ("favorites".equals(id) && I18n.t("favorites").equals(n)) return i;
      if ("recent".equals(id) && I18n.t("recent").equals(n)) return i;
    }
    if ("playlist".equals(id)) return groupIndexOfId("all");
    return 0;
  }

  private void applyPlayingGroup(int ch) {
    String cur = profile.st.lastGroup == null || profile.st.lastGroup.isEmpty() ? "all" : profile.st.lastGroup;
    boolean inFav = profile.isFav(ch);
    boolean inRecent = ch >= 0 && profile.st.recent.contains(ch);
    if ("favorites".equals(cur) && inFav) { rememberGroup(1); fillGroup(); return; }
    if ("recent".equals(cur) && inRecent) { rememberGroup(2); fillGroup(); return; }
    if ("all".equals(cur) || "playlist".equals(cur) || cur.isEmpty() || I18n.t("allChannels").equals(cur)) {
      rememberGroup(0);
      fillGroup();
      return;
    }
    int gi = groupIndexOfId(cur);
    if (gi == 1 && inFav) { rememberGroup(1); fillGroup(); return; }
    if (gi == 2 && inRecent) { rememberGroup(2); fillGroup(); return; }
    if (gi >= 3 && ch >= 0 && ch < profile.pl.size()) {
      String g = profile.pl.get(ch).group;
      if (g == null || g.isEmpty()) g = I18n.t("others");
      if (g.equals(groups.get(gi))) { rememberGroup(gi); fillGroup(); return; }
    }
    if (ch >= 0 && ch < profile.pl.size()) {
      String g = profile.pl.get(ch).group;
      if (g == null || g.isEmpty()) g = I18n.t("others");
      for (int i = 3; i < groups.size(); i++) {
        if (groups.get(i).equals(g)) { rememberGroup(i); fillGroup(); return; }
      }
    }
    rememberGroup(0);
    fillGroup();
  }

  private void selectCurrentInGuide() {
    int cur = player.index;
    if (mvPick != 0 && mvFocus >= 0 && mvFocus < mvN && mvCh[mvFocus] >= 0) cur = mvCh[mvFocus];
    if (cur < 0) return;
    for (int i = 0; i < inGroup.size(); i++) {
      if (inGroup.get(i) == cur) { chI = i; return; }
    }
    chI = 0;
  }

  private int groupCount(int g) {
    if (g < 0 || g >= groups.size()) return 0;
    String id = groupIdOf(g);
    if ("all".equals(id)) return profile.pl.size();
    if ("favorites".equals(id)) return profile.st.fav.size();
    if ("recent".equals(id)) return profile.st.recent.size();
    String name = groups.get(g);
    int n = 0;
    for (Channel ch : profile.pl) {
      String cg = ch.group == null || ch.group.isEmpty() ? I18n.t("others") : ch.group;
      if (name.equals(cg) || name.equals(ch.group)) n++;
    }
    return n;
  }

  void zap(int d) {
    if (profile.pl.isEmpty()) return;
    restoreFolderCursor();
    if (groups.isEmpty()) rebuildGroups();
    if (inGroup.isEmpty()) applyPlayingGroup(player.index);
    if (inGroup.isEmpty()) {
      int i = player.index + d;
      if (i < 0) i = profile.pl.size() - 1;
      if (i >= profile.pl.size()) i = 0;
      zapTo(i);
      return;
    }
    int pos = 0;
    for (int i = 0; i < inGroup.size(); i++) if (inGroup.get(i) == player.index) { pos = i; break; }
    int n = inGroup.size();
    pos = (pos + n + (d % n) + n) % n;
    rememberGroup(groupI);
    zapTo(inGroup.get(pos));
  }

  void zapTo(int i) {
    rec.onZap();
    player.play(i, 4);
    setMode(LIVE);
  }

  private void zapStay(int i) {
    rec.onZap();
    player.play(i, 4);
    invalidate();
  }

  private void closeGuide() {
    if (mvPick != 0) {
      mvPick = 0;
      mvMenu = 0;
      switchTo(MULTI);
      applyMultiLayout();
      return;
    }
    setMode(LIVE);
  }

  private void activateChannel() {
    rememberGroup(groupI);
    int ch = channelAt();
    if (ch < 0) return;
    if (mvPick != 0) {
      int cur = (mvFocus >= 0 && mvFocus < mvN) ? mvCh[mvFocus] : -1;
      if (ch != cur) {
        if (mvPick == 2) mvSetCh(mvFocus, ch);
        else mvPlaceCh(ch);
        applyMultiLayout();
        invalidate();
      } else {
        closeGuide();
      }
      return;
    }
    if (player == null || ch != player.index) {
      zapStay(ch);
      return;
    }
    closeGuide();
  }

  private int channelAt() {
    if (chI < 0 || chI >= inGroup.size()) return player.index;
    return inGroup.get(chI);
  }

  private void rebuildGroups() {
    groups.clear();
    if (!hiddenOn("all") && !hiddenOn(I18n.t("allChannels"))) groups.add(I18n.t("allChannels"));
    if (!hiddenOn("favorites") && !hiddenOn(I18n.t("favorites"))) groups.add(I18n.t("favorites"));
    if (!hiddenOn("recent") && !hiddenOn(I18n.t("recent"))) groups.add(I18n.t("recent"));
    for (Channel ch : profile.pl) {
      String g = ch.group == null || ch.group.isEmpty() ? I18n.t("others") : ch.group;
      if (profile.st.hidden.contains(g) || profile.st.hidden.contains(ch.group)) continue;
      if (!groups.contains(g)) groups.add(g);
    }
    if (groupI >= groups.size()) groupI = 0;
  }

  private void fillGroup() {
    inGroup.clear();
    String id = groupIdOf(groupI);
    if ("all".equals(id)) {
      for (int i = 0; i < profile.pl.size(); i++) {
        String g = profile.pl.get(i).group;
        if (g != null && profile.st.hidden.contains(g)) continue;
        inGroup.add(i);
      }
    } else if ("favorites".equals(id)) inGroup.addAll(profile.st.fav);
    else if ("recent".equals(id)) inGroup.addAll(profile.st.recent);
    else {
      String g = groups.get(groupI);
      for (int i = 0; i < profile.pl.size(); i++) {
        String cg = profile.pl.get(i).group;
        if (cg == null || cg.isEmpty()) cg = I18n.t("others");
        if (g.equals(cg) || g.equals(profile.pl.get(i).group)) inGroup.add(i);
      }
    }
    sortGroup();
    if (chI >= inGroup.size()) chI = Math.max(0, inGroup.size() - 1);
    fillGuide();
  }

  private void startFavMove(int ch) {
    if (ch < 0 || ch >= profile.pl.size()) { setMode(LIVE); return; }
    rememberGroup(1);
    fillGroup();
    favBak.clear();
    favBak.addAll(profile.st.favIds);
    favMoving = true;
    int idPos = profile.st.fav.indexOf(ch);
    if (idPos >= 0) chI = idPos;
    note(I18n.t("favMoveHint"), 2000);
    setMode(GUIDE);
  }

  private void cancelFavMove() {
    if (!favBak.isEmpty()) {
      profile.st.favIds.clear();
      profile.st.favIds.addAll(favBak);
      profile.resolveLists();
      profile.saveSettings();
    }
    favBak.clear();
    favMoving = false;
    fillGroup();
    invalidate();
  }

  private void commitFavMove() {
    favBak.clear();
    favMoving = false;
    profile.saveSettings();
    note(I18n.t("favMove"), 1200);
    invalidate();
  }

  private void moveFav(int dir) {
    List<String> ids = profile.st.favIds;
    if (ids.size() < 2) return;
    int i = chI;
    int j = i + dir;
    if (j < 0 || j >= ids.size()) return;
    String a = ids.get(i);
    ids.set(i, ids.get(j));
    ids.set(j, a);
    chI = j;
    profile.resolveLists();
    fillGroup();
    invalidate();
  }

  private void sortGroup() {
    if (groupI == 1 || groupI == 2) return;
    String sort = profile.st.channelSort;
    if (sort == null || "playlist".equals(sort)) return;
    inGroup.sort((a, b) -> {
      Channel ca = profile.pl.get(a), cb = profile.pl.get(b);
      if ("number".equals(sort)) {
        int na = ca.number > 0 ? ca.number : Integer.MAX_VALUE;
        int nb = cb.number > 0 ? cb.number : Integer.MAX_VALUE;
        if (na != nb) return Integer.compare(na, nb);
      }
      return ca.name.compareToIgnoreCase(cb.name);
    });
  }

  private List<String> visibleLogoUrls() {
    List<String> o = new ArrayList<>();
    int from = firstRow(lChan);
    int to = Math.min(inGroup.size(), from + 24);
    for (int i = from; i < to; i++) {
      int idx = inGroup.get(i);
      if (idx >= 0 && idx < profile.pl.size()) {
        String u = logos.urlFor(idx);
        if (u != null && !u.isEmpty()) o.add(u);
      }
    }
    return o;
  }

  private void fillGuide() {
    int ch = channelAt();
    if (epg == null) {
      guideList.clear();
      guideI = 0;
      return;
    }
    List<Programme> got = epg.list(ch);
    if (got.isEmpty() && epg.writerBusy()) {
      guideList.clear();
      guideI = 0;
      return;
    }
    guideList.clear();
    guideList.addAll(got);
    if (guideI >= guideList.size()) guideI = Math.max(0, guideList.size() - 1);
  }

  /** Как sync_prog_to_channel на Windows: для канала — эфир сегодня, иначе первая передача дня. */
  private void syncProgToChannel() {
    fillGuide();
    if (guideDays.isEmpty()) rebuildDays();
    if (guideList.isEmpty()) return;
    long day = (dayI >= 0 && dayI < guideDays.size()) ? guideDays.get(dayI) : dayStart(System.currentTimeMillis());
    pickProgramForDay(day);
    syncDayFromProgram();
  }

  private long dayStart(long ms) {
    Calendar cal = Calendar.getInstance();
    cal.setTimeInMillis(ms);
    cal.set(Calendar.HOUR_OF_DAY, 0);
    cal.set(Calendar.MINUTE, 0);
    cal.set(Calendar.SECOND, 0);
    cal.set(Calendar.MILLISECOND, 0);
    return cal.getTimeInMillis();
  }

  private void pickProgramForDay(long day) {
    if (guideList.isEmpty()) return;
    long now = System.currentTimeMillis();
    long today = dayStart(now);
    if (day == today) {
      for (int i = 0; i < guideList.size(); i++) {
        Programme p = guideList.get(i);
        if (p.start <= now && now < p.end) {
          guideI = i;
          return;
        }
      }
    }
    int first = -1, after = -1;
    long d1 = day + 86400000L;
    for (int i = 0; i < guideList.size(); i++) {
      long s = guideList.get(i).start;
      if (s >= day && s < d1 && first < 0) first = i;
      if (s >= d1 && after < 0) after = i;
    }
    if (first >= 0) guideI = first;
    else if (after >= 0) guideI = after;
    else guideI = guideList.size() - 1;
  }

  private void rebuildDays() {
    guideDays.clear();
    Calendar cal = Calendar.getInstance();
    cal.set(Calendar.HOUR_OF_DAY, 0);
    cal.set(Calendar.MINUTE, 0);
    cal.set(Calendar.SECOND, 0);
    cal.set(Calendar.MILLISECOND, 0);
    long today = cal.getTimeInMillis();
    int back = Math.max(1, profile.st.epgDays);
    if (profile.st.archiveEnabled && profile.st.archiveDays > back) back = profile.st.archiveDays;
    if (back > 32) back = 32;
    cal.add(Calendar.DATE, -back);
    int n = back + 1 + Math.max(0, profile.st.epgDays);
    dayI = 0;
    for (int i = 0; i < n; i++) {
      long t = cal.getTimeInMillis();
      guideDays.add(t);
      if (t == today) dayI = i;
      cal.add(Calendar.DATE, 1);
    }
  }

  private void syncDayFromProgram() {
    if (guideList.isEmpty() || guideDays.isEmpty()) return;
    Programme p = guideList.get(Math.min(guideI, guideList.size() - 1));
    for (int i = 0; i < guideDays.size(); i++) {
      long d0 = guideDays.get(i);
      if (p.start >= d0 && p.start < d0 + 86400000L) { dayI = i; return; }
    }
  }

  private void jumpToDay() {
    if (guideDays.isEmpty() || guideList.isEmpty() || dayI < 0 || dayI >= guideDays.size()) return;
    pickProgramForDay(guideDays.get(dayI));
  }

  private void fillSearch() {
    searchHits.clear();
    String q = searchQ == null ? "" : searchQ.trim();
    for (int i = 0; i < profile.pl.size() && searchHits.size() < 40; i++) {
      if (mvPick == 1 && mvUsed(i)) continue;
      if (q.isEmpty() || Util.containsI(profile.pl.get(i).name, q)) searchHits.add(i);
    }
    searchI = q.isEmpty() ? -1 : (searchHits.isEmpty() ? -1 : 0);
  }

  private boolean mvUsed(int ch) {
    for (int k = 0; k < mvN; k++) if (mvCh[k] == ch) return true;
    return false;
  }

  private List<String> searchLabels() {
    List<String> o = new ArrayList<>();
    for (int i : searchHits) o.add(profile.pl.get(i).name);
    return o;
  }

  private List<String> histLabels() {
    List<String> o = new ArrayList<>();
    for (int i : profile.st.recent) if (i >= 0 && i < profile.pl.size()) o.add(profile.pl.get(i).name);
    return o;
  }

  private String fmtTime(long ms) {
    return new SimpleDateFormat("HH:mm", Locale.US).format(new Date(ms));
  }

  static final class Row {
    int id;
    String title, hint;
    Row(int id, String t, String h) { this.id = id; title = t; hint = h; }
  }

  private void fillRows() {
    rows.clear();
    Settings s = profile.st;
    switch (setTab) {
      case 0:
        if (s.playlists.isEmpty()) rows.add(new Row(-1, I18n.t("setEmpty"), I18n.t("setNeedPl")));
        for (int i = 0; i < s.playlists.size(); i++) {
          SavedLink l = s.playlists.get(i);
          rows.add(new Row(1000 + i, l.name + (l.id.equals(s.activePl) ? I18n.t("setActive") : ""),
              Util.urlHint(l.url)));
          if (plMenu == i) {
            if (!l.id.equals(s.activePl)) rows.add(new Row(3000 + i, I18n.t("plEnable"), ""));
            rows.add(new Row(3100 + i, I18n.t("plReplace"), I18n.t("setNeedPl")));
            rows.add(new Row(3200 + i, I18n.t("plCopy"), ""));
            rows.add(new Row(3300 + i, I18n.t("plDel"), ""));
          }
        }
        rows.add(new Row(1, I18n.t("setAddUrl"), I18n.t("setNeedPl")));
        rows.add(new Row(6, I18n.t("setAddM3u"), I18n.t("setFromDisk")));
        rows.add(new Row(7, I18n.t("setXtSrv"), xtSrv.isEmpty() ? I18n.t("xtPaste") : I18n.t("xtSet")));
        rows.add(new Row(8, I18n.t("setXtUser"), xtUser.isEmpty() ? I18n.t("xtPaste") : I18n.t("xtSet")));
        rows.add(new Row(9, I18n.t("setXtPass"), xtPass.isEmpty() ? I18n.t("xtPaste") : I18n.t("xtSet")));
        rows.add(new Row(2, I18n.t("setXtAdd"), I18n.t("xtBuild")));
        rows.add(new Row(3, I18n.t("setInterval"), s.playlistUpdateHours + " " + I18n.t("hoursShort")));
        rows.add(new Row(4, I18n.t("setUa"), s.userAgent));
        rows.add(new Row(5, I18n.t("setSort"), sortLab(s.channelSort)));
        rows.add(new Row(-1, I18n.t("setShowGuide"), I18n.t("setShowGuideHint")));
        rows.add(new Row(400, I18n.t("allChannels"), hiddenOn("all") ? I18n.t("off") : I18n.t("on")));
        rows.add(new Row(401, I18n.t("favorites"), hiddenOn("favorites") ? I18n.t("off") : I18n.t("on")));
        rows.add(new Row(402, I18n.t("recent"), hiddenOn("recent") ? I18n.t("off") : I18n.t("on")));
        List<String> gs = playlistGroups();
        for (int gi = 0; gi < gs.size(); gi++) {
          String g = gs.get(gi);
          rows.add(new Row(500 + gi, g, hiddenOn(g) ? I18n.t("off") : I18n.t("on")));
        }
        break;
      case 1:
        SavedLink pri = epgBySlot(1), extra = epgBySlot(2);
        rows.add(new Row(10, I18n.t("setEpgMain"), pri == null ? I18n.t("setNeedEpgPaste") : Util.urlHint(pri.url)));
        rows.add(new Row(11, I18n.t("setEpgExtra"), extra == null ? "" : Util.urlHint(extra.url)));
        for (int i = 0; i < s.epgSrc.size(); i++) {
          SavedLink l = s.epgSrc.get(i);
          if (l.enabled == 1 || l.enabled == 2) continue;
          rows.add(new Row(1100 + i, l.name.isEmpty() ? Util.nameFromUrl(l.url) : l.name, Util.urlHint(l.url)));
          if (epgMenu == i) {
            rows.add(new Row(6000 + i, I18n.t("epgMakeMain"), ""));
            rows.add(new Row(6100 + i, I18n.t("epgMakeExtra"), ""));
            rows.add(new Row(6200 + i, I18n.t("paste"), ""));
            rows.add(new Row(6300 + i, I18n.t("copy"), ""));
            rows.add(new Row(6400 + i, I18n.t("plDel"), ""));
          }
        }
        rows.add(new Row(18, I18n.t("setAddUrl"), I18n.t("setNeedEpgPaste")));
        rows.add(new Row(12, I18n.t("setLoadEpg"), pri == null ? I18n.t("setNeedEpg") : I18n.t("setEpgChain")));
        rows.add(new Row(13, I18n.t("setOffset"), offsetLab(s.epgOffsetHours)));
        rows.add(new Row(14, I18n.t("setEpgDays"), s.epgDays + " " + I18n.t("daysShort")));
        rows.add(new Row(15, I18n.t("setEpgAuto"), s.epgAutoUpdate ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(16, I18n.t("setInterval"), s.epgUpdateHours + " " + I18n.t("hoursShort")));
        rows.add(new Row(17, I18n.t("setDesc"), s.showProgramDesc ? I18n.t("on") : I18n.t("off")));
        break;
      case 2:
        rows.add(new Row(20, I18n.t("setTheme"), themeLab(s.theme)));
        rows.add(new Row(21, I18n.t("setAccent"), accLab(s.highlight)));
        rows.add(new Row(22, I18n.t("setPanel"), s.panelOpacity + "%"));
        rows.add(new Row(23, I18n.t("setFont"), fontLab(s.fontSize)));
        rows.add(new Row(24, I18n.t("setLogo"), sizeLab(s.logoSize)));
        rows.add(new Row(25, I18n.t("setGuideRows"), "" + s.guideRows));
        rows.add(new Row(26, I18n.t("setTwoLines"), s.twoLineTitles ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(27, I18n.t("setClock"), s.clockEnabled ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(28, I18n.t("setClockPos"), clockPosLab(s.clockPos)));
        rows.add(new Row(29, I18n.t("setClockSize"), sizeLab(s.clockSize)));
        rows.add(new Row(30, I18n.t("setClockOp"), s.clockOpacity + "%"));
        rows.add(new Row(31, I18n.t("setNumbers"), s.showNumbers ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(32, I18n.t("setProgress"), s.showEpgProgress ? I18n.t("on") : I18n.t("off")));
        break;
      case 3:
        rows.add(new Row(40, I18n.t("setAutoplay"), s.autoplay ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(41, I18n.t("setRememberVol"), s.rememberVolume ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(42, I18n.t("setConfirmExit"), s.confirmExit ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(43, I18n.t("setMic"), s.voiceEnabled ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(44, I18n.t("setDecoder"), decLab(s.decoder)));
        rows.add(new Row(45, I18n.t("setBuffer"), s.bufferSec + " " + I18n.t("secShort")));
        rows.add(new Row(46, I18n.t("setAspect"), aspectLab(s.aspect)));
        rows.add(new Row(47, I18n.t("setExternal"), s.externalPlayer ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(48, I18n.t("setAudioLang"), audioLab(s.audio)));
        rows.add(new Row(49, I18n.t("setSysPip"), s.systemPip ? I18n.t("on") : I18n.t("off")));
        break;
      case 4:
        rows.add(new Row(50, I18n.t("setRec"), s.recordingsEnabled ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(51, I18n.t("setRecPath"), rec != null ? rec.recPathLabel() : I18n.t("setPickFolder")));
        rows.add(new Row(-1, I18n.t("recInfo"), I18n.t("recInfoHint")));
        break;
      case 5:
        rows.add(new Row(60, I18n.t("archive"), s.archiveEnabled ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(61, I18n.t("setArchiveDays"), s.archiveDays + " " + I18n.t("daysShort")));
        break;
      case 6:
        rows.add(new Row(70, I18n.t("setParental"), s.parentalEnabled ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(71, I18n.t("setLockSettings"), s.lockSettings ? I18n.t("on") : I18n.t("off")));
        rows.add(new Row(72, I18n.t("setPin"), I18n.t("urlPinHint")));
        List<String> lg = playlistGroups();
        for (int i = 0; i < lg.size(); i++) {
          String g = lg.get(i);
          boolean lock = profile.st.locked.contains(g);
          rows.add(new Row(700 + i, g, lock ? "PIN" : I18n.t("groupOpen")));
        }
        break;
      case 7:
        rows.add(new Row(80, I18n.t("keyMenu"), keyLab(1, s.keyMenu)));
        rows.add(new Row(81, I18n.t("keyMute"), keyLab(2, s.keyMute)));
        rows.add(new Row(82, I18n.t("keyFav"), keyLab(3, s.keyFav)));
        rows.add(new Row(83, I18n.t("keyFs"), keyLab(4, s.keyFs)));
        rows.add(new Row(84, I18n.t("keyGuide"), keyLab(5, s.keyGuide)));
        rows.add(new Row(85, I18n.t("keySearch"), keyLab(6, s.keySearch)));
        rows.add(new Row(86, I18n.t("keyRec"), keyLab(7, s.keyRec)));
        rows.add(new Row(87, I18n.t("keyPip"), keyLab(8, s.keyPip)));
        rows.add(new Row(88, I18n.t("keyVoice"), keyLab(9, s.keyVoice)));
        rows.add(new Row(89, I18n.t("keyLive"), keyLab(10, s.keyLive)));
        break;
      case 8:
        rows.add(new Row(90, I18n.t("setLang"), "en".equals(s.language) ? "English" : "Русский"));
        break;
      case 9:
        rows.add(new Row(200, I18n.t("setCloudCopy"), I18n.t("setCloudHint")));
        rows.add(new Row(201, I18n.t("setCloudShare"), I18n.t("setCloudShareHint")));
        rows.add(new Row(207, I18n.t("setBackupShare"), I18n.t("setBackupShareHint")));
        rows.add(new Row(202, I18n.t("setCloudPaste"), ""));
        rows.add(new Row(203, I18n.t("setExport"), I18n.t("setExportHint")));
        rows.add(new Row(204, I18n.t("setImport"), I18n.t("setImportHint")));
        rows.add(new Row(205, I18n.t("setReset"), I18n.t("setResetHint")));
        rows.add(new Row(206, I18n.t("setVosk"), voice != null && voice.voskReady() ? I18n.t("voskReady") : I18n.t("voskDownload")));
        break;
      case 10:
        rows.add(new Row(-1, "MirEfir", I18n.t("setNativeAnd") + "  " + App.VERSION));
        if (update != null && update.hasFile()) rows.add(new Row(210, I18n.t("update"), I18n.t("updateReadyHint")));
        break;
    }
  }

  private String sortLab(String v) {
    if ("name".equals(v)) return I18n.t("sortName");
    if ("number".equals(v)) return I18n.t("sortNumber");
    return I18n.t("sortPlaylist");
  }
  private String themeLab(String v) {
    if ("oled".equals(v)) return I18n.t("themeOled");
    if ("graphite".equals(v)) return I18n.t("themeGraphite");
    return I18n.t("themeDark");
  }
  private String accLab(String v) {
    if ("orange".equals(v)) return I18n.t("accentOrange");
    if ("teal".equals(v)) return I18n.t("accentTeal");
    return I18n.t("accentBlue");
  }
  private String decLab(String v) {
    if ("hardware".equals(v)) return I18n.t("decoderHw");
    if ("software".equals(v)) return I18n.t("decoderSw");
    return I18n.t("decoderAuto");
  }
  private String fontLab(String v) {
    if ("sm".equals(v)) return I18n.t("fontSm");
    if ("lg".equals(v)) return I18n.t("fontLg");
    return I18n.t("fontMd");
  }
  private String sizeLab(String v) {
    if ("sm".equals(v)) return I18n.t("clockSm");
    if ("lg".equals(v)) return I18n.t("clockLg");
    return I18n.t("clockMd");
  }
  private String clockPosLab(String v) {
    if ("top-left".equals(v)) return I18n.t("clockTL");
    if ("bottom-left".equals(v)) return I18n.t("clockBL");
    if ("bottom-right".equals(v)) return I18n.t("clockBR");
    return I18n.t("clockTR");
  }
  private String offsetLab(int h) {
    return (h > 0 ? "+" : "") + h + " " + I18n.t("hoursShort");
  }
  private String keyLab(int slot, String stored) {
    if (waitKey == slot) return I18n.t("pressKey");
    return Keys.label(stored);
  }
  private String aspectLab(String v) {
    if ("16:9".equals(v) || "4:3".equals(v) || "zoom".equals(v)) return v;
    return I18n.t("aspectAuto");
  }
  private String audioLab(String v) {
    if ("en".equals(v)) return "EN";
    if ("orig".equals(v)) return I18n.t("audioOrig");
    return "RU";
  }
  private boolean hiddenOn(String name) {
    return name != null && profile.st.hidden.contains(name);
  }
  private SavedLink epgBySlot(int slot) {
    for (SavedLink s : profile.st.epgSrc) if (s.enabled == slot) return s;
    return null;
  }
  private List<String> playlistGroups() {
    List<String> o = new ArrayList<>();
    for (Channel ch : profile.pl) {
      String g = ch.group == null || ch.group.isEmpty() ? I18n.t("others") : ch.group;
      if (!o.contains(g)) o.add(g);
    }
    return o;
  }

  private void activateRow() {
    if (setI < 0 || setI >= rows.size()) return;
    if (setTab == 6 && profile.st.parentalEnabled && profile.st.lockSettings && !pinOk) {
      pendingSetTab = setTab;
      setMode(PIN);
      return;
    }
    int id = rows.get(setI).id;
    Settings s = profile.st;
    switch (id) {
      case 1: openUrlEditor(U_PL, SETTINGS); return;
      case 2:
        if (xtSrv.isEmpty() || xtUser.isEmpty() || xtPass.isEmpty()) {
          note(I18n.t("xtNeedAll"), 1600);
          return;
        }
        addPlaylistInBg(profile.xtreamM3u(xtSrv, xtUser, xtPass), () -> {
          xtSrv = xtUser = xtPass = "";
          note(I18n.t("plAdded"), 1600);
        });
        return;
      case 3: s.playlistUpdateHours = cycle(s.playlistUpdateHours, new int[] {1, 3, 6, 12, 24}); break;
      case 4: openUrlEditor(U_UA, SETTINGS); return;
      case 5: s.channelSort = "playlist".equals(s.channelSort) ? "name" : "name".equals(s.channelSort) ? "number" : "playlist"; break;
      case 6:
        if (host != null) host.pickM3uOrBrowse();
        return;
      case 7: openUrlEditor(U_XTS, SETTINGS); return;
      case 8: openUrlEditor(U_XTU, SETTINGS); return;
      case 9: openUrlEditor(U_XTP, SETTINGS); return;
      case 10: openUrlEditor(U_EPG1, SETTINGS); return;
      case 11: openUrlEditor(U_EPG2, SETTINGS); return;
      case 12:
        if (profile.epgChain().isEmpty()) { note(I18n.t("epgNeed"), 2000); return; }
        note(I18n.t("epgUpdating"), 2000);
        epg.start(true);
        break;
      case 13: s.epgOffsetHours = s.epgOffsetHours >= 12 ? -12 : s.epgOffsetHours + 1; epg.applySettings(); epg.start(true); break;
      case 18: openUrlEditor(U_EPG1, SETTINGS); return;
      case 14: s.epgDays = cycle(s.epgDays, new int[] {1, 2, 3, 5, 7, 14}); epg.applySettings(); break;
      case 15: s.epgAutoUpdate = !s.epgAutoUpdate; break;
      case 16: s.epgUpdateHours = cycle(s.epgUpdateHours, new int[] {3, 6, 12, 24}); break;
      case 17: s.showProgramDesc = !s.showProgramDesc; break;
      case 20: s.theme = "dark".equals(s.theme) ? "oled" : "oled".equals(s.theme) ? "graphite" : "dark"; break;
      case 21: s.highlight = "blue".equals(s.highlight) ? "orange" : "orange".equals(s.highlight) ? "teal" : "blue"; break;
      case 22: s.panelOpacity = cycle(s.panelOpacity, new int[] {70, 80, 90, 100}); break;
      case 23: s.fontSize = "sm".equals(s.fontSize) ? "md" : "md".equals(s.fontSize) ? "lg" : "sm"; break;
      case 24: s.logoSize = "sm".equals(s.logoSize) ? "md" : "md".equals(s.logoSize) ? "lg" : "sm"; break;
      case 25: s.guideRows = s.guideRows >= 9 ? 4 : s.guideRows + 1; break;
      case 26: s.twoLineTitles = !s.twoLineTitles; break;
      case 27: s.clockEnabled = !s.clockEnabled; if (host != null) host.placeClock(); break;
      case 28:
        s.clockPos = "top-right".equals(s.clockPos) ? "top-left" : "top-left".equals(s.clockPos) ? "bottom-left"
            : "bottom-left".equals(s.clockPos) ? "bottom-right" : "top-right";
        break;
      case 29: s.clockSize = "sm".equals(s.clockSize) ? "md" : "md".equals(s.clockSize) ? "lg" : "sm"; break;
      case 30: s.clockOpacity = cycle(s.clockOpacity, new int[] {60, 75, 90, 100}); break;
      case 31: s.showNumbers = !s.showNumbers; break;
      case 32: s.showEpgProgress = !s.showEpgProgress; break;
      case 40: s.autoplay = !s.autoplay; break;
      case 41: s.rememberVolume = !s.rememberVolume; break;
      case 42: s.confirmExit = !s.confirmExit; break;
      case 43: s.voiceEnabled = !s.voiceEnabled; break;
      case 44: s.decoder = "auto".equals(s.decoder) ? "hardware" : "hardware".equals(s.decoder) ? "software" : "auto"; player.applySettings(); break;
      case 45: s.bufferSec = cycle(s.bufferSec, new int[] {8, 15, 30, 60}); player.applySettings(); break;
      case 46: s.aspect = "auto".equals(s.aspect) ? "16:9" : "16:9".equals(s.aspect) ? "4:3" : "4:3".equals(s.aspect) ? "zoom" : "auto"; player.applyAspect(); break;
      case 47:
        s.externalPlayer = !s.externalPlayer;
        if (player != null && player.index >= 0) player.play(player.index, 4);
        note(I18n.t(s.externalPlayer ? "extOn" : "extOff"), 1600);
        break;
      case 48: s.audio = "ru".equals(s.audio) ? "en" : "en".equals(s.audio) ? "orig" : "ru"; player.applyAudioLang(); break;
      case 49: s.systemPip = !s.systemPip; break;
      case 50: s.recordingsEnabled = !s.recordingsEnabled; break;
      case 51: askWhy = 0; setMode(ASK); break;
      case 60: s.archiveEnabled = !s.archiveEnabled; if (epg != null) epg.applySettings(); break;
      case 61: s.archiveDays = cycle(s.archiveDays, new int[] {1, 3, 5, 7, 14}); if (epg != null) epg.applySettings(); break;
      case 70: s.parentalEnabled = !s.parentalEnabled; if (!s.parentalEnabled) pinOk = true; break;
      case 71: s.lockSettings = !s.lockSettings; break;
      case 72: openUrlEditor(U_PIN, SETTINGS); return;
      case 80: case 81: case 82: case 83: case 84:
      case 85: case 86: case 87: case 88: case 89:
        waitKey = id - 79;
        note(I18n.t("pressKey"), 4000);
        invalidate();
        return;
      case 90: s.language = "en".equals(s.language) ? "ru" : "en"; I18n.set(s.language); rebuildGroups(); fillGroup(); break;
      case 200: Util.setClipboard(getContext(), Cloud.make(profile)); note(I18n.t("codeCopied"), 1600); break;
      case 201: shareFile = false; shareI = 0; setMode(SHARE); break;
      case 207: shareFile = true; shareI = 0; setMode(SHARE); break;
      case 202: {
        String clip = Util.clipboard(getContext());
        if (Cloud.looksLike(clip)) { applyCloudInBg(clip); return; }
        openUrlEditor(U_CLOUD, SETTINGS);
        return;
      }
      case 203:
        if (host != null) host.pickBackupSave();
        break;
      case 204:
        if (host != null) host.pickBackupOpen();
        break;
      case 205: profile.resetSettings(); I18n.set(s.language); rebuildGroups(); note(I18n.t("settingsReset"), 1600); break;
      case 206:
        if (voice != null && !voice.voskReady()) {
          note(I18n.t("voiceDlModel"), 2500);
          voice.downloadModel(() -> note(voice.voskReady() ? I18n.t("voskReady") : I18n.t("voiceUnModel"), 2000));
        } else note(I18n.t("voskReady"), 1600);
        break;
      case 210:
        note(I18n.t("updateInstall"), 2000);
        if (host != null) host.applyUpdate();
        break;
      case 400: profile.toggleHidden("all"); rebuildGroups(); break;
      case 401: profile.toggleHidden("favorites"); rebuildGroups(); break;
      case 402: profile.toggleHidden("recent"); rebuildGroups(); break;
      default:
        if (id >= 1000 && id < 1032) {
          int pi = id - 1000;
          plMenu = plMenu == pi ? -1 : pi;
          invalidate();
          return;
        }
        if (id >= 3000 && id < 3400) {
          int pi = id % 100;
          if (id < 3100) {
            if (profile.setActivePlaylist(pi)) note(I18n.t("plOn"), 1600);
          } else if (id < 3200) {
            plRepI = pi;
            openUrlEditor(U_PL_REP, SETTINGS);
          } else if (id < 3300) {
            if (pi >= 0 && pi < profile.st.playlists.size()) {
              Util.setClipboard(getContext(), profile.st.playlists.get(pi).url);
              note(I18n.t("urlCopied"), 1600);
            }
          } else {
            if (profile.removePlaylist(pi)) note(I18n.t("srcDeleted"), 1600);
            plMenu = -1;
          }
          break;
        }
        if (id >= 1100 && id < 1132) {
          epgMenu = epgMenu == id - 1100 ? -1 : id - 1100;
          invalidate();
          return;
        }
        if (id >= 500 && id < 564) {
          List<String> gs = playlistGroups();
          int gi = id - 500;
          if (gi >= 0 && gi < gs.size()) profile.toggleHidden(gs.get(gi));
          rebuildGroups();
          break;
        }
        if (id >= 6000 && id < 6500) {
          int ei = id % 100;
          if (id < 6100) { profile.setEpgSlot(ei, 1); if (epg != null) epg.start(true); }
          else if (id < 6200) { profile.setEpgSlot(ei, 2); if (epg != null) epg.start(true); }
          else if (id < 6300) openUrlEditor(U_EPG1, SETTINGS);
          else if (id < 6400) {
            if (ei >= 0 && ei < profile.st.epgSrc.size()) {
              Util.setClipboard(getContext(), profile.st.epgSrc.get(ei).url);
              note(I18n.t("urlCopied"), 1600);
            }
          } else {
            profile.removeEpg(ei);
            note(I18n.t("srcDeleted"), 1600);
            epgMenu = -1;
          }
          break;
        }
        if (id >= 700 && id < 764) {
          List<String> lg = playlistGroups();
          int gi = id - 700;
          if (gi >= 0 && gi < lg.size()) profile.toggleLocked(lg.get(gi));
          break;
        }
        break;
    }
    profile.saveSettings();
    if (host != null) host.placeClock();
    invalidate();
  }

  private int cycle(int v, int[] opts) {
    for (int i = 0; i < opts.length; i++) if (opts[i] == v) return opts[(i + 1) % opts.length];
    return opts[0];
  }

  // ===================================================================================
  //  Касания: тап по строке под пальцем, прокрутка, свайпы, полоса прогресса
  // ===================================================================================
  static final int G_NONE = 0, G_SCROLL = 1, G_SWIPE = 2, G_SCRUB = 3, G_IGNORE = 4;
  private final Runnable longRun = this::onLongPress;
  private final Runnable flingRun = this::flingStep;

  @Override
  public boolean onTouchEvent(MotionEvent ev) {
    if (ev == null) return false;
    int act = ev.getActionMasked();
    if (act == MotionEvent.ACTION_HOVER_ENTER || act == MotionEvent.ACTION_HOVER_MOVE
        || act == MotionEvent.ACTION_HOVER_EXIT) return false;
    if (Ui.onTouchInput()) invalidate();
    if (act == MotionEvent.ACTION_BUTTON_PRESS) {
      if ((ev.getButtonState() & MotionEvent.BUTTON_SECONDARY) != 0) {
        return openContext(hitAt(ev.getX() - padL, ev.getY() - padT));
      }
      return true;
    }
    switch (act) {
      case MotionEvent.ACTION_DOWN:
        if ((ev.getButtonState() & MotionEvent.BUTTON_SECONDARY) != 0) {
          cancelTouch();
          return openContext(hitAt(ev.getX() - padL, ev.getY() - padT));
        }
        touchDown(ev);
        break;
      case MotionEvent.ACTION_MOVE: touchMove(ev); break;
      case MotionEvent.ACTION_UP: touchUp(ev); break;
      case MotionEvent.ACTION_CANCEL: cancelTouch(); break;
      default: break;
    }
    return true;
  }

  private void touchDown(MotionEvent ev) {
    stopFling();
    removeCallbacks(longRun);
    rawX = ev.getX();
    rawY = ev.getY();
    float x = rawX - padL, y = rawY - padT;
    downX = lastX = x;
    downY = lastY = y;
    touching = true;
    dragging = false;
    longFired = false;
    gesture = G_NONE;
    scrubbing = false;
    Hit hh = hitAt(x, y);
    downKind = hh == null ? 0 : hh.kind;
    downIdx = hh == null ? -1 : hh.idx;
    pressKind = downKind;
    if (hh != null) pressRect.set(hh.l, hh.t, hh.r, hh.b);
    dragList = scrollAt(x, y);
    if (vt != null) vt.recycle();
    vt = VelocityTracker.obtain();
    vt.addMovement(ev);
    if (mode == PAD) keepPad();
    if (mode == PAD && downKind == H_BAR) {
      gesture = G_SCRUB;
      scrubbing = true;
      scrubRatio = ratioAt(x);
    }
    boolean subHold = (downKind == H_PAD && downIdx == 7)
        || (downKind == H_BURGER_ITEM && downIdx >= 0 && downIdx < BURGER.length && BURGER[downIdx] == BURGER_AUDIO);
    if (subHold || (mode == LIVE && downKind == 0)) postDelayed(longRun, 550);
    invalidate();
  }

  private void touchMove(MotionEvent ev) {
    if (!touching) return;
    if (vt != null) vt.addMovement(ev);
    float x = ev.getX() - padL, y = ev.getY() - padT;
    float dx = x - downX, dy = y - downY;
    if (mode == PAD) keepPad();
    if (!dragging) {
      float slop = ViewConfiguration.get(getContext()).getScaledTouchSlop();
      if (Math.hypot(dx, dy) <= slop) {
        lastX = x;
        lastY = y;
        return;
      }
      dragging = true;
      removeCallbacks(longRun);
      if (gesture != G_SCRUB) {
        if (dragList != null && Math.abs(dy) >= Math.abs(dx)) gesture = G_SCROLL;
        else if ((mode == LIVE && downKind == 0) || (mode == PAD && (downKind == 0 || downKind == H_PANEL))) gesture = G_SWIPE;
        else gesture = G_IGNORE;
      }
    }
    if (gesture == G_SCROLL && dragList != null) {
      dragList.off -= (y - lastY) / dragList.rowH;
      dragList.clamp();
      dragList.touchedAt = SystemClock.uptimeMillis();
    } else if (gesture == G_SCRUB) {
      scrubRatio = ratioAt(x);
    }
    lastX = x;
    lastY = y;
    invalidate();
  }

  private void touchUp(MotionEvent ev) {
    if (!touching) return;
    removeCallbacks(longRun);
    touching = false;
    pressKind = 0;
    if (vt != null) vt.addMovement(ev);
    float x = ev.getX() - padL, y = ev.getY() - padT;
    float dx = x - downX, dy = y - downY;
    int g = gesture;
    boolean wasDrag = dragging, lf = longFired;
    Lst dl = dragList;
    float vy = 0;
    if (vt != null) {
      vt.computeCurrentVelocity(1000);
      vy = vt.getYVelocity();
      vt.recycle();
      vt = null;
    }
    dragList = null;
    gesture = G_NONE;
    dragging = false;
    if (g == G_SCRUB) {
      scrubbing = false;
      padSeekRatio(scrubRatio);
      keepPad();
    } else if (lf) {
      /* долгое нажатие уже сработало */
    } else if (!wasDrag) {
      handleTap(x, y);
    } else if (g == G_SCROLL && dl != null) {
      if (Math.abs(vy) > dp(300)) startFling(dl, vy);
      postInvalidateDelayed(1300);
    } else if (g == G_SWIPE) {
      handleSwipe(dx, dy);
    }
    invalidate();
  }

  private void cancelTouch() {
    removeCallbacks(longRun);
    touching = false;
    dragging = false;
    scrubbing = false;
    pressKind = 0;
    gesture = G_NONE;
    dragList = null;
    if (vt != null) {
      vt.recycle();
      vt = null;
    }
    invalidate();
  }

  private float ratioAt(float x) {
    float bw = padBar.width();
    if (bw <= 0) return 0;
    return Math.max(0f, Math.min(1f, (x - padBar.left) / bw));
  }

  private void onLongPress() {
    if (!touching || dragging) return;
    longFired = true;
    if (mode == LIVE && downKind == 0) {
      openChMenu();
    } else if (mode == GUIDE && downKind == H_GCH) {
      chI = downIdx;
      openChMenu();
    } else if (downKind == H_PAD && downIdx == 7) {
      if (player != null) player.cycleSubs();
      keepPad();
    } else if (downKind == H_BURGER_ITEM) {
      burgerOpen = false;
      if (player != null) player.cycleSubs();
      keepPad();
    } else {
      longFired = false;
      return;
    }
    performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
    invalidate();
  }

  private void handleSwipe(float dx, float dy) {
    float adx = Math.abs(dx), ady = Math.abs(dy), min = dp(64);
    int w = lw;
    if (mode == LIVE) {
      if (ady > adx && ady > min) {
        zap(dy < 0 ? -1 : 1);
      } else if (adx > ady && adx > min) {
        if (dx > 0 && downX < w * 0.4f) openGuide();
        else if (dx < 0 && downX > w * 0.6f && player != null && player.prev >= 0) zapTo(player.prev);
      }
    } else if (mode == PAD) {
      burgerOpen = false;
      if (ady > adx && dy > dp(80)) setMode(LIVE);
    }
  }

  private void tapPad(int k, int i) {
    if (burgerOpen) {
      if (k == H_BURGER_ITEM && i >= 0 && i < BURGER.length) {
        int code = BURGER[i];
        burgerOpen = false;
        if (code == BURGER_SUB) {
          if (player != null) player.cycleSubs();
          keepPad();
        } else {
          runPad(code);
        }
        return;
      }
      if (k == H_PANEL) { keepPad(); return; }
      burgerOpen = false;
      keepPad();
      return;
    }
    if (k == H_PAD) {
      if (!Ui.touch()) { padFocus = PAD_BUTTONS; padI = i; }
      runPad(i);
      keepPad();
    } else if (k == H_BURGER) {
      burgerOpen = true;
      lBurger.reset();
      keepPad();
    } else if (k == H_PANEL || k == H_BAR) {
      keepPad();
    } else {
      setMode(LIVE);
    }
  }

  private void tapTile() {
    int W = getWidth(), H = getHeight() - (int) dp(52);
    if (mvN <= 1 || W < 8 || H < 8) { mvMenu = 1; mvI = 0; return; }
    int[] grid = Ui.tileGrid(mvN, W, H);
    int cols = grid[0], rowsN = grid[1];
    int col = Math.min(cols - 1, Math.max(0, (int) (rawX / (W / (float) cols))));
    int row = (int) (rawY / (H / (float) rowsN));
    if (row >= rowsN) row = rowsN - 1;
    int idx = row * cols + col;
    if (idx >= mvN) return;
    if (idx == mvFocus) { mvMenu = 1; mvI = 0; return; }
    mvFocus = idx;
    player.focusPane(idx);
    host.layoutTiles(mvN, mvFocus, (int) dp(52));
  }

  /** Тап: строку определяет область под пальцем (координаты последнего кадра), затем выполняется действие. */
  private void handleTap(float x, float y) {
    Hit hh = hitAt(x, y);
    int k = hh == null ? 0 : hh.kind, i = hh == null ? -1 : hh.idx;
    if (k == H_CLOSE || k == H_CRUMB_BACK) { goBack(); return; }
    if (k == H_CRUMB) {
      /* тап по тексту «хлебной крошки»: в гиде со списка каналов — к папкам, иначе шаг назад */
      if (mode == GUIDE && focusCol == COL_C) {
        guideView = GV_GROUPS;
        focusCol = COL_G;
        invalidate();
      } else goBack();
      return;
    }
    switch (mode) {
      case LIVE:
        if (k == H_CHIP_UP) { if (update != null) update.apply(); }
        else if (k == H_CHIP_LATER) { if (update != null) update.later(); }
        else setMode(PAD);
        break;
      case PAD: tapPad(k, i); break;
      case MENU:
        if (k == H_MENU) { menuI = i; lMenu.lastSel = i; runMenu(i); }
        else if (k == 0) goBack();
        break;
      case SETTINGS:
        if (k == H_SET_BACK) setMode(MENU);
        else if (k == H_SET_BACK2) setPane = 0;
        else if (k == H_TAB) {
          setTab = i;
          lTabs.lastSel = i;
          setI = 0;
          lRows.reset();
          setPane = settingsNarrow() ? 1 : 0;
        } else if (k == H_ROW) {
          setPane = 1;
          setI = i;
          lRows.lastSel = i;
          fillRows();
          activateRow();
        } else if (k == 0) setMode(MENU);
        break;
      case GUIDE:
        if (chmOn) {
          if (k == H_CHM) { chmI = i; runChMenu(); }
          else if (k == 0) closeChMenu();
          break;
        }
        if (k == H_GGRP) {
          focusCol = COL_G;
          groupI = i;
          lGroups.lastSel = i;
          chI = 0;
          lChan.reset();
          guideOk();
        } else if (k == H_GCH) {
          focusCol = COL_C;
          chI = i;
          lChan.lastSel = i;
          rebuildDays();
          syncProgToChannel();
          activateChannel();
        } else if (k == H_GSCHED) {
          guideView = GV_CHANNELS;
          focusCol = COL_C;
          chI = i;
          lChan.lastSel = i;
          lProg.reset();
          guideRight();
        } else if (k == H_GPROG) {
          focusCol = COL_P;
          guideI = i;
          lProg.lastSel = i;
          syncDayFromProgram();
          guideOk();
        } else if (k == H_GDAY) {
          focusCol = COL_D;
          dayI = i;
          lDays.lastSel = i;
          jumpToDay();
        } else if (k == H_GDET) {
          focusCol = COL_DET;
        } else if (k == 0) closeGuide();
        break;
      case SEARCH:
        if (k == H_LIST) { searchI = i; lSearch.lastSel = i; onOk(null); }
        else if (k == H_FIELD) { searchI = -1; searchBtn = 0; kbOpen(); }
        else if (k == H_MIC) searchMic();
        else if (k == H_PASTE) doPaste();
        else if (k == H_COPY) doCopy();
        else if (k == 0) goBack();
        break;
      case HISTORY:
        if (k == H_LIST) { histI = i; lHist.lastSel = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case RECS:
        if (k == H_REC) { recI = i; lRecs.lastSel = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case EXIT:
        if (k == H_EXIT) { exitI = i; onOk(null); }
        else if (k == 0) setMode(LIVE);
        break;
      case MULTI:
        if (k == H_MVOPT) { mvI = i; multiOk(); }
        else if (k == H_MVBAR) { mvMenu = 1; mvI = 0; }
        else if (k == 0 && mvMenu == 1) mvMenu = 0;
        else if (k == 0) tapTile();
        break;
      case PIN:
        if (k == H_PIN) pinKey(i);
        else if (k == 0) goBack();
        break;
      case SETUP:
        if (k == H_SETUP) { setupI = i; setupOk(); }
        break;
      case SHARE:
        if (k == H_SHARE) { shareI = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case ASK:
        if (k == H_ASK) { askI = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case FILEBROWSE:
        if (k == H_LIST) { browseI = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case MICPERM:
        if (k == H_ASK) { micI = i; onOk(null); }
        else if (k == 0) goBack();
        break;
      case CHMENU:
        if (k == H_CHM) { chmI = i; runChMenu(); }
        else if (k == 0) goBack();
        break;
      case URL:
        if (k == H_FIELD) { urlI = 0; kbOpen(); }
        else if (k == H_PASTE) doPaste();
        else if (k == H_COPY) doCopy();
        else if (k == H_URL_OK) urlSubmit();
        break;
      default:
        break;
    }
    invalidate();
  }

  private void pinKey(int i) {
    if (i == 9) {
      if (pinBuf.length() > 0) pinBuf = pinBuf.substring(0, pinBuf.length() - 1);
    } else if (i == 11) {
      submitPin();
    } else {
      if (pinBuf.length() < 8) pinBuf += i == 10 ? "0" : String.valueOf(i + 1);
    }
    invalidate();
  }

  // ---------- прокрутка с инерцией ----------
  private void startFling(Lst l, float vy) {
    flingList = l;
    flingVel = vy;
    flingT = SystemClock.uptimeMillis();
    postOnAnimation(flingRun);
  }

  private void stopFling() {
    flingList = null;
    removeCallbacks(flingRun);
  }

  private void flingStep() {
    Lst l = flingList;
    if (l == null) return;
    long now = SystemClock.uptimeMillis();
    float dt = Math.min(0.05f, Math.max(0.001f, (now - flingT) / 1000f));
    flingT = now;
    float before = l.off;
    l.off -= flingVel / l.rowH * dt;
    l.clamp();
    l.touchedAt = now;
    flingVel *= (float) Math.exp(-3.4f * dt);
    invalidate();
    if (Math.abs(flingVel) < dp(40) || l.off == before) {
      flingList = null;
      postInvalidateDelayed(1300);
      return;
    }
    postOnAnimation(flingRun);
  }

  /** Колёсико мыши и тачпада прокручивает список под курсором. */
  private boolean wheel(MotionEvent ev) {
    float v = ev.getAxisValue(MotionEvent.AXIS_VSCROLL);
    if (v == 0) return false;
    Lst l = scrollAt(ev.getX() - padL, ev.getY() - padT);
    if (l == null) return false;
    l.off -= v * 2.5f;
    l.clamp();
    l.touchedAt = SystemClock.uptimeMillis();
    invalidate();
    return true;
  }

  // ===================================================================================
  //  Ввод текста: экранная клавиатура, редактор ссылки / кода, «Вставить» и «Копировать»
  // ===================================================================================
  static final int U_PL = 0, U_XTS = 1, U_XTU = 2, U_XTP = 3, U_UA = 4, U_EPG1 = 5, U_EPG2 = 6, U_CLOUD = 7,
      U_PIN = 8, U_PL_REP = 9;

  boolean textMode() { return mode == SEARCH || mode == URL; }

  /** Клавиша печатает в поле (буквы, цифры, пробел, Backspace) — её обрабатывает скрытое поле ввода. */
  boolean wantsTextKey(KeyEvent e) {
    if (e == null || !textMode()) return false;
    int code = e.getKeyCode();
    if (code == KeyEvent.KEYCODE_DEL || code == KeyEvent.KEYCODE_FORWARD_DEL) return true;
    if (e.isCtrlPressed() || e.isAltPressed()) return code == KeyEvent.KEYCODE_V || code == KeyEvent.KEYCODE_C
        || code == KeyEvent.KEYCODE_A || code == KeyEvent.KEYCODE_X;
    if (e.isPrintingKey() || code == KeyEvent.KEYCODE_SPACE) return true;
    return code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z;
  }

  private String currentText() { return mode == SEARCH ? searchQ : urlText; }

  private int kbKind() {
    if (mode == SEARCH) return 0;
    if (urlKind == U_PIN) return 3;
    if (urlKind == U_UA || urlKind == U_CLOUD || urlKind == U_XTU || urlKind == U_XTP) return 2;
    return 1;
  }

  void kbOpen() {
    if (host != null && textMode()) host.kbShow(currentText(), kbKind(), true);
  }

  void syncKb() {
    if (host != null && textMode()) host.kbSet(currentText());
  }

  /** Текст из скрытого поля ввода изменился. */
  void onKbText(String s) {
    if (s == null) s = "";
    if (mode == SEARCH) {
      searchQ = s;
      fillSearch();
      lSearch.reset();
    } else if (mode == URL) {
      urlText = s;
    }
    invalidate();
  }

  /** Кнопка «Готово» / «Найти» на экранной клавиатуре. */
  void onKbAction() {
    if (mode == SEARCH) {
      if (searchHits.size() == 1) {
        searchI = 0;
        onOk(null);
      } else if (host != null) {
        host.kbHide();
      }
    } else if (mode == URL) {
      urlSubmit();
    }
  }

  void setImeHeight(int px) {
    if (imeH == px) return;
    imeH = px;
    invalidate();
  }

  private void doPaste() {
    String clip = Util.clipboard(getContext());
    if (clip.isEmpty()) {
      note(I18n.t("clipEmpty"), 1200);
      return;
    }
    if (mode == SEARCH) {
      searchQ = Util.clip(clip, 120);
      fillSearch();
      lSearch.reset();
    } else if (mode == URL) {
      urlText = urlKind == U_PIN ? Util.clip(clip.replaceAll("\\D", ""), 8)
          : Util.clip(clip, urlKind == U_CLOUD ? 65536 : 4096);
    }
    syncKb();
    invalidate();
  }

  private void doCopy() {
    String t = currentText();
    if (t == null || t.isEmpty()) {
      note(I18n.t("clipEmpty"), 1200);
      return;
    }
    Util.setClipboard(getContext(), t);
    note(I18n.t("copied"), 1200);
  }

  void openUrlEditor(int kind, int back) {
    urlKind = kind;
    urlBack = back;
    urlText = "";
    switch (kind) {
      case U_XTS: urlTitle = I18n.t("setXtSrv"); urlHint = I18n.t("urlXtSrvHint"); break;
      case U_XTU: urlTitle = I18n.t("setXtUser"); urlHint = I18n.t("urlXtUserHint"); break;
      case U_XTP: urlTitle = I18n.t("setXtPass"); urlHint = I18n.t("urlXtPassHint"); break;
      case U_UA: urlTitle = I18n.t("setUa"); urlHint = I18n.t("urlUaHint"); urlText = profile.st.userAgent; break;
      case U_EPG1: urlTitle = I18n.t("setEpgMain"); urlHint = I18n.t("urlEpgHint"); break;
      case U_EPG2: urlTitle = I18n.t("setEpgExtra"); urlHint = I18n.t("urlEpgHint"); break;
      case U_CLOUD: urlTitle = I18n.t("setCloudPaste"); urlHint = I18n.t("urlCloudHint"); break;
      case U_PIN: urlTitle = I18n.t("setPin"); urlHint = I18n.t("urlPinHint"); break;
      default: urlTitle = I18n.t("setAddUrl"); urlHint = I18n.t("urlPlHint"); break;
    }
    urlI = Ui.touch() ? 0 : 1;
    setMode(URL);
    syncKb();
  }

  /** Подтверждение редактора («ОК» или «Готово» на клавиатуре). */
  void urlSubmit() {
    String t = urlText == null ? "" : urlText.trim();
    int back = urlBack;
    switch (urlKind) {
      case U_PL: {
        if (!playlistUrlOk(t)) { note(I18n.t("noClipUrl"), 1600); return; }
        setMode(back);
        if (back == SETUP) addPlaylistInBg(t, this::bootLive);
        else addPlaylistInBg(t, () -> { if (epg != null) epg.start(); });
        return;
      }
      case U_PL_REP: {
        if (!playlistUrlOk(t)) { note(I18n.t("noClipUrl"), 1600); return; }
        int pi = plRepI;
        plRepI = -1;
        setMode(back);
        netExec.execute(() -> {
          boolean ok = profile.replacePlaylistFromClip(pi, t);
          post(() -> note(ok ? I18n.t("plOn") : I18n.t("plFail"), 1600));
        });
        return;
      }
      case U_XTS:
        if (t.isEmpty()) { note(I18n.t("noClipUrl"), 1600); return; }
        xtSrv = t;
        note(I18n.t("xtSrvSet"), 1600);
        setMode(back);
        return;
      case U_XTU:
        if (t.isEmpty()) { note(I18n.t("noClipUrl"), 1600); return; }
        xtUser = t;
        note(I18n.t("xtUserSet"), 1600);
        setMode(back);
        return;
      case U_XTP:
        if (t.isEmpty()) { note(I18n.t("noClipUrl"), 1600); return; }
        xtPass = t;
        note(I18n.t("xtPassSet"), 1600);
        setMode(back);
        return;
      case U_UA:
        if (t.isEmpty()) { note(I18n.t("uaNeed"), 1600); return; }
        profile.st.userAgent = Util.clip(t, 256);
        profile.saveSettings();
        setMode(back);
        return;
      case U_EPG1:
      case U_EPG2:
        /* «Основной» = роль 1, «Дополнительный» = роль 2; новая ссылка сразу забирает роль и качается */
        if (!profile.addEpgUrl(t, urlKind == U_EPG1 ? 1 : 2)) { note(I18n.t("noClipUrl"), 1600); return; }
        setMode(back);
        note(I18n.t("epgUpdating"), 2000);
        if (epg != null) epg.start(true);
        return;
      case U_CLOUD:
        setMode(back);
        applyCloudInBg(t);
        return;
      case U_PIN: {
        String d = t.replaceAll("\\D", "");
        if (d.isEmpty() || !d.equals(t)) { note(I18n.t("pinDigits"), 1600); return; }
        profile.st.parentalPin = Util.clip(d, 8);
        profile.saveSettings();
        setMode(back);
        return;
      }
      default:
        setMode(back);
    }
  }

  /** Вход в режим / выход из него: экранная клавиатура, сброс прокрутки. */
  private void modeChanged(int old) {
    if (mode != PAD) burgerOpen = false;
    if (old != mode) {
      /* при открытии любого экрана выделена первая строка: фокус не уходит «в никуда» */
      switch (mode) {
        case MENU: menuI = 0; lMenu.reset(); break;
        case SETTINGS:
          if (old == MENU || old == LIVE) {
            setI = 0;
            setPane = settingsNarrow() ? setPane : 0;
            lTabs.reset();
            lRows.reset();
          }
          break;
        case GUIDE:
          if (old != MENU && old != CHMENU && old != MULTI) {
            lGroups.reset();
            lChan.reset();
            lProg.reset();
            lDays.reset();
          }
          break;
        case SEARCH: lSearch.reset(); break;
        case HISTORY: histI = 0; lHist.reset(); break;
        case RECS: recI = 0; lRecs.reset(); break;
        case EXIT: exitI = 0; break;
        case SETUP: setupI = 0; break;
        case SHARE: shareI = 0; break;
        case ASK: askI = 0; break;
        case CHMENU: chmI = 0; break;
        case MULTI: if (old != SEARCH) { mvMenu = 0; mvI = 0; } break;
        default: break;
      }
    }
    if (host == null) return;
    if (textMode()) {
      if (Ui.touch() && !kbSuppress && old != mode) host.kbShow(currentText(), kbKind(), true);
      else if (old != mode) host.kbShow(currentText(), kbKind(), false);
    } else if (old == SEARCH || old == URL) {
      host.kbHide();
    }
  }
}

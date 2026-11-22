package com.mirefir.nativedev;

import android.Manifest;
import android.app.Activity;
import android.app.PictureInPictureParams;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.SystemClock;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.Rational;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import androidx.core.content.ContextCompat;
import androidx.core.content.FileProvider;
import androidx.documentfile.provider.DocumentFile;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MainActivity extends Activity implements PlayerEngine.Listener, Voice.Callback, Update.Listener {
  private static final int REQ_M3U = 11, REQ_FOLDER = 12, REQ_MIC = 13, REQ_BACKUP_SAVE = 14, REQ_BACKUP_OPEN = 15;
  private FrameLayout root;
  private SurfaceView video;
  private final SurfaceView[] tiles = new SurfaceView[4];
  private OverlayView overlay;
  private ClockView clock;
  /** Отступ часов от края экрана, px — не overscan меню. */
  private static final int CLOCK_GAP_PX = 30;
  /** Сдвиг часов в правом верхнем углу к краю: вверх и вправо, px. */
  private static final int CLOCK_TR_NUDGE_PX = 15;
  private Profile profile;
  private PlayerEngine player;
  private Epg epg;
  private Rec rec;
  private Voice voice;
  private Update update;
  private final Handler h = new Handler(Looper.getMainLooper());
  private int pendingAsk;
  private EditText kbInput;
  private boolean kbQuiet;
  private int lastKb = -1;
  private boolean barsHidden = true;
  private boolean pausedByUs;
  /** Системный жест «Назад» (API 33+): предиктивный и обычный свайп с края, кнопка «Назад» навигации. */
  private final BackGesture backGesture = new BackGesture();
  private final Runnable tick = new Runnable() {
    @Override public void run() {
      try {
        if (overlay != null) overlay.tick();
        updateIme();
      } catch (Throwable ignored) {
      } finally {
        h.postDelayed(this, 500);
      }
    }
  };

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN
        | WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN);
    immersive();
    cutoutMode(getWindow().getAttributes());
    getWindow().setAttributes(getWindow().getAttributes());
    Ui.init(this);
    profile = new Profile(this);
    root = new FrameLayout(this);
    root.setBackgroundColor(Color.BLACK);
    video = new SurfaceView(this);
    video.setZOrderOnTop(false);
    video.setZOrderMediaOverlay(false);
    ignoreFocus(video);
    root.addView(video, new FrameLayout.LayoutParams(
        FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    for (int i = 0; i < 4; i++) {
      tiles[i] = new SurfaceView(this);
      tiles[i].setVisibility(View.GONE);
      ignoreFocus(tiles[i]);
      root.addView(tiles[i]);
    }
    overlay = new OverlayView(this, profile);
    overlay.setBackgroundColor(Color.TRANSPARENT);
    overlay.setFocusable(false);
    overlay.setFocusableInTouchMode(false);
    clock = new ClockView(this, profile);
    initKb();
    root.setFocusable(true);
    root.setFocusableInTouchMode(true);
    root.setDescendantFocusability(ViewGroup.FOCUS_AFTER_DESCENDANTS);
    setContentView(root);
    takeKeyEvents(true);
    getWindow().getDecorView().setFocusable(true);
    root.requestFocus();
    root.post(this::attachUi);

    /* громкость Android — системная (приставка/телефон). Плеер сам не крутит второй регулятор и при старте систему не трогает. */
    player = new PlayerEngine(this, video);
    System.arraycopy(tiles, 0, player.tiles, 0, 4);
    player.attach(profile);
    player.setListener(this);
    player.start();
    grabSurface(video);
    for (int i = 0; i < 4; i++) grabSurface(tiles[i]);
    epg = new Epg(profile);
    rec = new Rec(profile);
    voice = new Voice(this, profile);
    voice.setCallback(this);
    update = new Update(this);
    update.listener = this;
    overlay.bind(this, player, epg, rec, voice, update);

    profile.st.playClass = Device.probe(this);
    profile.st.weak = profile.st.playClass <= Device.PLAY_LOW;
    profile.saveSessionLater();
    if (!profile.pl.isEmpty()) overlay.bootLive();
    new Thread(() -> {
      profile.refreshIfDue(false);
      runOnUiThread(() -> {
        if (!profile.pl.isEmpty()) epg.start();
        handleIntent(getIntent());
      });
    }, "boot").start();
    update.start();
    h.post(tick);
    backGesture.attach(this, this::systemBack);
  }

  /**
   * Картинка и окна идут под вырез камеры (короткие края / всегда), а панели, меню и текст
   * удерживаются в безопасной области: её считает {@code OverlayView.computeInsets}.
   */
  private static void cutoutMode(WindowManager.LayoutParams lp) {
    if (Build.VERSION.SDK_INT >= 30) {
      lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
    } else if (Build.VERSION.SDK_INT >= 28) {
      lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
    }
  }

  /** Системное «Назад»: те же правила, что у клавиши Назад (таблица WIN.md §11.5), без сворачивания плеера. */
  private void systemBack() {
    if (overlay != null) overlay.goBack();
    else finish();
  }

  /** Android 7–12: обычный путь «Назад»; на Android 13+ его заменяет {@link BackGesture}. */
  @Override
  @SuppressWarnings("deprecation")
  public void onBackPressed() {
    systemBack();
  }

  @Override
  public void onConfigurationChanged(Configuration newConfig) {
    super.onConfigurationChanged(newConfig);
    Ui.onConfig(this);
    immersive();
    /* размеры окна обновляются после раскладки: пересчёт сразу и ещё раз чуть позже */
    root.post(this::onSizeChanged);
    h.postDelayed(this::onSizeChanged, 300);
  }

  private void onSizeChanged() {
    if (overlay != null) overlay.onConfig();
    placeClock();
  }

  // ---- экранная клавиатура: скрытое поле ввода в окне активности (оверлей не получает фокус) ----
  private void initKb() {
    kbInput = new EditText(this);
    kbInput.setBackground(null);
    kbInput.setTextColor(Color.TRANSPARENT);
    kbInput.setCursorVisible(false);
    kbInput.setAlpha(0.01f);
    kbInput.setPadding(0, 0, 0, 0);
    kbInput.setFocusable(false);
    kbInput.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI
        | EditorInfo.IME_FLAG_NO_FULLSCREEN);
    kbInput.addTextChangedListener(new TextWatcher() {
      @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
      @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
      @Override public void afterTextChanged(Editable s) {
        if (!kbQuiet && overlay != null) overlay.onKbText(s.toString());
      }
    });
    kbInput.setOnEditorActionListener((v, actionId, ev) -> {
      if (overlay != null) overlay.onKbAction();
      return true;
    });
    root.addView(kbInput, new FrameLayout.LayoutParams(1, 1, Gravity.TOP | Gravity.START));
  }

  /** kind: 0 поиск, 1 ссылка, 2 текст, 3 цифры. show — показать IME сразу (на пульте — по ОК на поле). */
  void kbShow(String text, int kind, boolean show) {
    if (kbInput == null) return;
    int type;
    int action = EditorInfo.IME_ACTION_DONE;
    switch (kind) {
      case 0:
        type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        action = EditorInfo.IME_ACTION_SEARCH;
        break;
      case 1:
        type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        break;
      case 3:
        type = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_VARIATION_PASSWORD;
        break;
      default:
        type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        break;
    }
    kbInput.setInputType(type);
    kbInput.setImeOptions(action | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
    kbSet(text);
    kbInput.setFocusable(true);
    kbInput.setFocusableInTouchMode(true);
    kbInput.requestFocus();
    final InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    if (imm == null) return;
    imm.restartInput(kbInput);
    if (show) kbInput.post(() -> imm.showSoftInput(kbInput, 0));
  }

  void kbSet(String text) {
    if (kbInput == null) return;
    if (text == null) text = "";
    if (kbInput.getText().toString().equals(text)) return;
    kbQuiet = true;
    kbInput.setText(text);
    kbInput.setSelection(text.length());
    kbQuiet = false;
  }

  void kbHide() {
    if (kbInput == null) return;
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    if (imm != null) imm.hideSoftInputFromWindow(kbInput.getWindowToken(), 0);
    kbInput.clearFocus();
    kbInput.setFocusable(false);
    kbQuiet = true;
    kbInput.setText("");
    kbQuiet = false;
    if (root != null) root.requestFocus();
  }

  /** Высота экранной клавиатуры сообщается оверлею, чтобы списки и поля не уходили под неё. */
  private void updateIme() {
    if (overlay == null) return;
    View decor = getWindow().getDecorView();
    int kb = 0;
    Rect r = new Rect();
    decor.getWindowVisibleDisplayFrame(r);
    int full = decor.getRootView().getHeight();
    int d = full - r.bottom;
    if (full > 0 && d > full * 0.15f) kb = d;
    if (Build.VERSION.SDK_INT >= 30) {
      try {
        WindowInsets wi = decor.getRootWindowInsets();
        if (wi != null && wi.isVisible(WindowInsets.Type.ime())) {
          kb = Math.max(kb, wi.getInsets(WindowInsets.Type.ime()).bottom);
        } else if (wi != null) {
          kb = 0;
        }
      } catch (Throwable ignored) {}
    }
    if (kb != lastKb) {
      lastKb = kb;
      overlay.setImeHeight(kb);
    }
  }
  private static void ignoreFocus(View v) {
    if (v == null) return;
    v.setFocusable(false);
    v.setFocusableInTouchMode(false);
    v.setClickable(false);
    v.setLongClickable(false);
  }

  private void grabSurface(View v) {
    if (v == null) return;
    ignoreFocus(v);
    v.setOnTouchListener((view, ev) -> overlay != null && overlay.onTouchEvent(ev));
    v.setOnKeyListener((view, code, ev) -> overlay != null && overlay.onKey(code, ev));
    v.setOnGenericMotionListener((view, ev) -> overlay != null && overlay.onMotion(ev));
  }

  /**
   * Оверлей и часы — дети Activity, не TYPE_APPLICATION_SUB_PANEL.
   * Полноэкранный NOT_FOCUSABLE SubPanel на Amlogic держит пульт 4–10 с
   * (окно сверху не фокусное, InputDispatcher ждёт таймаут).
   */
  private void attachUi() {
    if (overlay.getParent() != null && overlay.getParent() != root) {
      try { getWindowManager().removeViewImmediate(overlay); } catch (Exception ignored) {}
    }
    if (overlay.getParent() == null) {
      root.addView(overlay, new FrameLayout.LayoutParams(
          FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }
    if (clock.getParent() != null && clock.getParent() != root) {
      try { getWindowManager().removeViewImmediate(clock); } catch (Exception ignored) {}
    }
    if (clock.getParent() == null) {
      FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp(110), dp(36));
      lp.gravity = Gravity.TOP | Gravity.END;
      lp.topMargin = CLOCK_GAP_PX - CLOCK_TR_NUDGE_PX;
      lp.rightMargin = CLOCK_GAP_PX - CLOCK_TR_NUDGE_PX;
      root.addView(clock, lp);
    }
    clock.bringToFront();
    if (kbInput == null || !kbInput.hasFocus()) root.requestFocus();
    placeClock();
  }

  boolean clockOnTop() {
    return clock != null && clock.getParent() != null;
  }

  void placeClock() {
    if (clock == null || clock.getParent() == null) return;
    int w = root.getWidth(), h = root.getHeight();
    if (w < 8 || h < 8) return;
    int cw = dp(110), ch = dp(36);
    if ("sm".equals(profile.st.clockSize)) { cw = dp(90); ch = dp(30); }
    else if ("lg".equals(profile.st.clockSize)) { cw = dp(150); ch = dp(48); }
    int gap = CLOCK_GAP_PX;
    String pos = profile.st.clockPos;
    /* правый верхний угол (по умолчанию): на 15 px выше и правее, чем общий отступ */
    int x = w - cw - Math.max(0, gap - CLOCK_TR_NUDGE_PX), y = Math.max(0, gap - CLOCK_TR_NUDGE_PX);
    if ("top-left".equals(pos)) { x = gap; y = gap; }
    else if ("bottom-left".equals(pos)) { x = gap; y = h - ch - gap; }
    else if ("bottom-right".equals(pos)) { x = w - cw - gap; y = h - ch - gap; }
    if (overlay != null && overlay.mode == OverlayView.PAD && (pos == null || pos.startsWith("bottom"))) {
      int lift = overlay.padLift();
      if (lift > 0) y = Math.max(gap, y - lift);
    }
    if (overlay != null && overlay.mode == OverlayView.PAD && update != null && update.chips
        && (pos == null || pos.startsWith("bottom"))) {
      y = Math.max(gap, y - dp(56));
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    boolean show = profile.st.clockEnabled;
    clock.setVisibility(show ? View.VISIBLE : View.GONE);
    ViewGroup.LayoutParams raw = clock.getLayoutParams();
    if (raw instanceof FrameLayout.LayoutParams) {
      FrameLayout.LayoutParams fl = (FrameLayout.LayoutParams) raw;
      if (fl.width != cw || fl.height != ch || fl.leftMargin != x || fl.topMargin != y
          || fl.gravity != (Gravity.TOP | Gravity.START)) {
        fl.width = cw;
        fl.height = ch;
        fl.gravity = Gravity.TOP | Gravity.START;
        fl.leftMargin = x;
        fl.topMargin = y;
        fl.rightMargin = 0;
        fl.bottomMargin = 0;
        clock.setLayoutParams(fl);
      }
    }
    clock.invalidate();
  }

  private void detachUi() {
    try {
      if (overlay != null && overlay.getParent() != null && overlay.getParent() != root)
        getWindowManager().removeViewImmediate(overlay);
    } catch (Exception ignored) {}
    try {
      if (clock != null && clock.getParent() != null && clock.getParent() != root)
        getWindowManager().removeViewImmediate(clock);
    } catch (Exception ignored) {}
  }

  private void immersive() {
    View decor = getWindow().getDecorView();
    if (!barsHidden) {
      decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
      return;
    }
    decor.setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
  }

  @Override
  protected void onResume() {
    super.onResume();
    attachUi();
    placeClock();
    if (pausedByUs && player != null) {
      player.setPaused(false);
      pausedByUs = false;
    }
    if (update != null && update.pendingInstall) update.apply();
  }

  @Override
  protected void onPause() {
    super.onPause();
    boolean recOn = rec != null && (rec.liveActive() || rec.timerStatus != Rec.OFF);
    if (!recOn && !isInPictureInPictureMode() && player != null && !player.paused()) {
      player.setPaused(true);
      pausedByUs = true;
    }
  }

  @Override
  protected void onStop() {
    super.onStop();
    voiceEntry(false);
    if (player != null && video.getHolder().getSurface() != null && !video.getHolder().getSurface().isValid()) {
      player.setSurface(null);
    }
  }

  @Override
  protected void onStart() {
    super.onStart();
    /* onStart/onStop, а не onResume/onPause: окно выбора приложения (ResolverActivity) ставит нас на паузу
       раньше, чем читает список, и выключенный алиас в нём пропал бы */
    voiceEntry(true);
    if (player != null) player.setSurface(video);
  }

  @Override
  public void onUserLeaveHint() {
    super.onUserLeaveHint();
    if (profile != null && profile.st.systemPip) enterSystemPip();
  }

  @Override
  public void onWindowFocusChanged(boolean hasFocus) {
    super.onWindowFocusChanged(hasFocus);
    if (hasFocus) {
      immersive();
      placeClock();
    }
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent event) {
    boolean typing = overlay != null && overlay.textMode();
    if (event != null && OverlayView.isVoiceKey(event.getKeyCode()) && !typing) {
      if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0 && overlay != null)
        overlay.startVoiceSearch();
      return true;
    }
    if (overlay != null && event != null && kbInput != null && kbInput.hasFocus() && overlay.wantsTextKey(event)) {
      return super.dispatchKeyEvent(event);
    }
    if (overlay != null && event != null && overlay.onKey(event.getKeyCode(), event)) return true;
    return super.dispatchKeyEvent(event);
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) {
    if (OverlayView.isVoiceKey(keyCode) && (overlay == null || !overlay.textMode())) {
      if ((event == null || event.getRepeatCount() == 0) && overlay != null) overlay.startVoiceSearch();
      return true;
    }
    return super.onKeyDown(keyCode, event);
  }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) {
    if (OverlayView.isVoiceKey(keyCode)) return true;
    return super.onKeyUp(keyCode, event);
  }

  @Override
  public boolean onSearchRequested() {
    if (overlay != null) overlay.startVoiceSearch();
    return true;
  }

  @Override
  public boolean dispatchTouchEvent(MotionEvent event) {
    if (overlay != null && overlay.onTouchEvent(event)) return true;
    return super.dispatchTouchEvent(event);
  }

  @Override
  public boolean dispatchGenericMotionEvent(MotionEvent event) {
    if (overlay != null && overlay.onMotion(event)) return true;
    return super.dispatchGenericMotionEvent(event);
  }

  /** Колёсико мыши, стик и крестовина геймпада: те же шаги, что у стрелок D-pad (см. OverlayView.onMotion). */
  @Override
  public boolean onGenericMotionEvent(MotionEvent event) {
    if (overlay != null && overlay.onMotion(event)) return true;
    return super.onGenericMotionEvent(event);
  }

  @Override
  protected void onDestroy() {
    h.removeCallbacks(tick);
    backGesture.detach(this);
    detachUi();
    rec.stopLive();
    voice.stop();
    player.release();
    super.onDestroy();
  }


  @Override
  public void onNote(String text, int ms) {
    overlay.note(text, ms);
  }

  @Override
  public void onCues() {
    if (overlay != null) overlay.postInvalidate();
  }

  @Override
  public void onReady() {
    grabSurface(video);
    layoutVideo();
    if (overlay != null) overlay.invalidate();
    placeClock();
  }

  @Override
  public void onAspect() {
    layoutVideo();
  }

  @Override
  public void onStatus(String text) {
    overlay.voiceStatus(text);
  }

  @Override
  public void onHeard(String text, int channel, boolean zap) {
    overlay.onHeard(text, channel, zap);
  }

  @Override
  public void duck(boolean on) {
    if (player != null) player.duck(on);
  }

  int padClockLift() {
    return overlay == null ? 0 : overlay.padLift();
  }

  boolean hasMicPermission() {
    return ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
  }

  void requestMic() {
    if (Build.VERSION.SDK_INT >= 23) requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, REQ_MIC);
  }

  void openAppSettings() {
    try {
      Intent i = new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
          Uri.parse("package:" + getPackageName()));
      startActivity(i);
    } catch (Exception ignored) {}
  }

  void startVoice() {
    if (!hasMicPermission()) {
      if (overlay != null) overlay.setMode(OverlayView.MICPERM);
      return;
    }
    if (!voice.hasMic()) {
      overlay.note(I18n.t("voiceNoMic"), 2200);
      return;
    }
    voice.start();
  }

  boolean enterSystemPip() {
    if (Build.VERSION.SDK_INT < 26) return false;
    if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE)) return false;
    try {
      PictureInPictureParams p = new PictureInPictureParams.Builder()
          .setAspectRatio(new Rational(16, 9))
          .setSourceRectHint(new Rect(0, 0, video.getWidth(), video.getHeight()))
          .build();
      return enterPictureInPictureMode(p);
    } catch (Exception e) {
      return false;
    }
  }

  void layoutPip(boolean on) {
    if (on) {
      int w = Math.max(dp(280), root.getWidth() / 3);
      int h = Math.round(w * 9f / 16f);
      FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
      lp.gravity = Gravity.BOTTOM | Gravity.END;
      lp.rightMargin = dp(20);
      lp.bottomMargin = dp(20);
      video.setLayoutParams(lp);
    } else {
      video.setLayoutParams(new FrameLayout.LayoutParams(
          FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }
  }

  private int dp(int v) {
    return Math.round(v * getResources().getDisplayMetrics().density);
  }

  void playFile(String path) {
    playFile(path == null ? null : Uri.fromFile(new File(path)), false);
  }

  void playFile(Uri uri) {
    playFile(uri, false);
  }

  void playFile(Uri uri, boolean fromRecs) {
    if (uri == null) return;
    player.playFile(uri, fromRecs);
    overlay.setMode(OverlayView.LIVE);
  }

  void toggleFullscreen() {
    barsHidden = !barsHidden;
    immersive();
  }

  void applyUpdate() {
    if (update != null) update.applyFrom(this);
  }

  void pickM3uOrBrowse() {
    try {
      Intent i = new Intent(Intent.ACTION_GET_CONTENT);
      i.setType("*/*");
      i.addCategory(Intent.CATEGORY_OPENABLE);
      startActivityForResult(Intent.createChooser(i, "M3U"), REQ_M3U);
    } catch (ActivityNotFoundException e) {
      if (overlay != null) overlay.openBrowse(0, new File("/storage"));
    }
  }

  private void layoutVideo() {
    if (overlay != null && overlay.mode == OverlayView.MULTI) return;
    if (root == null || video == null || profile == null) return;
    int rw = root.getWidth(), rh = root.getHeight();
    if (rw < 8 || rh < 8) return;
    String a = profile.st.aspect;
    FrameLayout.LayoutParams lp;
    if ("16:9".equals(a) || "4:3".equals(a)) {
      int aw = "4:3".equals(a) ? 4 : 16, ah = "4:3".equals(a) ? 3 : 9;
      int w = rw, h = w * ah / aw;
      if (h > rh) { h = rh; w = h * aw / ah; }
      lp = new FrameLayout.LayoutParams(w, h);
      lp.gravity = Gravity.CENTER;
    } else {
      lp = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT);
    }
    video.setLayoutParams(lp);
  }

  void layoutTiles(int n, int focus) {
    layoutTiles(n, focus, 0);
  }

  void layoutTiles(int n, int focus, int bottom) {
    int w = root.getWidth(), h = root.getHeight() - Math.max(0, bottom);
    if (w < 8 || h < 8) return;
    int gap = bottom > 0 ? dp(8) : 0;
    if (n <= 1) {
      FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
          FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT);
      lp.setMargins(gap, gap, gap, bottom + gap);
      video.setLayoutParams(lp);
      for (int i = 0; i < 4; i++) tiles[i].setVisibility(View.GONE);
      return;
    }
    /* ландшафт: 2×1 / 2×2; портрет: окна в столбик одно под другим */
    int[] grid = Ui.tileGrid(n, w, h);
    int cols = grid[0];
    int rows = grid[1];
    int tw = w / cols, th = h / rows;
    FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(tw - gap * 2, th - gap * 2);
    lp.gravity = Gravity.TOP | Gravity.START;
    lp.leftMargin = gap;
    lp.topMargin = gap;
    video.setLayoutParams(lp);
    for (int i = 1; i < 4; i++) {
      if (i < n) {
        FrameLayout.LayoutParams tlp = new FrameLayout.LayoutParams(tw - gap * 2, th - gap * 2);
        tlp.leftMargin = (i % cols) * tw + gap;
        tlp.topMargin = (i / cols) * th + gap;
        tiles[i].setLayoutParams(tlp);
        tiles[i].setVisibility(View.VISIBLE);
      } else tiles[i].setVisibility(View.GONE);
    }
  }

  void pickM3u() {
    try {
      Intent i = new Intent(Intent.ACTION_GET_CONTENT);
      i.setType("*/*");
      i.addCategory(Intent.CATEGORY_OPENABLE);
      startActivityForResult(Intent.createChooser(i, "M3U"), REQ_M3U);
    } catch (ActivityNotFoundException e) {
      if (overlay != null) overlay.note(I18n.t("folderNoPicker"), 2200);
    }
  }

  void pickFolder(int why) {
    pendingAsk = why;
    try {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
      i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
          | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
          | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
          | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
      startActivityForResult(i, REQ_FOLDER);
    } catch (ActivityNotFoundException e) {
      if (overlay != null) {
        overlay.note(I18n.t("folderNoPicker"), 2200);
        overlay.onFolderCancel(why);
      }
    }
  }

  void pickBackupSave() {
    try {
      Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("application/json");
      i.putExtra(Intent.EXTRA_TITLE, "mirefir-backup.json");
      startActivityForResult(Intent.createChooser(i, I18n.t("setExport")), REQ_BACKUP_SAVE);
    } catch (ActivityNotFoundException e) {
      if (overlay != null) overlay.openBrowse(1, new File("/storage"));
    }
  }

  void pickBackupOpen() {
    try {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("*/*");
      startActivityForResult(Intent.createChooser(i, I18n.t("setImport")), REQ_BACKUP_OPEN);
    } catch (ActivityNotFoundException e) {
      if (overlay != null) overlay.openBrowse(2, new File("/storage"));
    }
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    super.onActivityResult(requestCode, resultCode, data);
    if (requestCode == REQ_M3U) {
      if (resultCode != RESULT_OK || data == null || data.getData() == null) {
        if (overlay != null) overlay.note(I18n.t("fileNone"), 1600);
        return;
      }
      importM3u(data.getData());
      return;
    }
    if (requestCode == REQ_FOLDER) {
      if (resultCode != RESULT_OK || data == null || data.getData() == null) {
        if (overlay != null) overlay.onFolderCancel(pendingAsk);
        return;
      }
      Uri uri = data.getData();
      int take = data.getFlags()
          & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
      if (take == 0) take = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
      try {
        getContentResolver().takePersistableUriPermission(uri, take);
      } catch (SecurityException ignored) {}
      if (pendingAsk == 0) {
        profile.st.recPath = uri.toString();
        profile.st.recordingsEnabled = true;
        profile.saveSettings();
        String err = rec != null ? rec.pathError() : null;
        overlay.note(err != null ? err : I18n.t("folderSaved"), 1800);
        overlay.onFolderPicked(true);
      } else if (pendingAsk == 1) {
        overlay.note(writeBackupToTree(uri) ? I18n.t("backupSaved") : I18n.t("backupFail"), 1600);
        overlay.setMode(OverlayView.SETTINGS);
      } else {
        importBackupUri(uri);
      }
    }
    if (requestCode == REQ_BACKUP_SAVE) {
      if (resultCode != RESULT_OK || data == null || data.getData() == null) {
        if (overlay != null) overlay.openBrowse(1, new File("/storage"));
        return;
      }
      boolean ok = writeBackupToUri(data.getData());
      if (overlay != null) {
        overlay.note(ok ? I18n.t("backupSaved") : I18n.t("backupFail"), 1800);
        overlay.setMode(OverlayView.SETTINGS);
      }
      return;
    }
    if (requestCode == REQ_BACKUP_OPEN) {
      if (resultCode != RESULT_OK || data == null || data.getData() == null) {
        if (overlay != null) overlay.openBrowse(2, new File("/storage"));
        return;
      }
      importBackupUri(data.getData());
    }
  }

  /** Файл плейлиста (выбор на диске, «Поделиться» или открытие .m3u из другого приложения): копия и загрузка вне UI-потока. */
  void importM3u(Uri uri) {
    if (uri == null || overlay == null) return;
    overlay.note(I18n.t("plLoading"), 2500);
    new Thread(() -> {
      File tmp = new File(getCacheDir(), "picked.m3u");
      boolean ok = copyUri(uri, tmp) && profile.fetchPlaylist(tmp.getAbsolutePath());
      runOnUiThread(() -> {
        if (ok) {
          overlay.note(I18n.t("plOn"), 1600);
          overlay.bootLive();
          epg.start();
        } else overlay.note(I18n.t("plFail"), 1600);
      });
    }, "m3u-import").start();
  }

  private static final Pattern URL_IN_TEXT = Pattern.compile("https?://[^\\s\"'<>]+", Pattern.CASE_INSENSITIVE);

  @Override
  protected void onNewIntent(Intent intent) {
    super.onNewIntent(intent);
    setIntent(intent);
    handleIntent(intent);
  }

  private long voiceIntentAt;

  private static boolean isVoiceAction(String a) {
    return "android.speech.action.WEB_SEARCH".equals(a) || Intent.ACTION_ASSIST.equals(a)
        || "android.intent.action.SEARCH_LONG_PRESS".equals(a) || Intent.ACTION_VOICE_COMMAND.equals(a)
        || "android.speech.action.VOICE_SEARCH_HANDS_FREE".equals(a);
  }

  /**
   * Кнопка микрофона на пультах без кода клавиши (G10S): система сама открывает выбор приложения на
   * WEB_SEARCH/ASSIST. Алиас {@code VoiceEntry} включён только пока мы на экране — тогда «Голос» наш.
   */
  private void voiceEntry(boolean on) {
    try {
      getPackageManager().setComponentEnabledSetting(new ComponentName(this, "com.mirefir.nativedev.VoiceEntry"),
          on ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED : PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
          PackageManager.DONT_KILL_APP);
    } catch (Exception ignored) {}
  }

  /**
   * Приём ссылки и файла из других приложений (приставки без буфера обмена):
   * {@code ACTION_SEND} (text/plain со ссылкой или файл .m3u) и {@code ACTION_VIEW} (http(s), .m3u/.m3u8).
   * Ссылка открывается в редакторе плейлиста и подтверждается ОК; файл загружается сразу.
   */
  private void handleIntent(Intent in) {
    if (in == null || overlay == null) return;
    String restore = in.getStringExtra("mirefir_restore");
    if (restore != null && !restore.isEmpty()) {
      setIntent(new Intent(Intent.ACTION_MAIN));
      importBackupFile(new File(restore));
      return;
    }
    String action = in.getAction();
    if (isVoiceAction(action)) {
      setIntent(new Intent(Intent.ACTION_MAIN));
      long now = SystemClock.uptimeMillis();
      if (now - voiceIntentAt > 1500) { /* удержание кнопки может прислать несколько интентов подряд */
        voiceIntentAt = now;
        overlay.startVoiceSearch();
      }
      return;
    }
    boolean send = Intent.ACTION_SEND.equals(action), view = Intent.ACTION_VIEW.equals(action);
    if (!send && !view) return;
    setIntent(new Intent(Intent.ACTION_MAIN)); /* не обрабатывать повторно после возврата в приложение */
    String url = null;
    Uri file = null;
    if (send) {
      CharSequence t = in.getCharSequenceExtra(Intent.EXTRA_TEXT);
      if (t != null) {
        Matcher m = URL_IN_TEXT.matcher(t);
        if (m.find()) url = m.group().replaceAll("[.,;:!?)\\]]+$", "");
      }
      if (url == null) file = streamExtra(in);
    } else {
      Uri d = in.getData();
      if (d != null) {
        String sc = d.getScheme();
        if ("http".equalsIgnoreCase(sc) || "https".equalsIgnoreCase(sc)) url = d.toString();
        else file = d;
      }
    }
    if (url != null) overlay.incomingUrl(url);
    else if (file != null) importIncomingFile(file);
    else overlay.note(I18n.t("noClipUrl"), 1600);
  }

  private boolean looksLikeBackup(Uri uri) {
    String n = uri != null && uri.getLastPathSegment() != null ? uri.getLastPathSegment().toLowerCase(Locale.ROOT) : "";
    if (n.endsWith(".json") || n.contains("backup") || n.contains("mirefir")) return true;
    String t = getIntent() != null && getIntent().getType() != null ? getIntent().getType().toLowerCase(Locale.ROOT) : "";
    return t.contains("json");
  }

  void shareCode(int dest) {
    String code = Cloud.make(profile);
    if (code == null || code.isEmpty()) {
      if (overlay != null) overlay.note(I18n.t("codeNeed"), 1600);
      return;
    }
    Util.setClipboard(this, code);
    if (dest <= 0) {
      if (overlay != null) overlay.note(I18n.t("codeCopied"), 1600);
      return;
    }
    Intent i = new Intent(Intent.ACTION_SEND);
    i.setType("text/plain");
    i.putExtra(Intent.EXTRA_TEXT, code);
    i.putExtra(Intent.EXTRA_SUBJECT, "MirEfir");
    sendShare(i, dest, I18n.t("sharePaste"));
  }

  void shareBackup(int dest) {
    File f = new File(profile.backupsDir(), "mirefir-backup.json");
    if (profile.exportBackup(f) == null) {
      if (overlay != null) overlay.note(I18n.t("backupFail"), 1600);
      return;
    }
    if (dest <= 0) {
      if (overlay != null) overlay.note(I18n.t("backupCopied"), 2000);
      return;
    }
    Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".files", f);
    Intent i = new Intent(Intent.ACTION_SEND);
    i.setType("application/json");
    i.putExtra(Intent.EXTRA_STREAM, uri);
    i.putExtra(Intent.EXTRA_SUBJECT, "MirEfir");
    i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    sendShare(i, dest, I18n.t("sharePasteFile"));
  }

  private void sendShare(Intent i, int dest, String fallback) {
    String[] pkg = {null, "org.telegram.messenger", "com.whatsapp", "ru.oneme.app", null};
    try {
      if (dest > 0 && dest < pkg.length && pkg[dest] != null) {
        Intent spec = new Intent(i);
        spec.setPackage(pkg[dest]);
        try {
          startActivity(spec);
          if (overlay != null) overlay.note(I18n.t("shareSent"), 1600);
          return;
        } catch (ActivityNotFoundException ignored) {}
      }
      startActivity(Intent.createChooser(i, I18n.t(dest == 4 ? "shareMail" : "shareTitle")));
      if (overlay != null) overlay.note(I18n.t("shareSent"), 1600);
    } catch (Exception e) {
      if (overlay != null) overlay.note(fallback, 2000);
    }
  }

  private boolean writeBackupToUri(Uri uri) {
    File tmp = new File(getCacheDir(), "mirefir-backup.json");
    if (profile.exportBackup(tmp) == null) return false;
    try (InputStream in = new FileInputStream(tmp);
         OutputStream out = getContentResolver().openOutputStream(uri)) {
      if (out == null) return false;
      byte[] buf = new byte[8192];
      int n;
      while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
      out.flush();
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  private boolean writeBackupToTree(Uri tree) {
    File tmp = new File(getCacheDir(), "mirefir-backup.json");
    if (profile.exportBackup(tmp) == null) return false;
    DocumentFile root = DocumentFile.fromTreeUri(this, tree);
    if (root == null) return false;
    DocumentFile json = root.findFile("mirefir-backup.json");
    if (json == null) json = root.createFile("application/json", "mirefir-backup.json");
    if (json == null) return false;
    try (InputStream in = new FileInputStream(tmp); OutputStream out = getContentResolver().openOutputStream(json.getUri())) {
      if (out == null) return false;
      byte[] buf = new byte[8192];
      int n;
      while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  private void importIncomingFile(Uri uri) {
    if (uri == null || overlay == null) return;
    overlay.note(I18n.t("plLoading"), 2000);
    new Thread(() -> {
      File tmp = new File(getCacheDir(), "incoming.bin");
      boolean copied = copyUri(uri, tmp);
      String text = copied ? Util.read(tmp) : null;
      boolean backup = looksLikeBackup(uri) || looksLikeBackupText(text);
      boolean ok = backup ? copied && profile.importBackup(text) : copied && profile.fetchPlaylist(tmp.getAbsolutePath());
      runOnUiThread(() -> {
        if (ok && backup) {
          overlay.note(I18n.t("backupLoaded"), 2000);
          overlay.bootLive();
          if (epg != null) epg.start();
        } else if (ok) {
          overlay.note(I18n.t("plOn"), 1600);
          overlay.bootLive();
          if (epg != null) epg.start();
        } else overlay.note(I18n.t(backup ? "backupFail" : "plFail"), 2000);
      });
    }, "incoming-file").start();
  }

  void importBackupUri(Uri uri) {
    if (uri == null || overlay == null) return;
    overlay.note(I18n.t("plLoading"), 2000);
    new Thread(() -> {
      File tmp = new File(getCacheDir(), "restore.json");
      boolean copied = copyBackupFromUri(uri, tmp);
      boolean ok = copied && profile.importBackup(Util.read(tmp));
      runOnUiThread(() -> {
        if (ok) {
          overlay.note(I18n.t("backupLoaded"), 2000);
          overlay.bootLive();
          if (epg != null) epg.start();
        } else overlay.note(I18n.t("backupFail"), 2000);
      });
    }, "backup-import").start();
  }

  void importBackupFile(File f) {
    if (f == null) {
      if (overlay != null) overlay.note(I18n.t("backupFail"), 2000);
      return;
    }
    overlay.note(I18n.t("plLoading"), 2000);
    new Thread(() -> {
      boolean ok = profile.importBackup(Util.read(f));
      runOnUiThread(() -> {
        if (ok) {
          overlay.note(I18n.t("backupLoaded"), 2000);
          overlay.bootLive();
          if (epg != null) epg.start();
        } else overlay.note(I18n.t("backupFail"), 2000);
      });
    }, "backup-import").start();
  }

  private boolean copyBackupFromUri(Uri uri, File dest) {
    if (copyUri(uri, dest) && looksLikeBackupText(Util.read(dest))) return true;
    DocumentFile root = DocumentFile.fromTreeUri(this, uri);
    if (root == null) root = DocumentFile.fromSingleUri(this, uri);
    DocumentFile json = findBackupJson(root, 0);
    return json != null && copyUri(json.getUri(), dest) && looksLikeBackupText(Util.read(dest));
  }

  private static boolean looksLikeBackupText(String t) {
    if (t == null) return false;
    String s = t.trim();
    return s.startsWith("{") && (s.contains("playlistText") || s.contains("favoriteIds") || s.contains("\"settings\"")
        || s.contains("playlists"));
  }

  private static DocumentFile findBackupJson(DocumentFile dir, int depth) {
    if (dir == null || depth > 3) return null;
    if (dir.isFile()) {
      String n = dir.getName() == null ? "" : dir.getName().toLowerCase(Locale.ROOT);
      return n.endsWith(".json") ? dir : null;
    }
    DocumentFile named = null, any = null;
    DocumentFile[] kids = dir.listFiles();
    if (kids == null) return null;
    for (DocumentFile f : kids) {
      if (f.isDirectory()) {
        DocumentFile inner = findBackupJson(f, depth + 1);
        if (inner != null && "mirefir-backup.json".equalsIgnoreCase(inner.getName())) return inner;
        if (named == null) named = inner;
        continue;
      }
      String n = f.getName() == null ? "" : f.getName().toLowerCase(Locale.ROOT);
      if (n.equals("mirefir-backup.json")) return f;
      if (n.endsWith(".json") && n.contains("mirefir")) named = f;
      else if (n.endsWith(".json") && any == null) any = f;
    }
    return named != null ? named : any;
  }

  @SuppressWarnings("deprecation")
  private static Uri streamExtra(Intent in) {
    try {
      if (Build.VERSION.SDK_INT >= 33) return in.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class);
      return in.getParcelableExtra(Intent.EXTRA_STREAM);
    } catch (Exception e) {
      return null;
    }
  }

  private boolean copyUri(Uri uri, File dest) {
    try (InputStream in = getContentResolver().openInputStream(uri);
         FileOutputStream out = new FileOutputStream(dest)) {
      if (in == null) return false;
      byte[] buf = new byte[8192];
      int n;
      while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    if (requestCode == REQ_MIC && grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
      if (overlay != null && overlay.mode == OverlayView.MICPERM) overlay.setMode(OverlayView.SEARCH);
      voice.start();
    } else if (requestCode == REQ_MIC && overlay != null) overlay.setMode(OverlayView.MICPERM);
  }
}

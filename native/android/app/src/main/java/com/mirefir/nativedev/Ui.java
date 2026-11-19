package com.mirefir.nativedev;

import android.app.UiModeManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.SystemClock;

/**
 * Режим интерфейса.
 * REMOTE — приставка / пульт / клавиатура: нижняя панель из 10 кнопок, безопасная зона 5%, крупный шрифт.
 * TOUCH — телефон или планшет, последний ввод был пальцем: мобильная раскладка.
 * На устройстве с тачскрином режим переключается по последнему вводу (касание ↔ клавиши).
 */
final class Ui {
  static final int REMOTE = 0, TOUCH = 1;

  private static boolean tv;
  private static boolean touchCapable;
  private static boolean tablet;
  private static volatile boolean lastTouch;
  /** Момент последней смены режима, для плавного появления панели. */
  static volatile long changedAt;

  private Ui() {}

  static void init(Context c) {
    UiModeManager um = (UiModeManager) c.getSystemService(Context.UI_MODE_SERVICE);
    boolean television = um != null && um.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION;
    PackageManager pm = c.getPackageManager();
    boolean leanback = pm.hasSystemFeature(PackageManager.FEATURE_LEANBACK);
    boolean ts = pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN);
    tv = television || leanback;
    touchCapable = ts && !tv;
    tablet = c.getResources().getConfiguration().smallestScreenWidthDp >= 600;
    lastTouch = touchCapable;
  }

  /** Поворот, складной экран, DeX: размеры изменились. */
  static void onConfig(Context c) {
    tablet = c.getResources().getConfiguration().smallestScreenWidthDp >= 600;
  }

  /** Есть ли у устройства экран, которого можно касаться. */
  static boolean touchCapable() { return touchCapable; }

  /** Приставка или устройство без тачскрина: нужна безопасная зона. */
  static boolean overscan() { return tv || !touchCapable; }

  static boolean tablet() { return tablet; }

  /** Класс ширины по ширине рабочей области в dp: узкий / средний / широкий. */
  static final int W_NARROW = 0, W_MID = 1, W_WIDE = 2;
  static final float DP_MID = 600f, DP_WIDE = 840f;

  /** < 600 dp — одна колонка на экран; 600–840 — две колонки со сдвигом; ≥ 840 — все колонки рядом. */
  static int widthClass(float widthDp) {
    return widthDp >= DP_WIDE ? W_WIDE : widthDp >= DP_MID ? W_MID : W_NARROW;
  }

  /** Портрет: рабочая область выше, чем шире. */
  static boolean portrait(float w, float h) { return h > w; }

  /**
   * Сетка мультиэкрана: {столбцы, строки}. В ландшафте 2×1 или 2×2 как в Windows,
   * в портрете окна выстраиваются в столбик.
   */
  static int[] tileGrid(int n, float w, float h) {
    if (n <= 1) return new int[] {1, 1};
    if (portrait(w, h)) return new int[] {1, n};
    return new int[] {2, n <= 2 ? 1 : 2};
  }

  static int mode() { return touchCapable && lastTouch ? TOUCH : REMOTE; }

  static boolean touch() { return mode() == TOUCH; }

  /** Касание или мышь. Возвращает true, если режим сменился. */
  static boolean onTouchInput() {
    if (!touchCapable || lastTouch) return false;
    lastTouch = true;
    changedAt = SystemClock.uptimeMillis();
    return true;
  }

  /** Клавиша пульта, D-pad или клавиатуры. Возвращает true, если режим сменился. */
  static boolean onKeyInput() {
    if (!lastTouch) return false;
    lastTouch = false;
    changedAt = SystemClock.uptimeMillis();
    return true;
  }
}

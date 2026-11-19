package com.mirefir.nativedev;

import android.app.Activity;
import android.os.Build;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import androidx.annotation.RequiresApi;

/**
 * Системный жест «Назад» для targetSdk 35/36.
 * Android 13+ (API 33): {@link OnBackInvokedCallback} в диспетчере активности перехватывает свайп с края экрана
 * и кнопку «Назад» навигации — приложение не сворачивается, а идёт по таблице «Назад» (WIN.md §11.5).
 * Старые версии (API 24–32): обычный {@code Activity.onBackPressed} и перехват {@code KEYCODE_BACK}
 * в {@code MainActivity}. Класс API 33 вынесен во вложенный, чтобы старые устройства его не загружали.
 */
final class BackGesture {
  private Object callback;

  void attach(Activity a, Runnable onBack) {
    if (Build.VERSION.SDK_INT >= 33 && callback == null) callback = Api33.attach(a, onBack);
  }

  void detach(Activity a) {
    if (Build.VERSION.SDK_INT >= 33 && callback != null) {
      Api33.detach(a, callback);
      callback = null;
    }
  }

  @RequiresApi(33)
  private static final class Api33 {
    static Object attach(Activity a, Runnable onBack) {
      OnBackInvokedCallback cb = onBack::run;
      a.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb);
      return cb;
    }

    static void detach(Activity a, Object cb) {
      a.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback((OnBackInvokedCallback) cb);
    }
  }
}

package com.mirefir.nativedev;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;
import java.util.Calendar;
import java.util.Locale;

final class ClockView extends View {
  private final Profile profile;
  private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Runnable tick = new Runnable() {
    @Override public void run() {
      invalidate();
      postDelayed(this, 1000);
    }
  };

  ClockView(Context c, Profile p) {
    super(c);
    profile = p;
    setWillNotDraw(false);
    setClickable(false);
    setFocusable(false);
    setFocusableInTouchMode(false);
    setLayerType(LAYER_TYPE_SOFTWARE, null);
    text.setFakeBoldText(true);
  }

  @Override
  protected void onAttachedToWindow() {
    super.onAttachedToWindow();
    removeCallbacks(tick);
    post(tick);
  }

  @Override
  protected void onDetachedFromWindow() {
    removeCallbacks(tick);
    super.onDetachedFromWindow();
  }

  @Override
  protected void onDraw(Canvas c) {
    if (profile == null || !profile.st.clockEnabled) return;
    Calendar cal = Calendar.getInstance();
    String t = String.format(Locale.US, "%02d:%02d",
        cal.get(Calendar.HOUR_OF_DAY), cal.get(Calendar.MINUTE));
    float size = "lg".equals(profile.st.clockSize) ? dp(28)
        : "sm".equals(profile.st.clockSize) ? dp(16) : dp(22);
    text.setTextSize(size);
    int a = Math.round(255 * Math.max(40, Math.min(100, profile.st.clockOpacity)) / 100f);
    text.setColor(Color.argb(a, 255, 255, 255));
    boolean right = profile.st.clockPos == null || profile.st.clockPos.contains("right");
    float y = getHeight() / 2f - (text.descent() + text.ascent()) / 2f;
    if (right) {
      text.setTextAlign(Paint.Align.RIGHT);
      c.drawText(t, getWidth() - dp(4), y, text);
    } else {
      text.setTextAlign(Paint.Align.LEFT);
      c.drawText(t, dp(4), y, text);
    }
  }

  private float dp(float v) {
    return v * getResources().getDisplayMetrics().density;
  }
}

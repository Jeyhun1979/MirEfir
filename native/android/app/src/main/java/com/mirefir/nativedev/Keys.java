package com.mirefir.nativedev;

import android.view.KeyEvent;
import java.util.Locale;

/** Имена клавиш пульта: Android KEYCODE_* и Windows-имена из резервной копии. */
final class Keys {
  private Keys() {}

  static String store(int code) {
    if (code == KeyEvent.KEYCODE_SPACE) return " ";
    if (code == KeyEvent.KEYCODE_DPAD_LEFT) return "ArrowLeft";
    if (code == KeyEvent.KEYCODE_DPAD_RIGHT) return "ArrowRight";
    if (code == KeyEvent.KEYCODE_DPAD_UP) return "ArrowUp";
    if (code == KeyEvent.KEYCODE_DPAD_DOWN) return "ArrowDown";
    if (code == KeyEvent.KEYCODE_ESCAPE) return "Escape";
    if (code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_NUMPAD_ENTER) return "Enter";
    if (code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z)
      return String.valueOf((char) ('a' + (code - KeyEvent.KEYCODE_A)));
    if (code >= KeyEvent.KEYCODE_0 && code <= KeyEvent.KEYCODE_9)
      return String.valueOf((char) ('0' + (code - KeyEvent.KEYCODE_0)));
    if (code >= KeyEvent.KEYCODE_F1 && code <= KeyEvent.KEYCODE_F12)
      return "F" + (code - KeyEvent.KEYCODE_F1 + 1);
    String s = KeyEvent.keyCodeToString(code);
    if (s != null && s.startsWith("KEYCODE_")) s = s.substring(8);
    return s == null ? "" : s;
  }

  static String label(String name) {
    if (name == null || name.isEmpty()) return "";
    if (" ".equals(name) || "Space".equalsIgnoreCase(name) || "space".equals(name)) return "Space";
    return name;
  }

  static boolean is(String name, int code) {
    int want = toCode(name);
    return want != 0 && want == code;
  }

  static int toCode(String name) {
    if (name == null || name.isEmpty()) return 0;
    if (" ".equals(name) || "Space".equalsIgnoreCase(name) || "space".equals(name))
      return KeyEvent.KEYCODE_SPACE;
    if ("Enter".equalsIgnoreCase(name) || "enter".equals(name)) return KeyEvent.KEYCODE_ENTER;
    if ("Escape".equalsIgnoreCase(name) || "Esc".equalsIgnoreCase(name)) return KeyEvent.KEYCODE_ESCAPE;
    if ("ArrowLeft".equals(name)) return KeyEvent.KEYCODE_DPAD_LEFT;
    if ("ArrowRight".equals(name)) return KeyEvent.KEYCODE_DPAD_RIGHT;
    if ("ArrowUp".equals(name)) return KeyEvent.KEYCODE_DPAD_UP;
    if ("ArrowDown".equals(name)) return KeyEvent.KEYCODE_DPAD_DOWN;
    if (name.length() == 1) {
      char c = name.charAt(0);
      if (c >= 'a' && c <= 'z') return KeyEvent.KEYCODE_A + (c - 'a');
      if (c >= 'A' && c <= 'Z') return KeyEvent.KEYCODE_A + (c - 'A');
      if (c >= '0' && c <= '9') return KeyEvent.KEYCODE_0 + (c - '0');
    }
    if (name.length() >= 2 && (name.charAt(0) == 'F' || name.charAt(0) == 'f')) {
      try {
        int n = Integer.parseInt(name.substring(1));
        if (n >= 1 && n <= 12) return KeyEvent.KEYCODE_F1 + n - 1;
      } catch (NumberFormatException ignored) {}
    }
    if (name.charAt(0) == '#' ) {
      try { return Integer.parseInt(name.substring(1)); } catch (NumberFormatException ignored) {}
    }
    String k = name.startsWith("KEYCODE_") ? name : "KEYCODE_" + name.toUpperCase(Locale.ROOT);
    int c = KeyEvent.keyCodeFromString(k);
    return c == KeyEvent.KEYCODE_UNKNOWN ? 0 : c;
  }
}

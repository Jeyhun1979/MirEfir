package com.mirefir.nativedev;

import android.app.Application;

public class App extends Application {
  public static final String VERSION = "1.0.26";
  public static final String GITHUB_API =
      "https://api.github.com/repos/Jeyhun1979/MirEfir/releases/latest";
  private static App inst;

  @Override
  public void onCreate() {
    super.onCreate();
    inst = this;
  }

  public static App get() {
    return inst;
  }
}

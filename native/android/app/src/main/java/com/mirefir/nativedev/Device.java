package com.mirefir.nativedev;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;

/** Класс устройства как Windows device.c: PLAY_MIN=0 … PLAY_HIGH=3. В UI не показывается. */
final class Device {
  static final int PLAY_MIN = 0, PLAY_LOW = 1, PLAY_MID = 2, PLAY_HIGH = 3;

  static int probe(Context ctx) {
    int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
    long ramMb = 0;
    try {
      ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
      if (am != null) {
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        ramMb = mi.totalMem / (1024 * 1024);
      }
    } catch (Exception ignored) {}
    String abi = "";
    if (Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0) abi = Build.SUPPORTED_ABIS[0];
    String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "";
    String board = (Build.BOARD + " " + Build.HARDWARE + " " + soc + " " + abi).toLowerCase();
    boolean hevc = hasHevc();
    boolean weakCpu = board.contains("atom") || board.contains("mt6580") || board.contains("sc8830")
        || board.contains("rk312") || cores <= 2;
    if (weakCpu || (!hevc && cores <= 2)) return PLAY_MIN;
    if (hevc && (cores >= 4 || ramMb >= 3072)) return PLAY_HIGH;
    if (hevc || (cores >= 4 && ramMb >= 2048) || cores >= 4) return PLAY_MID;
    return PLAY_LOW;
  }

  static boolean hasHevc() {
    try {
      android.media.MediaCodecList list = new android.media.MediaCodecList(android.media.MediaCodecList.REGULAR_CODECS);
      for (android.media.MediaCodecInfo info : list.getCodecInfos()) {
        if (info.isEncoder()) continue;
        for (String t : info.getSupportedTypes()) {
          if (t != null && t.toLowerCase().contains("hevc")) return true;
        }
      }
    } catch (Exception ignored) {}
    return false;
  }
}

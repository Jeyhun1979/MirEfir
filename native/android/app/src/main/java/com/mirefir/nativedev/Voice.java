package com.mirefir.nativedev;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONObject;
import org.vosk.Model;
import org.vosk.Recognizer;

final class Voice {
  static final int IDLE = 0, BUSY = 1, HEARD = 2, EMPTY = 3, NEEDMIC = 4, FAIL = 5;
  interface Callback {
    void onStatus(String text);
    void onHeard(String text, int channel, boolean zap);
    void duck(boolean on);
  }

  /* пороги записи фразы — те же, что в voice.c на Windows */
  private static final double SPEECH_RMS = 0.015, PEAK_SILENT = 0.008;
  private static final long SILENCE_MS = 2000, MAX_LISTEN_MS = 10000, WAIT_SPEECH_MS = 5000;

  private final Context ctx;
  private final Profile profile;
  private SpeechRecognizer rec;
  private Callback cb;
  private final Handler h = new Handler(Looper.getMainLooper());
  private boolean retryBusy;
  private Thread voskTh;
  private volatile boolean voskStop;
  private volatile int state = IDLE;
  private volatile String lastText = "";
  private Model voskModel;

  Voice(Context ctx, Profile profile) {
    this.ctx = ctx;
    this.profile = profile;
  }

  void setCallback(Callback cb) { this.cb = cb; }

  int state() { return state; }

  boolean available() {
    return voskReady() || SpeechRecognizer.isRecognitionAvailable(ctx);
  }

  static File modelDir(Profile p) {
    File d = new File(p.dir, "vosk");
    String name = I18n.en() ? "vosk-model-small-en-us-0.15" : "vosk-model-small-ru-0.22";
    return new File(d, name);
  }

  static String modelUrl() {
    if (I18n.en()) return "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip";
    return "https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip";
  }

  boolean voskReady() {
    File d = modelDir(profile);
    if (new File(d, "am/final.mdl").isFile() || new File(d, "conf/model.conf").isFile()) return true;
    File[] kids = d.isDirectory() ? d.listFiles() : null;
    if (kids != null) {
      for (File k : kids) {
        if (new File(k, "am/final.mdl").isFile() || new File(k, "conf/model.conf").isFile()) return true;
      }
    }
    return false;
  }

  File voskRoot() {
    File d = modelDir(profile);
    if (new File(d, "am/final.mdl").isFile() || new File(d, "conf/model.conf").isFile()) return d;
    File[] kids = d.isDirectory() ? d.listFiles() : null;
    if (kids != null) {
      for (File k : kids) {
        if (k.isDirectory() && (new File(k, "am/final.mdl").isFile() || new File(k, "conf/model.conf").isFile()))
          return k;
      }
    }
    return d;
  }

  private volatile boolean downloading;

  boolean downloading() { return downloading; }

  /**
   * Скачать и распаковать модель Vosk в профиль. Статус (с процентами) идёт через Callback.onStatus,
   * по окончании done выполняется в главном потоке; готовность проверять через voskReady().
   */
  void downloadModel(Runnable done) {
    if (downloading) {
      if (done != null) h.post(done);
      return;
    }
    downloading = true;
    new Thread(() -> {
      try {
        status(I18n.t("voiceDlModel") + " 0%");
        File zip = new File(profile.dir, "vosk-model.zip");
        final int[] last = {-5};
        boolean got = Http.getFile(modelUrl(), "MirEfir/1.0", zip, 120000, 1024 * 1024, (d, total) -> {
          if (total <= 0) return;
          int pct = (int) Math.min(100, d * 100L / total);
          if (pct >= last[0] + 5) {
            last[0] = pct;
            status(I18n.t("voiceDlModel") + " " + pct + "%");
          }
        });
        if (!got) {
          status(I18n.t("voskFail"));
        } else {
          status(I18n.t("voiceUnModel"));
          File dest = modelDir(profile);
          dest.mkdirs();
          if (!Util.unzip(zip, dest.getParentFile() == null ? dest : dest.getParentFile())) {
            status(I18n.t("voskFail"));
          } else {
            status(I18n.t("voiceLoad"));
          }
        }
        try { zip.delete(); } catch (Exception ignored) {}
      } catch (Exception e) {
        status(I18n.t("voskFail"));
      }
      downloading = false;
      if (done != null) h.post(done);
    }, "vosk-dl").start();
  }

  void start() {
    stop();
    if (!profile.st.voiceEnabled) {
      state = FAIL;
      if (cb != null) cb.onStatus(I18n.t("voiceOffMenu"));
      return;
    }
    if (profile.pl.isEmpty()) {
      state = FAIL;
      if (cb != null) cb.onStatus(I18n.t("voiceNoPl"));
      return;
    }
    if (!hasMic()) {
      state = FAIL;
      if (cb != null) cb.onStatus(I18n.t("voiceNoMic"));
      return;
    }
    if (voskReady()) {
      startVosk();
      return;
    }
    if (!SpeechRecognizer.isRecognitionAvailable(ctx)) {
      state = FAIL;
      if (cb != null) cb.onStatus(I18n.t("voiceNoEngine"));
      return;
    }
    startAndroid();
  }

  private void startAndroid() {
    rec = SpeechRecognizer.createSpeechRecognizer(ctx);
    rec.setRecognitionListener(new RecognitionListener() {
      @Override public void onReadyForSpeech(Bundle p) { status(I18n.t("voiceListenHint")); }
      @Override public void onBeginningOfSpeech() {}
      @Override public void onRmsChanged(float v) {}
      @Override public void onBufferReceived(byte[] b) {}
      @Override public void onEndOfSpeech() {}
      @Override public void onError(int error) {
        if (error == SpeechRecognizer.ERROR_RECOGNIZER_BUSY && !retryBusy) {
          retryBusy = true;
          h.postDelayed(() -> start(), 500);
          return;
        }
        state = error == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS ? NEEDMIC : EMPTY;
        status(errText(error));
        duck(false);
      }
      @Override public void onResults(Bundle b) { handle(b); duck(false); }
      @Override public void onPartialResults(Bundle b) {}
      @Override public void onEvent(int a, Bundle b) {}
    });
    Intent i = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
    i.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
    i.putExtra(RecognizerIntent.EXTRA_LANGUAGE, I18n.en() ? Locale.ENGLISH : new Locale("ru", "RU"));
    i.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5);
    i.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
    i.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, ctx.getPackageName());
    state = BUSY;
    duck(true);
    rec.startListening(i);
    status(I18n.t("voiceListenHint"));
  }

  private void startVosk() {
    state = BUSY;
    voskStop = false;
    duck(true);
    status(I18n.t("voicePrep"));
    voskTh = new Thread(this::voskLoop, "vosk");
    voskTh.start();
  }

  private void voskLoop() {
    AudioRecord ar = null;
    try {
      File root = voskRoot();
      if (voskModel == null) {
        status(I18n.t("voiceLoad"));
        voskModel = new Model(root.getAbsolutePath());
      }
      List<VoiceCmd.Ch> list = voiceList();
      int min = AudioRecord.getMinBufferSize(16000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
      ar = new AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, 16000,
          AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, Math.max(min, 4096));
      if (ar.getState() != AudioRecord.STATE_INITIALIZED) {
        state = FAIL;
        status(I18n.t("voiceNoMic"));
        return;
      }
      ar.startRecording();
      status(I18n.t("voiceListenHint"));
      /* как capture_pcm на Windows: пишем фразу целиком, конец — по паузе 2 с после речи, ждём речь до 5 с */
      byte[] buf = new byte[4096];
      byte[] pcm = new byte[32000 * 12];
      int used = 0, loud = 0;
      double peak = 0;
      long started = SystemClock.uptimeMillis(), lastLoud = 0;
      while (!voskStop) {
        int n = ar.read(buf, 0, buf.length);
        long now = SystemClock.uptimeMillis();
        if (n > 0) {
          int samples = n / 2;
          double sum = 0;
          for (int k = 0; k < samples; k++) {
            double v = (short) ((buf[2 * k] & 0xff) | (buf[2 * k + 1] << 8)) / 32768.0;
            sum += v * v;
            double a = Math.abs(v);
            if (a > peak) peak = a;
          }
          double rms = samples > 0 ? Math.sqrt(sum / samples) : 0;
          if (rms >= SPEECH_RMS) {
            loud++;
            lastLoud = now;
          }
          if (used + n <= pcm.length) {
            System.arraycopy(buf, 0, pcm, used, n);
            used += n;
          }
        }
        boolean speaking = loud >= 2;
        if (speaking && lastLoud > 0 && now - lastLoud >= SILENCE_MS) break;
        if (!speaking && now - started >= WAIT_SPEECH_MS) break;
        if (now - started >= MAX_LISTEN_MS || used >= pcm.length) break;
      }
      if (voskStop) {
        state = IDLE;
        return;
      }
      try { ar.stop(); } catch (Exception ignored) {}
      duck(false);
      if (peak < PEAK_SILENT) {
        state = EMPTY;
        status(I18n.t("voiceSilent"));
        return;
      }
      status(I18n.t("voiceHear"));
      String heard = "";
      int ch = -1;
      boolean zap = false;
      String shown = "";
      String grammar = VoiceCmd.grammar(list, I18n.en());
      for (int pass = 0; pass < 2 && !voskStop; pass++) {
        if (pass == 1 && ch >= 0) break;
        if (pass == 1 && grammar == null) break;
        String t = transcribe(pcm, used, pass == 0 ? grammar : null);
        if (t.isEmpty()) continue;
        VoiceCmd.Cmd cmd = VoiceCmd.parse(t);
        int c = VoiceCmd.pick(list, cmd.query);
        if (c < 0) c = VoiceCmd.pick(list, t);
        android.util.Log.i("MirEfirVoice", "pass=" + pass + " heard=\"" + t + "\" sw=" + cmd.sw + " ch=" + c
            + " grammar=" + (grammar == null ? 0 : grammar.length()));
        /* первый непустой результат запоминаем, второй проход (без грамматики) заменяет его, если нашёл канал */
        if (heard.isEmpty() || c >= 0) {
          heard = t;
          ch = c;
          zap = cmd.sw;
          shown = cmd.query.isEmpty() ? t : cmd.query;
        }
      }
      if (voskStop) {
        state = IDLE;
        return;
      }
      lastText = heard;
      if (heard.isEmpty()) {
        state = EMPTY;
        status(I18n.t("voiceEmpty"));
      } else {
        state = HEARD;
        final String t = shown;
        final int c = ch;
        final boolean z = zap && ch >= 0;
        if (cb != null) h.post(() -> cb.onHeard(t, c, z));
      }
    } catch (Throwable t) {
      android.util.Log.w("MirEfirVoice", "vosk: " + t);
      state = FAIL;
      status(I18n.t("voiceFailStart"));
    } finally {
      duck(false);
      if (ar != null) {
        try { ar.stop(); } catch (Exception ignored) {}
        try { ar.release(); } catch (Exception ignored) {}
      }
    }
  }

  /** Каналы для разбора фразы: без скрытых групп, как voice_thread на Windows. */
  private List<VoiceCmd.Ch> voiceList() {
    List<VoiceCmd.Ch> out = new ArrayList<>();
    int n = profile.pl.size();
    for (int i = 0; i < n && out.size() < 8192; i++) {
      Channel c = profile.pl.get(i);
      if (c == null || c.name == null || c.name.isEmpty()) continue;
      String grp = c.group == null || c.group.isEmpty() ? "Другие" : c.group;
      if (profile.st.hidden.contains(grp)) continue;
      VoiceCmd.Ch v = new VoiceCmd.Ch();
      v.index = i;
      v.fav = profile.isFav(i);
      v.name = c.name;
      v.tvg = c.tvgName == null ? "" : c.tvgName;
      out.add(v);
    }
    return out;
  }

  /**
   * Распознавание записанной фразы (transcribe на Windows): грамматика + до 3 вариантов, берём самый уверенный;
   * «[unk]» вычищаем.
   */
  private String transcribe(byte[] pcm, int bytes, String grammar) {
    if (voskModel == null || bytes < 3200) return "";
    Recognizer r = null;
    try {
      if (grammar != null) {
        try { r = new Recognizer(voskModel, 16000.0f, grammar); } catch (Throwable e) { r = null; }
      }
      if (r == null) r = new Recognizer(voskModel, 16000.0f);
      try { r.setMaxAlternatives(3); } catch (Throwable ignored) {}
      byte[] chunk = new byte[16000];
      for (int off = 0; off < bytes && !voskStop; off += 16000) {
        int n = Math.min(16000, bytes - off);
        System.arraycopy(pcm, off, chunk, 0, n);
        r.acceptWaveForm(chunk, n);
      }
      String raw = bestText(r.getFinalResult());
      /* с грамматикой незнакомое слово перед названием («включи», «поставь» вне фраз) приходит как «[unk]»:
         «[unk] россия» — это команда включить канал, а не просто поиск названия */
      if (grammar != null && raw.startsWith("[unk]") && !cleanHeard(raw).isEmpty()) {
        raw = (I18n.en() ? "turn on " : "включи ") + cleanHeard(raw);
      }
      return cleanHeard(raw);
    } catch (Throwable e) {
      android.util.Log.w("MirEfirVoice", "transcribe: " + e);
      return "";
    } finally {
      if (r != null) try { r.close(); } catch (Throwable ignored) {}
    }
  }

  private static String bestText(String raw) {
    if (raw == null) return "";
    try {
      JSONObject o = new JSONObject(raw);
      JSONArray alts = o.optJSONArray("alternatives");
      String best = "";
      double conf = -1;
      if (alts != null) {
        for (int i = 0; i < alts.length(); i++) {
          JSONObject a = alts.optJSONObject(i);
          if (a == null) continue;
          String t = a.optString("text", "").trim();
          double c = a.optDouble("confidence", 1);
          if (t.isEmpty() || (c > 0 && c < 0.12)) continue;
          if (best.isEmpty() || c > conf) {
            best = t;
            conf = c;
          }
        }
      }
      return best.isEmpty() ? o.optString("text", "").trim() : best;
    } catch (Exception e) {
      return jsonText(raw);
    }
  }

  private static String cleanHeard(String s) {
    return s == null ? "" : s.replace("[unk]", " ").replaceAll("\\s+", " ").trim();
  }

  private static String jsonText(String json) {
    if (json == null) return "";
    int i = json.indexOf("\"text\"");
    if (i < 0) i = json.indexOf("\"partial\"");
    if (i < 0) return "";
    int c = json.indexOf(':', i);
    int q = json.indexOf('"', c + 1);
    int q2 = json.indexOf('"', q + 1);
    if (q < 0 || q2 < 0) return "";
    return json.substring(q + 1, q2).trim();
  }

  void stop() {
    retryBusy = false;
    voskStop = true;
    state = IDLE;
    duck(false);
    if (rec != null) {
      try { rec.destroy(); } catch (Exception ignored) {}
      rec = null;
    }
  }

  private String errText(int error) {
    switch (error) {
      case SpeechRecognizer.ERROR_AUDIO:
      case SpeechRecognizer.ERROR_CLIENT:
        return I18n.t("voiceNoMic");
      case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS:
        return I18n.t("voiceNeedMic");
      case SpeechRecognizer.ERROR_NETWORK:
      case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
        return I18n.t("voiceNet");
      case SpeechRecognizer.ERROR_SERVER:
        return I18n.t("voiceNoEngine");
      default:
        return I18n.t("voiceEmpty");
    }
  }

  boolean hasMic() {
    AudioManager am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
    if (am == null) return false;
    if (Build.VERSION.SDK_INT >= 23) {
      AudioDeviceInfo[] all = am.getDevices(AudioManager.GET_DEVICES_INPUTS);
      for (AudioDeviceInfo d : all) {
        int t = d.getType();
        if (t == AudioDeviceInfo.TYPE_BUILTIN_MIC
            || t == AudioDeviceInfo.TYPE_WIRED_HEADSET
            || t == AudioDeviceInfo.TYPE_USB_DEVICE
            || t == AudioDeviceInfo.TYPE_BLUETOOTH_SCO)
          return true;
        if (Build.VERSION.SDK_INT >= 26 && t == AudioDeviceInfo.TYPE_USB_HEADSET)
          return true;
      }
      return false;
    }
    return ctx.getPackageManager().hasSystemFeature(PackageManager.FEATURE_MICROPHONE);
  }

  private void handle(Bundle b) {
    ArrayList<String> list = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
    if (list == null || list.isEmpty()) {
      state = EMPTY;
      status(I18n.t("voiceEmpty"));
      return;
    }
    String text = list.get(0);
    lastText = text;
    state = HEARD;
    List<VoiceCmd.Ch> chs = voiceList();
    VoiceCmd.Cmd cmd = VoiceCmd.parse(text);
    int c = VoiceCmd.pick(chs, cmd.query);
    if (c < 0) c = VoiceCmd.pick(chs, text);
    final int ch = c;
    final boolean zap = cmd.sw && ch >= 0;
    final String shown = cmd.query.isEmpty() ? text : cmd.query;
    if (cb != null) h.post(() -> cb.onHeard(shown, ch, zap));
  }

  private void duck(boolean on) {
    if (cb != null) h.post(() -> cb.duck(on));
  }

  private void status(String t) {
    if (cb != null) h.post(() -> cb.onStatus(t));
  }
}

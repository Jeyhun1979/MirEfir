package com.mirefir.nativedev;

import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.SurfaceView;
import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.Tracks;
import androidx.media3.common.text.Cue;
import androidx.media3.common.text.CueGroup;
import androidx.media3.common.VideoSize;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.datasource.DataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.datasource.UdpDataSource;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.Renderer;
import androidx.media3.exoplayer.audio.AudioRendererEventListener;
import androidx.media3.exoplayer.audio.AudioSink;
import androidx.media3.exoplayer.audio.DefaultAudioSink;
import androidx.media3.exoplayer.hls.DefaultHlsExtractorFactory;
import androidx.media3.exoplayer.hls.HlsMediaSource;
import androidx.media3.exoplayer.mediacodec.MediaCodecInfo;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import androidx.media3.exoplayer.source.BehindLiveWindowException;
import androidx.media3.exoplayer.source.MediaSource;
import androidx.media3.exoplayer.source.ProgressiveMediaSource;
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector;
import androidx.media3.extractor.DefaultExtractorsFactory;
import androidx.media3.extractor.ts.DefaultTsPayloadReaderFactory;
import io.github.anilbeesetti.nextlib.media3ext.ffdecoder.NextRenderersFactory;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

@UnstableApi
final class PlayerEngine implements Player.Listener {
  interface Listener {
    void onNote(String text, int ms);
    void onReady();
    void onCues();
    void onAspect();
  }

  private final Context ctx;
  private final Handler h = new Handler(Looper.getMainLooper());
  private final SurfaceView surface;
  final SurfaceView[] tiles = new SurfaceView[4];
  private ExoPlayer player;
  private final ExoPlayer[] extra = new ExoPlayer[4];
  private Profile profile;
  private Listener listener;
  int index = -1;
  int prev = -1;
  private List<String> urls = new ArrayList<>();
  private int urlI;
  private boolean altTried;
  private boolean archive;
  private long originS, originE, playS;
  private long lastPos;
  private int stall;
  private boolean loading;
  private String ua = "";
  private String ref = "";
  private final Runnable tick = this::tick;
  private final Runnable loadRun = this::loadCurrent;
  private int mvN;
  private int mvFocus;
  private final int[] mvCh = new int[] {-1, -1, -1, -1};
  int vidW, vidH;
  String audioLabel = "";
  /** Текущие реплики субтитров (текст или картинка), рисует OverlayView. */
  volatile List<Cue> cues = Collections.emptyList();
  private DefaultTrackSelector selector;
  private int subIdx = -1;
  boolean playingFile;
  boolean fromRecs;
  private long playTick;
  private long pauseFileMs;
  private boolean duckOn;
  private boolean quietReload;
  private boolean sawFrame;
  private boolean ffmpegOnce;
  private boolean useFfmpegVideo;
  private long readyAt;
  private int mvWant;
  private final int[] mvWantCh = new int[] {-1, -1, -1, -1};
  private int extraRetry;
  private final DefaultTrackSelector[] extraSel = new DefaultTrackSelector[4];
  private final int[] extraPlay = new int[] {-1, -1, -1, -1};

  PlayerEngine(Context ctx, SurfaceView surface) {
    this.ctx = ctx;
    this.surface = surface;
  }

  void setListener(Listener l) { listener = l; }
  void attach(Profile p) { profile = p; }

  void start() {
    rebuild();
    h.postDelayed(tick, 400);
  }

  void release() {
    h.removeCallbacks(tick);
    stopExtras();
    if (player != null) {
      player.release();
      player = null;
    }
  }

  private void rebuild() {
    int sys = readSysVol();
    if (player != null) {
      player.release();
      player = null;
    }
    Settings st = profile == null ? new Settings() : profile.st;
    DefaultRenderersFactory rf = audioFactory(st.decoder);
    DefaultTrackSelector tracks = new DefaultTrackSelector(ctx);
    tracks.setParameters(tracks.buildUponParameters()
        .setForceHighestSupportedBitrate(true)
        .setMaxVideoSize(4096, 2160)
        .setMaxVideoBitrate(Integer.MAX_VALUE)
        .setMaxAudioBitrate(Integer.MAX_VALUE)
        .setMaxAudioChannelCount(8)
        .setConstrainAudioChannelCountToDeviceCapabilities(false)
        .setExceedVideoConstraintsIfNecessary(true)
        .setViewportSize(4096, 2160, false)
        .setAllowVideoNonSeamlessAdaptiveness(false)
        .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)
        .build());
    selector = tracks;
    int readMs = st.bufferSec >= 30 ? 2000 : 1000;
    DefaultLoadControl lc = new DefaultLoadControl.Builder()
        .setBufferDurationsMs(3000, 8000, Math.min(800, readMs), 1500)
        .setTargetBufferBytes(8 * 1024 * 1024)
        .build();
    player = new ExoPlayer.Builder(ctx, rf).setLoadControl(lc).setTrackSelector(tracks).build();
    try { player.setForegroundMode(true); } catch (Exception ignored) {}
    player.setVideoSurfaceView(surface);
    player.addListener(this);
    player.setRepeatMode(Player.REPEAT_MODE_OFF);
    player.setAudioAttributes(new AudioAttributes.Builder()
        .setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), false);
    applyVolume();
    applyAspect();
    applyAudioLang();
    restoreSysVol(sys);
  }

  void applySettings() {
    quietReload = true;
    rebuild();
    if (index >= 0 && !playingFile) loadCurrent();
    quietReload = false;
  }

  void duck(boolean on) {
    duckOn = on;
    applyVolume();
    for (int i = 1; i < 4; i++) {
      if (extra[i] != null) extra[i].setPlayWhenReady(!on);
    }
  }

  void applyVolume() {
    if (mvWant > 1) {
      applyMvAudio();
      return;
    }
    if (player != null) player.setVolume(playerGain());
  }

  void applyAspect() {
    if (player == null || profile == null) return;
    String a = profile.st.aspect;
    int mode = C.VIDEO_SCALING_MODE_SCALE_TO_FIT;
    if ("zoom".equals(a)) mode = C.VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING;
    else if ("auto".equals(a) && vidW > 0 && vidH > 0 && vidW * 9 > vidH * 16 + vidH / 20)
      mode = C.VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING;
    player.setVideoScalingMode(mode);
    if (listener != null) listener.onAspect();
  }

  void applyAudioLang() {
    if (player == null || profile == null || selector == null) return;
    String lang = profile.st.audio;
    DefaultTrackSelector.Parameters.Builder b = selector.buildUponParameters();
    b.setPreferredAudioLanguage("orig".equals(lang) ? null : lang);
    selector.setParameters(b.build());
  }

  void play(int i, int startQ) {
    if (profile == null || i < 0 || i >= profile.pl.size()) return;
    h.removeCallbacks(loadRun);
    boolean changed = index != i;
    if (index >= 0 && changed) prev = index;
    index = i;
    archive = false;
    playingFile = false;
    fromRecs = false;
    Channel ch = profile.pl.get(i);
    ua = ch.ua;
    ref = ch.referer;
    urls = M3u.playUrls(profile.pl, i, startQ);
    urlI = 0;
    altTried = false;
    if (!quietReload) profile.rememberChannel(i);
    if (profile.st.externalPlayer) {
      stop();
      openExternal(urls.isEmpty() ? ch.url : urls.get(0));
      if (changed) note(ch.name, 1600);
      return;
    }
    vidW = vidH = 0;
    audioLabel = "";
    resetPictureWatch();
    loadCurrent();
    if (mvWant <= 1) applyVolume();
    if (changed) note(ch.name, 1600);
  }

  void playArchive(int i, List<String> list, long start, long end) {
    playArchive(i, list, start, end, start);
  }

  void playArchive(int i, List<String> list, long start, long end, long playAt) {
    if (profile == null || i < 0 || i >= profile.pl.size() || list == null || list.isEmpty()) return;
    h.removeCallbacks(loadRun);
    if (index >= 0 && index != i) prev = index;
    index = i;
    archive = true;
    originS = start;
    originE = end > start ? end : start + 3600_000L;
    playS = playAt > 0 ? playAt : start;
    Channel ch = profile.pl.get(i);
    ua = ch.ua;
    ref = ch.referer;
    urls = new ArrayList<>(list);
    urlI = 0;
    altTried = false;
    resetPictureWatch();
    loadCurrent();
    note(I18n.t("archive"), 1600);
  }

  boolean isArchive() { return archive; }

  void goLive() {
    if (index >= 0) play(index, 4);
  }

  void playFile(String path) {
    if (path == null) return;
    playFile(Uri.fromFile(new java.io.File(path)), false);
  }

  void playFile(Uri uri) {
    playFile(uri, false);
  }

  void playFile(Uri uri, boolean recs) {
    if (player == null || uri == null) return;
    archive = false;
    playingFile = true;
    fromRecs = recs;
    pauseFileMs = 0;
    playTick = SystemClock.elapsedRealtime();
    loading = true;
    stall = 0;
    resetPictureWatch();
    resetTrackChoices();
    player.setMediaItem(MediaItem.fromUri(uri));
    player.prepare();
    player.setPlayWhenReady(true);
  }

  void stop() {
    h.removeCallbacks(loadRun);
    loading = false;
    if (player == null) return;
    try {
      player.stop();
      player.clearMediaItems();
    } catch (Exception ignored) {}
  }

  void setSurface(SurfaceView sv) {
    if (player == null) return;
    if (sv == null) player.clearVideoSurface();
    else player.setVideoSurfaceView(sv);
  }

  private void loadCurrent() {
    if (player == null || urls.isEmpty()) return;
    if (urlI < 0 || urlI >= urls.size()) urlI = 0;
    String url = urls.get(urlI);
    loading = true;
    stall = 0;
    lastPos = 0;
    playTick = SystemClock.elapsedRealtime();
    playingFile = false;
    DataSource.Factory ds = dataFactory(url, headerUa(), ref);
    boolean hls = isHls(url);
    MediaItem.Builder itemB = new MediaItem.Builder().setUri(url);
    if (hls) itemB.setLiveConfiguration(new MediaItem.LiveConfiguration.Builder().build());
    MediaItem item = itemB.build();
    MediaSource src = mediaSource(ds, item, url);
    resetTrackChoices();
    player.setRepeatMode(Player.REPEAT_MODE_OFF);
    player.setMediaSource(src, true);
    player.prepare();
    player.setPlayWhenReady(true);
  }

  private DataSource.Factory dataFactory(String url, String userAgent, String referer) {
    if (url != null && (url.startsWith("udp:") || url.startsWith("rtp:"))) {
      return () -> new UdpDataSource(2000, 8000);
    }
    DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory()
        .setUserAgent(userAgent)
        .setAllowCrossProtocolRedirects(true)
        .setConnectTimeoutMs(8000)
        .setReadTimeoutMs(15000);
    Map<String, String> props = new HashMap<>();
    if (referer != null && !referer.isEmpty()) props.put("Referer", referer);
    if (!props.isEmpty()) http.setDefaultRequestProperties(props);
    return http;
  }

  private static DefaultExtractorsFactory tsExtractors() {
    return new DefaultExtractorsFactory()
        .setConstantBitrateSeekingEnabled(true)
        .setTsExtractorFlags(DefaultTsPayloadReaderFactory.FLAG_ALLOW_NON_IDR_KEYFRAMES
            | DefaultTsPayloadReaderFactory.FLAG_ENABLE_HDMV_DTS_AUDIO_STREAMS);
  }

  private static int tsFlags() {
    return DefaultTsPayloadReaderFactory.FLAG_ALLOW_NON_IDR_KEYFRAMES
        | DefaultTsPayloadReaderFactory.FLAG_ENABLE_HDMV_DTS_AUDIO_STREAMS;
  }

  private static MediaSource mediaSource(DataSource.Factory ds, MediaItem item, String url) {
    if (isHls(url)) {
      return new HlsMediaSource.Factory(ds)
          .setAllowChunklessPreparation(false)
          .setExtractorFactory(new DefaultHlsExtractorFactory(tsFlags(), true))
          .createMediaSource(item);
    }
    return new ProgressiveMediaSource.Factory(ds, tsExtractors()).createMediaSource(item);
  }

  private void resetPictureWatch() {
    boolean wasFfmpeg = useFfmpegVideo;
    sawFrame = false;
    ffmpegOnce = false;
    useFfmpegVideo = false;
    readyAt = 0;
    if (wasFfmpeg) rebuild();
  }

  private void tryFfmpegVideo() {
    if (ffmpegOnce || archive || playingFile || player == null) return;
    ffmpegOnce = true;
    useFfmpegVideo = true;
    sawFrame = false;
    readyAt = 0;
    quietReload = true;
    rebuild();
    quietReload = false;
    loadCurrent();
  }

  private static boolean isHls(String url) {
    if (url == null) return false;
    String low = url.toLowerCase();
    return low.contains(".m3u8") || low.contains("m3u8?") || low.contains("/hls");
  }

  private String headerUa() {
    if (ua != null && !ua.isEmpty()) return ua;
    if (profile != null && profile.st.userAgent != null && !profile.st.userAgent.isEmpty()) return profile.st.userAgent;
    return "MirEfir/1.0";
  }

  private void openExternal(String url) {
    try {
      Intent i = new Intent(Intent.ACTION_VIEW);
      i.setDataAndType(Uri.parse(url), "video/*");
      i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      ctx.startActivity(i);
    } catch (Exception e) {
      note(I18n.t("extFail"), 1600);
    }
  }

  @Override
  public void onRenderedFirstFrame() {
    sawFrame = true;
  }

  @Override
  public void onPlaybackStateChanged(int state) {
    if (state == Player.STATE_READY) {
      loading = false;
      stall = 0;
      if (!sawFrame && readyAt == 0) readyAt = SystemClock.elapsedRealtime();
      if (listener != null) listener.onReady();
    } else if (state == Player.STATE_ENDED) {
      stall = 0;
      loading = false;
      if (!archive && !playingFile) loadCurrent();
    }
  }

  @Override
  public void onVideoSizeChanged(VideoSize size) {
    vidW = size.width;
    vidH = size.height;
    applyAspect();
  }

  @Override
  public void onCues(CueGroup group) {
    cues = group == null ? Collections.<Cue>emptyList() : group.cues;
    if (listener != null) listener.onCues();
  }

  @Override
  public void onTracksChanged(Tracks tracks) {
    audioLabel = "";
    Tracks.Group firstAudio = null;
    boolean audioOn = false;
    for (int i = 0; i < tracks.getGroups().size(); i++) {
      Tracks.Group g = tracks.getGroups().get(i);
      if (g.getType() != C.TRACK_TYPE_AUDIO) continue;
      if (firstAudio == null) firstAudio = g;
      if (g.isSelected()) {
        audioOn = true;
        for (int t = 0; t < g.length; t++) {
          if (g.isTrackSelected(t)) {
            audioLabel = audioName(g.getTrackFormat(t));
            break;
          }
        }
      }
    }
    if (audioOn && audioLabel.isEmpty() && firstAudio != null && firstAudio.length > 0) {
      audioLabel = audioName(firstAudio.getTrackFormat(0));
    }
    if (player != null && player.getPlaybackState() == Player.STATE_READY
        && firstAudio == null && listener != null) {
      note(I18n.t("noAudio"), 2000);
    }
  }

  boolean paused() {
    return player != null && !player.getPlayWhenReady();
  }

  static String sizeQuality(int w, int h) {
    int m = Math.max(w, h);
    if (m >= 2100) return "4K";
    if (m >= 1000) return "FHD";
    if (m >= 700) return "HD";
    if (m > 0) return "SD";
    return "";
  }

  private static String audioName(Format f) {
    if (f == null || f.sampleMimeType == null) return "";
    String m = f.sampleMimeType.toLowerCase();
    if (m.contains("eac3") || m.contains("ec-3")) return "EAC3";
    if (m.contains("ac3")) return "AC3";
    if (m.contains("aac") || m.contains("mp4a")) return "AAC";
    if (m.contains("mpeg") || m.contains("mp3") || m.contains("mp2")) return "MPEG";
    if (m.contains("dts")) return "DTS";
    if (m.contains("opus")) return "Opus";
    if (m.contains("vorbis")) return "Vorbis";
    if (m.contains("pcm") || m.contains("raw")) return "PCM";
    int slash = m.indexOf('/');
    return slash >= 0 ? m.substring(slash + 1).toUpperCase() : m.toUpperCase();
  }

  @Override
  public void onPlayerError(PlaybackException error) {
    if (archive) {
      if (urlI + 1 < urls.size()) {
        urlI++;
        h.removeCallbacks(loadRun);
        h.postDelayed(loadRun, 200);
        return;
      }
      archiveUnavailable();
      return;
    }
    if (player != null && (error.errorCode == PlaybackException.ERROR_CODE_BEHIND_LIVE_WINDOW
        || causedBy(error, BehindLiveWindowException.class) || causedByName(error, "PlaylistStuck"))) {
      try {
        player.seekToDefaultPosition();
        player.prepare();
        player.setPlayWhenReady(true);
        loading = true;
        stall = 0;
      } catch (Exception ignored) {
        h.removeCallbacks(loadRun);
        h.postDelayed(loadRun, 800);
      }
      return;
    }
    boolean net = error.errorCode == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_FAILED
        || error.errorCode == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_TIMEOUT
        || error.errorCode == PlaybackException.ERROR_CODE_IO_UNSPECIFIED
        || error.errorCode == PlaybackException.ERROR_CODE_TIMEOUT
        || error.errorCode == PlaybackException.ERROR_CODE_IO_BAD_HTTP_STATUS;
    if (!altTried && urlI == 0 && urls.size() > 1) {
      altTried = true;
      urlI = 1;
      h.removeCallbacks(loadRun);
      h.postDelayed(loadRun, net ? 400 : 200);
      return;
    }
    urlI = 0;
    h.removeCallbacks(loadRun);
    h.postDelayed(loadRun, net ? 800 : 1200);
  }

  private void archiveUnavailable() {
    h.removeCallbacks(loadRun);
    loading = false;
    stall = 0;
    if (player != null) {
      try { player.stop(); } catch (Exception ignored) {}
    }
    note(I18n.t("archiveOff"), 2000);
  }

  private static boolean causedBy(PlaybackException error, Class<?> type) {
    Throwable c = error;
    while (c != null) {
      if (type.isInstance(c)) return true;
      c = c.getCause();
    }
    return false;
  }

  private static boolean causedByName(PlaybackException error, String part) {
    Throwable c = error;
    while (c != null) {
      String n = c.getClass().getSimpleName();
      if (n != null && n.contains(part)) return true;
      c = c.getCause();
    }
    return false;
  }

  private void tick() {
    h.postDelayed(tick, 400);
    if (player == null) return;
    if (archive) {
      if (!loading) return;
      stall += 400;
      if (stall >= 12000) {
        stall = 0;
        if (urlI + 1 < urls.size()) {
          urlI++;
          loadCurrent();
        } else {
          archiveUnavailable();
        }
      }
      return;
    }
    spawnMv();
    retryExtras();
    if (!player.getPlayWhenReady()) {
      stall = 0;
      return;
    }
    int state = player.getPlaybackState();
    if (state == Player.STATE_ENDED) {
      stall = 0;
      if (!archive && !playingFile) loadCurrent();
      return;
    }
    if (playingFile) return;
    if (!sawFrame && !ffmpegOnce && state == Player.STATE_READY && readyAt > 0
        && SystemClock.elapsedRealtime() - readyAt >= 2800) {
      tryFfmpegVideo();
      return;
    }
    if (loading) {
      stall += 400;
      if (stall >= 12000) {
        stall = 0;
        if (!altTried && urls.size() > 1) {
          altTried = true;
          urlI = 1;
        } else {
          urlI = 0;
        }
        loadCurrent();
      }
      return;
    }
    if (player.isPlaying()) {
      stall = 0;
      return;
    }
    if (state == Player.STATE_BUFFERING || state == Player.STATE_IDLE) stall += 400;
    else stall = 0;
    if (stall >= 8000) {
      stall = 0;
      loadCurrent();
    }
  }

  void pauseToggle() {
    if (player == null) return;
    boolean on = !player.getPlayWhenReady();
    player.setPlayWhenReady(on);
    if (playingFile || archive) {
      if (!on) pauseFileMs = player.getCurrentPosition();
      else playTick = SystemClock.elapsedRealtime() - Math.max(0, pauseFileMs);
    }
  }

  void setPaused(boolean on) {
    if (player == null) return;
    player.setPlayWhenReady(!on);
  }

  void seek(int seconds) {
    if (player == null) return;
    player.seekTo(Math.max(0, player.getCurrentPosition() + seconds * 1000L));
  }

  /** Короткое «Звук»: следующая звуковая дорожка по кругу, название — в тосте. */
  void cycleAudio() {
    if (player == null || selector == null) return;
    List<Tracks.Group> gs = trackGroups(C.TRACK_TYPE_AUDIO);
    List<int[]> items = new ArrayList<>();
    int cur = -1;
    for (int gi = 0; gi < gs.size(); gi++) {
      Tracks.Group g = gs.get(gi);
      for (int t = 0; t < g.length; t++) {
        if (!g.isTrackSupported(t)) continue;
        if (g.isSelected() && g.isTrackSelected(t)) cur = items.size();
        items.add(new int[] {gi, t});
      }
    }
    if (items.isEmpty()) {
      note(I18n.t("noAudio"), 1600);
      return;
    }
    if (items.size() == 1) {
      int[] only = items.get(0);
      note(I18n.t("audioTrack") + ": " + trackLabel(gs.get(only[0]).getTrackFormat(only[1]), 1, true)
          + " (" + I18n.t("audioOne") + ")", 2000);
      return;
    }
    int next = (cur + 1) % items.size();
    int[] it = items.get(next);
    Tracks.Group g = gs.get(it[0]);
    DefaultTrackSelector.Parameters.Builder b = selector.buildUponParameters();
    b.setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, false);
    b.setOverrideForType(new TrackSelectionOverride(g.getMediaTrackGroup(), it[1]));
    selector.setParameters(b.build());
    note(I18n.t("audioTrack") + ": " + trackLabel(g.getTrackFormat(it[1]), next + 1, true)
        + " (" + (next + 1) + "/" + items.size() + ")", 2000);
  }

  /** Удержание «Звук»: субтитры — следующая дорожка, после последней — выключены. */
  void cycleSubs() {
    if (player == null || selector == null) return;
    List<Tracks.Group> gs = trackGroups(C.TRACK_TYPE_TEXT);
    List<int[]> items = new ArrayList<>();
    for (int gi = 0; gi < gs.size(); gi++) {
      Tracks.Group g = gs.get(gi);
      for (int t = 0; t < g.length; t++) {
        if (g.isTrackSupported(t)) items.add(new int[] {gi, t});
      }
    }
    if (items.isEmpty()) {
      subIdx = -1;
      note(I18n.t("subNone"), 1800);
      return;
    }
    int next = subIdx + 1;
    DefaultTrackSelector.Parameters.Builder b = selector.buildUponParameters();
    if (next >= items.size()) {
      subIdx = -1;
      b.clearOverridesOfType(C.TRACK_TYPE_TEXT);
      b.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true);
      selector.setParameters(b.build());
      cues = Collections.emptyList();
      if (listener != null) listener.onCues();
      note(I18n.t("subTrack") + ": " + I18n.t("subOff"), 1800);
      return;
    }
    subIdx = next;
    int[] it = items.get(next);
    Tracks.Group g = gs.get(it[0]);
    b.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, false);
    b.setOverrideForType(new TrackSelectionOverride(g.getMediaTrackGroup(), it[1]));
    selector.setParameters(b.build());
    note(I18n.t("subTrack") + ": " + trackLabel(g.getTrackFormat(it[1]), next + 1, false)
        + " (" + (next + 1) + "/" + items.size() + ")", 2000);
  }

  /** Новая загрузка потока: выбор дорожки сбрасывается, субтитры выключены. */
  private void resetTrackChoices() {
    subIdx = -1;
    cues = Collections.emptyList();
    if (selector == null) return;
    DefaultTrackSelector.Parameters.Builder b = selector.buildUponParameters();
    b.clearOverridesOfType(C.TRACK_TYPE_AUDIO);
    b.clearOverridesOfType(C.TRACK_TYPE_TEXT);
    b.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true);
    selector.setParameters(b.build());
    if (listener != null) listener.onCues();
  }

  private List<Tracks.Group> trackGroups(int type) {
    List<Tracks.Group> out = new ArrayList<>();
    if (player == null) return out;
    for (Tracks.Group g : player.getCurrentTracks().getGroups()) {
      if (g.getType() == type) out.add(g);
    }
    return out;
  }

  private static String trackLabel(Format f, int n, boolean audio) {
    String name = f == null ? null : f.label;
    if ((name == null || name.isEmpty()) && f != null && f.language != null && !f.language.isEmpty()
        && !"und".equalsIgnoreCase(f.language)) {
      String d = Locale.forLanguageTag(f.language).getDisplayLanguage(I18n.en() ? Locale.ENGLISH : new Locale("ru"));
      if (d == null || d.isEmpty()) d = f.language;
      name = d.substring(0, 1).toUpperCase(Locale.ROOT) + d.substring(1);
    }
    if (name == null || name.isEmpty()) name = "#" + n;
    StringBuilder sb = new StringBuilder(name);
    if (audio && f != null) {
      String codec = audioName(f);
      if (!codec.isEmpty()) sb.append(" · ").append(codec);
      if (f.channelCount == 6) sb.append(" 5.1");
      else if (f.channelCount == 8) sb.append(" 7.1");
    }
    return sb.toString();
  }

  int volume() {
    if (streamMuted()) return 0;
    AudioManager am = audioMgr();
    if (am == null) return profile == null ? 100 : profile.st.volume;
    int max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
    if (max <= 0) return 100;
    return Math.max(0, Math.min(100, Math.round(100f * am.getStreamVolume(AudioManager.STREAM_MUSIC) / max)));
  }

  void addVolume(int d) {
    if (d == 0) return;
    AudioManager am = audioMgr();
    if (canUseSystemVol(am)) {
      int dir = d > 0 ? AudioManager.ADJUST_RAISE : AudioManager.ADJUST_LOWER;
      am.adjustStreamVolume(AudioManager.STREAM_MUSIC, dir, AudioManager.FLAG_SHOW_UI);
      if (profile != null) {
        profile.st.muted = streamMuted();
        profile.st.volume = volume();
        profile.saveSession();
      }
      return;
    }
    if (profile == null) return;
    profile.st.volume = Math.max(0, Math.min(100, profile.st.volume + d));
    profile.st.muted = false;
    applyVolume();
    profile.saveSession();
  }

  void toggleMute() {
    AudioManager am = audioMgr();
    if (canUseSystemVol(am)) {
      am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_TOGGLE_MUTE, AudioManager.FLAG_SHOW_UI);
      if (profile != null) {
        profile.st.muted = streamMuted();
        profile.st.volume = volume();
        profile.saveSession();
      }
      return;
    }
    if (profile == null) return;
    profile.st.muted = !profile.st.muted;
    applyVolume();
    profile.saveSession();
  }

  private AudioManager audioMgr() {
    try {
      return (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
    } catch (Exception e) {
      return null;
    }
  }

  /** Системный STREAM_MUSIC как есть: не поднимать и не восстанавливать из профиля. */
  private int readSysVol() {
    AudioManager am = audioMgr();
    if (!canUseSystemVol(am)) return -1;
    try {
      return am.getStreamVolume(AudioManager.STREAM_MUSIC);
    } catch (Exception e) {
      return -1;
    }
  }

  private void restoreSysVol(int v) {
    if (v < 0) return;
    AudioManager am = audioMgr();
    if (!canUseSystemVol(am)) return;
    try {
      if (am.getStreamVolume(AudioManager.STREAM_MUSIC) != v) {
        am.setStreamVolume(AudioManager.STREAM_MUSIC, v, 0);
      }
    } catch (Exception ignored) {}
  }

  private static boolean canUseSystemVol(AudioManager am) {
    if (am == null) return false;
    if (Build.VERSION.SDK_INT >= 21 && am.isVolumeFixed()) return false;
    return am.getStreamMaxVolume(AudioManager.STREAM_MUSIC) > 0;
  }

  private boolean streamMuted() {
    AudioManager am = audioMgr();
    if (am == null) return profile != null && profile.st.muted;
    if (Build.VERSION.SDK_INT >= 23) {
      try { return am.isStreamMute(AudioManager.STREAM_MUSIC); } catch (Exception ignored) {}
    }
    return am.getStreamVolume(AudioManager.STREAM_MUSIC) <= 0;
  }

  long positionMs() {
    if (player == null) return 0;
    if (paused() && pauseFileMs > 0 && (playingFile || archive)) {
      return archive ? playS + pauseFileMs : pauseFileMs;
    }
    long pos = player.getCurrentPosition();
    if (pos < 150 && playTick > 0) {
      long ticks = SystemClock.elapsedRealtime() - playTick;
      if (archive) return playS + ticks;
      if (playingFile) return ticks;
    }
    if (archive) return playS + pos;
    return pos;
  }

  boolean archiveBounds(long[] out) {
    if (!archive || originE <= originS) return false;
    out[0] = originS;
    out[1] = originE;
    out[2] = positionMs();
    return true;
  }

  void setMultiview(int n, int[] ch) {
    mvWant = Math.max(1, Math.min(4, n));
    mvN = mvWant;
    for (int i = 0; i < 4; i++) {
      int v = (ch != null && i < ch.length) ? ch[i] : -1;
      mvCh[i] = v;
      mvWantCh[i] = v;
    }
    if (mvWant <= 1) {
      stopExtras();
      return;
    }
    for (int i = 1; i < 4; i++) {
      if (i >= mvWant || mvWantCh[i] < 0) stopExtra(i);
    }
  }

  /** Выход из мультиэкрана: убрать соседние окна, вернуть звук основному эфиру. */
  void endMultiview() {
    mvWant = 1;
    mvN = 1;
    mvFocus = 0;
    stopExtras();
    setAudioEnabled(selector, true);
    if (player != null) {
      int sys = readSysVol();
      try {
        player.setAudioAttributes(new AudioAttributes.Builder()
            .setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), false);
      } catch (Exception ignored) {}
      restoreSysVol(sys);
    }
    applyVolume();
  }

  private int playIndex(int slot) {
    if (profile == null || slot < 0 || slot >= 4) return -1;
    int ch = mvWantCh[slot];
    if (ch < 0 || ch >= profile.pl.size()) return -1;
    if (slot == mvFocus) return ch;
    return M3u.lighterIndex(profile.pl, ch);
  }

  private void spawnMv() {
    if (mvWant <= 1) return;
    for (int i = 1; i < mvWant; i++) {
      if (mvWantCh[i] < 0) continue;
      int want = playIndex(i);
      if (extra[i] == null && extraPlay[i] < 0) {
        startExtra(i);
        return;
      }
      if (extra[i] != null && extraPlay[i] != want) {
        startExtra(i);
        return;
      }
    }
  }

  private void retryExtras() {
    extraRetry += 400;
    if (extraRetry < 8000) return;
    extraRetry = 0;
    for (int i = 1; i < 4; i++) {
      if (i >= mvWant || mvWantCh[i] < 0) continue;
      if (extra[i] == null) {
        startExtra(i);
        return;
      }
      int st = extra[i].getPlaybackState();
      if (st == Player.STATE_IDLE || st == Player.STATE_ENDED) startExtra(i);
    }
  }

  void focusPane(int i) {
    if (i < 0) i = 0;
    boolean changed = mvFocus != i;
    mvFocus = i;
    applyMvAudio();
    if (mvWant <= 1) return;
    syncMvMain();
    if (!changed) return;
    for (int s = 1; s < mvWant; s++) {
      int want = playIndex(s);
      if (want >= 0 && extraPlay[s] != want) startExtra(s);
    }
  }

  private void syncMvMain() {
    int want = playIndex(0);
    if (want < 0 || player == null) return;
    if (index != want) {
      quietReload = true;
      play(want, 4);
      quietReload = false;
    }
    setAudioEnabled(selector, mvFocus == 0);
  }

  /** Внутренний уровень плеера: всегда полный. Громкость — системная. Duck только для голоса. */
  private float playerGain() {
    return duckOn ? 0.08f : 1f;
  }

  private float vol() {
    return playerGain();
  }

  private static void setAudioEnabled(DefaultTrackSelector sel, boolean on) {
    if (sel == null) return;
    sel.setParameters(sel.buildUponParameters().setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, !on).build());
  }

  private void applyMvAudio() {
    boolean mainOn = mvWant <= 1 || mvFocus == 0;
    setAudioEnabled(selector, mainOn);
    if (player != null) player.setVolume(mainOn ? vol() : 0f);
    for (int k = 1; k < 4; k++) {
      boolean on = mvWant > 1 && k == mvFocus;
      setAudioEnabled(extraSel[k], on);
      if (extra[k] != null) extra[k].setVolume(on ? vol() : 0f);
    }
  }

  private void startExtra(int slot) {
    if (profile == null || slot < 1 || slot > 3 || tiles[slot] == null) return;
    int playCh = playIndex(slot);
    if (playCh < 0 || playCh >= profile.pl.size()) return;
    stopExtra(slot);
    Channel ch = profile.pl.get(playCh);
    List<String> u = M3u.playUrls(profile.pl, playCh, 4);
    if (u.isEmpty()) return;
    boolean focused = slot == mvFocus;
    try {
      DefaultTrackSelector tracks = new DefaultTrackSelector(ctx);
      tracks.setParameters(tracks.buildUponParameters()
          .setForceHighestSupportedBitrate(focused)
          .setMaxVideoSize(focused ? 4096 : 1280, focused ? 2160 : 720)
          .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)
          .setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, !focused)
          .setExceedVideoConstraintsIfNecessary(true)
          .build());
      DefaultLoadControl lc = new DefaultLoadControl.Builder()
          .setBufferDurationsMs(1000, 3000, 500, 1000)
          .setTargetBufferBytes(2 * 1024 * 1024)
          .build();
      extraSel[slot] = tracks;
      extra[slot] = new ExoPlayer.Builder(ctx, audioFactory())
          .setLoadControl(lc)
          .setTrackSelector(tracks)
          .build();
      extra[slot].setVideoSurfaceView(tiles[slot]);
      extra[slot].setRepeatMode(Player.REPEAT_MODE_OFF);
      extra[slot].setVolume(focused ? vol() : 0f);
      extra[slot].setAudioAttributes(new AudioAttributes.Builder()
          .setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), false);
      String url = u.get(0);
      DataSource.Factory ds = dataFactory(url, ch.ua.isEmpty() ? headerUa() : ch.ua, ch.referer);
      MediaItem item = MediaItem.fromUri(url);
      MediaSource src = mediaSource(ds, item, url);
      extra[slot].setMediaSource(src);
      extra[slot].prepare();
      extra[slot].setPlayWhenReady(!duckOn);
      extraPlay[slot] = playCh;
    } catch (Throwable t) {
      stopExtra(slot);
      extraPlay[slot] = playCh;
    }
  }

  private void stopExtra(int slot) {
    if (extra[slot] != null) {
      try { extra[slot].release(); } catch (Exception ignored) {}
      extra[slot] = null;
    }
    extraSel[slot] = null;
    extraPlay[slot] = -1;
  }

  void stopExtras() {
    for (int i = 1; i < 4; i++) stopExtra(i);
    mvN = 0;
  }

  boolean pipEnter() {
    return false;
  }

  private MediaCodecSelector codecSel(String decoder) {
    if ("hardware".equals(decoder)) {
      return (mime, secure, tunnel) -> {
        List<MediaCodecInfo> all = MediaCodecSelector.DEFAULT.getDecoderInfos(mime, secure, tunnel);
        List<MediaCodecInfo> hw = new ArrayList<>();
        for (MediaCodecInfo i : all) if (!i.softwareOnly) hw.add(i);
        return hw;
      };
    }
    if ("software".equals(decoder)) {
      return (mime, secure, tunnel) -> {
        List<MediaCodecInfo> all = MediaCodecSelector.DEFAULT.getDecoderInfos(mime, secure, tunnel);
        List<MediaCodecInfo> sw = new ArrayList<>();
        for (MediaCodecInfo i : all) if (i.softwareOnly) sw.add(i);
        return sw;
      };
    }
    return MediaCodecSelector.DEFAULT;
  }

  private DefaultRenderersFactory audioFactory() {
    return audioFactory(profile == null ? "auto" : profile.st.decoder);
  }

  private DefaultRenderersFactory audioFactory(String decoder) {
    NextRenderersFactory rf = new NextRenderersFactory(ctx) {
      @Override
      protected AudioSink buildAudioSink(Context context, boolean enableFloatOutput, boolean enableAudioTrackPlaybackParams) {
        return new DefaultAudioSink.Builder(context)
            .setEnableFloatOutput(false)
            .setEnableAudioTrackPlaybackParams(enableAudioTrackPlaybackParams)
            .build();
      }

      @Override
      protected void buildAudioRenderers(
          Context context,
          int extensionRendererMode,
          MediaCodecSelector mediaCodecSelector,
          boolean enableDecoderFallback,
          AudioSink audioSink,
          Handler eventHandler,
          AudioRendererEventListener eventListener,
          ArrayList<Renderer> out) {
        super.buildAudioRenderers(
            context,
            extensionRendererMode,
            mediaCodecSelector,
            enableDecoderFallback,
            audioSink,
            eventHandler,
            eventListener,
            out);
        AudioSink mpegSink = new DefaultAudioSink.Builder(context)
            .setEnableFloatOutput(false)
            .build();
        out.add(new MpegAudioRenderer(eventHandler, eventListener, mpegSink));
      }
    };
    if (useFfmpegVideo) {
      rf.setMediaCodecSelector((mime, secure, tunnel) -> {
        if (mime != null && mime.startsWith("video/")) return Collections.emptyList();
        return codecSel(decoder).getDecoderInfos(mime, secure, tunnel);
      });
      rf.setEnableDecoderFallback(true);
      rf.setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER);
    } else {
      rf.setMediaCodecSelector(codecSel(decoder));
      if ("hardware".equals(decoder)) {
        rf.setEnableDecoderFallback(false);
        rf.setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_OFF);
      } else if ("software".equals(decoder)) {
        rf.setEnableDecoderFallback(true);
        rf.setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER);
      } else {
        rf.setEnableDecoderFallback(true);
        rf.setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_ON);
      }
    }
    return rf;
  }

  private void note(String t, int ms) {
    if (listener != null) listener.onNote(t, ms);
  }
}

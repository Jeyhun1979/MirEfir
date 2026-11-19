package com.mirefir.nativedev;

import java.util.ArrayList;
import java.util.List;

final class Limits {
  static final int MAX_URL = 3072;
  static final int MAX_NAME = 192;
  static final int MAX_UA = 256;
  static final int MAX_REF = 512;
  static final int MAX_FAV = 512;
  static final int MAX_RECENT = 10;
  static final int MAX_SAVED = 32;
  static final int MAX_HIDDEN = 64;
  static final int MAX_PIN = 8;
  static final int MAX_EPG_PROG = 1400;
  static final int TITLE_MAX = 72;
  static final int DESC_MAX = 96;
  static final int DESC_NOW = 400;
}

final class Channel {
  String name = "";
  String url = "";
  String ua = "";
  String referer = "";
  String family = "";
  String group = "";
  String id = "";
  String tvgId = "";
  String tvgName = "";
  String catchup = "";
  String catchupSource = "";
  String logo = "";
  int catchupDays;
  int number;
  int quality = 3;
}

/** Плейлист: enabled всегда 1. EPG: 0 выкл, 1 основной, 2 дополнительный. */
final class SavedLink {
  String id = "";
  String name = "";
  String url = "";
  int enabled = 1;
}

final class Settings {
  String userAgent = "Mozilla/5.0 (Linux; Android 7) MirEfir/1.0";
  String decoder = "auto";
  String aspect = "auto";
  String audio = "ru";
  int bufferSec = 15;
  boolean rememberVolume = true;
  boolean autoplay = true;
  int volume = 95;
  boolean muted;
  boolean weak;
  int playClass;
  boolean clockEnabled = true;
  String clockPos = "top-right";
  String clockSize = "md";
  int clockOpacity = 90;
  boolean confirmExit = true;
  boolean voiceEnabled = true;
  boolean showNumbers = true;
  boolean showEpgProgress = true;
  boolean twoLineTitles = true;
  String theme = "dark";
  String highlight = "blue";
  int panelOpacity = 90;
  String fontSize = "md";
  String logoSize = "md";
  String channelSort = "playlist";
  int playlistUpdateHours = 6;
  long plFetchedMs;
  boolean externalPlayer;
  int epgOffsetHours;
  int epgDays = 7;
  boolean archiveEnabled = true;
  int archiveDays = 7;
  boolean showProgramDesc = true;
  int guideRows = 6;
  int epgUpdateHours = 6;
  boolean epgAutoUpdate = true;
  boolean recordingsEnabled = true;
  String recPath = "";
  boolean parentalEnabled;
  String parentalPin = "0000";
  boolean lockSettings;
  String language = "ru";
  /** Системный Android PiP при сворачивании. Кнопка PiP по-прежнему открывает мультиэкран. */
  boolean systemPip = true;
  String keyMenu = "F2";
  String keyMute = "m";
  String keyFav = " ";
  String keyFs = "F11";
  String keyGuide = "g";
  String keySearch = "s";
  String keyRec = "r";
  String keyPip = "p";
  String keyVoice = "v";
  String keyLive = "ArrowLeft";
  final List<String> hidden = new ArrayList<>();
  final List<String> locked = new ArrayList<>();
  final List<SavedLink> playlists = new ArrayList<>();
  final List<SavedLink> epgSrc = new ArrayList<>();
  String activePl = "";
  String lastName = "";
  String lastGroup = "";
  int lastIndex;
  final List<String> favIds = new ArrayList<>();
  final List<String> recentIds = new ArrayList<>();
  final List<Integer> fav = new ArrayList<>();
  final List<Integer> recent = new ArrayList<>();

  static Settings defaults() {
    return new Settings();
  }
}

final class Programme {
  long start;
  long end;
  String title = "";
  String desc = "";
}

#include "app.h"
#include "tinf.h"
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ctype.h>
#include <stdio.h>

static const wchar_t kNative[] = L"MirEfir-native";
static const wchar_t kLive[] = L"MirEfir";

/* Релиз работает в %AppData%\MirEfir (там же профиль 1.0.24). Отладочный профиль MirEfir-native — только если
   запуск из native\windows\run.bat (переменная MIREFIR_PROFILE=native). */
static const wchar_t *work_profile(void) {
  wchar_t v[16];
  DWORD n = GetEnvironmentVariableW(L"MIREFIR_PROFILE", v, 16);
  return n > 0 && n < 16 && _wcsicmp(v, L"native") == 0 ? kNative : kLive;
}

void profile_dirs(wchar_t *native_dir, wchar_t *live_dir, int max) {
  wchar_t appdata[MAX_PATH];
  appdata[0] = 0;
  SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata);
  if (native_dir && max > 0) {
    swprintf(native_dir, max, L"%s\\%s", appdata, work_profile());
    CreateDirectoryW(native_dir, NULL);
  }
  if (live_dir && max > 0) swprintf(live_dir, max, L"%s\\%s", appdata, kLive);
}

static void apply_volume(Settings *st, double raw) {
  if (raw > 0 && raw <= 1.0) st->volume = (int)(raw * 100.0 + 0.5);
  else if (raw > 1.0 && raw <= 100.0) st->volume = (int)(raw + 0.5);
  if (st->volume < 5) st->volume = 35;
  if (st->volume > 100) st->volume = 100;
}

static void defaults(Settings *st) {
  memset(st, 0, sizeof *st);
  snprintf(st->user_agent, MAX_UA, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) MirEfir/1.0");
  snprintf(st->decoder, sizeof st->decoder, "auto");
  snprintf(st->aspect, sizeof st->aspect, "auto");
  snprintf(st->audio, sizeof st->audio, "ru");
  st->buffer_sec = 15;
  st->remember_volume = 1;
  st->autoplay = 1;
  st->volume = 35;
  st->clock_enabled = 1;
  snprintf(st->clock_pos, sizeof st->clock_pos, "top-right");
  snprintf(st->clock_size, sizeof st->clock_size, "md");
  st->clock_opacity = 90;
  st->confirm_exit = 1;
  st->voice_enabled = 1;
  st->show_numbers = 1;
  st->show_epg_progress = 1;
  st->two_line_titles = 1;
  snprintf(st->theme, sizeof st->theme, "dark");
  snprintf(st->highlight, sizeof st->highlight, "blue");
  st->panel_opacity = 90;
  snprintf(st->font_size, sizeof st->font_size, "md");
  snprintf(st->logo_size, sizeof st->logo_size, "md");
  snprintf(st->channel_sort, sizeof st->channel_sort, "playlist");
  st->playlist_update_hours = 6;
  st->external_player = 0;
  st->epg_offset_hours = 0;
  st->epg_days = 7;
  st->archive_enabled = 1;
  st->archive_days = 7;
  st->show_program_desc = 1;
  st->guide_rows = 6;
  st->epg_update_hours = 6;
  st->epg_auto_update = 1;
  st->recordings_enabled = 1;
  snprintf(st->parental_pin, sizeof st->parental_pin, "0000");
  snprintf(st->language, sizeof st->language, "ru");
  snprintf(st->key_menu, MAX_KEY, "F2");
  snprintf(st->key_mute, MAX_KEY, "m");
  snprintf(st->key_fav, MAX_KEY, " ");
  snprintf(st->key_fs, MAX_KEY, "F11");
  snprintf(st->key_guide, MAX_KEY, "g");
  snprintf(st->key_search, MAX_KEY, "s");
  snprintf(st->key_rec, MAX_KEY, "r");
  snprintf(st->key_pip, MAX_KEY, "p");
  snprintf(st->key_voice, MAX_KEY, "v");
  snprintf(st->key_live, MAX_KEY, "ArrowLeft");
  st->last_index = 0;
}

static void add_epg_item(Settings *st, const char *id, const char *url, int enabled);
static void add_pl_item(Settings *st, const char *id, const char *name, const char *url);

void saved_name_from_url(const char *url, char *name, int max) {
  if (!name || max < 2) return;
  name[0] = 0;
  if (!url || !url[0]) {
    snprintf(name, max, "Источник");
    return;
  }
  const char *slash = url;
  for (const char *p = url; *p && *p != '?' && *p != '#'; p++) {
    if (*p == '/' || *p == '\\') slash = p + 1;
  }
  int i = 0;
  while (slash[i] && slash[i] != '?' && slash[i] != '#' && i < max - 1) {
    name[i] = slash[i];
    i++;
  }
  name[i] = 0;
  if (!name[0] || strcmp(name, "/") == 0) snprintf(name, max, "Источник");
}

void profile_sync_epg(Settings *st) {
  if (!st) return;
  st->epg_url[0] = st->epg_url2[0] = 0;
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 1 && st->epg_src[i].url[0]) {
      snprintf(st->epg_url, MAX_URL, "%s", st->epg_src[i].url);
      break;
    }
  }
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 2 && st->epg_src[i].url[0] && strcmp(st->epg_src[i].url, st->epg_url) != 0) {
      snprintf(st->epg_url2, MAX_URL, "%s", st->epg_src[i].url);
      break;
    }
  }
}

void sources_normalize_epg(Settings *st) {
  if (!st) return;
  int has_slot = 0;
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 2) has_slot = 1;
  }
  if (!has_slot) {
    int n = 0;
    for (int i = 0; i < st->epg_n; i++) {
      if (!st->epg_src[i].enabled || !st->epg_src[i].url[0]) {
        st->epg_src[i].enabled = 0;
        continue;
      }
      n++;
      st->epg_src[i].enabled = n == 1 ? 1 : n == 2 ? 2 : 0;
    }
  } else {
    int pri = 0, fb = 0;
    for (int i = 0; i < st->epg_n; i++) {
      if (st->epg_src[i].enabled == 1) {
        if (pri) st->epg_src[i].enabled = 0;
        else pri = 1;
      } else if (st->epg_src[i].enabled == 2) {
        if (fb) st->epg_src[i].enabled = 0;
        else fb = 1;
      } else {
        st->epg_src[i].enabled = 0;
      }
    }
  }
  profile_sync_epg(st);
}

void sources_replace_url(SavedLink *item, const char *url) {
  if (!item || !url || !url[0]) return;
  snprintf(item->url, MAX_URL, "%s", url);
  saved_name_from_url(url, item->name, MAX_NAME);
}

static void remove_saved(SavedLink *list, int *n, int i) {
  if (!list || !n || i < 0 || i >= *n) return;
  if (i < *n - 1) memmove(&list[i], &list[i + 1], (size_t)(*n - i - 1) * sizeof(SavedLink));
  (*n)--;
  memset(&list[*n], 0, sizeof(SavedLink));
}

void sources_remove_playlist(Settings *st, int i) {
  if (!st) return;
  int was_active = i >= 0 && i < st->pl_n && strcmp(st->playlists[i].id, st->active_pl) == 0;
  remove_saved(st->playlists, &st->pl_n, i);
  if (was_active || !st->active_pl[0]) {
    st->active_pl[0] = 0;
    if (st->pl_n) snprintf(st->active_pl, MAX_ID, "%s", st->playlists[0].id);
  }
}

void sources_remove_epg(Settings *st, int i) {
  if (!st) return;
  int role = (i >= 0 && i < st->epg_n) ? st->epg_src[i].enabled : 0;
  remove_saved(st->epg_src, &st->epg_n, i);
  if (role == 1) {
    for (int k = 0; k < st->epg_n; k++) {
      if (st->epg_src[k].enabled == 2) {
        st->epg_src[k].enabled = 1;
        break;
      }
    }
  }
  sources_normalize_epg(st);
}

void sources_set_epg_role(Settings *st, int i, int role) {
  if (!st || i < 0 || i >= st->epg_n) return;
  if (role != 1 && role != 2) {
    st->epg_src[i].enabled = 0;
    sources_normalize_epg(st);
    return;
  }
  for (int k = 0; k < st->epg_n; k++) {
    if (st->epg_src[k].enabled == role) st->epg_src[k].enabled = 0;
  }
  st->epg_src[i].enabled = role;
  if (role == 2) {
    int pri = 0;
    for (int k = 0; k < st->epg_n; k++) {
      if (k != i && st->epg_src[k].enabled == 1) pri = 1;
    }
    if (!pri) st->epg_src[i].enabled = 1;
  }
  sources_normalize_epg(st);
}

int sources_epg_role(const Settings *st, int i) {
  if (!st || i < 0 || i >= st->epg_n) return 0;
  int r = st->epg_src[i].enabled;
  return r == 1 || r == 2 ? r : 0;
}

int sources_add_playlist(Settings *st, const char *url) {
  if (!st || !url || !url[0]) return 0;
  int before = st->pl_n;
  add_pl_item(st, NULL, NULL, url);
  if (st->pl_n == before) return 0;
  if (!st->active_pl[0]) snprintf(st->active_pl, MAX_ID, "%s", st->playlists[st->pl_n - 1].id);
  return 1;
}

int sources_add_epg(Settings *st, const char *url) {
  if (!st || !url || !url[0]) return 0;
  int before = st->epg_n;
  int role = 0;
  int has_pri = 0, has_fb = 0;
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 1) has_pri = 1;
    if (st->epg_src[i].enabled == 2) has_fb = 1;
  }
  if (!has_pri) role = 1;
  else if (!has_fb) role = 2;
  add_epg_item(st, NULL, url, role);
  if (st->epg_n == before) return 0;
  sources_normalize_epg(st);
  return 1;
}

static void add_epg_item(Settings *st, const char *id, const char *url, int enabled) {
  if (!st || !url || !url[0] || st->epg_n >= MAX_SAVED) return;
  for (int i = 0; i < st->epg_n; i++) {
    if (strcmp(st->epg_src[i].url, url) == 0) return;
  }
  SavedLink *L = &st->epg_src[st->epg_n];
  memset(L, 0, sizeof *L);
  if (id && id[0]) snprintf(L->id, MAX_ID, "%s", id);
  else snprintf(L->id, MAX_ID, "epg-%d", st->epg_n + 1);
  snprintf(L->url, MAX_URL, "%s", url);
  saved_name_from_url(url, L->name, MAX_NAME);
  L->enabled = enabled;
  if (L->enabled < 0) L->enabled = 0;
  if (L->enabled > 2) L->enabled = 2;
  st->epg_n++;
}

static void add_pl_item(Settings *st, const char *id, const char *name, const char *url) {
  if (!st || !url || !url[0] || st->pl_n >= MAX_SAVED) return;
  for (int i = 0; i < st->pl_n; i++) {
    if (strcmp(st->playlists[i].url, url) == 0) return;
  }
  SavedLink *L = &st->playlists[st->pl_n];
  memset(L, 0, sizeof *L);
  if (id && id[0]) snprintf(L->id, MAX_ID, "%s", id);
  else snprintf(L->id, MAX_ID, "pl-%d", st->pl_n + 1);
  if (name && name[0]) snprintf(L->name, MAX_NAME, "%s", name);
  else saved_name_from_url(url, L->name, MAX_NAME);
  snprintf(L->url, MAX_URL, "%s", url);
  L->enabled = 1;
  st->pl_n++;
}

static void on_epg_source(const char *item, void *u) {
  Settings *st = (Settings *)u;
  if (!st || !item) return;
  char url[MAX_URL], id[MAX_ID];
  url[0] = id[0] = 0;
  int enabled = 0, slot = 0;
  if (*item == '"') json_parse_string(item, url, MAX_URL);
  else {
    json_extract_string(item, "url", url, MAX_URL);
    json_extract_string(item, "id", id, MAX_ID);
    json_extract_bool(item, "enabled", &enabled);
    json_extract_int(item, "slot", &slot);
  }
  if (!url[0]) return;
  int role = 0;
  if (slot == 1 || slot == 2) role = slot;
  else if (enabled) role = 1;
  add_epg_item(st, id, url, role);
}

static void on_playlist_item(const char *item, void *u) {
  Settings *st = (Settings *)u;
  if (!st || !item) return;
  char url[MAX_URL], id[MAX_ID], name[MAX_NAME];
  url[0] = id[0] = name[0] = 0;
  if (*item == '"') json_parse_string(item, url, MAX_URL);
  else {
    json_extract_string(item, "url", url, MAX_URL);
    json_extract_string(item, "id", id, MAX_ID);
    json_extract_string(item, "name", name, MAX_NAME);
  }
  add_pl_item(st, id, name, url);
}

static void on_name_list(const char *s, void *u, char (*list)[MAX_NAME], int *n, int maxn) {
  (void)u;
  if (!s || !s[0] || !list || !n || *n >= maxn) return;
  for (int i = 0; i < *n; i++) {
    if (strcmp(list[i], s) == 0) return;
  }
  snprintf(list[*n], MAX_NAME, "%s", s);
  (*n)++;
}

static void on_hidden(const char *s, void *u) {
  Settings *st = (Settings *)u;
  if (st) on_name_list(s, u, st->hidden, &st->hidden_n, MAX_HIDDEN);
}
static void on_locked(const char *s, void *u) {
  Settings *st = (Settings *)u;
  if (st) on_name_list(s, u, st->locked, &st->locked_n, MAX_HIDDEN);
}
static void on_fav_id(const char *s, void *u) {
  Settings *st = (Settings *)u;
  if (!st || !s || !s[0] || st->fav_id_n >= MAX_FAV) return;
  snprintf(st->fav_id[st->fav_id_n++], MAX_ID, "%s", s);
}
static void on_recent_id(const char *s, void *u) {
  Settings *st = (Settings *)u;
  if (!st || !s || !s[0] || st->recent_id_n >= MAX_RECENT) return;
  snprintf(st->recent_id[st->recent_id_n++], MAX_ID, "%s", s);
}
static void on_cloud_pl(const char *s, void *u) {
  if (s && u) sources_add_playlist((Settings *)u, s);
}
static void on_cloud_epg(const char *s, void *u) {
  if (s && u) sources_add_epg((Settings *)u, s);
}

static void clamp_prefs(Settings *st) {
  if (st->epg_update_hours < 1) st->epg_update_hours = 6;
  if (st->epg_update_hours > 24) st->epg_update_hours = 24;
  if (st->epg_offset_hours < -12) st->epg_offset_hours = -12;
  if (st->epg_offset_hours > 12) st->epg_offset_hours = 12;
  if (st->epg_days < 1) st->epg_days = 7;
  if (st->epg_days > 14) st->epg_days = 14;
  if (st->archive_days < 1) st->archive_days = 7;
  if (st->archive_days > 14) st->archive_days = 14;
  if (st->guide_rows < 4) st->guide_rows = 6;
  if (st->guide_rows > 9) st->guide_rows = 9;
  if (st->buffer_sec < 3) st->buffer_sec = 3;
  if (st->buffer_sec > 90) st->buffer_sec = 90;
  if (st->clock_opacity < 40) st->clock_opacity = 90;
  if (st->clock_opacity > 100) st->clock_opacity = 100;
  if (st->panel_opacity < 50) st->panel_opacity = 90;
  if (st->panel_opacity > 100) st->panel_opacity = 100;
  if (st->playlist_update_hours < 1) st->playlist_update_hours = 6;
  if (st->recent_n > MAX_RECENT) st->recent_n = MAX_RECENT;
  if (st->recent_id_n > MAX_RECENT) st->recent_id_n = MAX_RECENT;
  if (!st->clock_pos[0]) snprintf(st->clock_pos, sizeof st->clock_pos, "top-right");
  if (!st->clock_size[0]) snprintf(st->clock_size, sizeof st->clock_size, "md");
  if (!st->theme[0]) snprintf(st->theme, sizeof st->theme, "dark");
  if (!st->highlight[0]) snprintf(st->highlight, sizeof st->highlight, "blue");
  if (!st->font_size[0]) snprintf(st->font_size, sizeof st->font_size, "md");
  if (!st->logo_size[0]) snprintf(st->logo_size, sizeof st->logo_size, "md");
  if (!st->channel_sort[0]) snprintf(st->channel_sort, sizeof st->channel_sort, "playlist");
  if (!st->language[0]) snprintf(st->language, sizeof st->language, "ru");
  if (!st->parental_pin[0]) snprintf(st->parental_pin, sizeof st->parental_pin, "0000");
  if (!st->key_menu[0]) snprintf(st->key_menu, MAX_KEY, "F2");
  if (!st->key_mute[0]) snprintf(st->key_mute, MAX_KEY, "m");
  if (!st->key_fav[0]) snprintf(st->key_fav, MAX_KEY, " ");
  if (!st->key_fs[0]) snprintf(st->key_fs, MAX_KEY, "F11");
  if (!st->key_guide[0]) snprintf(st->key_guide, MAX_KEY, "g");
  if (!st->key_search[0]) snprintf(st->key_search, MAX_KEY, "s");
  if (!st->key_rec[0]) snprintf(st->key_rec, MAX_KEY, "r");
  if (!st->key_pip[0]) snprintf(st->key_pip, MAX_KEY, "p");
  if (!st->key_voice[0]) snprintf(st->key_voice, MAX_KEY, "v");
  if (!st->key_live[0]) snprintf(st->key_live, MAX_KEY, "ArrowLeft");
}

static void apply_prefs_obj(const char *obj, Settings *st) {
  if (!obj || !st) return;
  char buf[MAX_UA];
  if (json_extract_string(obj, "userAgent", buf, MAX_UA) && buf[0] && strcmp(buf, "MirEfir/1.0") != 0) {
    snprintf(st->user_agent, MAX_UA, "%s", buf);
  } else if (json_extract_string(obj, "userAgent", buf, MAX_UA) && buf[0]) {
    snprintf(st->user_agent, MAX_UA, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) %s", buf);
  }
  json_extract_string(obj, "decoder", st->decoder, (int)sizeof st->decoder);
  json_extract_string(obj, "aspect", st->aspect, (int)sizeof st->aspect);
  json_extract_string(obj, "defaultAudio", st->audio, (int)sizeof st->audio);
  json_extract_int(obj, "bufferSec", &st->buffer_sec);
  json_extract_bool(obj, "rememberVolume", &st->remember_volume);
  json_extract_bool(obj, "autoplay", &st->autoplay);
  json_extract_bool(obj, "clockEnabled", &st->clock_enabled);
  json_extract_string(obj, "clockPosition", st->clock_pos, (int)sizeof st->clock_pos);
  json_extract_string(obj, "clockSize", st->clock_size, (int)sizeof st->clock_size);
  json_extract_int(obj, "clockOpacity", &st->clock_opacity);
  json_extract_bool(obj, "confirmExit", &st->confirm_exit);
  json_extract_bool(obj, "voiceEnabled", &st->voice_enabled);
  json_extract_bool(obj, "showChannelNumbers", &st->show_numbers);
  json_extract_bool(obj, "showEpgProgress", &st->show_epg_progress);
  json_extract_bool(obj, "showProgramDesc", &st->show_program_desc);
  json_extract_bool(obj, "twoLineTitles", &st->two_line_titles);
  json_extract_string(obj, "theme", st->theme, (int)sizeof st->theme);
  json_extract_string(obj, "highlight", st->highlight, (int)sizeof st->highlight);
  json_extract_int(obj, "panelOpacity", &st->panel_opacity);
  json_extract_string(obj, "fontSize", st->font_size, (int)sizeof st->font_size);
  json_extract_string(obj, "logoSize", st->logo_size, (int)sizeof st->logo_size);
  json_extract_string(obj, "channelSort", st->channel_sort, (int)sizeof st->channel_sort);
  json_extract_int(obj, "playlistUpdateHours", &st->playlist_update_hours);
  {
    double fetched = 0;
    if (json_extract_double(obj, "playlistFetchedAt", &fetched) && fetched > 0)
      st->pl_fetched_ms = (long long)fetched;
  }
  json_extract_bool(obj, "externalPlayer", &st->external_player);
  json_extract_int(obj, "epgOffsetHours", &st->epg_offset_hours);
  json_extract_int(obj, "epgDays", &st->epg_days);
  json_extract_bool(obj, "archiveEnabled", &st->archive_enabled);
  json_extract_int(obj, "archiveDays", &st->archive_days);
  json_extract_int(obj, "guideRows", &st->guide_rows);
  json_extract_string(obj, "epgUrl", st->epg_url, MAX_URL);
  json_extract_int(obj, "epgUpdateHours", &st->epg_update_hours);
  json_extract_bool(obj, "epgAutoUpdate", &st->epg_auto_update);
  json_extract_bool(obj, "recordingsEnabled", &st->recordings_enabled);
  json_extract_string(obj, "recordingPath", st->rec_path, MAX_PATHU);
  json_extract_bool(obj, "parentalEnabled", &st->parental_enabled);
  json_extract_string(obj, "parentalPin", st->parental_pin, (int)sizeof st->parental_pin);
  json_extract_bool(obj, "lockSettings", &st->lock_settings);
  json_extract_string(obj, "language", st->language, (int)sizeof st->language);
  json_extract_string(obj, "activePlaylistId", st->active_pl, MAX_ID);
  const char *keys = json_value_of(obj, "keys");
  if (keys) {
    json_extract_string(keys, "menu", st->key_menu, MAX_KEY);
    json_extract_string(keys, "mute", st->key_mute, MAX_KEY);
    json_extract_string(keys, "favorite", st->key_fav, MAX_KEY);
    json_extract_string(keys, "fullscreen", st->key_fs, MAX_KEY);
    json_extract_string(keys, "guide", st->key_guide, MAX_KEY);
    json_extract_string(keys, "search", st->key_search, MAX_KEY);
    json_extract_string(keys, "record", st->key_rec, MAX_KEY);
    json_extract_string(keys, "pip", st->key_pip, MAX_KEY);
    json_extract_string(keys, "voice", st->key_voice, MAX_KEY);
    json_extract_string(keys, "liveGuide", st->key_live, MAX_KEY);
  }
  json_array_strings(json_value_of(obj, "hiddenGroups"), on_hidden, st);
  json_array_strings(json_value_of(obj, "lockedGroups"), on_locked, st);
  {
    const char *ids = json_value_of(obj, "favoriteIds");
    if (ids) {
      st->fav_id_n = 0;
      json_array_strings(ids, on_fav_id, st);
    }
    ids = json_value_of(obj, "recentIds");
    if (ids) {
      st->recent_id_n = 0;
      json_array_strings(ids, on_recent_id, st);
    }
  }
  json_array_each(json_value_of(obj, "playlists"), on_playlist_item, st);
  json_array_each(json_value_of(obj, "epgSources"), on_epg_source, st);
  clamp_prefs(st);
}

static void parse_settings_blob(const char *json, Settings *st) {
  const char *settings = json_value_of(json, "settings");
  if (settings) apply_prefs_obj(settings, st);
  else apply_prefs_obj(json, st);
  if (!st->epg_url[0]) json_extract_string(json, "epgUrl", st->epg_url, MAX_URL);
  if (!st->pl_n) {
    char one[MAX_URL];
    one[0] = 0;
    json_extract_string(json, "playlistUrl", one, MAX_URL);
    add_pl_item(st, "default", "Основной", one);
  }
  if (!st->epg_n && st->epg_url[0]) add_epg_item(st, "primary", st->epg_url, 1);
  if (!st->active_pl[0] && st->pl_n) snprintf(st->active_pl, MAX_ID, "%s", st->playlists[0].id);
  int any_en = 0;
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled) any_en = 1;
  }
  if (!any_en && st->epg_n) st->epg_src[0].enabled = 1;
  sources_normalize_epg(st);
  clamp_prefs(st);

  double vol = 0;
  if (json_extract_double(json, "volume", &vol)) apply_volume(st, vol);
  if (!st->remember_volume) st->volume = 35;
  json_extract_bool(json, "muted", &st->muted);

  const char *session = json_value_of(json, "session");
  if (session) {
    json_extract_string(session, "channelName", st->last_name, MAX_NAME);
    json_extract_string(session, "groupId", st->last_group, MAX_NAME);
    json_extract_int(session, "index", &st->last_index);
  }
}

typedef struct {
  const Playlist *pl;
  Settings *st;
} ListCtx;

static void on_favorite(const char *id, void *u) {
  ListCtx *c = (ListCtx *)u;
  if (!c || !c->pl || !c->st || !id || !id[0]) return;
  if (c->st->fav_n >= MAX_FAV) return;
  int found = -1;
  for (int i = 0; i < c->pl->count; i++) {
    if (strcmp(c->pl->items[i].id, id) == 0) {
      found = i;
      break;
    }
  }
  if (found < 0) found = playlist_find_name(c->pl, id);
  if (found < 0) return;
  for (int i = 0; i < c->st->fav_n; i++) {
    if (c->st->fav[i] == found) return;
  }
  c->st->fav[c->st->fav_n++] = found;
}

static void on_history(const char *item, void *u) {
  ListCtx *c = (ListCtx *)u;
  if (!c || !c->pl || !c->st || !item) return;
  if (c->st->recent_n >= MAX_RECENT) return;
  char id[MAX_ID], name[MAX_NAME];
  id[0] = name[0] = 0;
  json_extract_string(item, "id", id, MAX_ID);
  json_extract_string(item, "name", name, MAX_NAME);
  int found = -1;
  if (id[0]) {
    for (int i = 0; i < c->pl->count; i++) {
      if (strcmp(c->pl->items[i].id, id) == 0) {
        found = i;
        break;
      }
    }
  }
  if (found < 0 && name[0]) found = playlist_find_name(c->pl, name);
  if (found < 0) return;
  for (int i = 0; i < c->st->recent_n; i++) {
    if (c->st->recent[i] == found) return;
  }
  c->st->recent[c->st->recent_n++] = found;
}

static void parse_lists(const char *json, Settings *st, Playlist *pl) {
  if (!json || !st || !pl) return;
  ListCtx ctx = {pl, st};
  st->fav_n = 0;
  st->recent_n = 0;
  json_array_strings(json_value_of(json, "favorites"), on_favorite, &ctx);
  if (!st->fav_n) json_array_strings(json_value_of(json, "favoriteIds"), on_favorite, &ctx);
  const char *settings = json_value_of(json, "settings");
  if (!st->fav_n && settings) json_array_strings(json_value_of(settings, "favoriteIds"), on_favorite, &ctx);
  json_array_each(json_value_of(json, "history"), on_history, &ctx);
}

static int load_playlist_from_json(const char *json, Playlist *pl) {
  char *text = json_dup_string(json, "playlistText");
  if (text && playlist_parse(pl, text)) {
    free(text);
    return 1;
  }
  free(text);
  char url[MAX_URL];
  url[0] = 0;
  json_extract_string(json, "playlistUrl", url, MAX_URL);
  if (!url[0]) return 0;
  if (istarts(url, "http://") || istarts(url, "https://")) {
    char *body = NULL;
    int n = 0;
    if (http_get(url, &body, &n) == 0 && body) {
      int ok = playlist_parse(pl, body);
      free(body);
      return ok;
    }
    free(body);
    return 0;
  }
  wchar_t path[MAX_PATH];
  utf8_to_wide(url, path, MAX_PATH);
  char *file = read_file_utf8(path, NULL);
  if (!file) return 0;
  int ok = playlist_parse(pl, file);
  free(file);
  return ok;
}

static int load_config_path(const wchar_t *path, Settings *st, Playlist *pl, int with_playlist) {
  char *json = read_file_utf8(path, NULL);
  if (!json) return 0;
  parse_settings_blob(json, st);
  int ok = 1;
  if (with_playlist) ok = load_playlist_from_json(json, pl);
  if (pl && pl->count) parse_lists(json, st, pl);
  free(json);
  return ok;
}

static void import_epg_from_json(Settings *st, const char *json) {
  if (!st || !json) return;
  json_array_each(json_value_of(json, "epgSources"), on_epg_source, st);
  const char *settings = json_value_of(json, "settings");
  if (settings) json_array_each(json_value_of(settings, "epgSources"), on_epg_source, st);
  if (st->epg_n) return;
  char url[MAX_URL];
  url[0] = 0;
  if (settings) json_extract_string(settings, "epgUrl", url, MAX_URL);
  if (!url[0]) json_extract_string(json, "epgUrl", url, MAX_URL);
  if (url[0]) add_epg_item(st, "primary", url, 1);
  url[0] = 0;
  if (settings) json_extract_string(settings, "epgUrl2", url, MAX_URL);
  if (!url[0]) json_extract_string(json, "epgUrl2", url, MAX_URL);
  if (url[0]) add_epg_item(st, "fallback", url, 2);
}

static void import_epg_from_path(Settings *st, const wchar_t *path) {
  char *json = read_file_utf8(path, NULL);
  if (!json) return;
  import_epg_from_json(st, json);
  free(json);
}

int profile_load(Settings *st, Playlist *pl) {
  defaults(st);
  if (pl) {
    pl->items = NULL;
    pl->count = 0;
    pl->cap = 0;
  }
  wchar_t native_dir[MAX_PATH], live_dir[MAX_PATH];
  profile_dirs(native_dir, live_dir, MAX_PATH);

  wchar_t path[MAX_PATH];
  swprintf(path, MAX_PATH, L"%s\\session.json", native_dir);
  char native_group[MAX_NAME] = {0};
  char *session = read_file_utf8(path, NULL);
  if (session) {
    json_extract_string(session, "name", st->last_name, MAX_NAME);
    json_extract_string(session, "groupId", native_group, MAX_NAME);
    json_extract_int(session, "index", &st->last_index);
    json_extract_int(session, "volume", &st->volume);
    json_extract_int(session, "muted", &st->muted);
    json_extract_int(session, "weak", &st->weak);
    json_extract_int(session, "playClass", &st->play_class);
    free(session);
  }

  swprintf(path, MAX_PATH, L"%s\\playlist.m3u", native_dir);
  char *m3u = read_file_utf8(path, NULL);
  int have = 0;
  if (m3u) {
    have = playlist_parse(pl, m3u);
    free(m3u);
  }

  swprintf(path, MAX_PATH, L"%s\\mirefir-config.json", native_dir);
  if (!have) have = load_config_path(path, st, pl, 1);
  else load_config_path(path, st, pl, 0);

  if (!have) {
    swprintf(path, MAX_PATH, L"%s\\mirefir-config.json", live_dir);
    have = load_config_path(path, st, pl, 1);
  } else if (st->fav_n == 0 && st->fav_id_n == 0) {
    swprintf(path, MAX_PATH, L"%s\\mirefir-config.json", live_dir);
    char *live = read_file_utf8(path, NULL);
    if (live) {
      parse_lists(live, st, pl);
      if (!st->last_group[0]) {
        const char *session = json_value_of(live, "session");
        if (session) json_extract_string(session, "groupId", st->last_group, MAX_NAME);
      }
      free(live);
    }
  }

  if (native_group[0]) snprintf(st->last_group, MAX_NAME, "%s", native_group);

  swprintf(path, MAX_PATH, L"%s\\settings.json", native_dir);
  char *prefs = read_file_utf8(path, NULL);
  if (prefs) {
    apply_prefs_obj(prefs, st);
    free(prefs);
  }

  swprintf(path, MAX_PATH, L"%s\\sources.json", native_dir);
  char *src = read_file_utf8(path, NULL);
  if (src) {
    SavedLink epg_keep[MAX_SAVED];
    int epg_keep_n = st->epg_n;
    if (epg_keep_n > 0) memcpy(epg_keep, st->epg_src, sizeof(SavedLink) * (size_t)epg_keep_n);
    st->pl_n = 0;
    st->epg_n = 0;
    st->active_pl[0] = 0;
    json_extract_string(src, "activePlaylistId", st->active_pl, MAX_ID);
    json_array_each(json_value_of(src, "playlists"), on_playlist_item, st);
    json_array_each(json_value_of(src, "epgSources"), on_epg_source, st);
    if (!st->epg_n && epg_keep_n > 0) {
      memcpy(st->epg_src, epg_keep, sizeof(SavedLink) * (size_t)epg_keep_n);
      st->epg_n = epg_keep_n;
    }
    sources_normalize_epg(st);
    free(src);
  }
  if (!st->epg_n) {
    swprintf(path, MAX_PATH, L"%s\\sources.json", live_dir);
    import_epg_from_path(st, path);
  }
  if (!st->epg_n) {
    swprintf(path, MAX_PATH, L"%s\\settings.json", live_dir);
    import_epg_from_path(st, path);
  }
  if (st->epg_n) {
    sources_normalize_epg(st);
    profile_save_sources(st);
  }

  if (!st->pl_n && have) {
    char local[MAX_URL];
    snprintf(local, MAX_URL, "playlist.m3u");
    add_pl_item(st, "local", "playlist.m3u", local);
    snprintf(st->active_pl, MAX_ID, "local");
  }

  if (have && pl && pl->count) {
    swprintf(path, MAX_PATH, L"%s\\playlist.m3u", native_dir);
    /* keep a local copy so debug runs do not depend on the live profile */
  }
  i18n_set(st->language);
  if (pl) profile_restore_lists(st, pl);
  return have && pl && pl->count > 0;
}

void profile_save_session(const Settings *st, int index, const char *name) {
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\session.json", dir);
  char buf[1024];
  char safe[MAX_NAME];
  safe[0] = 0;
  if (name) {
    int o = 0;
    for (int i = 0; name[i] && o < MAX_NAME - 1; i++) {
      char c = name[i];
      if (c == '"' || c == '\\') continue;
      safe[o++] = c;
    }
    safe[o] = 0;
  }
  char group[MAX_NAME];
  group[0] = 0;
  if (st && st->last_group[0]) {
    int o = 0;
    for (int i = 0; st->last_group[i] && o < MAX_NAME - 1; i++) {
      char c = st->last_group[i];
      if (c == '"' || c == '\\') continue;
      group[o++] = c;
    }
    group[o] = 0;
  }
  snprintf(buf, sizeof buf,
           "{\"index\":%d,\"name\":\"%s\",\"groupId\":\"%s\",\"volume\":%d,\"muted\":%d,\"weak\":%d,\"playClass\":%d}\n",
           index, safe, group, st ? st->volume : 35, st ? st->muted : 0, st ? st->weak : 0,
           st ? st->play_class : device_class());
  write_file_utf8(path, buf);
}

static void json_esc(const char *in, char *out, int max) {
  int o = 0;
  if (!out || max < 2) return;
  for (int i = 0; in && in[i] && o < max - 2; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == '"' || c == '\\') {
      if (o >= max - 3) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c >= 32) {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
}

void profile_save_sources(const Settings *st) {
  if (!st) return;
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\sources.json", dir);
  int cap = MAX_SAVED * (MAX_URL * 2 + MAX_NAME * 2 + 160) + 512;
  char *buf = (char *)malloc((size_t)cap);
  if (!buf) return;
  int n = 0;
  char esc[MAX_URL * 2];
  json_esc(st->active_pl, esc, sizeof esc);
  n += snprintf(buf + n, cap - n, "{\"activePlaylistId\":\"%s\",\"playlists\":[", esc);
  for (int i = 0; i < st->pl_n && n < cap - 8; i++) {
    char id[MAX_ID * 2], name[MAX_NAME * 2], url[MAX_URL * 2];
    json_esc(st->playlists[i].id, id, sizeof id);
    json_esc(st->playlists[i].name, name, sizeof name);
    json_esc(st->playlists[i].url, url, sizeof url);
    n += snprintf(buf + n, cap - n, "%s{\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\"}", i ? "," : "", id, name, url);
  }
  n += snprintf(buf + n, cap - n, "],\"epgSources\":[");
  for (int i = 0; i < st->epg_n && n < cap - 8; i++) {
    char id[MAX_ID * 2], url[MAX_URL * 2];
    json_esc(st->epg_src[i].id, id, sizeof id);
    json_esc(st->epg_src[i].url, url, sizeof url);
    int slot = st->epg_src[i].enabled == 2 ? 2 : st->epg_src[i].enabled == 1 ? 1 : 0;
    n += snprintf(buf + n, cap - n, "%s{\"id\":\"%s\",\"url\":\"%s\",\"enabled\":%s,\"slot\":%d}", i ? "," : "", id, url,
                  slot ? "true" : "false", slot);
  }
  snprintf(buf + n, cap - n, "]}\n");
  write_file_utf8(path, buf);
  free(buf);
}

static int json_bool(int v) { return v ? 1 : 0; }

void profile_save_settings(const Settings *st) {
  if (!st) return;
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\settings.json", dir);
  int cap = 16384 + MAX_HIDDEN * (MAX_NAME + 8) + MAX_PATHU * 2 + MAX_UA * 2 + MAX_FAV * (MAX_ID + 4) +
            MAX_RECENT * (MAX_ID + 4);
  char *buf = (char *)malloc((size_t)cap);
  if (!buf) return;
  int n = 0;
  char ua[MAX_UA * 2], rec[MAX_PATHU * 2], pin[32];
  json_esc(st->user_agent, ua, sizeof ua);
  json_esc(st->rec_path, rec, sizeof rec);
  json_esc(st->parental_pin, pin, sizeof pin);
  n += snprintf(buf + n, cap - n,
                "{\"decoder\":\"%s\",\"aspect\":\"%s\",\"defaultAudio\":\"%s\",\"bufferSec\":%d,"
                "\"rememberVolume\":%s,\"autoplay\":%s,\"clockEnabled\":%s,\"clockPosition\":\"%s\","
                "\"clockSize\":\"%s\",\"clockOpacity\":%d,\"confirmExit\":%s,\"voiceEnabled\":%s,"
                "\"showChannelNumbers\":%s,\"showEpgProgress\":%s,\"showProgramDesc\":%s,"
                "\"twoLineTitles\":%s,\"theme\":\"%s\",\"highlight\":\"%s\",\"panelOpacity\":%d,"
                "\"fontSize\":\"%s\",\"logoSize\":\"%s\",\"channelSort\":\"%s\",\"playlistUpdateHours\":%d,"
                "\"playlistFetchedAt\":%lld,"
                "\"externalPlayer\":%s,\"userAgent\":\"%s\",\"epgOffsetHours\":%d,\"epgDays\":%d,"
                "\"archiveEnabled\":%s,\"archiveDays\":%d,\"guideRows\":%d,\"epgUpdateHours\":%d,"
                "\"epgAutoUpdate\":%s,\"recordingsEnabled\":%s,\"recordingPath\":\"%s\","
                "\"parentalEnabled\":%s,\"parentalPin\":\"%s\",\"lockSettings\":%s,\"language\":\"%s\","
                "\"keys\":{\"menu\":\"%s\",\"mute\":\"%s\",\"favorite\":\"%s\",\"fullscreen\":\"%s\","
                "\"guide\":\"%s\",\"search\":\"%s\",\"record\":\"%s\",\"pip\":\"%s\",\"voice\":\"%s\","
                "\"liveGuide\":\"%s\"},\"hiddenGroups\":[",
                st->decoder, st->aspect, st->audio, st->buffer_sec,
                json_bool(st->remember_volume) ? "true" : "false",
                json_bool(st->autoplay) ? "true" : "false",
                json_bool(st->clock_enabled) ? "true" : "false", st->clock_pos,
                st->clock_size, st->clock_opacity,
                json_bool(st->confirm_exit) ? "true" : "false",
                json_bool(st->voice_enabled) ? "true" : "false",
                json_bool(st->show_numbers) ? "true" : "false",
                json_bool(st->show_epg_progress) ? "true" : "false",
                json_bool(st->show_program_desc) ? "true" : "false",
                json_bool(st->two_line_titles) ? "true" : "false",
                st->theme, st->highlight, st->panel_opacity,
                st->font_size, st->logo_size, st->channel_sort, st->playlist_update_hours, st->pl_fetched_ms,
                json_bool(st->external_player) ? "true" : "false", ua, st->epg_offset_hours, st->epg_days,
                json_bool(st->archive_enabled) ? "true" : "false", st->archive_days, st->guide_rows,
                st->epg_update_hours, json_bool(st->epg_auto_update) ? "true" : "false",
                json_bool(st->recordings_enabled) ? "true" : "false", rec,
                json_bool(st->parental_enabled) ? "true" : "false", pin,
                json_bool(st->lock_settings) ? "true" : "false", st->language,
                st->key_menu, st->key_mute, st->key_fav, st->key_fs, st->key_guide, st->key_search,
                st->key_rec, st->key_pip, st->key_voice, st->key_live);
  for (int i = 0; i < st->hidden_n && n < cap - 8; i++) {
    char name[MAX_NAME * 2];
    json_esc(st->hidden[i], name, sizeof name);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", i ? "," : "", name);
  }
  n += snprintf(buf + n, cap - n, "],\"lockedGroups\":[");
  for (int i = 0; i < st->locked_n && n < cap - 8; i++) {
    char name[MAX_NAME * 2];
    json_esc(st->locked[i], name, sizeof name);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", i ? "," : "", name);
  }
  n += snprintf(buf + n, cap - n, "],\"favoriteIds\":[");
  for (int i = 0; i < st->fav_id_n && n < cap - 8; i++) {
    char id[MAX_ID * 2];
    json_esc(st->fav_id[i], id, sizeof id);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", i ? "," : "", id);
  }
  n += snprintf(buf + n, cap - n, "],\"recentIds\":[");
  for (int i = 0; i < st->recent_id_n && n < cap - 8; i++) {
    char id[MAX_ID * 2];
    json_esc(st->recent_id[i], id, sizeof id);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", i ? "," : "", id);
  }
  snprintf(buf + n, cap - n, "]}\n");
  write_file_utf8(path, buf);
  free(buf);
}

void profile_reset_settings(Settings *st) {
  if (!st) return;
  SavedLink pl[MAX_SAVED], epg[MAX_SAVED];
  int pl_n = st->pl_n, epg_n = st->epg_n;
  memcpy(pl, st->playlists, sizeof pl);
  memcpy(epg, st->epg_src, sizeof epg);
  char active[MAX_ID], last[MAX_NAME], group[MAX_NAME];
  snprintf(active, MAX_ID, "%s", st->active_pl);
  snprintf(last, MAX_NAME, "%s", st->last_name);
  snprintf(group, MAX_NAME, "%s", st->last_group);
  int last_i = st->last_index, vol = st->volume, muted = st->muted, weak = st->weak, play_class = st->play_class;
  int fav[MAX_FAV], fav_n = st->fav_n, recent[MAX_RECENT], recent_n = st->recent_n;
  char fav_id[MAX_FAV][MAX_ID], rec_id[MAX_RECENT][MAX_ID];
  int fav_id_n = st->fav_id_n, rec_id_n = st->recent_id_n;
  memcpy(fav, st->fav, sizeof fav);
  memcpy(recent, st->recent, sizeof recent);
  memcpy(fav_id, st->fav_id, sizeof fav_id);
  memcpy(rec_id, st->recent_id, sizeof rec_id);
  defaults(st);
  memcpy(st->playlists, pl, sizeof pl);
  memcpy(st->epg_src, epg, sizeof epg);
  st->pl_n = pl_n;
  st->epg_n = epg_n;
  snprintf(st->active_pl, MAX_ID, "%s", active);
  snprintf(st->last_name, MAX_NAME, "%s", last);
  snprintf(st->last_group, MAX_NAME, "%s", group);
  st->last_index = last_i;
  st->volume = vol;
  st->muted = muted;
  st->weak = weak;
  st->play_class = play_class;
  memcpy(st->fav, fav, sizeof fav);
  st->fav_n = fav_n;
  memcpy(st->recent, recent, sizeof recent);
  st->recent_n = recent_n;
  memcpy(st->fav_id, fav_id, sizeof fav_id);
  st->fav_id_n = fav_id_n;
  memcpy(st->recent_id, rec_id, sizeof rec_id);
  st->recent_id_n = rec_id_n;
  profile_sync_epg(st);
  profile_save_settings(st);
}

void profile_remember_lists(Settings *st, const Playlist *pl) {
  if (!st) return;
  if (!pl) {
    st->fav_id_n = 0;
    st->recent_id_n = 0;
    return;
  }
  int n = 0;
  for (int i = 0; i < st->fav_n && n < MAX_FAV; i++) {
    int idx = st->fav[i];
    if (idx < 0 || idx >= pl->count || !pl->items[idx].id[0]) continue;
    snprintf(st->fav_id[n++], MAX_ID, "%s", pl->items[idx].id);
  }
  st->fav_id_n = n;
  n = 0;
  for (int i = 0; i < st->recent_n && n < MAX_RECENT; i++) {
    int idx = st->recent[i];
    if (idx < 0 || idx >= pl->count || !pl->items[idx].id[0]) continue;
    snprintf(st->recent_id[n++], MAX_ID, "%s", pl->items[idx].id);
  }
  st->recent_id_n = n;
}

void profile_restore_lists(Settings *st, const Playlist *pl) {
  if (!st || !pl) return;
  if (st->fav_id_n) {
    int n = 0;
    for (int i = 0; i < st->fav_id_n && n < MAX_FAV; i++) {
      if (!st->fav_id[i][0]) continue;
      int found = -1;
      for (int k = 0; k < pl->count; k++) {
        if (strcmp(pl->items[k].id, st->fav_id[i]) == 0) {
          found = k;
          break;
        }
      }
      if (found >= 0) st->fav[n++] = found;
    }
    st->fav_n = n;
  }
  if (st->recent_id_n) {
    int n = 0;
    for (int i = 0; i < st->recent_id_n && n < MAX_RECENT; i++) {
      if (!st->recent_id[i][0]) continue;
      int found = -1;
      for (int k = 0; k < pl->count; k++) {
        if (strcmp(pl->items[k].id, st->recent_id[i]) == 0) {
          found = k;
          break;
        }
      }
      if (found >= 0) st->recent[n++] = found;
    }
    st->recent_n = n;
  }
}

int profile_fetch_playlist(Playlist *pl, const char *url) {
  if (!pl || !url || !url[0]) return 0;
  Playlist tmp;
  memset(&tmp, 0, sizeof tmp);
  int ok = 0;
  char *body = NULL;
  if (istarts(url, "http://") || istarts(url, "https://")) {
    int n = 0;
    if (http_get_lim(url, &body, &n, 16 * 1024 * 1024, 60000) == 0 && body) ok = playlist_parse(&tmp, body);
  } else {
    wchar_t path[MAX_PATH];
    utf8_to_wide(url, path, MAX_PATH);
    body = read_file_utf8(path, NULL);
    if (body) ok = playlist_parse(&tmp, body);
  }
  if (!ok || tmp.count < 1) {
    playlist_free(&tmp);
    free(body);
    return 0;
  }
  playlist_free(pl);
  *pl = tmp;
  wchar_t dir[MAX_PATH], m3u[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(m3u, MAX_PATH, L"%s\\playlist.m3u", dir);
  if (body) write_file_utf8(m3u, body);
  free(body);
  return 1;
}

static HANDLE g_pl_th;
static volatile LONG g_pl_busy;
static volatile LONG g_pl_done;
static volatile LONG g_pl_ok;
static Playlist g_pl_tmp;
static char g_pl_fetch_url[MAX_URL];

void profile_mark_playlist_fetched(Settings *st) {
  if (!st) return;
  st->pl_fetched_ms = epg_now_ms();
}

static DWORD WINAPI playlist_fetch_thread(void *arg) {
  (void)arg;
  Playlist tmp;
  memset(&tmp, 0, sizeof tmp);
  int ok = 0;
  char url[MAX_URL];
  snprintf(url, MAX_URL, "%s", g_pl_fetch_url);
  char *body = NULL;
  if (istarts(url, "http://") || istarts(url, "https://")) {
    int n = 0;
    if (http_get_lim(url, &body, &n, 16 * 1024 * 1024, 60000) == 0 && body) ok = playlist_parse(&tmp, body);
  }
  if (ok && tmp.count >= 1) {
    wchar_t dir[MAX_PATH], m3u[MAX_PATH];
    profile_dirs(dir, NULL, MAX_PATH);
    swprintf(m3u, MAX_PATH, L"%s\\playlist.m3u", dir);
    if (body) write_file_utf8(m3u, body);
    playlist_free(&g_pl_tmp);
    g_pl_tmp = tmp;
    InterlockedExchange(&g_pl_ok, 1);
  } else {
    playlist_free(&tmp);
    InterlockedExchange(&g_pl_ok, 0);
  }
  free(body);
  InterlockedExchange(&g_pl_done, 1);
  InterlockedExchange(&g_pl_busy, 0);
  return 0;
}

int profile_playlist_refresh_start(Settings *st, int force) {
  if (!st || st->playlist_update_hours <= 0) return 0;
  if (InterlockedCompareExchange(&g_pl_busy, 1, 0) != 0) return 0;
  if (!force) {
    long long now = epg_now_ms();
    long long span = (long long)st->playlist_update_hours * 3600LL * 1000LL;
    if (st->pl_fetched_ms && now - st->pl_fetched_ms < span) {
      InterlockedExchange(&g_pl_busy, 0);
      return 0;
    }
  }
  const char *url = NULL;
  for (int i = 0; i < st->pl_n; i++) {
    if (st->playlists[i].id[0] && strcmp(st->playlists[i].id, st->active_pl) == 0) {
      url = st->playlists[i].url;
      break;
    }
  }
  if (!url && st->pl_n) url = st->playlists[0].url;
  if (!url || !(istarts(url, "http://") || istarts(url, "https://"))) {
    profile_mark_playlist_fetched(st);
    InterlockedExchange(&g_pl_busy, 0);
    return 0;
  }
  snprintf(g_pl_fetch_url, MAX_URL, "%s", url);
  InterlockedExchange(&g_pl_done, 0);
  InterlockedExchange(&g_pl_ok, 0);
  HANDLE t = CreateThread(NULL, 0, playlist_fetch_thread, NULL, 0, NULL);
  if (!t) {
    InterlockedExchange(&g_pl_busy, 0);
    return 0;
  }
  if (g_pl_th) CloseHandle(g_pl_th);
  g_pl_th = t;
  return 1;
}

int profile_playlist_refresh_take(Playlist *pl) {
  if (!pl || !InterlockedCompareExchange(&g_pl_done, 0, 1)) return 0;
  int ok = (int)InterlockedExchange(&g_pl_ok, 0);
  if (!ok || g_pl_tmp.count < 1) {
    playlist_free(&g_pl_tmp);
    memset(&g_pl_tmp, 0, sizeof g_pl_tmp);
    return -1;
  }
  playlist_free(pl);
  *pl = g_pl_tmp;
  memset(&g_pl_tmp, 0, sizeof g_pl_tmp);
  return 1;
}

void profile_playlist_refresh_shutdown(void) {
  DWORD t0 = GetTickCount();
  while (InterlockedCompareExchange(&g_pl_busy, 0, 0) && GetTickCount() - t0 < 1500) Sleep(40);
  if (g_pl_th) {
    CloseHandle(g_pl_th);
    g_pl_th = NULL;
  }
  playlist_free(&g_pl_tmp);
  memset(&g_pl_tmp, 0, sizeof g_pl_tmp);
}

static void trim_clip(char *s) {
  if (!s) return;
  char *a = s;
  while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
  if (a != s) memmove(s, a, strlen(a) + 1);
  char *nl = s;
  while (*nl && *nl != '\r' && *nl != '\n') nl++;
  *nl = 0;
  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

int clipboard_read_utf8_full(char *out, int max) {
  if (!out || max < 2) return 0;
  out[0] = 0;
  if (!OpenClipboard(NULL)) return 0;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  int ok = 0;
  if (h) {
    wchar_t *w = (wchar_t *)GlobalLock(h);
    if (w) {
      wide_to_utf8(w, out, max);
      GlobalUnlock(h);
      ok = 1;
    }
  }
  CloseClipboard();
  return ok && out[0] != 0;
}

int clipboard_read_utf8(char *out, int max) {
  if (!clipboard_read_utf8_full(out, max)) return 0;
  trim_clip(out);
  return out[0] != 0;
}

int clipboard_write_file_drop(const char *path) {
  if (!path || !path[0]) return 0;
  wchar_t w[MAX_PATH];
  if (!utf8_to_wide(path, w, MAX_PATH) || !w[0]) return 0;
  size_t wlen = wcslen(w);
  SIZE_T bytes = sizeof(DROPFILES) + (wlen + 2) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GHND, bytes);
  if (!mem) return 0;
  DROPFILES *df = (DROPFILES *)GlobalLock(mem);
  if (!df) {
    GlobalFree(mem);
    return 0;
  }
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  wchar_t *dest = (wchar_t *)((char *)df + sizeof(DROPFILES));
  memcpy(dest, w, (wlen + 1) * sizeof(wchar_t));
  dest[wlen + 1] = 0;
  GlobalUnlock(mem);
  if (!OpenClipboard(NULL)) {
    GlobalFree(mem);
    return 0;
  }
  EmptyClipboard();
  SetClipboardData(CF_HDROP, mem);
  CloseClipboard();
  return 1;
}

int clipboard_write_utf8(const char *text) {
  if (!text || !text[0]) return 0;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (wlen < 2) return 0;
  size_t bytes = (size_t)wlen * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!mem) return 0;
  wchar_t *p = (wchar_t *)GlobalLock(mem);
  if (!p) {
    GlobalFree(mem);
    return 0;
  }
  MultiByteToWideChar(CP_UTF8, 0, text, -1, p, wlen);
  GlobalUnlock(mem);
  if (!OpenClipboard(NULL)) {
    GlobalFree(mem);
    return 0;
  }
  EmptyClipboard();
  SetClipboardData(CF_UNICODETEXT, mem);
  CloseClipboard();
  return 1;
}

int pick_m3u_file(char *out, int max) {
  if (!out || max < 4) return 0;
  out[0] = 0;
  wchar_t file[MAX_PATH];
  file[0] = 0;
  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"M3U\0*.m3u;*.m3u8\0All\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn) || !file[0]) return 0;
  return wide_to_utf8(file, out, max) && out[0] != 0;
}

static int pick_json_path(char *out, int max, int save) {
  if (!out || max < 4) return 0;
  out[0] = 0;
  wchar_t file[MAX_PATH];
  file[0] = 0;
  if (save) wcscpy(file, L"mirefir-backup.json");
  wchar_t desk[MAX_PATH];
  desk[0] = 0;
  SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desk);
  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"JSON\0*.json\0All\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrInitialDir = desk[0] ? desk : NULL;
  ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
              (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
  ofn.lpstrDefExt = L"json";
  ofn.lpstrTitle = save ? L"Куда сохранить копию?" : L"Откуда восстановить?";
  int ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
  if (!ok || !file[0]) return 0;
  return wide_to_utf8(file, out, max) && out[0] != 0;
}

int pick_json_open(char *out, int max) { return pick_json_path(out, max, 0); }
int pick_json_save(char *out, int max) { return pick_json_path(out, max, 1); }

int profile_backup_internal_path(char *out, int max) {
  wchar_t dir[MAX_PATH], file[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(file, MAX_PATH, L"%s\\backups", dir);
  CreateDirectoryW(file, NULL);
  swprintf(file, MAX_PATH, L"%s\\backups\\mirefir-backup.json", dir);
  return wide_to_utf8(file, out, max) && out[0] != 0;
}

int profile_backup_in_folder(char *out, int max, const char *folder) {
  if (!out || max < 8 || !folder || !folder[0]) return 0;
  int n = (int)strlen(folder);
  while (n > 0 && (folder[n - 1] == '\\' || folder[n - 1] == '/')) n--;
  if (n < 1) return 0;
  snprintf(out, max, "%.*s\\mirefir-backup.json", n, folder);
  return out[0] != 0;
}

int pick_folder(char *out, int max) {
  if (!out || max < 4) return 0;
  out[0] = 0;
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  BROWSEINFOW bi;
  memset(&bi, 0, sizeof bi);
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  bi.lpszTitle = L"Выберите папку";
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) {
    CoUninitialize();
    return 0;
  }
  wchar_t w[MAX_PATH];
  w[0] = 0;
  int ok = SHGetPathFromIDListW(pidl, w);
  CoTaskMemFree(pidl);
  CoUninitialize();
  if (!ok || !w[0]) return 0;
  return wide_to_utf8(w, out, max) && out[0] != 0;
}

int profile_export_backup(const Settings *st, const Playlist *pl, const char *path) {
  (void)pl;
  if (!st || !path || !path[0]) return 0;
  wchar_t wpath[MAX_PATH], dir[MAX_PATH], m3u[MAX_PATH];
  if (!utf8_to_wide(path, wpath, MAX_PATH)) return 0;
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(m3u, MAX_PATH, L"%s\\playlist.m3u", dir);
  char *text = read_file_utf8(m3u, NULL);
  int cap = 16384 + (text ? (int)strlen(text) * 2 : 0) + st->pl_n * (MAX_URL * 2 + 80) + st->epg_n * (MAX_URL * 2 + 80) +
            MAX_FAV * (MAX_NAME * 2 + MAX_ID * 2 + 16);
  char *buf = (char *)malloc((size_t)cap);
  if (!buf) {
    free(text);
    return 0;
  }
  int n = 0;
  n += snprintf(buf + n, cap - n, "{\"v\":1,\"settings\":");
  /* embed current prefs by writing them to a temp buffer via profile_save_settings file */
  profile_save_settings(st);
  wchar_t spath[MAX_PATH];
  swprintf(spath, MAX_PATH, L"%s\\settings.json", dir);
  char *prefs = read_file_utf8(spath, NULL);
  if (prefs) {
    int plen = (int)strlen(prefs);
    while (plen > 0 && (prefs[plen - 1] == '\n' || prefs[plen - 1] == '\r' || prefs[plen - 1] == ' ')) plen--;
    prefs[plen] = 0;
    n += snprintf(buf + n, cap - n, "%s", prefs);
    free(prefs);
  } else {
    n += snprintf(buf + n, cap - n, "{}");
  }
  n += snprintf(buf + n, cap - n, ",\"playlists\":[");
  for (int i = 0; i < st->pl_n && n < cap - 8; i++) {
    char id[MAX_ID * 2], name[MAX_NAME * 2], url[MAX_URL * 2];
    json_esc(st->playlists[i].id, id, sizeof id);
    json_esc(st->playlists[i].name, name, sizeof name);
    json_esc(st->playlists[i].url, url, sizeof url);
    n += snprintf(buf + n, cap - n, "%s{\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\"}", i ? "," : "", id, name, url);
  }
  n += snprintf(buf + n, cap - n, "],\"epgSources\":[");
  for (int i = 0; i < st->epg_n && n < cap - 8; i++) {
    char id[MAX_ID * 2], url[MAX_URL * 2];
    json_esc(st->epg_src[i].id, id, sizeof id);
    json_esc(st->epg_src[i].url, url, sizeof url);
    int slot = st->epg_src[i].enabled == 2 ? 2 : st->epg_src[i].enabled == 1 ? 1 : 0;
    n += snprintf(buf + n, cap - n, "%s{\"id\":\"%s\",\"url\":\"%s\",\"enabled\":%s,\"slot\":%d}", i ? "," : "", id, url,
                  slot ? "true" : "false", slot);
  }
  n += snprintf(buf + n, cap - n, "],\"activePlaylistId\":\"%s\"", st->active_pl);
  n += snprintf(buf + n, cap - n, ",\"favorites\":[");
  int fn = 0;
  for (int i = 0; i < st->fav_n && pl && n < cap - 8; i++) {
    int idx = st->fav[i];
    if (idx < 0 || idx >= pl->count) continue;
    char name[MAX_NAME * 2];
    json_esc(pl->items[idx].name, name, sizeof name);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", fn ? "," : "", name);
    fn++;
  }
  n += snprintf(buf + n, cap - n, "],\"favoriteIds\":[");
  fn = 0;
  for (int i = 0; i < st->fav_n && pl && n < cap - 8; i++) {
    int idx = st->fav[i];
    if (idx < 0 || idx >= pl->count || !pl->items[idx].id[0]) continue;
    char id[MAX_ID * 2];
    json_esc(pl->items[idx].id, id, sizeof id);
    n += snprintf(buf + n, cap - n, "%s\"%s\"", fn ? "," : "", id);
    fn++;
  }
  n += snprintf(buf + n, cap - n, "]");
  if (text) {
    char *esc = (char *)malloc(strlen(text) * 2 + 8);
    if (esc) {
      json_esc(text, esc, (int)(strlen(text) * 2 + 8));
      n += snprintf(buf + n, cap - n, ",\"playlistText\":\"%s\"", esc);
      free(esc);
    }
    free(text);
  }
  snprintf(buf + n, cap - n, "}\n");
  int ok = write_file_utf8(wpath, buf);
  free(buf);
  return ok;
}

int profile_import_backup(Settings *st, Playlist *pl, const char *path) {
  if (!st || !path || !path[0]) return 0;
  wchar_t wpath[MAX_PATH];
  if (!utf8_to_wide(path, wpath, MAX_PATH)) return 0;
  char *json = read_file_utf8(wpath, NULL);
  if (!json) return 0;
  st->pl_n = 0;
  st->epg_n = 0;
  parse_settings_blob(json, st);
  json_extract_string(json, "activePlaylistId", st->active_pl, MAX_ID);
  json_array_each(json_value_of(json, "playlists"), on_playlist_item, st);
  json_array_each(json_value_of(json, "epgSources"), on_epg_source, st);
  import_epg_from_json(st, json);
  int ok = 1;
  if (pl) {
    Playlist tmp;
    memset(&tmp, 0, sizeof tmp);
    if (load_playlist_from_json(json, &tmp) && tmp.count) {
      playlist_free(pl);
      *pl = tmp;
      char *text = json_dup_string(json, "playlistText");
      if (text) {
        wchar_t dir[MAX_PATH], m3u[MAX_PATH];
        profile_dirs(dir, NULL, MAX_PATH);
        swprintf(m3u, MAX_PATH, L"%s\\playlist.m3u", dir);
        write_file_utf8(m3u, text);
        free(text);
      }
    } else {
      playlist_free(&tmp);
      if (st->pl_n) ok = profile_fetch_playlist(pl, st->playlists[0].url);
    }
    if (pl->count) {
      parse_lists(json, st, pl);
      if (!st->fav_n) profile_restore_lists(st, pl);
    }
  }
  if (!st->pl_n && pl && pl->count) {
    add_pl_item(st, "local", "playlist.m3u", "playlist.m3u");
    if (!st->active_pl[0]) snprintf(st->active_pl, MAX_ID, "local");
  }
  free(json);
  profile_save_settings(st);
  profile_save_sources(st);
  return ok && (!pl || pl->count > 0 || st->pl_n > 0);
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_enc(const unsigned char *in, int n, char *out, int max) {
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    unsigned v = ((unsigned)in[i]) << 16;
    if (i + 1 < n) v |= ((unsigned)in[i + 1]) << 8;
    if (i + 2 < n) v |= (unsigned)in[i + 2];
    if (o + 4 >= max) return 0;
    out[o++] = kB64[(v >> 18) & 63];
    out[o++] = kB64[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? kB64[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? kB64[v & 63] : '=';
  }
  out[o] = 0;
  return 1;
}

static int b64_val(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int b64_dec(const char *in, unsigned char *out, int max) {
  int n = 0, o = 0;
  unsigned v = 0;
  int bits = 0;
  for (int i = 0; in[i]; i++) {
    if (in[i] == '=' || in[i] == '\n' || in[i] == '\r' || in[i] == ' ') continue;
    int d = b64_val((unsigned char)in[i]);
    if (d < 0) return -1;
    v = (v << 6) | (unsigned)d;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= max) return -1;
      out[o++] = (unsigned char)((v >> bits) & 255);
      n++;
    }
  }
  return o;
}

char *cloud_make_code(const Settings *st, const Playlist *pl) {
  if (!st) return NULL;
  int cap = 16384 + st->pl_n * (MAX_URL * 2 + 8) + st->epg_n * (MAX_URL * 2 + 8) + MAX_FAV * (MAX_NAME * 2 + 8);
  char *json = (char *)malloc((size_t)cap);
  if (!json) return NULL;
  int n = 0;
  n += snprintf(json + n, cap - n, "{\"v\":2,\"f\":[");
  int fn = 0;
  for (int i = 0; i < st->fav_n && pl && fn < MAX_FAV; i++) {
    int idx = st->fav[i];
    if (idx < 0 || idx >= pl->count) continue;
    char name[MAX_NAME * 2];
    json_esc(pl->items[idx].name, name, sizeof name);
    n += snprintf(json + n, cap - n, "%s\"%s\"", fn ? "," : "", name);
    fn++;
  }
  n += snprintf(json + n, cap - n, "],\"p\":[");
  int pn = 0;
  for (int i = 0; i < st->pl_n; i++) {
    if (!st->playlists[i].url[0] || strcmp(st->playlists[i].url, "local") == 0) continue;
    char url[MAX_URL * 2];
    json_esc(st->playlists[i].url, url, sizeof url);
    n += snprintf(json + n, cap - n, "%s\"%s\"", pn ? "," : "", url);
    pn++;
  }
  n += snprintf(json + n, cap - n, "],\"e\":[");
  int en = 0;
  for (int i = 0; i < st->epg_n; i++) {
    if (!st->epg_src[i].url[0]) continue;
    char url[MAX_URL * 2];
    json_esc(st->epg_src[i].url, url, sizeof url);
    n += snprintf(json + n, cap - n, "%s\"%s\"", en ? "," : "", url);
    en++;
  }
  char ua[MAX_UA * 2];
  json_esc(st->user_agent, ua, sizeof ua);
  n += snprintf(json + n, cap - n,
                "],\"s\":{\"decoder\":\"%s\",\"aspect\":\"%s\",\"defaultAudio\":\"%s\",\"bufferSec\":%d,"
                "\"rememberVolume\":%s,\"autoplay\":%s,\"clockEnabled\":%s,\"clockPosition\":\"%s\","
                "\"clockSize\":\"%s\",\"clockOpacity\":%d,\"confirmExit\":%s,\"voiceEnabled\":%s,"
                "\"showChannelNumbers\":%s,\"showEpgProgress\":%s,\"showProgramDesc\":%s,"
                "\"twoLineTitles\":%s,\"theme\":\"%s\",\"highlight\":\"%s\",\"panelOpacity\":%d,"
                "\"fontSize\":\"%s\",\"logoSize\":\"%s\",\"channelSort\":\"%s\",\"playlistUpdateHours\":%d,"
                "\"externalPlayer\":%s,\"userAgent\":\"%s\",\"epgOffsetHours\":%d,\"epgDays\":%d,"
                "\"archiveEnabled\":%s,\"archiveDays\":%d,\"guideRows\":%d,\"epgUpdateHours\":%d,"
                "\"epgAutoUpdate\":%s,\"language\":\"%s\"}}",
                st->decoder, st->aspect, st->audio, st->buffer_sec,
                json_bool(st->remember_volume) ? "true" : "false", json_bool(st->autoplay) ? "true" : "false",
                json_bool(st->clock_enabled) ? "true" : "false", st->clock_pos, st->clock_size, st->clock_opacity,
                json_bool(st->confirm_exit) ? "true" : "false", json_bool(st->voice_enabled) ? "true" : "false",
                json_bool(st->show_numbers) ? "true" : "false", json_bool(st->show_epg_progress) ? "true" : "false",
                json_bool(st->show_program_desc) ? "true" : "false", json_bool(st->two_line_titles) ? "true" : "false",
                st->theme, st->highlight, st->panel_opacity, st->font_size, st->logo_size, st->channel_sort,
                st->playlist_update_hours, json_bool(st->external_player) ? "true" : "false", ua, st->epg_offset_hours,
                st->epg_days, json_bool(st->archive_enabled) ? "true" : "false", st->archive_days, st->guide_rows,
                st->epg_update_hours, json_bool(st->epg_auto_update) ? "true" : "false", st->language);
  int jn = (int)strlen(json);
  unsigned char *gz = NULL;
  int gn = 0;
  if (!gzip_compress(json, jn, &gz, &gn) || !gz) {
    free(json);
    return NULL;
  }
  free(json);
  char *code = (char *)malloc((size_t)gn * 2 + 8);
  if (!code) {
    free(gz);
    return NULL;
  }
  memcpy(code, "ME2.", 4);
  int ok = b64_enc(gz, gn, code + 4, gn * 2 + 4);
  free(gz);
  if (!ok) {
    free(code);
    return NULL;
  }
  return code;
}

int cloud_copy_code(const Settings *st, const Playlist *pl) {
  char *code = cloud_make_code(st, pl);
  if (!code) return 0;
  int ok = clipboard_write_utf8(code);
  free(code);
  return ok;
}

static void strip_ws_copy(const char *in, char *out, int max) {
  int o = 0;
  for (int i = 0; in && in[i] && o < max - 1; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    out[o++] = (char)c;
  }
  out[o] = 0;
}

int cloud_apply_code(Settings *st, Playlist *pl, const char *code) {
  if (!st || !code || !code[0]) return 0;
  int rawcap = (int)strlen(code) + 8;
  char *clean = (char *)malloc((size_t)rawcap);
  if (!clean) return 0;
  strip_ws_copy(code, clean, rawcap);
  code = clean;
  char packed[16];
  packed[0] = 0;
  const char *body = code;
  if ((code[0] == 'M' || code[0] == 'm' || code[0] == 'O' || code[0] == 'o') && code[3] == '.') {
    packed[0] = (char)toupper((unsigned char)code[0]);
    packed[1] = (char)toupper((unsigned char)code[1]);
    packed[2] = (char)toupper((unsigned char)code[2]);
    packed[3] = 0;
    body = code + 4;
  } else {
    snprintf(packed, sizeof packed, "ME1");
  }
  int blen = (int)strlen(body);
  unsigned char *raw = (unsigned char *)malloc((size_t)blen + 8);
  if (!raw) {
    free(clean);
    return 0;
  }
  int rn = b64_dec(body, raw, blen + 8);
  if (rn < 2) {
    free(raw);
    free(clean);
    return 0;
  }
  char *json = NULL;
  if (strcmp(packed, "ME2") == 0) {
    unsigned dlen = 0;
    if (rn >= 4) dlen = (unsigned)raw[rn - 4] | ((unsigned)raw[rn - 3] << 8) | ((unsigned)raw[rn - 2] << 16) | ((unsigned)raw[rn - 1] << 24);
    if (dlen < 8 || dlen > 2u * 1024u * 1024u) {
      free(raw);
      free(clean);
      return 0;
    }
    json = (char *)malloc(dlen + 1);
    if (!json) {
      free(raw);
      free(clean);
      return 0;
    }
    unsigned got = dlen;
    if (tinf_gzip_uncompress(json, &got, raw, (unsigned)rn) != TINF_OK) {
      free(json);
      free(raw);
      free(clean);
      return 0;
    }
    json[got] = 0;
  } else {
    json = (char *)malloc((size_t)rn + 1);
    if (!json) {
      free(raw);
      free(clean);
      return 0;
    }
    memcpy(json, raw, (size_t)rn);
    json[rn] = 0;
  }
  free(raw);
  json_array_strings(json_value_of(json, "p"), on_cloud_pl, st);
  json_array_strings(json_value_of(json, "e"), on_cloud_epg, st);
  const char *sobj = json_value_of(json, "s");
  if (sobj) apply_prefs_obj(sobj, st);
  int ok = 1;
  if (pl && st->pl_n) {
    snprintf(st->active_pl, MAX_ID, "%s", st->playlists[0].id);
    ok = profile_fetch_playlist(pl, st->playlists[0].url);
    if (ok && pl->count) {
      ListCtx fc = {pl, st};
      st->fav_n = 0;
      json_array_strings(json_value_of(json, "f"), on_favorite, &fc);
    }
  }
  free(json);
  free(clean);
  profile_save_settings(st);
  profile_save_sources(st);
  return ok;
}

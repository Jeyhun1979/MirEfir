#pragma once
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MIREFIR_VERSION "1.0.27"
#define IDI_APP 1
enum { MAX_URL = 3072, MAX_NAME = 192, MAX_UA = 256, MAX_REF = 512, MAX_FAMILY = 192, MAX_ID = 80, MAX_TVG = 96, MAX_FAV = 512, MAX_RECENT = 10, MAX_CATCHUP = 32, MAX_CATCHUP_SRC = 768, MAX_SAVED = 32, MAX_PIN = 8, MAX_KEY = 24, MAX_PATHU = 512, MAX_HIDDEN = 64 };

typedef struct Channel {
  char name[MAX_NAME];
  char url[MAX_URL];
  char ua[MAX_UA];
  char referer[MAX_REF];
  char family[MAX_FAMILY];
  char group[MAX_NAME];
  char id[MAX_ID];
  char tvg_id[MAX_TVG];
  char tvg_name[MAX_NAME];
  char catchup[MAX_CATCHUP];
  char catchup_source[MAX_CATCHUP_SRC];
  char logo[MAX_REF];
  int catchup_days;
  int number;
  int quality;
} Channel;

typedef struct Playlist {
  Channel *items;
  int count;
  int cap;
} Playlist;

typedef struct SavedLink {
  char id[MAX_ID];
  char name[MAX_NAME];
  char url[MAX_URL];
  int enabled;
} SavedLink;

typedef struct Settings {
  char user_agent[MAX_UA];
  char decoder[32];
  char aspect[16];
  char audio[16];
  int buffer_sec;
  int remember_volume;
  int autoplay;
  int volume;
  int muted;
  int weak;
  int play_class;
  int clock_enabled;
  char clock_pos[24];
  char clock_size[8];
  int clock_opacity;
  int confirm_exit;
  int voice_enabled;
  int show_numbers;
  int show_epg_progress;
  int two_line_titles;
  char theme[16];
  char highlight[16];
  int panel_opacity;
  char font_size[8];
  char logo_size[8];
  char channel_sort[16];
  int playlist_update_hours;
  long long pl_fetched_ms;
  int external_player;
  int epg_offset_hours;
  int epg_days;
  int archive_enabled;
  int archive_days;
  int show_program_desc;
  int guide_rows;
  char epg_url[MAX_URL];
  char epg_url2[MAX_URL];
  int epg_update_hours;
  int epg_auto_update;
  int recordings_enabled;
  char rec_path[MAX_PATHU];
  int parental_enabled;
  char parental_pin[MAX_PIN + 1];
  int lock_settings;
  char language[8];
  char key_menu[MAX_KEY];
  char key_mute[MAX_KEY];
  char key_fav[MAX_KEY];
  char key_fs[MAX_KEY];
  char key_guide[MAX_KEY];
  char key_search[MAX_KEY];
  char key_rec[MAX_KEY];
  char key_pip[MAX_KEY];
  char key_voice[MAX_KEY];
  char key_live[MAX_KEY];
  char hidden[MAX_HIDDEN][MAX_NAME];
  int hidden_n;
  char locked[MAX_HIDDEN][MAX_NAME];
  int locked_n;
  SavedLink playlists[MAX_SAVED];
  int pl_n;
  char active_pl[MAX_ID];
  SavedLink epg_src[MAX_SAVED];
  int epg_n;
  char last_name[MAX_NAME];
  char last_group[MAX_NAME];
  int last_index;
  int fav[MAX_FAV];
  int fav_n;
  char fav_id[MAX_FAV][MAX_ID];
  int fav_id_n;
  int recent[MAX_RECENT];
  int recent_n;
  char recent_id[MAX_RECENT][MAX_ID];
  int recent_id_n;
} Settings;

int utf8_to_wide(const char *s, wchar_t *w, int max);
int wide_to_utf8(const wchar_t *w, char *s, int max);
char *read_file_utf8(const wchar_t *path, int *len);
int write_file_utf8(const wchar_t *path, const char *text);
int istarts(const char *s, const char *pfx);

const char *json_value_of(const char *obj, const char *key);
int json_extract_string(const char *obj, const char *key, char *out, int max);
char *json_dup_string(const char *obj, const char *key);
int json_extract_int(const char *obj, const char *key, int *out);
int json_extract_double(const char *obj, const char *key, double *out);
int json_extract_bool(const char *obj, const char *key, int *out);
int json_array_strings(const char *v, void (*cb)(const char *s, void *u), void *u);
int json_array_each(const char *v, void (*cb)(const char *item, void *u), void *u);
int json_parse_string(const char *quoted, char *out, int max);

void profile_dirs(wchar_t *native_dir, wchar_t *live_dir, int max);
int profile_load(Settings *st, Playlist *pl);
void profile_save_session(const Settings *st, int index, const char *name);
void profile_save_sources(const Settings *st);
void profile_save_settings(const Settings *st);
void profile_sync_epg(Settings *st);
void profile_remember_lists(Settings *st, const Playlist *pl);
void profile_restore_lists(Settings *st, const Playlist *pl);
int profile_fetch_playlist(Playlist *pl, const char *url);
void profile_mark_playlist_fetched(Settings *st);
int profile_playlist_refresh_start(Settings *st, int force);
int profile_playlist_refresh_take(Playlist *pl);
void profile_playlist_refresh_shutdown(void);
void i18n_set(const char *lang);
int i18n_en(void);
const wchar_t *i18n(const char *key);
const char *i18n_u(const char *key);
const wchar_t *i18n_wd_short(int w);
const wchar_t *i18n_wd_long(int w);
const wchar_t *i18n_mo_short(int m);
const wchar_t *i18n_mo_long(int m);
int clipboard_read_utf8(char *out, int max);
int clipboard_read_utf8_full(char *out, int max);
int clipboard_write_utf8(const char *text);
int clipboard_write_file_drop(const char *path);
int gzip_compress(const void *in, int inlen, unsigned char **out, int *outlen);
int pick_m3u_file(char *out, int max);
int pick_folder(char *out, int max);
int pick_json_open(char *out, int max);
int pick_json_save(char *out, int max);
int profile_backup_internal_path(char *out, int max);
int profile_backup_in_folder(char *out, int max, const char *folder);
int profile_export_backup(const Settings *st, const Playlist *pl, const char *path);
int profile_import_backup(Settings *st, Playlist *pl, const char *path);
void profile_reset_settings(Settings *st);
char *cloud_make_code(const Settings *st, const Playlist *pl);
int cloud_copy_code(const Settings *st, const Playlist *pl);
int cloud_apply_code(Settings *st, Playlist *pl, const char *code);
void saved_name_from_url(const char *url, char *name, int max);
int sources_add_playlist(Settings *st, const char *url);
int sources_add_epg(Settings *st, const char *url);
void sources_remove_playlist(Settings *st, int i);
void sources_remove_epg(Settings *st, int i);
void sources_replace_url(SavedLink *item, const char *url);
void sources_set_epg_role(Settings *st, int i, int role);
int sources_epg_role(const Settings *st, int i);
void sources_normalize_epg(Settings *st);

int http_get(const char *url, char **out, int *len);
int http_get_lim(const char *url, char **out, int *len, int max_bytes, int timeout_ms);
int http_get_lim_ua(const char *url, char **out, int *len, int max_bytes, int timeout_ms, const wchar_t *agent);

enum { WM_MIREFIR_UPDATE = WM_APP + 41, WM_MIREFIR_INSTALL = WM_APP + 42 };
void update_start(HWND host);
void update_stop(void);
int update_ready(void);
int update_has_file(void);
void update_later(void);
int update_apply(HWND host);
int update_should_apply(void);
int update_splash(HINSTANCE inst);

int playlist_parse(Playlist *pl, const char *text);
void playlist_free(Playlist *pl);
int playlist_find_name(const Playlist *pl, const char *name);
void fold_family(const char *name, char *out, int max);
enum { PLAY_MIN = 0, PLAY_LOW = 1, PLAY_MID = 2, PLAY_HIGH = 3 };
void device_probe(void);
int device_class(void);
int device_can_hevc(void);
int device_start_quality(void);
int device_prefer_d3d(void);
void device_sync(Settings *st);
void device_lower(Settings *st);

int quality_of(const char *name, const char *url);
const char *quality_label(int q);
void build_play_urls(const Playlist *pl, int index, int start_q, char urls[][MAX_URL], int *n, int maxn);
int channel_has_catchup(const Channel *ch);
int program_has_archive(const Channel *ch, const Settings *st, long long start, long long end);
int can_play_archive(const Channel *ch, const Settings *st, long long start, long long end);
int catchup_url_candidates(const Channel *ch, long long start, long long end, char urls[][MAX_URL], int maxn);

int player_start(HWND hwnd);
void player_stop(void);
void player_resize(HWND hwnd);
int player_play_channel(const Playlist *pl, Settings *st, int index);
int player_play_at(const Playlist *pl, Settings *st, int index, int start_q);
int player_open_external(const char *url);
int player_play_archive(const Playlist *pl, Settings *st, int index, char urls[][MAX_URL], int n,
                        long long play_start, long long origin_start, long long origin_end);
int player_is_archive(void);
int player_archive_bounds(long long *origin_start, long long *origin_end, long long *pos_ms);
void player_tick(const Playlist *pl, Settings *st);
void player_set_volume(int vol, int muted);
void player_cycle_audio(void);
void player_cycle_sub(void);
void player_show_text(const char *utf8, int ms);
int player_current_index(void);
int player_prev_index(void);
int player_alive(void);
void player_keep_focus(HWND hwnd);
void player_toggle_pause(void);
int player_paused(void);
void player_seek(int seconds);
void player_apply_settings(const Settings *st);
int player_recording(void);
int player_record_start(const char *path);
void player_record_stop(void);
int player_play_file(const char *path);
int player_playing_file(void);
void player_set_pause(int on);
void player_set_multiview(int on);
void player_reload(const Playlist *pl, Settings *st);
const wchar_t *player_mpv_path(void);
int player_pip_on(void);
int player_pip_toggle(HWND host, const char *url, const char *ua);
void player_pip_stop(void);
void player_set_slot(int x, int y, int w, int h);
void player_clear_slot(void);
int player_tile_start(int slot, HWND hwnd, const char *url, const char *ua);
void player_tile_stop(int slot);
void player_tile_stop_all(void);
void player_tile_mute(int slot, int mute);
void player_tile_pause(int slot, int on);
void player_tile_pause_all(int on);

enum { MAX_REC_FILES = 96, REC_OFF = 0, REC_SCHED = 1, REC_RUN = 2 };
typedef struct RecFile {
  char path[MAX_PATHU];
  char title[MAX_NAME];
  long long bytes;
  long long mtime;
} RecFile;
void rec_internal_dir(char *out, int max);
int rec_live_start(const Playlist *pl, Settings *st, int index);
void rec_live_stop(void);
int rec_live_active(void);
void rec_on_zap(void);
int rec_scan(const Settings *st, RecFile *out, int max);
int rec_timer_toggle(const Playlist *pl, const Settings *st, int ch, const char *title, long long start, long long end);
void rec_timer_cancel(void);
int rec_timer_get(int *ch, char *title, int tmax, char *channel, int cmax, long long *start, long long *end, int *status);
void rec_tick(const Playlist *pl, Settings *st);
int rec_has_timer_for(int ch, long long start, long long end);
void rec_load(const Playlist *pl, Settings *st);
void rec_save(const Playlist *pl, const Settings *st);
int rec_path_error(const char *path);

int logo_px(const Settings *st);
void logo_init(void);
void logo_shutdown(void);
int logo_tick(void);
void logo_want(const char *url);
void logo_queue(const char *url);
void logo_hold(DWORD ms_from_now);
void logo_retain_begin(void);
void logo_retain(const char *url);
void logo_retain_end(void);
void logo_blit(unsigned char *bits, int bw, int bh, int x, int y, int size, const char *url);

enum { VOICE_IDLE = 0, VOICE_BUSY = 1, VOICE_HEARD = 2, VOICE_EMPTY = 3, VOICE_NEEDMIC = 4, VOICE_FAIL = 5 };
void voice_start(const Playlist *pl, Settings *st);
void voice_stop(void);
int voice_busy(void);
const char *voice_status(void);
int voice_poll(char *text, int tmax, int *channel, int *do_switch);

void epg_start(const Playlist *pl, const Settings *st);
void epg_reload(const Playlist *pl, const Settings *st);
void epg_apply_settings(const Settings *st);
void epg_tick(void);
void epg_stop(void);
int epg_ready(void);
int epg_dirty(void);
void epg_clear_dirty(void);
long long epg_now_ms(void);
long long epg_day_start(long long ms);
void epg_local_fields(long long ms, SYSTEMTIME *out);
int epg_now(int index, char *title, int tmax, char *desc, int dmax, long long *start, long long *end);
int epg_count(int index);
int epg_live_index(int index);
int epg_index_for_day(int index, long long day);
int epg_list(int index, int slot, char *title, int tmax, char *desc, int dmax, long long *start, long long *end);
int epg_channel_icon(int index, char *out, int max);

void shell_init(HWND host, Playlist *pl, Settings *st, void (*zap)(int), void (*quit)(void), int (*fullscreen)(int));
void shell_shutdown(void);
void shell_layout(HWND host);
int shell_keydown(WPARAM key);
int shell_keyup(WPARAM key);
int shell_char(unsigned ch);
WPARAM shell_map_key(WPARAM key);
int shell_typing(void);
int shell_context(void);
int shell_appcommand(int cmd);
void shell_mouse_moved(void);
void shell_tick(void);
void shell_on_play(int index);
void shell_request_exit(void);
int shell_host_click(void);
int shell_next_channel(int delta);

#include "app.h"
#include <shlobj.h>

static char g_live_path[MAX_PATHU];
static char g_dump_path[MAX_PATHU];
static HANDLE g_dump;
static int g_tm_ch = -1;
static int g_tm_status;
static char g_tm_title[96];
static char g_tm_name[MAX_NAME];
static long long g_tm_start;
static long long g_tm_end;

void rec_internal_dir(char *out, int max) {
  wchar_t dir[MAX_PATH], rec[MAX_PATH];
  if (!out || max < 2) return;
  out[0] = 0;
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(rec, MAX_PATH, L"%s\\Recordings", dir);
  CreateDirectoryW(rec, NULL);
  wide_to_utf8(rec, out, max);
}

static void sanitize(const char *in, char *out, int max) {
  int o = 0;
  if (!out || max < 2) return;
  for (int i = 0; in && in[i] && o < max - 1; i++) {
    char c = in[i];
    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    if (c == ' ' && o && out[o - 1] == '_') continue;
    out[o++] = c;
  }
  while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '_')) o--;
  out[o] = 0;
  if (!out[0]) snprintf(out, max, "channel");
}

static void make_name(const char *channel, const char *ext, char *out, int max) {
  char safe[96];
  SYSTEMTIME st;
  GetLocalTime(&st);
  sanitize(channel, safe, 96);
  snprintf(out, max, "%s_%04u%02u%02u_%02u%02u.%s", safe, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
           ext && ext[0] ? ext : "ts");
}

static void join_path(const char *dir, const char *name, char *out, int max) {
  int n = (int)strlen(dir ? dir : "");
  if (n && (dir[n - 1] == '\\' || dir[n - 1] == '/')) snprintf(out, max, "%s%s", dir, name);
  else snprintf(out, max, "%s\\%s", dir ? dir : "", name);
}

static int spawn_dump(const char *url, const char *ua, const char *path) {
  const wchar_t *mpv = player_mpv_path();
  if (!mpv || !mpv[0] || !url || !path) return 0;
  wchar_t wurl[MAX_URL], wpath[MAX_PATH], wua[MAX_UA], cmd[MAX_URL + 512];
  utf8_to_wide(url, wurl, MAX_URL);
  utf8_to_wide(path, wpath, MAX_PATH);
  utf8_to_wide(ua && ua[0] ? ua : "Mozilla/5.0", wua, MAX_UA);
  swprintf(cmd, MAX_URL + 512,
           L"\"%s\" --no-config --vo=null --ao=null --hwdec=no --really-quiet --no-osc --osd-level=0 "
           L"--no-input-default-bindings --ytdl=no --cache=yes --cache-secs=8 "
           L"--user-agent=\"%s\" --stream-record=\"%s\" --idle=no \"%s\"",
           mpv, wua, wpath, wurl);
  wchar_t cwd[MAX_PATH];
  wcsncpy(cwd, mpv, MAX_PATH);
  wchar_t *cut = wcsrchr(cwd, L'\\');
  if (cut) *cut = 0;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(mpv, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) return 0;
  CloseHandle(pi.hThread);
  g_dump = pi.hProcess;
  snprintf(g_dump_path, MAX_PATHU, "%s", path);
  return 1;
}

static void stop_dump(void) {
  if (g_dump) {
    TerminateProcess(g_dump, 0);
    WaitForSingleObject(g_dump, 800);
    CloseHandle(g_dump);
    g_dump = NULL;
  }
}

static void with_ext(const char *src, const char *ext, char *dst, int max) {
  snprintf(dst, max, "%s", src ? src : "");
  char *slash = strrchr(dst, '\\');
  char *dot = strrchr(dst, '.');
  if (dot && (!slash || dot > slash)) *dot = 0;
  int n = (int)strlen(dst);
  snprintf(dst + n, max - n, ".%s", ext && ext[0] ? ext : "mp4");
}

static int file_nonempty(const wchar_t *path) {
  WIN32_FILE_ATTRIBUTE_DATA fd;
  if (!path || !path[0]) return 0;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fd)) return 0;
  return fd.nFileSizeLow || fd.nFileSizeHigh;
}

static int remux_copy(const wchar_t *mpv, const wchar_t *cwd, const wchar_t *src, const wchar_t *dst, const wchar_t *ofmt) {
  wchar_t cmd[MAX_PATH * 4];
  if (!mpv || !src || !dst || !ofmt) return 0;
  DeleteFileW(dst);
  swprintf(cmd, MAX_PATH * 4,
           L"\"%s\" --no-config --ovc=copy --oac=copy --of=%s --o=\"%s\" --vo=null --ao=null "
           L"--really-quiet --no-terminal --force-window=no \"%s\"",
           mpv, ofmt, dst, src);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(mpv, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) return 0;
  WaitForSingleObject(pi.hProcess, 120000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return file_nonempty(dst);
}

static DWORD WINAPI remux_thread(void *arg) {
  char *src = (char *)arg;
  if (!src || !src[0]) {
    free(src);
    return 0;
  }
  const wchar_t *mpv = player_mpv_path();
  if (!mpv || !mpv[0]) {
    free(src);
    return 0;
  }
  wchar_t cwd[MAX_PATH], wsrc[MAX_PATH], wmp4[MAX_PATH], wmkv[MAX_PATH];
  wcsncpy(cwd, mpv, MAX_PATH);
  wchar_t *cut = wcsrchr(cwd, L'\\');
  if (cut) *cut = 0;
  char mp4[MAX_PATHU], mkv[MAX_PATHU];
  with_ext(src, "mp4", mp4, MAX_PATHU);
  with_ext(src, "mkv", mkv, MAX_PATHU);
  utf8_to_wide(src, wsrc, MAX_PATH);
  utf8_to_wide(mp4, wmp4, MAX_PATH);
  utf8_to_wide(mkv, wmkv, MAX_PATH);
  if (remux_copy(mpv, cwd, wsrc, wmp4, L"mp4")) {
    DeleteFileW(wsrc);
    DeleteFileW(wmkv);
  } else if (remux_copy(mpv, cwd, wsrc, wmkv, L"matroska")) {
    DeleteFileW(wsrc);
    DeleteFileW(wmp4);
  } else {
    DeleteFileW(wmp4);
    DeleteFileW(wmkv);
  }
  free(src);
  return 0;
}

static void remux_later(const char *path) {
  if (!path || !path[0]) return;
  size_t n = strlen(path) + 1;
  char *dup = (char *)malloc(n);
  if (!dup) return;
  memcpy(dup, path, n);
  HANDLE t = CreateThread(NULL, 0, remux_thread, dup, 0, NULL);
  if (t) CloseHandle(t);
  else free(dup);
}

int rec_live_active(void) { return player_recording(); }

int rec_path_error(const char *path) {
  if (!path || !path[0]) return -2;
  wchar_t w[MAX_PATH];
  if (!utf8_to_wide(path, w, MAX_PATH)) return -4;
  DWORD attr = GetFileAttributesW(w);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return -4;
  ULARGE_INTEGER freeb;
  memset(&freeb, 0, sizeof freeb);
  if (!GetDiskFreeSpaceExW(w, &freeb, NULL, NULL)) return -5;
  if (freeb.QuadPart < 50ULL * 1024ULL * 1024ULL) return -3;
  wchar_t test[MAX_PATH];
  swprintf(test, MAX_PATH, L"%s\\.mirefir-write", w);
  HANDLE h = CreateFileW(test, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    if (e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL) return -3;
    if (e == ERROR_ACCESS_DENIED || e == ERROR_WRITE_PROTECT) return -5;
    return -4;
  }
  CloseHandle(h);
  return 0;
}

int rec_live_start(const Playlist *pl, Settings *st, int index) {
  if (!st || !pl || index < 0 || index >= pl->count) return 0;
  if (!st->recordings_enabled) return -1;
  if (!st->rec_path[0]) return -2;
  int err = rec_path_error(st->rec_path);
  if (err) return err;
  if (player_recording()) {
    rec_live_stop();
    return 2;
  }
  char name[160], full[MAX_PATHU];
  make_name(pl->items[index].name, "ts", name, 160);
  join_path(st->rec_path, name, full, MAX_PATHU);
  if (!player_record_start(full)) return 0;
  snprintf(g_live_path, MAX_PATHU, "%s", full);
  return 1;
}

void rec_live_stop(void) {
  if (!player_recording()) return;
  player_record_stop();
  remux_later(g_live_path);
  g_live_path[0] = 0;
}

void rec_on_zap(void) {
  if (player_recording()) rec_live_stop();
}

int rec_scan(const Settings *st, RecFile *out, int max) {
  if (!out || max < 1) return 0;
  int n = 0;
  char dirs[2][MAX_PATHU];
  int dn = 0;
  rec_internal_dir(dirs[dn], MAX_PATHU);
  if (dirs[dn][0]) dn++;
  if (st && st->rec_path[0] && (!dn || strcmp(st->rec_path, dirs[0]) != 0)) {
    snprintf(dirs[dn], MAX_PATHU, "%s", st->rec_path);
    dn++;
  }
  for (int d = 0; d < dn && n < max; d++) {
    wchar_t wdir[MAX_PATH], pat[MAX_PATH];
    utf8_to_wide(dirs[d], wdir, MAX_PATH);
    swprintf(pat, MAX_PATH, L"%s\\*.*", wdir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      wchar_t *dot = wcsrchr(fd.cFileName, L'.');
      if (!dot) continue;
      if (_wcsicmp(dot, L".ts") && _wcsicmp(dot, L".mkv") && _wcsicmp(dot, L".mp4") && _wcsicmp(dot, L".m4v")) continue;
      RecFile *f = &out[n];
      memset(f, 0, sizeof *f);
      char name[MAX_NAME];
      wide_to_utf8(fd.cFileName, name, MAX_NAME);
      snprintf(f->title, MAX_NAME, "%s", name);
      char diru[MAX_PATHU];
      wide_to_utf8(wdir, diru, MAX_PATHU);
      join_path(diru, name, f->path, MAX_PATHU);
      ULARGE_INTEGER sz;
      sz.LowPart = fd.nFileSizeLow;
      sz.HighPart = fd.nFileSizeHigh;
      f->bytes = (long long)sz.QuadPart;
      FILETIME loc;
      FileTimeToLocalFileTime(&fd.ftLastWriteTime, &loc);
      ULARGE_INTEGER u;
      u.LowPart = loc.dwLowDateTime;
      u.HighPart = loc.dwHighDateTime;
      f->mtime = (long long)(u.QuadPart / 10000ULL - 11644473600000ULL);
      n++;
    } while (FindNextFileW(h, &fd) && n < max);
    FindClose(h);
  }
  for (int i = 0; i < n; i++) {
    for (int k = i + 1; k < n; k++) {
      if (out[k].mtime > out[i].mtime) {
        RecFile t = out[i];
        out[i] = out[k];
        out[k] = t;
      }
    }
  }
  return n;
}

int rec_has_timer_for(int ch, long long start, long long end) {
  if (g_tm_status == REC_OFF) return 0;
  return g_tm_ch == ch && g_tm_start == start && g_tm_end == end;
}

static void persist_timer(void) {
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\timer.json", dir);
  if (g_tm_status == REC_OFF) {
    DeleteFileW(path);
    return;
  }
  char title[192], name[MAX_NAME * 2], recpath[MAX_PATHU * 2];
  int t = 0, n = 0, p = 0;
  for (int i = 0; g_tm_title[i] && t < 190; i++) {
    char c = g_tm_title[i];
    if (c == '"' || c == '\\') continue;
    title[t++] = c;
  }
  title[t] = 0;
  for (int i = 0; g_tm_name[i] && n < MAX_NAME * 2 - 1; i++) {
    char c = g_tm_name[i];
    if (c == '"' || c == '\\') continue;
    name[n++] = c;
  }
  name[n] = 0;
  for (int i = 0; g_dump_path[i] && p < MAX_PATHU * 2 - 1; i++) {
    char c = g_dump_path[i];
    if (c == '"' || c == '\\') continue;
    recpath[p++] = c;
  }
  recpath[p] = 0;
  char buf[1024];
  snprintf(buf, sizeof buf,
           "{\"name\":\"%s\",\"title\":\"%s\",\"start\":%lld,\"end\":%lld,\"status\":%d,\"path\":\"%s\"}\n", name, title,
           g_tm_start, g_tm_end, g_tm_status, recpath);
  write_file_utf8(path, buf);
}

void rec_save(const Playlist *pl, const Settings *st) {
  (void)pl;
  (void)st;
  persist_timer();
}

void rec_load(const Playlist *pl, Settings *st) {
  if (!pl || !st) return;
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\timer.json", dir);
  char *json = read_file_utf8(path, NULL);
  if (!json) return;
  char name[MAX_NAME], title[96], recpath[MAX_PATHU];
  name[0] = title[0] = recpath[0] = 0;
  int status = 0;
  double start = 0, end = 0;
  json_extract_string(json, "name", name, MAX_NAME);
  json_extract_string(json, "title", title, 96);
  json_extract_string(json, "path", recpath, MAX_PATHU);
  json_extract_int(json, "status", &status);
  json_extract_double(json, "start", &start);
  json_extract_double(json, "end", &end);
  free(json);
  long long now = epg_now_ms();
  if (!name[0] || end <= start || now >= (long long)end) {
    DeleteFileW(path);
    return;
  }
  int ch = playlist_find_name(pl, name);
  if (ch < 0) {
    DeleteFileW(path);
    return;
  }
  g_tm_ch = ch;
  g_tm_start = (long long)start;
  g_tm_end = (long long)end;
  snprintf(g_tm_title, 96, "%s", title[0] ? title : name);
  snprintf(g_tm_name, MAX_NAME, "%s", name);
  snprintf(g_dump_path, MAX_PATHU, "%s", recpath);
  g_tm_status = (status == REC_RUN) ? REC_SCHED : REC_SCHED;
  persist_timer();
}

void rec_timer_cancel(void) {
  stop_dump();
  g_tm_status = REC_OFF;
  g_tm_ch = -1;
  g_tm_title[0] = 0;
  g_tm_name[0] = 0;
  persist_timer();
}

int rec_timer_toggle(const Playlist *pl, const Settings *st, int ch, const char *title, long long start, long long end) {
  (void)st;
  if (!pl || ch < 0 || ch >= pl->count || end <= start) return 0;
  if (rec_has_timer_for(ch, start, end)) {
    rec_timer_cancel();
    return 2;
  }
  rec_timer_cancel();
  g_tm_ch = ch;
  g_tm_start = start;
  g_tm_end = end;
  g_tm_status = REC_SCHED;
  snprintf(g_tm_title, 96, "%s", title && title[0] ? title : "Запись");
  snprintf(g_tm_name, MAX_NAME, "%s", pl->items[ch].name);
  persist_timer();
  return 1;
}

int rec_timer_get(int *ch, char *title, int tmax, char *channel, int cmax, long long *start, long long *end, int *status) {
  if (g_tm_status == REC_OFF) return 0;
  if (ch) *ch = g_tm_ch;
  if (title && tmax > 0) snprintf(title, tmax, "%s", g_tm_title);
  if (channel && cmax > 0) snprintf(channel, cmax, "%s", g_tm_name);
  if (start) *start = g_tm_start;
  if (end) *end = g_tm_end;
  if (status) *status = g_tm_status;
  return 1;
}

void rec_tick(const Playlist *pl, Settings *st) {
  if (g_tm_status == REC_OFF || !pl || !st) return;
  long long now = epg_now_ms();
  if (g_tm_status == REC_SCHED && now >= g_tm_start && now < g_tm_end) {
    if (!st->recordings_enabled || !st->rec_path[0]) {
      rec_timer_cancel();
      player_show_text(i18n_u("recTimerFail"), 2000);
      return;
    }
    int err = rec_path_error(st->rec_path);
    if (err) {
      rec_timer_cancel();
      if (err == -3) player_show_text(i18n_u("recNoSpace"), 2000);
      else if (err == -4) player_show_text(i18n_u("recNoDisk"), 2000);
      else if (err == -5) player_show_text(i18n_u("recNoPerm"), 2000);
      else player_show_text(i18n_u("recTimerFail"), 2000);
      return;
    }
    if (g_tm_ch < 0 || g_tm_ch >= pl->count) {
      rec_timer_cancel();
      return;
    }
    char urls[12][MAX_URL];
    int n = 0;
    build_play_urls(pl, g_tm_ch, device_start_quality(), urls, &n, 12);
    if (!n) {
      rec_timer_cancel();
      return;
    }
    char name[160], full[MAX_PATHU];
    make_name(g_tm_name, "ts", name, 160);
    join_path(st->rec_path, name, full, MAX_PATHU);
    if (!spawn_dump(urls[0], pl->items[g_tm_ch].ua[0] ? pl->items[g_tm_ch].ua : st->user_agent, full)) {
      player_show_text(i18n_u("recTimerFail"), 2000);
      rec_timer_cancel();
      return;
    }
    g_tm_status = REC_RUN;
    persist_timer();
    player_show_text(i18n_u("recTimerOn"), 1600);
  }
  if (g_tm_status == REC_RUN && now >= g_tm_end) {
    stop_dump();
    remux_later(g_dump_path);
    g_dump_path[0] = 0;
    g_tm_status = REC_OFF;
    g_tm_ch = -1;
    persist_timer();
    player_show_text(i18n_u("recTimerReady"), 2000);
  }
}

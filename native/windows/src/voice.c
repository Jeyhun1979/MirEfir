#include "app.h"
#include <math.h>
#include <mmsystem.h>
#include <shlobj.h>

#define MODEL_DIR_RU L"vosk-model-small-ru-0.22"
#define MODEL_DIR_EN L"vosk-model-small-en-us-0.15"
#define MODEL_ZIP_RU "https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip"
#define MODEL_ZIP_EN "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
#define SPEECH_RMS 0.015
#define SILENCE_MS 2000
#define MAX_LISTEN_MS 10000
#define WAIT_SPEECH_MS 5000
#define PEAK_SILENT 0.008
#define GRAMMAR_LIMIT 20000
#define MAX_VOICE_CH 8192
#define MAX_PHRASE 80

typedef void *(*fn_model_new)(const char *);
typedef void (*fn_model_free)(void *);
typedef void *(*fn_rec_new)(void *, float);
typedef void *(*fn_rec_grm)(void *, float, const char *);
typedef int (*fn_rec_accept)(void *, const char *, int);
typedef const char *(*fn_rec_final)(void *);
typedef void (*fn_rec_free)(void *);
typedef void (*fn_log)(int);
typedef void (*fn_alts)(void *, int);

typedef struct VoiceCh {
  int index;
  int fav;
  char name[MAX_NAME];
  char tvg[MAX_NAME];
} VoiceCh;

static CRITICAL_SECTION g_cs;
static int g_cs_on;
static HANDLE g_thread;
static volatile LONG g_cancel;
static volatile LONG g_busy;
static int g_kind = VOICE_IDLE;
static char g_status[160];
static char g_text[192];
static int g_channel = -1;
static int g_switch;
static const Playlist *g_pl;
static Settings *g_st;
static int g_duck_on;
static int g_duck_vol;
static int g_duck_mute;

static HMODULE g_lib;
static int g_loaded_en = -1;

static int want_en(void) { return g_st && (g_st->language[0] == 'e' || g_st->language[0] == 'E'); }
static const wchar_t *model_dir(void) { return want_en() ? MODEL_DIR_EN : MODEL_DIR_RU; }
static const char *model_zip(void) { return want_en() ? MODEL_ZIP_EN : MODEL_ZIP_RU; }
static void *g_model;
static fn_model_new p_model_new;
static fn_model_free p_model_free;
static fn_rec_new p_rec_new;
static fn_rec_grm p_rec_grm;
static fn_rec_accept p_rec_accept;
static fn_rec_final p_rec_final;
static fn_rec_free p_rec_free;
static fn_log p_log;
static fn_alts p_alts;

static void lock_cs(void) {
  if (!g_cs_on) {
    InitializeCriticalSection(&g_cs);
    g_cs_on = 1;
  }
  EnterCriticalSection(&g_cs);
}

static void unlock_cs(void) { LeaveCriticalSection(&g_cs); }

static void set_status(const char *s) {
  lock_cs();
  snprintf(g_status, sizeof g_status, "%s", s ? s : "");
  unlock_cs();
}

static int cancelled(void) { return InterlockedCompareExchange(&g_cancel, 0, 0) != 0; }

static int dir_exists(const wchar_t *path) {
  DWORD a = GetFileAttributesW(path);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int file_exists(const wchar_t *path) {
  DWORD a = GetFileAttributesW(path);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int model_ready(const wchar_t *root) {
  wchar_t p[MAX_PATH];
  swprintf(p, MAX_PATH, L"%s\\%s\\am", root, model_dir());
  if (dir_exists(p)) return 1;
  swprintf(p, MAX_PATH, L"%s\\%s\\conf", root, model_dir());
  return dir_exists(p);
}

static int lib_ready(const wchar_t *root) {
  wchar_t p[MAX_PATH];
  swprintf(p, MAX_PATH, L"%s\\lib\\libvosk.dll", root);
  return file_exists(p);
}

static int find_named(const wchar_t *root, const wchar_t *want, wchar_t *out, int max, int depth) {
  if (depth > 8) return 0;
  wchar_t pat[MAX_PATH];
  swprintf(pat, MAX_PATH, L"%s\\*", root);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  int hit = 0;
  do {
    if (fd.cFileName[0] == L'.') continue;
    wchar_t full[MAX_PATH];
    swprintf(full, MAX_PATH, L"%s\\%s", root, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (find_named(full, want, out, max, depth + 1)) {
        hit = 1;
        break;
      }
    } else if (_wcsicmp(fd.cFileName, want) == 0) {
      wcsncpy(out, full, max);
      out[max - 1] = 0;
      hit = 1;
      break;
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return hit;
}

static int run_tar(const wchar_t *zip, const wchar_t *dest) {
  wchar_t tar[MAX_PATH], sys[MAX_PATH], cmd[MAX_PATH * 3];
  GetSystemDirectoryW(sys, MAX_PATH);
  swprintf(tar, MAX_PATH, L"%s\\tar.exe", sys);
  if (!file_exists(tar)) return 0;
  CreateDirectoryW(dest, NULL);
  swprintf(cmd, MAX_PATH * 3, L"\"%s\" -xf \"%s\" -C \"%s\"", tar, zip, dest);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 0;
  WaitForSingleObject(pi.hProcess, 180000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

static int write_bytes(const wchar_t *path, const char *buf, int len) {
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  DWORD w = 0;
  int ok = WriteFile(h, buf, (DWORD)len, &w, NULL) && (int)w == len;
  CloseHandle(h);
  return ok;
}

static int download_to(const char *url, const wchar_t *path) {
  char *buf = NULL;
  int len = 0;
  if (http_get_lim(url, &buf, &len, 90 * 1024 * 1024, 180000) != 0 || !buf || len < 1000) {
    free(buf);
    return 0;
  }
  int ok = write_bytes(path, buf, len);
  free(buf);
  return ok;
}

static void copy_dir_files(const wchar_t *from, const wchar_t *to) {
  CreateDirectoryW(to, NULL);
  wchar_t pat[MAX_PATH];
  swprintf(pat, MAX_PATH, L"%s\\*", from);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    wchar_t *dot = wcsrchr(fd.cFileName, L'.');
    if (dot && (!_wcsicmp(dot, L".lib") || !_wcsicmp(dot, L".a") || !_wcsicmp(dot, L".exp") || !_wcsicmp(dot, L".pdb")))
      continue;
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    swprintf(src, MAX_PATH, L"%s\\%s", from, fd.cFileName);
    swprintf(dst, MAX_PATH, L"%s\\%s", to, fd.cFileName);
    CopyFileW(src, dst, FALSE);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static int ensure_lib_from_zip(const wchar_t *root) {
  wchar_t zip[MAX_PATH], unpack[MAX_PATH], found[MAX_PATH], libdir[MAX_PATH];
  swprintf(zip, MAX_PATH, L"%s\\vosk-lib-0.3.45.zip", root);
  swprintf(unpack, MAX_PATH, L"%s\\lib-unpack", root);
  swprintf(libdir, MAX_PATH, L"%s\\lib", root);
  set_status(i18n_u("voiceDlEngine"));
  if (!download_to("https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-win64-0.3.45.zip", zip)) return 0;
  if (cancelled()) return 0;
  set_status(i18n_u("voiceUnEngine"));
  if (!run_tar(zip, unpack)) return 0;
  if (!find_named(unpack, L"libvosk.dll", found, MAX_PATH, 0)) return 0;
  wchar_t srcdir[MAX_PATH];
  wcsncpy(srcdir, found, MAX_PATH);
  wchar_t *cut = wcsrchr(srcdir, L'\\');
  if (cut) *cut = 0;
  copy_dir_files(srcdir, libdir);
  DeleteFileW(zip);
  return lib_ready(root);
}

static int ensure_model_from_zip(const wchar_t *root) {
  wchar_t zip[MAX_PATH];
  swprintf(zip, MAX_PATH, L"%s\\%s.zip", root, model_dir());
  set_status(i18n_u("voiceDlModel"));
  if (!download_to(model_zip(), zip)) return 0;
  if (cancelled()) return 0;
  set_status(i18n_u("voiceUnModel"));
  if (!run_tar(zip, root)) return 0;
  DeleteFileW(zip);
  return model_ready(root);
}

static int pick_root(wchar_t *out, int max) {
  wchar_t native_dir[MAX_PATH], live[MAX_PATH], nvosk[MAX_PATH], lvosk[MAX_PATH];
  profile_dirs(native_dir, live, MAX_PATH);
  swprintf(nvosk, MAX_PATH, L"%s\\vosk", native_dir);
  swprintf(lvosk, MAX_PATH, L"%s\\vosk", live);
  CreateDirectoryW(nvosk, NULL);
  if (model_ready(nvosk) && lib_ready(nvosk)) {
    wcsncpy(out, nvosk, max);
    out[max - 1] = 0;
    return 1;
  }
  if (!want_en() && model_ready(lvosk) && lib_ready(lvosk)) {
    wcsncpy(out, lvosk, max);
    out[max - 1] = 0;
    return 1;
  }
  if (!model_ready(nvosk) && !ensure_model_from_zip(nvosk)) return 0;
  if (!lib_ready(nvosk) && !ensure_lib_from_zip(nvosk)) return 0;
  if (!model_ready(nvosk) || !lib_ready(nvosk)) return 0;
  wcsncpy(out, nvosk, max);
  out[max - 1] = 0;
  return 1;
}

static int load_engine(const wchar_t *root) {
  if (g_model && g_lib && g_loaded_en == want_en()) return 1;
  if (g_model && p_model_free) {
    p_model_free(g_model);
    g_model = NULL;
  }
  wchar_t libdir[MAX_PATH], dll[MAX_PATH], model[MAX_PATH];
  char modelu[MAX_PATHU];
  swprintf(libdir, MAX_PATH, L"%s\\lib", root);
  swprintf(dll, MAX_PATH, L"%s\\libvosk.dll", libdir);
  swprintf(model, MAX_PATH, L"%s\\%s", root, model_dir());
  wide_to_utf8(model, modelu, MAX_PATHU);
  SetDllDirectoryW(libdir);
  g_lib = LoadLibraryW(dll);
  SetDllDirectoryW(NULL);
  if (!g_lib) return 0;
  p_model_new = (fn_model_new)GetProcAddress(g_lib, "vosk_model_new");
  p_model_free = (fn_model_free)GetProcAddress(g_lib, "vosk_model_free");
  p_rec_new = (fn_rec_new)GetProcAddress(g_lib, "vosk_recognizer_new");
  p_rec_grm = (fn_rec_grm)GetProcAddress(g_lib, "vosk_recognizer_new_grm");
  p_rec_accept = (fn_rec_accept)GetProcAddress(g_lib, "vosk_recognizer_accept_waveform");
  p_rec_final = (fn_rec_final)GetProcAddress(g_lib, "vosk_recognizer_final_result");
  p_rec_free = (fn_rec_free)GetProcAddress(g_lib, "vosk_recognizer_free");
  p_log = (fn_log)GetProcAddress(g_lib, "vosk_set_log_level");
  p_alts = (fn_alts)GetProcAddress(g_lib, "vosk_recognizer_set_max_alternatives");
  if (!p_model_new || !p_rec_new || !p_rec_accept || !p_rec_final || !p_rec_free) return 0;
  if (p_log) p_log(-1);
  set_status(i18n_u("voiceLoad"));
  g_model = p_model_new(modelu);
  g_loaded_en = want_en();
  return g_model != NULL;
}

static int hidden_group(const Settings *st, const char *name) {
  if (!st || !name) return 0;
  for (int i = 0; i < st->hidden_n; i++) {
    if (strcmp(st->hidden[i], name) == 0) return 1;
  }
  return 0;
}

static int is_fav(const Settings *st, int index) {
  if (!st) return 0;
  for (int i = 0; i < st->fav_n; i++) {
    if (st->fav[i] == index) return 1;
  }
  return 0;
}

static void json_esc(const char *in, char *out, int max) {
  int o = 0;
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

static void fold_text(const char *in, char *out, int max) {
  wchar_t w[256], t[256];
  if (!out || max < 2) return;
  out[0] = 0;
  utf8_to_wide(in ? in : "", w, 256);
  CharLowerBuffW(w, (DWORD)wcslen(w));
  int n = 0, space = 0;
  for (int i = 0; w[i] && n < 254; i++) {
    wchar_t c = w[i];
    if (c == 0x0451 || c == L'ё') c = 0x0435;
    int let = (c >= L'a' && c <= L'z') || (c >= 0x0430 && c <= 0x044F) || (c >= L'0' && c <= L'9');
    if (let) {
      t[n++] = c;
      space = 0;
    } else if (!space && n) {
      t[n++] = L' ';
      space = 1;
    }
  }
  while (n && t[n - 1] == L' ') n--;
  t[n] = 0;
  wide_to_utf8(t, out, max);
  static const char *drop[] = {"uhd", "fhd", "hd", "sd", "4k", "hevc", "hdr", "tv", "тв", "канал", "телеканал", "программа", NULL};
  char buf[256];
  buf[0] = 0;
  const char *p = out;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    char word[48];
    int k = 0;
    while (p[k] && p[k] != ' ' && k < 46) {
      word[k] = p[k];
      k++;
    }
    word[k] = 0;
    p += k;
    int skip = 0;
    for (int i = 0; drop[i]; i++) {
      if (strcmp(word, drop[i]) == 0) {
        skip = 1;
        break;
      }
    }
    if (skip) continue;
    if (!strcmp(word, "один") || !strcmp(word, "одна")) snprintf(word, sizeof word, "1");
    else if (!strcmp(word, "два")) snprintf(word, sizeof word, "2");
    else if (!strcmp(word, "три")) snprintf(word, sizeof word, "3");
    if (buf[0]) strncat(buf, " ", sizeof buf - strlen(buf) - 1);
    strncat(buf, word, sizeof buf - strlen(buf) - 1);
  }
  snprintf(out, max, "%s", buf);
}

static void clean_phrase(const char *in, char *out, int max) {
  wchar_t w[256], t[256];
  out[0] = 0;
  utf8_to_wide(in ? in : "", w, 256);
  int n = 0, space = 0;
  for (int i = 0; w[i] && n < 254; i++) {
    wchar_t c = w[i];
    if (c == 0x0451 || c == 0x0401) c = (c == 0x0401) ? L'Е' : 0x0435;
    int let = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
              (c >= 0x0410 && c <= 0x044F) || c == 0x0401 || c == 0x0451;
    if (let) {
      t[n++] = c;
      space = 0;
    } else if (!space && n) {
      t[n++] = L' ';
      space = 1;
    }
  }
  while (n && t[n - 1] == L' ') n--;
  t[n] = 0;
  wide_to_utf8(t, out, max);
}

static int edit_dist(const char *a, const char *b) {
  int na = (int)strlen(a ? a : ""), nb = (int)strlen(b ? b : "");
  if (a && b && strcmp(a, b) == 0) return 0;
  if (!na || !nb) return 99;
  if (na > 48 || nb > 48 || abs(na - nb) > 3) return 99;
  int prev[50], cur[50];
  for (int j = 0; j <= nb; j++) prev[j] = j;
  for (int i = 1; i <= na; i++) {
    cur[0] = i;
    for (int j = 1; j <= nb; j++) {
      int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      int v = prev[j] + 1;
      if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
      if (prev[j - 1] + cost < v) v = prev[j - 1] + cost;
      cur[j] = v;
    }
    memcpy(prev, cur, (size_t)(nb + 1) * sizeof(int));
  }
  return prev[nb];
}

static int score_ch(const VoiceCh *c, const char *query) {
  char names[2][128];
  fold_text(c->name, names[0], 128);
  fold_text(c->tvg, names[1], 128);
  int score = 0;
  char parts[8][32];
  int pn = 0;
  const char *p = query;
  while (*p && pn < 8) {
    while (*p == ' ') p++;
    if (!*p) break;
    int k = 0;
    while (p[k] && p[k] != ' ' && k < 30) {
      parts[pn][k] = p[k];
      k++;
    }
    parts[pn][k] = 0;
    if (k >= 2) pn++;
    p += k;
  }
  for (int i = 0; i < 2; i++) {
    const char *name = names[i];
    if (!name[0]) continue;
    int s = 0;
    if (!strcmp(name, query)) s = 120 - (int)strlen(name);
    else if (!strncmp(name, query, strlen(query)) || (strlen(name) >= 3 && !strncmp(query, name, strlen(name)))) s = 86;
    else if (strstr(name, query) || (strlen(name) >= 3 && strstr(query, name))) s = 72;
    else if (pn >= 2) {
      int all = 1;
      for (int k = 0; k < pn; k++) {
        if (!strstr(name, parts[k])) {
          all = 0;
          break;
        }
      }
      if (all) s = 70;
    }
    if (!s && strlen(name) >= 4 && strlen(query) >= 4 && edit_dist(name, query) <= 1) s = 68;
    else if (!s && strlen(name) >= 5 && strlen(query) >= 5 && edit_dist(name, query) <= 2) s = 62;
    if (s > score) score = s;
  }
  return score;
}

static int pick_channel(const VoiceCh *list, int n, const char *spoken, int fav_first) {
  char query[192];
  fold_text(spoken, query, 192);
  if (!query[0]) return -1;
  int best = -1, best_s = 0;
  for (int pass = 0; pass < (fav_first ? 2 : 1); pass++) {
    for (int i = 0; i < n; i++) {
      if (fav_first && ((pass == 0 && !list[i].fav) || (pass == 1 && list[i].fav))) continue;
      int s = score_ch(&list[i], query);
      if (s > best_s) {
        best_s = s;
        best = list[i].index;
      }
    }
    if (best_s >= 60) return best;
  }
  return best_s >= 60 ? best : -1;
}

static int starts_utf(const char *s, const char *pfx) {
  if (!s || !pfx) return 0;
  size_t n = strlen(pfx);
  return n && _strnicmp(s, pfx, n) == 0;
}

static void parse_cmd(const char *raw, int *sw, char *query, int qmax) {
  char t[192];
  int o = 0;
  for (int i = 0; raw && raw[i] && o < 190; i++) {
    if (raw[i] == ' ' && o && t[o - 1] == ' ') continue;
    t[o++] = raw[i];
  }
  while (o && t[o - 1] == ' ') o--;
  t[o] = 0;
  *sw = 0;
  query[0] = 0;
  const char *p = t;
  static const char *kZap[] = {
      "переключай на ", "переключить на ", "переключи на ", "включай на ", "включить на ", "включи на ",
      "переключай ",    "переключить ",    "переключи ",    "включай ",    "включить ",    "включи ",
      "открой канал ",  "открой ",         "поставь ",      "switch to ",  "turn on ",     "put on ",
      NULL};
  for (int i = 0; kZap[i]; i++) {
    if (!starts_utf(p, kZap[i])) continue;
    *sw = 1;
    p += (int)strlen(kZap[i]);
    while (*p == ' ') p++;
    if (starts_utf(p, "на ") || starts_utf(p, "on ")) p += 3;
    break;
  }
  if (!*sw && (starts_utf(p, "найди ") || starts_utf(p, "найти ") || starts_utf(p, "поиск ") || starts_utf(p, "ищи ") ||
               starts_utf(p, "find ") || starts_utf(p, "search "))) {
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    if (starts_utf(p, "канал ")) p += 6;
  }
  snprintf(query, qmax, "%s", p && p[0] ? p : t);
}

static int add_phrase(char phrases[][MAX_PHRASE], int *n, int maxn, const char *raw) {
  char p[MAX_PHRASE];
  clean_phrase(raw, p, MAX_PHRASE);
  if ((int)strlen(p) < 2 || (int)strlen(p) > 72) return 0;
  for (int i = 0; i < *n; i++) {
    if (_stricmp(phrases[i], p) == 0) return 0;
  }
  if (*n >= maxn) return 0;
  snprintf(phrases[*n], MAX_PHRASE, "%s", p);
  (*n)++;
  return 1;
}

static void strip_qtail(char *s) {
  int L = (int)strlen(s);
  if (L <= 3) return;
  const char *tail[] = {" UHD", " FHD", " HD", " SD", " 4K", " HEVC", " HDR", NULL};
  for (int t = 0; tail[t]; t++) {
    int tl = (int)strlen(tail[t]);
    if (L > tl && _stricmp(s + L - tl, tail[t]) == 0) {
      s[L - tl] = 0;
      return;
    }
  }
}

static char *build_grammar(const VoiceCh *list, int n) {
  if (n < 1) return NULL;
  int maxn = n * 2 + 8;
  if (maxn > GRAMMAR_LIMIT) maxn = GRAMMAR_LIMIT;
  char (*phrases)[MAX_PHRASE] = (char (*)[MAX_PHRASE])malloc((size_t)maxn * MAX_PHRASE);
  if (!phrases) return NULL;
  int pn = 0;
  for (int i = 0; i < n && pn < maxn; i++) {
    char stripped[MAX_NAME];
    clean_phrase(list[i].name, stripped, MAX_NAME);
    strip_qtail(stripped);
    add_phrase(phrases, &pn, maxn, stripped);
    add_phrase(phrases, &pn, maxn, list[i].tvg);
  }
  const char *pref[8];
  int npref = 0;
  if (want_en()) {
    pref[npref++] = "";
    pref[npref++] = "switch to ";
    pref[npref++] = "turn on ";
  } else {
    pref[npref++] = "";
    pref[npref++] = "переключи на ";
    pref[npref++] = "переключай на ";
    pref[npref++] = "включи ";
    pref[npref++] = "включай ";
  }
  int cap = pn * npref * 96 + 32;
  if (cap < 4096) cap = 4096;
  char *json = (char *)malloc((size_t)cap);
  if (!json) {
    free(phrases);
    return NULL;
  }
  int o = 0, count = 0;
  json[o++] = '[';
  for (int k = 0; k < npref && count < GRAMMAR_LIMIT; k++) {
    for (int i = 0; i < pn && count < GRAMMAR_LIMIT; i++) {
      char low[MAX_PHRASE], full[160], esc[200];
      wchar_t wl[MAX_PHRASE];
      utf8_to_wide(phrases[i], wl, MAX_PHRASE);
      CharLowerBuffW(wl, (DWORD)wcslen(wl));
      wide_to_utf8(wl, low, MAX_PHRASE);
      snprintf(full, sizeof full, "%s%s", pref[k], low);
      json_esc(full, esc, (int)sizeof esc);
      if (count) {
        if (o + 4 >= cap) break;
        json[o++] = ',';
      }
      int add = snprintf(json + o, cap - o, "\"%s\"", esc);
      if (add < 0 || o + add >= cap - 16) break;
      o += add;
      count++;
    }
  }
  free(phrases);
  if (count) json[o++] = ',';
  o += snprintf(json + o, cap - o, "\"[unk]\"]");
  return json;
}

typedef struct AltPick {
  char text[192];
  double conf;
} AltPick;

static void on_alt(const char *item, void *u) {
  AltPick *p = (AltPick *)u;
  char t[192];
  double c = 1;
  if (!json_extract_string(item, "text", t, 192) || !t[0]) return;
  json_extract_double(item, "confidence", &c);
  if (c > 0 && c < 0.12) return;
  if (!p->text[0] || c > p->conf) {
    snprintf(p->text, 192, "%s", t);
    p->conf = c;
  }
}

static void clean_heard(char *s) {
  char tmp[192];
  int o = 0, sp = 0;
  for (int i = 0; s && s[i] && o < 190;) {
    if (s[i] == '[' && (_strnicmp(s + i, "[unk]", 5) == 0)) {
      i += 5;
      continue;
    }
    if (s[i] == ' ') {
      if (o && !sp) tmp[o++] = ' ';
      sp = 1;
      i++;
      continue;
    }
    tmp[o++] = s[i++];
    sp = 0;
  }
  while (o && tmp[o - 1] == ' ') o--;
  tmp[o] = 0;
  snprintf(s, 192, "%s", tmp);
}

static void transcribe(const char *pcm, int bytes, const char *grammar, char *out, int max) {
  out[0] = 0;
  if (!g_model || bytes < 3200) return;
  void *rec = NULL;
  if (grammar && grammar[0] && p_rec_grm) rec = p_rec_grm(g_model, 16000.f, grammar);
  if (!rec) rec = p_rec_new(g_model, 16000.f);
  if (!rec) return;
  if (p_alts) p_alts(rec, 3);
  for (int off = 0; off < bytes; off += 16000) {
    int n = bytes - off;
    if (n > 16000) n = 16000;
    p_rec_accept(rec, pcm + off, n);
  }
  const char *raw = p_rec_final(rec);
  char text[192];
  text[0] = 0;
  if (raw) {
    AltPick pick;
    memset(&pick, 0, sizeof pick);
    pick.conf = -1;
    const char *alts = json_value_of(raw, "alternatives");
    if (alts && *alts == '[') json_array_each(alts, on_alt, &pick);
    if (pick.text[0]) snprintf(text, sizeof text, "%s", pick.text);
    else json_extract_string(raw, "text", text, (int)sizeof text);
  }
  p_rec_free(rec);
  clean_heard(text);
  if (!text[0] && grammar && grammar[0]) {
    transcribe(pcm, bytes, "", out, max);
    return;
  }
  snprintf(out, max, "%s", text);
}

static int capture_pcm(char **out, int *len, double *peak) {
  *out = NULL;
  *len = 0;
  *peak = 0;
  WAVEFORMATEX fmt;
  memset(&fmt, 0, sizeof fmt);
  fmt.wFormatTag = WAVE_FORMAT_PCM;
  fmt.nChannels = 1;
  fmt.nSamplesPerSec = 16000;
  fmt.wBitsPerSample = 16;
  fmt.nBlockAlign = 2;
  fmt.nAvgBytesPerSec = 32000;
  HWAVEIN h = NULL;
  MMRESULT mm = waveInOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
  if (mm != MMSYSERR_NOERROR) return mm == MMSYSERR_ALLOCATED || mm == MMSYSERR_NODRIVER ? -2 : -1;
  enum { NBUF = 8, BLEN = 8000 };
  WAVEHDR hdr[NBUF];
  char chunks[NBUF][BLEN];
  memset(hdr, 0, sizeof hdr);
  for (int i = 0; i < NBUF; i++) {
    hdr[i].lpData = chunks[i];
    hdr[i].dwBufferLength = BLEN;
    waveInPrepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    waveInAddBuffer(h, &hdr[i], sizeof(WAVEHDR));
  }
  int cap = 32000 * 12;
  char *pcm = (char *)malloc((size_t)cap);
  if (!pcm) {
    waveInClose(h);
    return -1;
  }
  int used = 0, loud = 0;
  DWORD last_loud = 0, started = GetTickCount();
  waveInStart(h);
  set_status(i18n_u("voiceListen"));
  while (!cancelled()) {
    DWORD now = GetTickCount();
    for (int i = 0; i < NBUF; i++) {
      if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
      int n = (int)hdr[i].dwBytesRecorded;
      if (n > 0 && used + n <= cap) {
        short *s = (short *)hdr[i].lpData;
        int samples = n / 2;
        double sum = 0;
        for (int k = 0; k < samples; k++) {
          double v = s[k] / 32768.0;
          sum += v * v;
          if (v < 0) v = -v;
          if (v > *peak) *peak = v;
        }
        double rms = samples ? sqrt(sum / samples) : 0;
        if (rms >= SPEECH_RMS) {
          loud++;
          last_loud = now;
        }
        memcpy(pcm + used, hdr[i].lpData, (size_t)n);
        used += n;
      }
      hdr[i].dwFlags &= ~WHDR_DONE;
      hdr[i].dwBytesRecorded = 0;
      waveInAddBuffer(h, &hdr[i], sizeof(WAVEHDR));
    }
    int speaking = loud >= 2;
    if (speaking && last_loud && now - last_loud >= SILENCE_MS) break;
    if (!speaking && now - started >= WAIT_SPEECH_MS) break;
    if (now - started >= MAX_LISTEN_MS) break;
    Sleep(40);
  }
  waveInStop(h);
  waveInReset(h);
  for (int i = 0; i < NBUF; i++) waveInUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
  waveInClose(h);
  *out = pcm;
  *len = used;
  return 0;
}

static void duck(int on) {
  if (!g_st) return;
  if (on && !g_duck_on) {
    g_duck_vol = g_st->volume;
    g_duck_mute = g_st->muted;
    g_duck_on = 1;
    player_set_volume(g_st->volume, 1);
    player_tile_pause_all(1);
  } else if (!on && g_duck_on) {
    g_duck_on = 0;
    player_set_volume(g_st->volume, g_st->muted);
    player_tile_pause_all(0);
  }
}

static void finish(int kind, const char *text, int ch, int sw) {
  lock_cs();
  g_kind = kind;
  snprintf(g_text, sizeof g_text, "%s", text ? text : "");
  g_channel = ch;
  g_switch = sw;
  g_status[0] = 0;
  InterlockedExchange(&g_busy, 0);
  unlock_cs();
  duck(0);
}

static DWORD WINAPI voice_thread(void *arg) {
  (void)arg;
  const Playlist *pl = g_pl;
  Settings *st = g_st;
  if (!pl || !st) {
    finish(VOICE_FAIL, i18n_u("voiceNoPl"), -1, 0);
    return 0;
  }
  int cap = pl->count;
  if (cap < 1) cap = 1;
  if (cap > MAX_VOICE_CH) cap = MAX_VOICE_CH;
  VoiceCh *list = (VoiceCh *)malloc((size_t)cap * sizeof(VoiceCh));
  int n = 0;
  if (!list) {
    finish(VOICE_FAIL, i18n_u("voicePrepFail"), -1, 0);
    return 0;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < pl->count && n < cap; i++) {
      const char *grp = pl->items[i].group[0] ? pl->items[i].group : "Другие";
      if (hidden_group(st, grp)) continue;
      int fav = is_fav(st, i);
      if ((pass == 0 && !fav) || (pass == 1 && fav)) continue;
      list[n].index = i;
      list[n].fav = fav;
      snprintf(list[n].name, MAX_NAME, "%s", pl->items[i].name);
      snprintf(list[n].tvg, MAX_NAME, "%s", pl->items[i].tvg_name);
      n++;
    }
  }
  wchar_t root[MAX_PATH];
  set_status(i18n_u("voicePrep"));
  if (!pick_root(root, MAX_PATH) || cancelled()) {
    free(list);
    finish(cancelled() ? VOICE_IDLE : VOICE_FAIL, i18n_u("voicePrepFail"), -1, 0);
    return 0;
  }
  if (cancelled()) {
    free(list);
    finish(VOICE_IDLE, "", -1, 0);
    return 0;
  }
  if (!load_engine(root)) {
    free(list);
    finish(VOICE_FAIL, i18n_u("voiceLoadFail"), -1, 0);
    return 0;
  }
  if (cancelled()) {
    free(list);
    finish(VOICE_IDLE, "", -1, 0);
    return 0;
  }
  set_status(i18n_u("voiceMicOpen"));
  char *pcm = NULL;
  int bytes = 0;
  double peak = 0;
  int rec = capture_pcm(&pcm, &bytes, &peak);
  if (cancelled()) {
    free(pcm);
    free(list);
    finish(VOICE_IDLE, "", -1, 0);
    return 0;
  }
  if (rec == -2) {
    free(pcm);
    free(list);
    finish(VOICE_NEEDMIC, i18n_u("voiceNeedMic"), -1, 0);
    return 0;
  }
  if (rec != 0) {
    free(pcm);
    free(list);
    finish(VOICE_FAIL, i18n_u("voiceMicFail"), -1, 0);
    return 0;
  }
  if (peak < PEAK_SILENT) {
    free(pcm);
    free(list);
    finish(VOICE_EMPTY, i18n_u("voiceSilent"), -1, 0);
    return 0;
  }
  set_status(i18n_u("voiceHear"));
  char *grammar = build_grammar(list, n);
  char heard[192];
  transcribe(pcm, bytes, grammar ? grammar : "", heard, 192);
  free(grammar);
  free(pcm);
  if (cancelled()) {
    free(list);
    finish(VOICE_IDLE, "", -1, 0);
    return 0;
  }
  if (!heard[0]) {
    free(list);
    finish(VOICE_EMPTY, i18n_u("voiceEmpty"), -1, 0);
    return 0;
  }
  int sw = 0;
  char query[192];
  parse_cmd(heard, &sw, query, 192);
  int ch = pick_channel(list, n, query, 1);
  if (ch < 0) ch = pick_channel(list, n, heard, 1);
  free(list);
  finish(VOICE_HEARD, query[0] ? query : heard, ch, sw);
  return 0;
}

void voice_stop(void) {
  InterlockedExchange(&g_cancel, 1);
  if (g_thread) {
    WaitForSingleObject(g_thread, 4000);
    CloseHandle(g_thread);
    g_thread = NULL;
  }
  InterlockedExchange(&g_busy, 0);
  duck(0);
  lock_cs();
  if (g_kind == VOICE_BUSY) g_kind = VOICE_IDLE;
  g_status[0] = 0;
  unlock_cs();
}

void voice_start(const Playlist *pl, Settings *st) {
  voice_stop();
  InterlockedExchange(&g_cancel, 0);
  g_pl = pl;
  g_st = st;
  lock_cs();
  g_kind = VOICE_BUSY;
  g_channel = -1;
  g_switch = 0;
  g_text[0] = 0;
  snprintf(g_status, sizeof g_status, "%s", i18n_u("voicePrep"));
  unlock_cs();
  InterlockedExchange(&g_busy, 1);
  duck(1);
  g_thread = CreateThread(NULL, 0, voice_thread, NULL, 0, NULL);
  if (!g_thread) {
    finish(VOICE_FAIL, i18n_u("voiceStartFail"), -1, 0);
  }
}

int voice_busy(void) { return InterlockedCompareExchange(&g_busy, 0, 0) != 0; }

const char *voice_status(void) {
  static char copy[160];
  lock_cs();
  snprintf(copy, sizeof copy, "%s", g_status);
  unlock_cs();
  return copy;
}

int voice_poll(char *text, int tmax, int *channel, int *do_switch) {
  if (voice_busy()) return VOICE_BUSY;
  lock_cs();
  int kind = g_kind;
  if (text && tmax > 0) snprintf(text, tmax, "%s", g_text);
  if (channel) *channel = g_channel;
  if (do_switch) *do_switch = g_switch;
  if (kind != VOICE_BUSY && kind != VOICE_IDLE) g_kind = VOICE_IDLE;
  unlock_cs();
  return kind;
}

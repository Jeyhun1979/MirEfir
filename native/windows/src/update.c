#include "app.h"
#include <winhttp.h>

#ifndef CREATE_BREAKAWAY_FROM_JOB
#define CREATE_BREAKAWAY_FROM_JOB 0x01000000
#endif

#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
#define WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS 2
#endif

static const char kApi[] = "https://api.github.com/repos/Jeyhun1979/MirEfir/releases/latest";

static HWND g_notify;
static HANDLE g_th;
static volatile LONG g_stop;
static volatile LONG g_chips;
static char g_ver[40];
static wchar_t g_file[MAX_PATH];
static int g_bar;

static void upd_paths(wchar_t *setup, wchar_t *pending, wchar_t *lockp) {
  wchar_t root[MAX_PATH];
  profile_dirs(root, NULL, MAX_PATH);
  wchar_t folder[MAX_PATH];
  swprintf(folder, MAX_PATH, L"%s\\updates", root);
  CreateDirectoryW(folder, NULL);
  if (setup) swprintf(setup, MAX_PATH, L"%s\\MirEfir-Setup.exe", folder);
  if (pending) swprintf(pending, MAX_PATH, L"%s\\pending.json", folder);
  if (lockp) swprintf(lockp, MAX_PATH, L"%s\\installing.lock", root);
}

static int newer(const char *remote, const char *local) {
  int a[6] = {0}, b[6] = {0}, na = 0, nb = 0;
  const char *p = remote && (remote[0] == 'v' || remote[0] == 'V') ? remote + 1 : remote;
  const char *q = local && (local[0] == 'v' || local[0] == 'V') ? local + 1 : local;
  while (p && *p && na < 6) {
    a[na++] = atoi(p);
    while (*p && *p != '.') p++;
    if (*p == '.') p++;
  }
  while (q && *q && nb < 6) {
    b[nb++] = atoi(q);
    while (*q && *q != '.') q++;
    if (*q == '.') q++;
  }
  int n = na > nb ? na : nb;
  for (int i = 0; i < n; i++) {
    if (a[i] > b[i]) return 1;
    if (a[i] < b[i]) return 0;
  }
  return 0;
}

static int file_ok(const wchar_t *path) {
  WIN32_FILE_ATTRIBUTE_DATA st;
  if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &st)) return 0;
  return st.nFileSizeHigh > 0 || st.nFileSizeLow > 64 * 1024;
}

static void save_pending(int apply) {
  wchar_t pend[MAX_PATH];
  char utf[MAX_PATHU * 2], esc[MAX_PATHU * 2];
  upd_paths(NULL, pend, NULL);
  utf[0] = 0;
  wide_to_utf8(g_file, utf, (int)sizeof utf);
  int o = 0;
  for (int i = 0; utf[i] && o + 2 < (int)sizeof esc; i++) {
    if (utf[i] == '\\' || utf[i] == '"') esc[o++] = '\\';
    esc[o++] = utf[i];
  }
  esc[o] = 0;
  char json[MAX_PATHU * 2 + 80];
  snprintf(json, sizeof json, "{\"v\":\"%s\",\"file\":\"%s\",\"apply\":%d}", g_ver[0] ? g_ver : "", esc, apply ? 1 : 0);
  write_file_utf8(pend, json);
}

static int load_pending(int *apply) {
  wchar_t pend[MAX_PATH];
  upd_paths(NULL, pend, NULL);
  char *json = read_file_utf8(pend, NULL);
  if (!json) return 0;
  char ver[40], file[MAX_PATHU];
  int ap = 0;
  json_extract_string(json, "v", ver, (int)sizeof ver);
  json_extract_string(json, "file", file, (int)sizeof file);
  json_extract_int(json, "apply", &ap);
  free(json);
  if (!ver[0] || !file[0] || !newer(ver, MIREFIR_VERSION)) return 0;
  wchar_t wfile[MAX_PATH];
  if (!utf8_to_wide(file, wfile, MAX_PATH) || !file_ok(wfile)) return 0;
  snprintf(g_ver, sizeof g_ver, "%s", ver);
  wcsncpy(g_file, wfile, MAX_PATH - 1);
  g_file[MAX_PATH - 1] = 0;
  if (apply) *apply = ap;
  return 1;
}

static void set_lock(int on) {
  wchar_t lockp[MAX_PATH];
  upd_paths(NULL, NULL, lockp);
  if (on) write_file_utf8(lockp, "1");
  else DeleteFileW(lockp);
}

static int lock_on(void) {
  wchar_t lockp[MAX_PATH];
  upd_paths(NULL, NULL, lockp);
  return GetFileAttributesW(lockp) != INVALID_FILE_ATTRIBUTES;
}

static int fetch_body(const char *url, const wchar_t *extra_hdr, char **out, int *len, int max_bytes) {
  if (out) *out = NULL;
  if (len) *len = 0;
  wchar_t wurl[4096];
  if (!url || !out || !utf8_to_wide(url, wurl, 4096)) return 0;
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof uc);
  wchar_t host[256], path[3072], extra[1024];
  uc.dwStructSize = sizeof uc;
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 3072;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 1024;
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return 0;
  HINTERNET ses = WinHttpOpen(L"MirEfir-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return 0;
  DWORD pol = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &pol, sizeof pol);
  WinHttpSetTimeouts(ses, 20000, 20000, 20000, 120000);
  HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return 0;
  }
  wchar_t full[4096];
  if (extra[0]) swprintf(full, 4096, L"%s%s", path[0] ? path : L"/", extra);
  else swprintf(full, 4096, L"%s", path[0] ? path : L"/");
  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, L"GET", full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  int ok = 0;
  char *buf = NULL;
  int used = 0, cap = 0;
  if (req) {
    if (extra_hdr) WinHttpAddRequestHeaders(req, extra_hdr, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL)) {
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (!avail) {
          ok = 1;
          break;
        }
        if (used + (int)avail + 1 > cap) {
          int next = cap ? cap * 2 : 64 * 1024;
          while (next < used + (int)avail + 1) next *= 2;
          if (next > max_bytes) break;
          char *nb = (char *)realloc(buf, (size_t)next);
          if (!nb) break;
          buf = nb;
          cap = next;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf + used, avail, &got) || !got) break;
        used += (int)got;
      }
    }
    WinHttpCloseHandle(req);
  }
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (!ok || !buf) {
    free(buf);
    return 0;
  }
  buf[used] = 0;
  *out = buf;
  if (len) *len = used;
  return 1;
}

static int fetch_file(const char *url, const wchar_t *dest) {
  wchar_t wurl[4096];
  if (!url || !dest || !utf8_to_wide(url, wurl, 4096)) return 0;
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof uc);
  wchar_t host[256], path[3072], extra[1024];
  uc.dwStructSize = sizeof uc;
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 3072;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 1024;
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return 0;
  HINTERNET ses = WinHttpOpen(L"MirEfir-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return 0;
  DWORD pol = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &pol, sizeof pol);
  WinHttpSetTimeouts(ses, 20000, 20000, 60000, 120000);
  HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
  int ok = 0;
  if (con) {
    wchar_t full[4096];
    if (extra[0]) swprintf(full, 4096, L"%s%s", path[0] ? path : L"/", extra);
    else swprintf(full, 4096, L"%s", path[0] ? path : L"/");
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (req) {
      if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(req, NULL)) {
        HANDLE out = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (out != INVALID_HANDLE_VALUE) {
          ok = 1;
          for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) {
              ok = 0;
              break;
            }
            if (!avail) break;
            char chunk[16384];
            DWORD got = 0, put = 0;
            DWORD want = avail > sizeof chunk ? (DWORD)sizeof chunk : avail;
            if (!WinHttpReadData(req, chunk, want, &got) || !got) {
              ok = 0;
              break;
            }
            if (!WriteFile(out, chunk, got, &put, NULL) || put != got) {
              ok = 0;
              break;
            }
            if (InterlockedCompareExchange(&g_stop, 0, 0)) {
              ok = 0;
              break;
            }
          }
          CloseHandle(out);
        }
      }
      WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(con);
  }
  WinHttpCloseHandle(ses);
  if (!ok) DeleteFileW(dest);
  return ok && file_ok(dest);
}

typedef struct Pick {
  char url[MAX_URL];
  int setup;
} Pick;

static int name_has(const char *s, const char *part) {
  if (!s || !part) return 0;
  for (int i = 0; s[i]; i++) {
    if (istarts(s + i, part)) return 1;
  }
  return 0;
}

static void pick_asset(const char *item, void *u) {
  Pick *p = (Pick *)u;
  if (!item || *item != '{') return;
  char name[256], url[MAX_URL];
  if (!json_extract_string(item, "name", name, (int)sizeof name)) return;
  if (!json_extract_string(item, "browser_download_url", url, (int)sizeof url) || !url[0]) return;
  int n = (int)strlen(name);
  if (n < 4 || !istarts(name + n - 4, ".exe")) return;
  int setup = name_has(name, "setup");
  if (p->setup && !setup) return;
  snprintf(p->url, MAX_URL, "%s", url);
  p->setup = setup;
}

static void ready_ui(void) {
  InterlockedExchange(&g_chips, 1);
  if (g_notify) PostMessageW(g_notify, WM_MIREFIR_UPDATE, 0, 0);
}

static DWORD WINAPI check_th(LPVOID unused) {
  (void)unused;
  Sleep(4000);
  if (InterlockedCompareExchange(&g_stop, 0, 0)) return 0;
  int apply = 0;
  if (load_pending(&apply)) {
    if (!apply) ready_ui();
    return 0;
  }
  char *body = NULL;
  if (!fetch_body(kApi, L"Accept: application/vnd.github+json", &body, NULL, 2 * 1024 * 1024) || !body) return 0;
  char tag[40];
  json_extract_string(body, "tag_name", tag, (int)sizeof tag);
  if (!tag[0]) json_extract_string(body, "name", tag, (int)sizeof tag);
  if (tag[0] == 'v' || tag[0] == 'V') memmove(tag, tag + 1, strlen(tag));
  Pick pick;
  memset(&pick, 0, sizeof pick);
  const char *assets = json_value_of(body, "assets");
  if (assets) json_array_each(assets, pick_asset, &pick);
  free(body);
  if (!tag[0] || !newer(tag, MIREFIR_VERSION) || !pick.url[0]) return 0;
  if (InterlockedCompareExchange(&g_stop, 0, 0)) return 0;
  wchar_t dest[MAX_PATH];
  upd_paths(dest, NULL, NULL);
  DeleteFileW(dest);
  if (!fetch_file(pick.url, dest)) return 0;
  snprintf(g_ver, sizeof g_ver, "%s", tag);
  wcsncpy(g_file, dest, MAX_PATH - 1);
  g_file[MAX_PATH - 1] = 0;
  save_pending(0);
  wchar_t zone[MAX_PATH + 8];
  swprintf(zone, MAX_PATH + 8, L"%s:Zone.Identifier", dest);
  DeleteFileW(zone);
  ready_ui();
  return 0;
}

static int write_bat(const wchar_t *setup, const wchar_t *exe, const wchar_t *dir, const wchar_t *bat, DWORD pid) {
  wchar_t lockp[MAX_PATH], pend[MAX_PATH];
  upd_paths(NULL, pend, lockp);
  char setupu[MAX_PATHU], exeu[MAX_PATHU], diru[MAX_PATHU], locku[MAX_PATHU], pendu[MAX_PATHU];
  wide_to_utf8(setup, setupu, MAX_PATHU);
  wide_to_utf8(exe, exeu, MAX_PATHU);
  wide_to_utf8(dir, diru, MAX_PATHU);
  wide_to_utf8(lockp, locku, MAX_PATHU);
  wide_to_utf8(pend, pendu, MAX_PATHU);
  char lines[4096];
  snprintf(lines, sizeof lines,
           "@echo off\r\nsetlocal EnableExtensions\r\n"
           "ping 127.0.0.1 -n 2 >nul\r\n"
           "\"%%SystemRoot%%\\System32\\taskkill.exe\" /F /PID %u >nul 2>&1\r\n"
           "\"%%SystemRoot%%\\System32\\taskkill.exe\" /F /IM MirEfir.exe >nul 2>&1\r\n"
           "ping 127.0.0.1 -n 2 >nul\r\n"
           "start /wait \"\" \"%s\" /S /NCRC\r\n"
           "if errorlevel 1 goto keep\r\n"
           "del /f /q \"%s\" >nul 2>&1\r\n"
           "del /f /q \"%s\" >nul 2>&1\r\n"
           ":keep\r\n"
           "\"%%SystemRoot%%\\System32\\taskkill.exe\" /F /IM MirEfir.exe >nul 2>&1\r\n"
           "ping 127.0.0.1 -n 2 >nul\r\n"
           "start \"\" /D \"%s\" \"%s\" --updated\r\n"
           "ping 127.0.0.1 -n 3 >nul\r\n"
           "del /f /q \"%s\" >nul 2>&1\r\n"
           "del /f /q \"%%~f0\" >nul 2>&1\r\n",
           (unsigned)pid, setupu, setupu, pendu, diru, exeu, locku);
  return write_file_utf8(bat, lines);
}

static LRESULT CALLBACK splashProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_TIMER) {
    g_bar = (g_bar + 8) % 220;
    if (!lock_on()) {
      KillTimer(hwnd, 1);
      DestroyWindow(hwnd);
      return 0;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(22, 27, 36));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(236, 240, 248));
    HFONT f = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldf = SelectObject(hdc, f);
    RECT t = {16, 10, rc.right - 16, 36};
    DrawTextW(hdc, i18n("updateInstall"), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT bar = {16, 42, rc.right - 16, 54};
    HBRUSH idle = CreateSolidBrush(RGB(40, 48, 62));
    FillRect(hdc, &bar, idle);
    DeleteObject(idle);
    RECT fill = bar;
    fill.left = bar.left + g_bar;
    fill.right = fill.left + 70;
    if (fill.right > bar.right) fill.right = bar.right;
    HBRUSH hi = CreateSolidBrush(RGB(37, 99, 235));
    FillRect(hdc, &fill, hi);
    DeleteObject(hi);
    SelectObject(hdc, oldf);
    DeleteObject(f);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_DESTROY) PostQuitMessage(0);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static int spawn_installer(void) {
  if (!file_ok(g_file)) return 0;
  wchar_t exe[MAX_PATH], dir[MAX_PATH], tmp[MAX_PATH], bat[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wcsncpy(dir, exe, MAX_PATH);
  wchar_t *cut = wcsrchr(dir, L'\\');
  if (cut) *cut = 0;
  GetTempPathW(MAX_PATH, tmp);
  swprintf(bat, MAX_PATH, L"%smirefir-update.cmd", tmp);
  wchar_t zone[MAX_PATH + 8];
  swprintf(zone, MAX_PATH + 8, L"%s:Zone.Identifier", g_file);
  DeleteFileW(zone);
  set_lock(1);
  if (!write_bat(g_file, exe, dir, bat, GetCurrentProcessId())) return 0;
  wchar_t cmd[MAX_PATH + 48];
  swprintf(cmd, MAX_PATH + 48, L"\"%s\" --install-ui", exe);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNOACTIVATE;
  CreateProcessW(exe, cmd, NULL, NULL, FALSE,
                 CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS, NULL, dir, &si, &pi);
  if (pi.hThread) CloseHandle(pi.hThread);
  if (pi.hProcess) CloseHandle(pi.hProcess);
  wchar_t run[MAX_PATH + 48];
  swprintf(run, MAX_PATH + 48, L"cmd.exe /C \"%s\"", bat);
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  CreateProcessW(NULL, run, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS, NULL,
                 tmp, &si, &pi);
  if (pi.hThread) CloseHandle(pi.hThread);
  if (pi.hProcess) CloseHandle(pi.hProcess);
  return 1;
}

int update_ready(void) { return InterlockedCompareExchange(&g_chips, 0, 0) != 0; }

int update_has_file(void) {
  if (g_file[0] && file_ok(g_file) && g_ver[0] && newer(g_ver, MIREFIR_VERSION)) return 1;
  int apply = 0;
  return load_pending(&apply);
}

void update_later(void) {
  InterlockedExchange(&g_chips, 0);
  if (g_file[0]) save_pending(1);
  if (g_notify) PostMessageW(g_notify, WM_MIREFIR_UPDATE, 0, 0);
}

int update_apply(HWND host) {
  int apply = 0;
  if (!g_file[0]) load_pending(&apply);
  if (!file_ok(g_file)) return 0;
  InterlockedExchange(&g_chips, 0);
  if (!spawn_installer()) {
    set_lock(0);
    return 0;
  }
  if (host) PostMessageW(host, WM_MIREFIR_INSTALL, 0, 0);
  else ExitProcess(0);
  return 1;
}

void update_start(HWND host) {
  g_notify = host;
  int apply = 0;
  if (load_pending(&apply) && !apply) ready_ui();
  if (g_th) return;
  InterlockedExchange(&g_stop, 0);
  g_th = CreateThread(NULL, 0, check_th, NULL, 0, NULL);
}

void update_stop(void) {
  g_notify = NULL;
  InterlockedExchange(&g_stop, 1);
  if (g_th) {
    WaitForSingleObject(g_th, 400);
    CloseHandle(g_th);
    g_th = NULL;
  }
}

int update_should_apply(void) {
  int apply = 0;
  return load_pending(&apply) && apply;
}

int update_splash(HINSTANCE inst) {
  i18n_set(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ENGLISH ? "en" : "ru");
  WNDCLASSW wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = splashProc;
  wc.hInstance = inst;
  wc.hbrBackground = CreateSolidBrush(RGB(22, 27, 36));
  wc.lpszClassName = L"MirEfirInstall";
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassW(&wc);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int w = 360, h = 68;
  int x = wa.left + 16;
  int y = wa.bottom - h - 16;
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"MirEfirInstall", L"MirEfir",
                              WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL, inst, NULL);
  if (!hwnd) return 1;
  SetTimer(hwnd, 1, 80, NULL);
  MSG msg;
  DWORD start = GetTickCount();
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    if (GetTickCount() - start > 20 * 60 * 1000) break;
  }
  return 0;
}

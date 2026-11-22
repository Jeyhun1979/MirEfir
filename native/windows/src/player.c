#include "app.h"
#include <shlobj.h>
#include <shellapi.h>
#include <stdlib.h>

static const wchar_t kPipe[] = L"\\\\.\\pipe\\mirefir-native-mpv";

static HANDLE g_proc;
static HANDLE g_pipe;
static HANDLE g_thread;
static HANDLE g_qevent;
static CRITICAL_SECTION g_qcs;
static int g_qready;
static HWND g_host;
static HWND g_pane;
static HWND g_child;
static WNDPROC g_old_child;
static volatile LONG g_run;
static volatile LONG g_evt_start;
static volatile LONG g_evt_error;
static volatile LONG g_idle;
static volatile LONG g_eof;

typedef struct Cmd {
  char *json;
  struct Cmd *next;
} Cmd;
static Cmd *g_qhead;
static Cmd *g_qtail;
static int g_index = -1;
static int g_prev = -1;
static int g_url_i;
static int g_url_n;
static char g_urls[12][MAX_URL];
static char g_ua[MAX_UA];
static char g_ref[MAX_REF];
static int g_loading;
static int g_live;
static int g_archive;
static int g_load_ms;
static int g_stall_ms;
static int g_volume = 35;
static int g_muted;
static int g_paused;
static long long g_origin_s;
static long long g_origin_e;
static long long g_play_s;
static DWORD g_play_tick;
static volatile LONG g_file_ms;
static LONG g_pause_file = -1;
static wchar_t g_mpv[MAX_PATH];
static char g_aspect[16] = "auto";
static int g_panscan = -1;
static int g_recording;
static int g_playing_file;
static int g_multiview;
static int g_prefs_on;
static int g_alt_tried;
static int g_extra_busy;

typedef struct ExtraVid {
  HWND hwnd;
  HANDLE proc;
  HANDLE pipe;
  HANDLE thread;
  wchar_t pipename[64];
  volatile LONG run;
} ExtraVid;
static ExtraVid g_pip;
static ExtraVid g_tile[4];
static int g_use_slot;
static RECT g_vid_slot;
static int g_vo_d3d = 1;
static volatile LONG g_drops;
static volatile LONG g_is_hevc;
static volatile LONG g_hw_off;
static int g_drop_acted;
static DWORD g_live_tick;
static LONG g_live_pos0;

static void json_escape(const char *in, char *out, int max) {
  int o = 0;
  if (!in) in = "";
  for (int i = 0; in[i] && o < max - 2; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == '"' || c == '\\') {
      if (o >= max - 3) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c < 32) {
      continue;
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
}

static void q_init(void) {
  if (g_qready) return;
  InitializeCriticalSection(&g_qcs);
  g_qevent = CreateEventW(NULL, FALSE, FALSE, NULL);
  g_qready = 1;
}

static int send_raw(const char *json) {
  if (!json || !json[0]) return 0;
  size_t n = strlen(json);
  Cmd *item = (Cmd *)malloc(sizeof(Cmd));
  if (!item) return 0;
  item->json = (char *)malloc(n + 2);
  if (!item->json) {
    free(item);
    return 0;
  }
  memcpy(item->json, json, n);
  item->json[n] = '\n';
  item->json[n + 1] = 0;
  item->next = NULL;
  q_init();
  EnterCriticalSection(&g_qcs);
  if (g_qtail) g_qtail->next = item;
  else g_qhead = item;
  g_qtail = item;
  LeaveCriticalSection(&g_qcs);
  if (g_qevent) SetEvent(g_qevent);
  return 1;
}

static int send_cmd(const char *cmd) { return send_raw(cmd); }

static void flush_cmds(void) {
  for (;;) {
    Cmd *item = NULL;
    EnterCriticalSection(&g_qcs);
    item = g_qhead;
    if (item) {
      g_qhead = item->next;
      if (!g_qhead) g_qtail = NULL;
    }
    LeaveCriticalSection(&g_qcs);
    if (!item) break;
    if (g_pipe && g_pipe != INVALID_HANDLE_VALUE) {
      const char *p = item->json;
      DWORD left = (DWORD)strlen(p);
      while (left) {
        DWORD w = 0;
        if (!WriteFile(g_pipe, p, left, &w, NULL) || !w) break;
        p += w;
        left -= w;
      }
    }
    free(item->json);
    free(item);
  }
}

static void q_clear(void) {
  Cmd *item = NULL;
  if (!g_qready) return;
  EnterCriticalSection(&g_qcs);
  item = g_qhead;
  g_qhead = g_qtail = NULL;
  LeaveCriticalSection(&g_qcs);
  while (item) {
    Cmd *n = item->next;
    free(item->json);
    free(item);
    item = n;
  }
}

void player_show_text(const char *utf8, int ms) {
  char esc[512], json[640];
  json_escape(utf8 ? utf8 : "", esc, sizeof esc);
  snprintf(json, sizeof json, "{\"command\":[\"show-text\",\"%s\",%d]}", esc, ms > 0 ? ms : 2000);
  send_cmd(json);
}

static const char *hwdec_for(const char *decoder) {
  if (decoder && strcmp(decoder, "software") == 0) return "no";
  if (decoder && strcmp(decoder, "hardware") == 0) return "d3d11va,dxva2";
  return "d3d11va,dxva2,no";
}

static void apply_hwdec(const char *decoder) {
  char json[256];
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"hwdec\",\"%s\"]}", hwdec_for(decoder));
  send_cmd(json);
}

static void set_panscan(int on) {
  if (on) on = 1;
  if (on == g_panscan) return;
  g_panscan = on;
  send_cmd(on ? "{\"command\":[\"set_property\",\"panscan\",1]}"
              : "{\"command\":[\"set_property\",\"panscan\",0]}");
}

static void fit_slot(HWND hwnd) {
  if (!hwnd) hwnd = g_host;
  if (!hwnd) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (h < 1) h = 1;
  int wide = w * 9 > h * 16 + h / 20;
  if (strcmp(g_aspect, "zoom") == 0) set_panscan(1);
  else if (strcmp(g_aspect, "16:9") == 0 || strcmp(g_aspect, "4:3") == 0) set_panscan(0);
  else set_panscan(wide ? 1 : 0);
}

static void apply_aspect(const char *aspect) {
  snprintf(g_aspect, sizeof g_aspect, "%s", aspect && aspect[0] ? aspect : "auto");
  if (strcmp(g_aspect, "16:9") == 0) {
    send_cmd("{\"command\":[\"set_property\",\"video-aspect-override\",\"16:9\"]}");
  } else if (strcmp(g_aspect, "4:3") == 0) {
    send_cmd("{\"command\":[\"set_property\",\"video-aspect-override\",\"4:3\"]}");
  } else {
    send_cmd("{\"command\":[\"set_property\",\"video-aspect-override\",-1]}");
  }
  fit_slot(g_host);
}

static void apply_buffer(int sec) {
  if (sec < 3) sec = 3;
  if (sec > 90) sec = 90;
  char json[256];
  /* cushion is for stalls, not before the first frame */
  send_cmd("{\"command\":[\"set_property\",\"cache-secs\",3]}");
  int ahead = sec >= 30 ? 2 : 1;
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"demuxer-readahead-secs\",%d]}", ahead);
  send_cmd(json);
}

void player_set_volume(int vol, int muted) {
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  g_volume = vol;
  g_muted = muted ? 1 : 0;
  char json[160];
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"volume\",%d]}", muted ? 0 : vol);
  send_cmd(json);
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"mute\",%s]}", muted ? "true" : "false");
  send_cmd(json);
}

void player_cycle_audio(void) { send_cmd("{\"command\":[\"cycle\",\"aid\"]}"); }
void player_cycle_sub(void) { send_cmd("{\"command\":[\"cycle\",\"sid\"]}"); }

void player_apply_settings(const Settings *st) {
  if (!st) return;
  apply_hwdec(st->decoder);
  apply_buffer(st->buffer_sec);
  apply_aspect(st->aspect);
  player_set_volume(st->volume, st->muted);
  if (st->audio[0]) {
    char json[160];
    snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"alang\",\"%s\"]}", st->audio);
    send_cmd(json);
  }
}

static void handle_line(const char *line) {
  if (!line || !line[0]) return;
  if (strstr(line, "\"playback-restart\"") || strstr(line, "\"file-loaded\"")) {
    InterlockedExchange(&g_evt_start, 1);
    InterlockedExchange(&g_idle, 0);
    InterlockedExchange(&g_eof, 0);
  }
  if (strstr(line, "\"end-file\"") && strstr(line, "\"error\"")) InterlockedExchange(&g_evt_error, 1);
  if (strstr(line, "eof-reached")) {
    if (strstr(line, "true")) InterlockedExchange(&g_eof, 1);
    else if (strstr(line, "false")) InterlockedExchange(&g_eof, 0);
  }
  if (strstr(line, "idle-active")) {
    if (strstr(line, "true")) InterlockedExchange(&g_idle, 1);
    else if (strstr(line, "false")) InterlockedExchange(&g_idle, 0);
  }
  if (strstr(line, "\"pause\"")) {
    if (strstr(line, "true")) g_paused = 1;
    else if (strstr(line, "false")) g_paused = 0;
  }
  if (strstr(line, "time-pos")) {
    const char *d = strstr(line, "\"data\":");
    if (d) {
      double v = atof(d + 7);
      if (v >= 0 && v < 86400.0 * 14.0) InterlockedExchange(&g_file_ms, (LONG)(v * 1000.0));
    }
  }
  if (strstr(line, "frame-drop-count")) {
    const char *d = strstr(line, "\"data\":");
    if (d) {
      int v = atoi(d + 7);
      if (v > 0) InterlockedExchange(&g_drops, v);
    }
  }
  if (strstr(line, "video-format")) {
    if (strstr(line, "hevc") || strstr(line, "h265")) InterlockedExchange(&g_is_hevc, 1);
    else if (strstr(line, "\"data\"")) InterlockedExchange(&g_is_hevc, 0);
  }
  if (strstr(line, "hwdec-current")) {
    if (strstr(line, "\"data\":\"no\"")) InterlockedExchange(&g_hw_off, 1);
    else if (strstr(line, "\"data\"")) InterlockedExchange(&g_hw_off, 0);
  }
}

static DWORD WINAPI io_thread(void *arg) {
  (void)arg;
  char chunk[4096];
  char *line = (char *)malloc(256 * 1024);
  int used = 0;
  if (!line) return 0;
  while (InterlockedCompareExchange(&g_run, 1, 1) == 1) {
    if (g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) break;
    if (!g_pipe) break;
    flush_cmds();
    DWORD avail = 0;
    if (g_pipe && PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL) && avail) {
      DWORD n = 0;
      DWORD want = avail > sizeof chunk ? (DWORD)sizeof chunk : avail;
      if (ReadFile(g_pipe, chunk, want, &n, NULL) && n) {
        for (DWORD i = 0; i < n; i++) {
          if (chunk[i] == '\n') {
            line[used] = 0;
            handle_line(line);
            used = 0;
          } else if (used < 256 * 1024 - 1) {
            line[used++] = chunk[i];
          }
        }
      }
    } else {
      HANDLE wait[2];
      DWORD nwait = 0;
      if (g_qevent) wait[nwait++] = g_qevent;
      if (g_proc) wait[nwait++] = g_proc;
      DWORD wr = nwait ? WaitForMultipleObjects(nwait, wait, FALSE, 40) : WAIT_TIMEOUT;
      if (g_proc && (wr == WAIT_OBJECT_0 + (g_qevent ? 1 : 0) || WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0)) break;
    }
  }
  free(line);
  return 0;
}

static int find_mpv_in(const wchar_t *dir, wchar_t *out, int max) {
  wchar_t stack[24][MAX_PATH];
  int sp = 0;
  wcsncpy(stack[sp++], dir, MAX_PATH - 1);
  while (sp > 0) {
    wchar_t here[MAX_PATH];
    wcsncpy(here, stack[--sp], MAX_PATH);
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*", here);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (fd.cFileName[0] == '.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0))) continue;
      wchar_t full[MAX_PATH];
      swprintf(full, MAX_PATH, L"%s\\%s", here, fd.cFileName);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (sp < 24) wcsncpy(stack[sp++], full, MAX_PATH - 1);
      } else if (_wcsicmp(fd.cFileName, L"mpv.exe") == 0) {
        wcsncpy(out, full, max - 1);
        out[max - 1] = 0;
        FindClose(h);
        return 1;
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  return 0;
}

static int locate_mpv(void) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t *slash = wcsrchr(exe, L'\\');
  if (slash) *slash = 0;
  wchar_t cand[MAX_PATH];
  swprintf(cand, MAX_PATH, L"%s\\mpv", exe);
  if (find_mpv_in(cand, g_mpv, MAX_PATH)) return 1;
  slash = wcsrchr(exe, L'\\');
  if (slash) {
    *slash = 0;
    swprintf(cand, MAX_PATH, L"%s\\vendor\\mpv", exe);
    if (find_mpv_in(cand, g_mpv, MAX_PATH)) return 1;
  }
  wchar_t appdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata))) {
    swprintf(cand, MAX_PATH, L"%s\\MirEfir\\mpv-bin", appdata);
    if (find_mpv_in(cand, g_mpv, MAX_PATH)) return 1;
  }
  return 0;
}

static void unhook_child(void) {
  if (g_child && g_old_child) {
    SetWindowLongPtrW(g_child, GWLP_WNDPROC, (LONG_PTR)g_old_child);
  }
  g_child = NULL;
  g_old_child = NULL;
}

static void close_engine(void) {
  InterlockedExchange(&g_run, 0);
  if (g_qevent) SetEvent(g_qevent);
  unhook_child();
  if (g_thread) {
    if (WaitForSingleObject(g_thread, 300) != WAIT_OBJECT_0 && g_pipe) {
      CancelIoEx(g_pipe, NULL);
      WaitForSingleObject(g_thread, 500);
    }
    CloseHandle(g_thread);
    g_thread = NULL;
  }
  q_clear();
  if (g_pipe && g_pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(g_pipe);
    g_pipe = NULL;
  }
  if (g_proc) {
    if (WaitForSingleObject(g_proc, 400) != WAIT_OBJECT_0) TerminateProcess(g_proc, 0);
    CloseHandle(g_proc);
    g_proc = NULL;
  }
}

static void pump_ui(void) {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage((int)msg.wParam);
      return;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

static int spawn_variant(HWND hwnd, const wchar_t *extra1, const wchar_t *extra2) {
  close_engine();
  wchar_t cwd[MAX_PATH];
  wcsncpy(cwd, g_mpv, MAX_PATH);
  wchar_t *cut = wcsrchr(cwd, L'\\');
  if (cut) *cut = 0;
  SetDllDirectoryW(cwd);
  g_vo_d3d = extra1 && wcsstr(extra1, L"direct3d") != NULL;

  wchar_t cmd[4096];
  swprintf(cmd, 4096,
           L"\"%s\" --no-config --idle=yes --input-ipc-server=%s --wid=%llu "
           L"--force-window=yes --ontop=no --no-border --no-osc --osd-level=0 "
           L"--no-input-default-bindings --input-vo-keyboard=no --window-dragging=no "
           L"--keepaspect=yes --keepaspect-window=no --panscan=0 --video-unscaled=no "
           L"--cursor-autohide=always --stop-screensaver=yes --keep-open=always --ytdl=no "
           L"--loop=no --loop-file=no --loop-playlist=no --hls-bitrate=max "
           L"--hwdec=d3d11va,dxva2,no --hwdec-codecs=h264,hevc,mpeg2video,vc1,mjpeg "
           L"--video-sync=audio --scale=bilinear --cscale=bilinear --dither=no --deband=no "
           L"--framedrop=vo --cache=yes --cache-pause=no --cache-secs=3 "
           L"--demuxer-readahead-secs=1.2 --demuxer-max-bytes=8388608 --demuxer-max-back-bytes=2097152 "
           L"--demuxer-lavf-o=fflags=+nobuffer+fastseek,analyzeduration=500000,probesize=512000 "
           L"--vd-lavc-threads=2 --msg-level=all=error --title=MirEfir %s %s",
           g_mpv, kPipe, (unsigned long long)(uintptr_t)hwnd, extra1 ? extra1 : L"", extra2 ? extra2 : L"");

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(g_mpv, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) return 0;
  CloseHandle(pi.hThread);
  g_proc = pi.hProcess;

  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int i = 0; i < 100; i++) {
    if (WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
      CloseHandle(g_proc);
      g_proc = NULL;
      return 0;
    }
    pipe = CreateFileW(kPipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe != INVALID_HANDLE_VALUE) break;
    pump_ui();
    Sleep(20);
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    TerminateProcess(g_proc, 0);
    CloseHandle(g_proc);
    g_proc = NULL;
    return 0;
  }
  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
  g_pipe = pipe;
  q_init();
  InterlockedExchange(&g_run, 1);
  g_thread = CreateThread(NULL, 0, io_thread, NULL, 0, NULL);
  send_cmd("{\"command\":[\"observe_property\",1,\"idle-active\"]}");
  send_cmd("{\"command\":[\"observe_property\",2,\"eof-reached\"]}");
  send_cmd("{\"command\":[\"observe_property\",3,\"time-pos\"]}");
  send_cmd("{\"command\":[\"observe_property\",4,\"frame-drop-count\"]}");
  send_cmd("{\"command\":[\"observe_property\",5,\"decoder-frame-drop-count\"]}");
  send_cmd("{\"command\":[\"observe_property\",6,\"hwdec-current\"]}");
  send_cmd("{\"command\":[\"observe_property\",7,\"video-format\"]}");
  player_resize(hwnd);
  return 1;
}

int player_start(HWND hwnd) {
  g_host = hwnd;
  device_probe();
  if (!locate_mpv()) return 0;
  if (spawn_variant(hwnd, L"--vo=gpu", L"--gpu-api=d3d11")) return 1;
  if (spawn_variant(hwnd, L"--vo=direct3d", L"")) return 1;
  return 0;
}

void player_stop(void) {
  rec_live_stop();
  rec_timer_cancel();
  player_pip_stop();
  player_tile_stop_all();
  close_engine();
  if (g_pane) {
    DestroyWindow(g_pane);
    g_pane = NULL;
  }
}

static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_MOUSEACTIVATE && g_host) {
    SetFocus(g_host);
    return MA_NOACTIVATE;
  }
  if (msg == WM_SETFOCUS && g_host && hwnd != g_host) {
    SetFocus(g_host);
    return 0;
  }
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR ||
       msg == WM_APPCOMMAND) &&
      g_host) {
    PostMessageW(g_host, msg, wp, lp);
    return 0;
  }
  if (msg == WM_LBUTTONDOWN && g_host) {
    POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
    ClientToScreen(hwnd, &pt);
    ScreenToClient(g_host, &pt);
    PostMessageW(g_host, WM_LBUTTONDOWN, wp, MAKELPARAM(pt.x, pt.y));
    SetFocus(g_host);
    return 0;
  }
  if (msg == WM_RBUTTONDOWN && g_host) {
    PostMessageW(g_host, WM_RBUTTONDOWN, wp, lp);
    SetFocus(g_host);
    return 0;
  }
  if (g_old_child) return CallWindowProcW(g_old_child, hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL CALLBACK move_child(HWND child, LPARAM lp) {
  if (GetPropW(child, L"mirefir-ui") || GetPropW(child, L"mirefir-tile") || GetPropW(child, L"mirefir-video"))
    return TRUE;
  HWND host = (HWND)lp;
  if (GetParent(child) != host) return TRUE;
  if (g_child && !IsWindow(g_child)) unhook_child();
  if (child != host && (!g_old_child || g_child != child)) {
    if (g_old_child && g_child && g_child != child) unhook_child();
    g_child = child;
    g_old_child = (WNDPROC)SetWindowLongPtrW(child, GWLP_WNDPROC, (LONG_PTR)child_proc);
  }
  RECT rc;
  if (g_use_slot && host == g_host) rc = g_vid_slot;
  else GetClientRect(host, &rc);
  RECT cur;
  GetClientRect(child, &cur);
  POINT pt = {0, 0};
  ClientToScreen(child, &pt);
  POINT hp = {rc.left, rc.top};
  ClientToScreen(host, &hp);
  int cw = cur.right - cur.left, ch = cur.bottom - cur.top;
  int nw = rc.right - rc.left, nh = rc.bottom - rc.top;
  if (pt.x == hp.x && pt.y == hp.y && cw == nw && ch == nh) return TRUE;
  SetWindowPos(child, HWND_BOTTOM, rc.left, rc.top, nw, nh, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
  return TRUE;
}

void player_set_slot(int x, int y, int w, int h) {
  RECT nrc;
  if (w < 16 || h < 16) {
    if (!g_use_slot) return;
    g_use_slot = 0;
  } else {
    nrc.left = x;
    nrc.top = y;
    nrc.right = x + w;
    nrc.bottom = y + h;
    if (g_use_slot && EqualRect(&g_vid_slot, &nrc)) return;
    g_use_slot = 1;
    g_vid_slot = nrc;
  }
  player_resize(g_host);
}

void player_clear_slot(void) {
  g_use_slot = 0;
  player_resize(g_host);
}

void player_resize(HWND hwnd) {
  static int busy;
  if (busy) return;
  busy = 1;
  if (!hwnd) hwnd = g_host;
  HWND box = (g_pane && IsWindow(g_pane)) ? g_pane : hwnd;
  if (g_pane && g_host && IsWindow(g_pane)) {
    RECT rc;
    if (g_use_slot) rc = g_vid_slot;
    else GetClientRect(g_host, &rc);
    int nw = rc.right - rc.left, nh = rc.bottom - rc.top;
    if (nw < 16) nw = 16;
    if (nh < 16) nh = 16;
    RECT wr;
    GetWindowRect(g_pane, &wr);
    POINT want = {rc.left, rc.top};
    ClientToScreen(g_host, &want);
    if (wr.left != want.x || wr.top != want.y || wr.right - wr.left != nw || wr.bottom - wr.top != nh)
      SetWindowPos(g_pane, HWND_BOTTOM, rc.left, rc.top, nw, nh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  if (box) EnumChildWindows(box, move_child, (LPARAM)box);
  fit_slot(g_host ? g_host : hwnd);
  busy = 0;
}

static int pid_is_ours(DWORD pid) {
  if (!pid) return 0;
  if (pid == GetCurrentProcessId()) return 1;
  if (g_proc && GetProcessId(g_proc) == pid) return 1;
  if (g_pip.proc && GetProcessId(g_pip.proc) == pid) return 1;
  for (int i = 0; i < 4; i++) {
    if (g_tile[i].proc && GetProcessId(g_tile[i].proc) == pid) return 1;
  }
  return 0;
}

void player_keep_focus(HWND hwnd) {
  if (!hwnd || IsIconic(hwnd)) return;
  if (g_child && !IsWindow(g_child)) unhook_child();
  if (!g_old_child) player_resize(hwnd);
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (fg && fg != hwnd && GetWindow(fg, GW_OWNER) != hwnd && !pid_is_ours(pid)) return;
  if (fg && fg != hwnd) {
    DWORD fgt = GetWindowThreadProcessId(fg, NULL);
    DWORD cur = GetCurrentThreadId();
    if (fgt && fgt != cur) AttachThreadInput(fgt, cur, TRUE);
    SetForegroundWindow(hwnd);
    if (fgt && fgt != cur) AttachThreadInput(fgt, cur, FALSE);
  }
  if (GetFocus() != hwnd) SetFocus(hwnd);
}

static int load_url(const Playlist *pl, Settings *st, const char *url) {
  if (!url || !url[0]) return 0;
  char esc_url[MAX_URL * 2], json[MAX_URL + 512];
  char esc_ua[MAX_UA * 2];
  json_escape(g_ua[0] ? g_ua : st->user_agent, esc_ua, sizeof esc_ua);
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"user-agent\",\"%s\"]}", esc_ua);
  send_cmd(json);
  if (g_ref[0]) {
    char esc[MAX_REF * 2];
    json_escape(g_ref, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"referrer\",\"%s\"]}", esc);
    send_cmd(json);
  }
  if (pl && g_index >= 0 && g_index < pl->count) {
    char esc[MAX_NAME * 2];
    json_escape(pl->items[g_index].name, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"force-media-title\",\"%s\"]}", esc);
    send_cmd(json);
  }
  send_cmd("{\"command\":[\"set_property\",\"pause\",false]}");
  send_cmd("{\"command\":[\"set_property\",\"loop-file\",\"no\"]}");
  send_cmd("{\"command\":[\"set_property\",\"loop\",\"no\"]}");
  json_escape(url, esc_url, sizeof esc_url);
  snprintf(json, sizeof json, "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", esc_url);
  send_cmd(json);
  InterlockedExchange(&g_evt_start, 0);
  InterlockedExchange(&g_evt_error, 0);
  InterlockedExchange(&g_idle, 1);
  InterlockedExchange(&g_eof, 0);
  InterlockedExchange(&g_drops, 0);
  InterlockedExchange(&g_is_hevc, 0);
  InterlockedExchange(&g_hw_off, 0);
  g_drop_acted = 0;
  InterlockedExchange(&g_file_ms, 0);
  g_play_tick = GetTickCount();
  g_pause_file = -1;
  g_loading = 1;
  g_live = 0;
  g_load_ms = 0;
  g_stall_ms = 0;
  g_paused = 0;
  return 1;
}

int player_play_at(const Playlist *pl, Settings *st, int index, int start_q) {
  if (!pl || index < 0 || index >= pl->count) return 0;
  rec_on_zap();
  g_playing_file = 0;
  int same = g_index == index;
  if (g_index >= 0 && g_index != index) g_prev = g_index;
  g_index = index;
  g_archive = 0;
  g_origin_s = g_origin_e = g_play_s = 0;
  const Channel *ch = &pl->items[index];
  snprintf(g_ua, MAX_UA, "%s", ch->ua);
  snprintf(g_ref, MAX_REF, "%s", ch->referer);
  if (!g_prefs_on) {
    player_apply_settings(st);
    g_prefs_on = 1;
  }
  if (start_q < 1) start_q = 1;
  if (start_q > 4) start_q = 4;
  build_play_urls(pl, index, start_q, g_urls, &g_url_n, 12);
  g_url_i = 0;
  g_alt_tried = 0;
  st->last_index = index;
  snprintf(st->last_name, MAX_NAME, "%s", ch->name);
  profile_save_session(st, index, ch->name);
  if (st->external_player) {
    send_cmd("{\"command\":[\"stop\"]}");
    int ok = g_url_n ? player_open_external(g_urls[g_url_i]) : 0;
    player_show_text(ok ? ch->name : i18n_u("extFail"), 1600);
    return ok;
  }
  int ok = g_url_n ? load_url(pl, st, g_urls[g_url_i]) : 0;
  if (!same) player_show_text(ch->name, 1600);
  return ok;
}

int player_play_channel(const Playlist *pl, Settings *st, int index) {
  return player_play_at(pl, st, index, 4);
}

int player_open_external(const char *url) {
  if (!url || !url[0]) return 0;
  wchar_t wurl[MAX_URL];
  if (!utf8_to_wide(url, wurl, MAX_URL)) return 0;
  if (!g_mpv[0]) locate_mpv();
  if (g_mpv[0]) {
    wchar_t cmd[MAX_PATH + MAX_URL + 96];
    swprintf(cmd, MAX_PATH + MAX_URL + 96, L"\"%s\" --force-window=yes --keepaspect=yes -- \"%s\"", g_mpv, wurl);
    wchar_t cwd[MAX_PATH];
    wcsncpy(cwd, g_mpv, MAX_PATH);
    cwd[MAX_PATH - 1] = 0;
    wchar_t *cut = wcsrchr(cwd, L'\\');
    if (cut) *cut = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    if (CreateProcessW(g_mpv, cmd, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, cwd, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return 1;
    }
  }
  return (INT_PTR)ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL) > 32;
}

int player_play_archive(const Playlist *pl, Settings *st, int index, char urls[][MAX_URL], int n,
                        long long play_start, long long origin_start, long long origin_end) {
  if (!pl || !st || index < 0 || index >= pl->count || !urls || n < 1) return 0;
  if (g_index >= 0 && g_index != index) g_prev = g_index;
  int already = g_archive;
  g_index = index;
  g_archive = 1;
  if (origin_start <= 0) origin_start = play_start;
  if (origin_end <= origin_start) origin_end = play_start + 60LL * 60 * 1000;
  g_origin_s = origin_start;
  g_origin_e = origin_end;
  g_play_s = play_start > 0 ? play_start : origin_start;
  const Channel *ch = &pl->items[index];
  snprintf(g_ua, MAX_UA, "%s", ch->ua);
  snprintf(g_ref, MAX_REF, "%s", ch->referer);
  g_url_n = 0;
  for (int i = 0; i < n && g_url_n < 12; i++) {
    if (urls[i][0]) snprintf(g_urls[g_url_n++], MAX_URL, "%s", urls[i]);
  }
  if (!g_url_n) return 0;
  g_url_i = 0;
  int ok = load_url(pl, st, g_urls[0]);
  if (!already) player_show_text("Архив", 1600);
  return ok;
}

int player_is_archive(void) { return g_archive; }

int player_archive_bounds(long long *origin_start, long long *origin_end, long long *pos_ms) {
  if (!g_archive || g_origin_e <= g_origin_s) return 0;
  if (origin_start) *origin_start = g_origin_s;
  if (origin_end) *origin_end = g_origin_e;
  if (pos_ms) {
    LONG file = InterlockedCompareExchange(&g_file_ms, 0, 0);
    if (g_paused && g_pause_file >= 0) file = g_pause_file;
    else if (file < 150) file = (LONG)(GetTickCount() - g_play_tick);
    long long pos = g_play_s + file;
    if (pos < g_origin_s) pos = g_origin_s;
    if (pos > g_origin_e) pos = g_origin_e;
    *pos_ms = pos;
  }
  return 1;
}

static void try_next_ex(const Playlist *pl, Settings *st, int want_lower) {
  (void)want_lower;
  if (g_playing_file) {
    g_loading = 0;
    return;
  }
  if (g_archive) {
    g_loading = 0;
    player_show_text("Архив недоступен", 2000);
    return;
  }
  if (!g_url_n) return;
  /* network drop is not a weak GPU: one silent alt of this same entry, then the selected URL again */
  if (!g_alt_tried && g_url_i == 0 && g_url_n > 1) {
    g_alt_tried = 1;
    g_url_i = 1;
    load_url(pl, st, g_urls[1]);
    return;
  }
  g_url_i = 0;
  load_url(pl, st, g_urls[0]);
}

static void try_next(const Playlist *pl, Settings *st) { try_next_ex(pl, st, 0); }

void player_tick(const Playlist *pl, Settings *st) {
  if (!g_pipe) return;
  if (InterlockedExchange(&g_evt_start, 0)) {
    g_loading = 0;
    g_live = g_archive ? 0 : 1;
    g_stall_ms = 0;
    g_load_ms = 0;
    g_live_tick = GetTickCount();
    g_live_pos0 = InterlockedCompareExchange(&g_file_ms, 0, 0);
  }
  if (InterlockedExchange(&g_evt_error, 0)) {
    if (g_multiview && g_url_n > 0) load_url(pl, st, g_urls[g_url_i < g_url_n ? g_url_i : 0]);
    else try_next(pl, st);
    return;
  }
  if (g_loading) {
    g_load_ms += 400;
    if (g_load_ms >= 12000) {
      if (g_multiview && g_url_n > 0) load_url(pl, st, g_urls[g_url_i < g_url_n ? g_url_i : 0]);
      else try_next(pl, st);
    }
    return;
  }
  if (g_live && !g_playing_file && !g_paused) {
    if (InterlockedCompareExchange(&g_eof, 0, 0)) {
      g_stall_ms = 0;
    } else if (InterlockedCompareExchange(&g_idle, 0, 0)) {
      g_stall_ms += 400;
    } else {
      g_stall_ms = 0;
    }
    if (g_stall_ms >= 8000) {
      g_stall_ms = 0;
      if (g_url_n > 0) load_url(pl, st, g_urls[g_url_i]);
    }
  }
}

int player_current_index(void) { return g_index; }
int player_prev_index(void) { return g_prev; }
int player_alive(void) { return g_proc && WaitForSingleObject(g_proc, 0) != WAIT_OBJECT_0; }

void player_toggle_pause(void) {
  if (!g_paused) {
    LONG file = InterlockedCompareExchange(&g_file_ms, 0, 0);
    if (file < 150) file = (LONG)(GetTickCount() - g_play_tick);
    g_pause_file = file;
    g_paused = 1;
  } else {
    if (g_pause_file >= 0) g_play_tick = GetTickCount() - (DWORD)g_pause_file;
    g_pause_file = -1;
    g_paused = 0;
  }
  send_cmd(g_paused ? "{\"command\":[\"set_property\",\"pause\",true]}"
                    : "{\"command\":[\"set_property\",\"pause\",false]}");
}

int player_paused(void) { return g_paused; }

void player_seek(int seconds) {
  char json[128];
  snprintf(json, sizeof json, "{\"command\":[\"seek\",%d,\"relative\"]}", seconds);
  send_cmd(json);
}

const wchar_t *player_mpv_path(void) {
  if (!g_mpv[0]) locate_mpv();
  return g_mpv;
}

int player_recording(void) { return g_recording; }

int player_record_start(const char *path) {
  if (!path || !path[0] || !g_pipe) return 0;
  char esc[MAX_PATHU * 2], json[MAX_PATHU * 2 + 96];
  json_escape(path, esc, sizeof esc);
  snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"stream-record\",\"%s\"]}", esc);
  if (!send_cmd(json)) return 0;
  g_recording = 1;
  return 1;
}

void player_record_stop(void) {
  if (!g_recording) return;
  send_cmd("{\"command\":[\"set_property\",\"stream-record\",\"\"]}");
  g_recording = 0;
}

int player_play_file(const char *path) {
  if (!path || !path[0]) return 0;
  rec_on_zap();
  g_playing_file = 1;
  g_archive = 0;
  g_live = 0;
  g_loading = 1;
  send_cmd("{\"command\":[\"set_property\",\"pause\",false]}");
  char esc[MAX_PATHU * 2], json[MAX_PATHU * 2 + 96];
  json_escape(path, esc, sizeof esc);
  snprintf(json, sizeof json, "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", esc);
  return send_cmd(json);
}

int player_playing_file(void) { return g_playing_file; }

void player_set_pause(int on) {
  if (on && !g_paused) player_toggle_pause();
  else if (!on && g_paused) player_toggle_pause();
}

void player_set_multiview(int on) { g_multiview = on ? 1 : 0; }

void player_reload(const Playlist *pl, Settings *st) {
  if (!pl || !st || g_index < 0) return;
  send_cmd("{\"command\":[\"set_property\",\"pause\",false]}");
  g_paused = 0;
  if (g_playing_file || g_archive) {
    player_resize(g_host);
    return;
  }
  if (g_url_n > 0) load_url(pl, st, g_urls[g_url_i < g_url_n ? g_url_i : 0]);
  player_resize(g_host);
}

static LRESULT CALLBACK extra_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_CLOSE) return 0;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND extra_make_hwnd(const wchar_t *cls, HWND owner, int x, int y, int w, int h) {
  WNDCLASSW wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = extra_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hbrBackground = GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = cls;
  RegisterClassW(&wc);
  return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls, L"",
                         WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
}

static void extra_close_proc(ExtraVid *e) {
  if (!e) return;
  if (e->pipe && e->pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(e->pipe);
    e->pipe = NULL;
  }
  if (e->proc) {
    TerminateProcess(e->proc, 0);
    WaitForSingleObject(e->proc, 80);
    CloseHandle(e->proc);
    e->proc = NULL;
  }
}

static int extra_send(ExtraVid *e, const char *json) {
  if (!e || !e->pipe || e->pipe == INVALID_HANDLE_VALUE || !json) return 0;
  char line[MAX_URL + 256];
  snprintf(line, sizeof line, "%s\n", json);
  DWORD w = 0;
  if (!WriteFile(e->pipe, line, (DWORD)strlen(line), &w, NULL)) {
    if (GetLastError() == ERROR_NO_DATA || GetLastError() == ERROR_BROKEN_PIPE) return 0;
    return 0;
  }
  return w > 0;
}

static int extra_connect_pipe(ExtraVid *e) {
  if (!e) return 0;
  if (e->pipe && e->pipe != INVALID_HANDLE_VALUE) return 1;
  if (!e->pipename[0]) return 0;
  HANDLE pipe = CreateFileW(e->pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (pipe == INVALID_HANDLE_VALUE) return 0;
  DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
  SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
  e->pipe = pipe;
  return 1;
}

static int extra_setup(ExtraVid *e, const char *url, const char *ua, int mute) {
  if (ua && ua[0]) {
    char esc[MAX_UA * 2], json[MAX_UA * 2 + 80];
    json_escape(ua, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"user-agent\",\"%s\"]}", esc);
    extra_send(e, json);
  }
  extra_send(e, mute ? "{\"command\":[\"set_property\",\"mute\",true]}" : "{\"command\":[\"set_property\",\"mute\",false]}");
  extra_send(e, "{\"command\":[\"set_property\",\"volume\",70]}");
  extra_send(e, "{\"command\":[\"set_property\",\"pause\",false]}");
  if (url && url[0]) {
    char esc[MAX_URL * 2], json[MAX_URL * 2 + 80];
    json_escape(url, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", esc);
    extra_send(e, json);
  }
  extra_send(e, "{\"command\":[\"set_property\",\"pause\",false]}");
  return 1;
}

static int extra_spawn(ExtraVid *e, HWND hwnd, const wchar_t *pipename, const char *url, const char *ua, int mute,
                      int light) {
  if (!e || !hwnd || !locate_mpv()) return 0;
  if (e->proc && WaitForSingleObject(e->proc, 0) == WAIT_TIMEOUT) {
    if (extra_connect_pipe(e)) return extra_setup(e, url, ua, mute);
    return 0;
  }
  if (g_extra_busy) return 0;
  g_extra_busy = 1;
  extra_close_proc(e);
  wcsncpy(e->pipename, pipename, 63);
  e->pipename[63] = 0;
  wchar_t cwd[MAX_PATH];
  wcsncpy(cwd, g_mpv, MAX_PATH);
  wchar_t *cut = wcsrchr(cwd, L'\\');
  if (cut) *cut = 0;
  wchar_t cmd[4096];
  if (light) {
    swprintf(cmd, 4096,
             L"\"%s\" --no-config --idle=yes --input-ipc-server=%s --wid=%llu "
             L"--force-window=yes --focus-on=never --ontop=no --no-border --no-osc --osd-level=0 "
             L"--no-input-default-bindings --input-vo-keyboard=no --window-dragging=no "
             L"--keepaspect=yes --keepaspect-window=no --cursor-autohide=always --ytdl=no "
             L"--loop=no --loop-file=no --hls-bitrate=max "
             L"--hwdec=no --vo=gpu --gpu-api=d3d11 --gpu-dumb-mode=yes --framedrop=vo "
             L"--video-sync=display-desync --cache=yes --cache-secs=1 --demuxer-readahead-secs=0.4 "
             L"--vd-lavc-threads=1 --vd-lavc-skiploopfilter=all --vd-lavc-fast --scale=bilinear "
             L"--dither=no --msg-level=all=error --title=MirEfir",
             g_mpv, e->pipename, (unsigned long long)(uintptr_t)hwnd);
  } else {
    swprintf(cmd, 4096,
             L"\"%s\" --no-config --idle=yes --input-ipc-server=%s --wid=%llu "
             L"--force-window=yes --focus-on=never --ontop=no --no-border --no-osc --osd-level=0 "
             L"--no-input-default-bindings --input-vo-keyboard=no --window-dragging=no "
             L"--keepaspect=yes --keepaspect-window=no --cursor-autohide=always --ytdl=no "
             L"--loop=no --loop-file=no --hls-bitrate=max "
             L"--hwdec=d3d11va,dxva2,no --vo=gpu --gpu-api=d3d11 --cache=yes --cache-secs=3 "
             L"--msg-level=all=error --title=MirEfir",
             g_mpv, e->pipename, (unsigned long long)(uintptr_t)hwnd);
  }
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(g_mpv, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
    g_extra_busy = 0;
    return 0;
  }
  CloseHandle(pi.hThread);
  e->proc = pi.hProcess;
  g_extra_busy = 0;
  if (!extra_connect_pipe(e)) return 0;
  return extra_setup(e, url, ua, mute);
}

int player_pip_on(void) { return g_pip.hwnd && IsWindow(g_pip.hwnd) && g_pip.proc; }

void player_pip_stop(void) {
  extra_close_proc(&g_pip);
  if (g_pip.hwnd) {
    DestroyWindow(g_pip.hwnd);
    g_pip.hwnd = NULL;
  }
}

int player_pip_toggle(HWND host, const char *url, const char *ua) {
  if (player_pip_on()) {
    player_pip_stop();
    return 0;
  }
  if (!host || !url || !url[0]) return 0;
  RECT rc;
  GetWindowRect(host, &rc);
  int w = 320, h = 180;
  int x = rc.right - w - 24;
  int y = rc.top + 48;
  g_pip.hwnd = extra_make_hwnd(L"MirEfirPip", host, x, y, w, h);
  if (!g_pip.hwnd) return 0;
  ShowWindow(g_pip.hwnd, SW_SHOWNOACTIVATE);
  if (!extra_spawn(&g_pip, g_pip.hwnd, L"\\\\.\\pipe\\mirefir-native-pip", url, ua, 0, device_class() <= PLAY_LOW)) {
    player_pip_stop();
    return 0;
  }
  return 1;
}

int player_tile_start(int slot, HWND hwnd, const char *url, const char *ua) {
  if (slot < 0 || slot > 3 || !hwnd) return 0;
  if (g_tile[slot].proc && WaitForSingleObject(g_tile[slot].proc, 0) == WAIT_TIMEOUT &&
      g_tile[slot].pipe && g_tile[slot].pipe != INVALID_HANDLE_VALUE && url && url[0]) {
    g_tile[slot].hwnd = hwnd;
    if (ua && ua[0]) {
      char esc[MAX_UA * 2], json[MAX_UA * 2 + 80];
      json_escape(ua, esc, sizeof esc);
      snprintf(json, sizeof json, "{\"command\":[\"set_property\",\"user-agent\",\"%s\"]}", esc);
      extra_send(&g_tile[slot], json);
    }
    char esc[MAX_URL * 2], json[MAX_URL * 2 + 80];
    json_escape(url, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", esc);
    extra_send(&g_tile[slot], json);
    extra_send(&g_tile[slot], "{\"command\":[\"set_property\",\"pause\",false]}");
    return 1;
  }
  wchar_t pipe[64];
  swprintf(pipe, 64, L"\\\\.\\pipe\\mirefir-native-mv%d", slot);
  g_tile[slot].hwnd = hwnd;
  return extra_spawn(&g_tile[slot], hwnd, pipe, url, ua, 1, 1);
}

void player_tile_stop(int slot) {
  if (slot < 0 || slot > 3) return;
  extra_close_proc(&g_tile[slot]);
  g_tile[slot].hwnd = NULL;
}

void player_tile_stop_all(void) {
  for (int i = 0; i < 4; i++) player_tile_stop(i);
}

void player_tile_mute(int slot, int mute) {
  if (slot < 0 || slot > 3) return;
  extra_send(&g_tile[slot], mute ? "{\"command\":[\"set_property\",\"mute\",true]}"
                                 : "{\"command\":[\"set_property\",\"mute\",false]}");
}

void player_tile_pause(int slot, int on) {
  if (slot < 0 || slot > 3) return;
  extra_send(&g_tile[slot], on ? "{\"command\":[\"set_property\",\"pause\",true]}"
                               : "{\"command\":[\"set_property\",\"pause\",false]}");
}

void player_tile_pause_all(int on) {
  for (int i = 0; i < 4; i++) player_tile_pause(i, on);
}

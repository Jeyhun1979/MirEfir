#include "app.h"

#ifndef GET_APPCOMMAND_LPARAM
#define FAPPCOMMAND_MASK 0xF000
#define GET_APPCOMMAND_LPARAM(lp) ((short)(HIWORD(lp) & ~FAPPCOMMAND_MASK))
#endif
#ifndef GET_XBUTTON_WPARAM
#define GET_XBUTTON_WPARAM(wp) (HIWORD(wp))
#endif

static const wchar_t kClass[] = L"MirEfirShell";
static const wchar_t kTitle[] = L"MirEfir";

static Playlist g_pl;
static Settings g_st;
static HWND g_hwnd;
static BOOL g_fullscreen;
static WINDOWPLACEMENT g_placement;
static LONG g_style;
static LONG g_ex;
static wchar_t g_status[256];
static int g_engine_ok;

static void set_status(const wchar_t *text) {
  wcsncpy(g_status, text ? text : L"", 255);
  g_status[255] = 0;
  if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

static void set_fullscreen(HWND hwnd, BOOL enable) {
  if (enable == g_fullscreen) return;
  if (enable) {
    g_placement.length = sizeof g_placement;
    GetWindowPlacement(hwnd, &g_placement);
    g_style = GetWindowLongW(hwnd, GWL_STYLE);
    g_ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
    SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    g_fullscreen = TRUE;
  } else {
    SetWindowLongW(hwnd, GWL_STYLE, g_style);
    SetWindowLongW(hwnd, GWL_EXSTYLE, g_ex);
    SetWindowPlacement(hwnd, &g_placement);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_fullscreen = FALSE;
  }
  player_resize(hwnd);
  shell_layout(hwnd);
}

static void zap(int index) {
  if (!g_pl.count) return;
  if (index < 0) index = g_pl.count - 1;
  if (index >= g_pl.count) index = 0;
  player_play_channel(&g_pl, &g_st, index);
  shell_on_play(index);
}

static void on_quit(void) { DestroyWindow(g_hwnd); }

static int on_fs(int cmd) {
  if (cmd == 2) {
    if (g_fullscreen) {
      set_fullscreen(g_hwnd, FALSE);
      return 1;
    }
    return 0;
  }
  set_fullscreen(g_hwnd, cmd ? TRUE : FALSE);
  return 1;
}

static int g_booted;

static void boot(HWND hwnd) {
  if (g_booted) return;
  g_booted = 1;
  set_status(L"Запуск…");
  InvalidateRect(hwnd, NULL, TRUE);
  UpdateWindow(hwnd);
  device_probe();
  player_start(hwnd);
  if (!profile_load(&g_st, &g_pl)) {
    set_status(L"Нет плейлиста. Добавьте ссылку на M3U в окне плейлиста или положите playlist.m3u в папку профиля MirEfir.");
  }
  shell_init(hwnd, &g_pl, &g_st, zap, on_quit, on_fs);
  if (g_pl.count) epg_start(&g_pl, &g_st);
  device_sync(&g_st);
  if (g_pl.count && (player_alive() || player_start(hwnd))) {
    g_engine_ok = 1;
    if (g_st.autoplay) {
      int index = playlist_find_name(&g_pl, g_st.last_name);
      if (index < 0) index = g_st.last_index;
      if (index < 0 || index >= g_pl.count) index = 0;
      player_play_channel(&g_pl, &g_st, index);
      shell_on_play(index);
    }
  } else if (g_pl.count) {
    g_engine_ok = 0;
    set_status(L"Нет проигрывателя mpv рядом с программой.");
  }
  shell_layout(hwnd);
  SetForegroundWindow(hwnd);
}

static void zap_delta(int delta) {
  int next = shell_next_channel(delta);
  if (next < 0) {
    int cur = player_current_index();
    if (cur < 0) cur = g_st.last_index;
    next = cur + delta;
  }
  zap(next);
}

static void paint_status(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
  if (!g_status[0]) return;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(230, 230, 230));
  DrawTextW(hdc, g_status, -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      SetTimer(hwnd, 1, 400, NULL);
      SetTimer(hwnd, 2, 200, NULL);
      SetTimer(hwnd, 3, 1, NULL);
      return 0;
    case WM_TIMER:
      if (wp == 3) {
        KillTimer(hwnd, 3);
        boot(hwnd);
        return 0;
      }
      if (wp == 1) {
        if (player_alive()) player_tick(&g_pl, &g_st);
        epg_tick();
        shell_tick();
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
      } else if (wp == 2) {
        player_keep_focus(hwnd);
      }
      return 0;
    case WM_SIZE:
      player_resize(hwnd);
      shell_layout(hwnd);
      break;
    case WM_MOVE:
      player_resize(hwnd);
      shell_layout(hwnd);
      break;
    case WM_CLOSE:
      shell_request_exit();
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      shell_host_click();
      return 0;
    case WM_RBUTTONDOWN:
      SetFocus(hwnd);
      shell_context();
      return 0;
    case WM_XBUTTONDOWN:
      if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) {
        shell_keydown(VK_ESCAPE);
        return 0;
      }
      break;
    case WM_APPCOMMAND:
      if (shell_appcommand(GET_APPCOMMAND_LPARAM(lp))) return TRUE;
      break;
    case WM_MOUSEACTIVATE:
      SetFocus(hwnd);
      return MA_ACTIVATE;
    case WM_MOUSEMOVE:
      shell_mouse_moved();
      break;
    case WM_KEYUP:
      wp = shell_map_key(wp);
      if (shell_keyup(wp)) return 0;
      break;
    case WM_KEYDOWN: {
      wp = shell_map_key(wp);
      if (wp == VK_F11) {
        set_fullscreen(hwnd, !g_fullscreen);
        return 0;
      }
      if (shell_keydown(wp)) return 0;
      if (wp == VK_RIGHT) {
        int prev = player_prev_index();
        if (prev >= 0) zap(prev);
        return 0;
      }
      if (wp == VK_UP || wp == VK_PRIOR) {
        zap_delta(-1);
        return 0;
      }
      if (wp == VK_DOWN || wp == VK_NEXT) {
        zap_delta(1);
        return 0;
      }
      if (wp == VK_VOLUME_UP || wp == VK_ADD) {
        g_st.volume += 5;
        if (g_st.volume > 100) g_st.volume = 100;
        g_st.muted = 0;
        player_set_volume(g_st.volume, 0);
        profile_save_session(&g_st, player_current_index(), g_st.last_name);
        return 0;
      }
      if (wp == VK_VOLUME_DOWN || wp == VK_SUBTRACT) {
        g_st.volume -= 5;
        if (g_st.volume < 0) g_st.volume = 0;
        player_set_volume(g_st.volume, g_st.muted);
        profile_save_session(&g_st, player_current_index(), g_st.last_name);
        return 0;
      }
      if (wp == 'M' && !shell_typing()) {
        g_st.muted = !g_st.muted;
        player_set_volume(g_st.volume, g_st.muted);
        profile_save_session(&g_st, player_current_index(), g_st.last_name);
        return 0;
      }
      if (wp == 'A' && !shell_typing()) {
        player_cycle_audio();
        return 0;
      }
      break;
    }
    case WM_CHAR:
      if (shell_char((unsigned)wp)) return 0;
      break;
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect((HDC)wp, &rc, GetStockObject(BLACK_BRUSH));
      if (!g_engine_ok) paint_status(hwnd, (HDC)wp);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      FillRect(hdc, &ps.rcPaint, GetStockObject(BLACK_BRUSH));
      if (!g_engine_ok) paint_status(hwnd, hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_MIREFIR_UPDATE:
      shell_layout(hwnd);
      return 0;
    case WM_MIREFIR_INSTALL:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, 1);
      KillTimer(hwnd, 2);
      shell_shutdown();
      epg_stop();
      player_stop();
      playlist_free(&g_pl);
      SetThreadExecutionState(ES_CONTINUOUS);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
  (void)prev;
  (void)show;
  if (cmd && wcsstr(cmd, L"--install-ui")) return update_splash(inst);
  if (update_should_apply()) {
    update_apply(NULL);
    return 0;
  }

  WNDCLASSW wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
  HICON icon_sm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  if (!icon) icon = LoadIconW(NULL, IDI_APPLICATION);
  if (!icon_sm) icon_sm = icon;
  wc.hIcon = icon;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN;
  DWORD ex = WS_EX_APPWINDOW;
  RECT wr = {0, 0, 1280, 720};
  AdjustWindowRectEx(&wr, style & ~WS_VISIBLE, FALSE, ex);
  g_hwnd = CreateWindowExW(ex, kClass, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, inst, NULL);
  if (!g_hwnd) return 1;
  SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
  SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon_sm);
  ShowWindow(g_hwnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_hwnd);
  set_status(L"Запуск…");
  UpdateWindow(g_hwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return (int)msg.wParam;
}

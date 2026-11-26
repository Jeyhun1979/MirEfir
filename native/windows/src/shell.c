#include "app.h"
#include <shellapi.h>
#include <winreg.h>
#include <mapi.h>

enum { UI_LIVE, UI_PAD, UI_MENU, UI_GUIDE, UI_EXIT, UI_NOTE };
enum { VIEW_CHANNELS, VIEW_CATEGORIES, VIEW_GROUPS, VIEW_SCHEDULE };
enum { COL_GROUPS, COL_CHANNELS, COL_PROGRAMS, COL_DAYS, COL_DETAIL };
enum { PAD_NONE, PAD_BUTTONS, PAD_BAR };
enum { MAX_GROUPS = 256, MAX_GUIDE_DAYS = 32, PAD_N = 10, MENU_N = 10, PLAY_PAD = 4 };
enum { GK_PL = 0, GK_ALL, GK_FAV, GK_RECENT };
enum { SET_ROW_H = 56, SET_HEAD_H = 36, SET_HEAD_GAP = 22 };
enum { MENU_MAIN, MENU_SETTINGS, MENU_STUB, MENU_PIN, MENU_SEARCH, MENU_HISTORY, MENU_RECS, MENU_MULTI, MENU_ASKREC,
       MENU_SHARE, MENU_MICPERM, MENU_SETUP, MENU_URL };
enum { HOLD_NONE, HOLD_PAD_SUB, HOLD_CHAN, HOLD_LIVE, HOLD_URL };
enum { SHARE_COPY = 0, SHARE_TG = 1, SHARE_WA = 2, SHARE_MAX = 3, SHARE_MAIL = 4 };
enum { CHM_OFF, CHM_MENU, CHM_MOVE };
#define HOLD_MS 550
enum { SET_TAB_N = 11, SET_ROW_MAX = 160 };
enum { ACT_NONE, ACT_PL, ACT_EPG, ACT_URL, ACT_SEARCH };
enum { AID_USE, AID_PRI, AID_FB, AID_PASTE, AID_COPY, AID_DEL, AID_SEL };

#ifndef VK_GAMEPAD_A
#define VK_GAMEPAD_A 0xC3
#define VK_GAMEPAD_B 0xC4
#define VK_GAMEPAD_X 0xC5
#define VK_GAMEPAD_Y 0xC6
#define VK_GAMEPAD_DPAD_UP 0xCB
#define VK_GAMEPAD_DPAD_DOWN 0xCC
#define VK_GAMEPAD_DPAD_LEFT 0xCD
#define VK_GAMEPAD_DPAD_RIGHT 0xCE
#define VK_GAMEPAD_MENU 0xCF
#define VK_GAMEPAD_VIEW 0xD0
#endif
#ifndef APPCOMMAND_BROWSER_BACKWARD
#define APPCOMMAND_BROWSER_BACKWARD 1
#define APPCOMMAND_BROWSER_SEARCH 5
#define APPCOMMAND_BROWSER_HOME 7
#define APPCOMMAND_VOLUME_MUTE 8
#define APPCOMMAND_MEDIA_STOP 13
#define APPCOMMAND_MEDIA_PLAY_PAUSE 14
#define APPCOMMAND_MEDIA_PLAY 46
#define APPCOMMAND_MEDIA_PAUSE 47
#define APPCOMMAND_MEDIA_RECORD 48
#define APPCOMMAND_MEDIA_FAST_FORWARD 49
#define APPCOMMAND_MEDIA_REWIND 50
#define APPCOMMAND_MEDIA_CHANNEL_UP 51
#define APPCOMMAND_MEDIA_CHANNEL_DOWN 52
#endif

static const wchar_t kUiClass[] = L"MirEfirUi";
static const char *kMenuId[] = {"live", "guide", "recordings", "multiview", "search", "microphone", "playlists", "history", "settings", "exit"};
static const char *kMenuHintId[] = {"liveHint", "guideHint", "recordingsHint", "multiviewHint", "searchHint", "microphone", "playlistsHint", "historyHint", "settingsHint", "exitHint"};
static const char *kPadId[] = {"padMenu", "padGuide", "padVoice", "padBack30", "padPause", "padFwd30", "padLive", "padMute", "padPip", "padRec"};
static const char *kSetTabId[] = {"tabPlaylists", "tabEpg", "tabAppearance", "tabPlayer", "tabRecordings", "tabArchive",
                                  "tabParental", "tabRemote", "tabLanguage", "tabData", "tabAbout"};

static const wchar_t *menu_title(int i) { return i18n(kMenuId[i]); }
static const wchar_t *menu_hint(int i) { return i18n(kMenuHintId[i]); }
static const wchar_t *set_tab(int i) { return i18n(kSetTabId[i]); }
static const wchar_t *pad_lab(int i) {
  if (i == 9 && rec_live_active()) return i18n("padStop");
  return i18n(kPadId[i]);
}

static HWND g_host, g_clock, g_pad, g_menu, g_guide, g_exit, g_upd;
enum { UPD_W = 252, UPD_H = 44, UPD_APPLY_W = 132 };
static Playlist *g_pl;
static Settings *g_st;
static void (*g_zap)(int);
static void (*g_quit)(void);
static int (*g_fs)(int);
static int g_mode = UI_LIVE;
static int g_menu_i;
static int g_pad_i = PLAY_PAD;
static int g_pad_focus;
static int g_guide_view;
static int g_focus_col;
static int g_group_i;
static int g_menu_w;
static int g_chan_i;
static int g_logo_armed;
static int g_logo_i;
static int g_logo_hold_set;
static int g_marquee;
static int g_prog_i;
static int g_day_i;
static int g_day_n;
static int g_days_open;
static long long g_days[MAX_GUIDE_DAYS];
static int g_exit_i;
static int g_resume_guide;
static int g_menu_page;
static int g_set_tab;
static int g_set_pane;
static int g_set_i;
static int g_stub_i;
static int g_act;
static int g_act_i;
static int g_act_cur;
static int g_act_n;
static int g_act_id[8];
static const wchar_t *g_act_title[8];
static wchar_t g_set_note[160];
static DWORD g_set_note_at;
static int g_wait_key;
static int g_pin_ok;
static char g_pin_buf[MAX_PIN + 1];
static char g_xt_server[MAX_URL];
static char g_xt_user[96];
static char g_xt_pass[96];
static char g_search[80];
static int g_hits[40];
static int g_hit_n;
static int g_hit_i;
static RecFile g_files[MAX_REC_FILES];
static int g_file_n;
static int g_file_i;
static int g_hist_i;
static int g_hist_del;
static char g_url_edit[MAX_URL];
static int g_url_kind;
static int g_url_btn;
static int g_url_sel;
static int g_url_back;
static RECT g_url_field_rc, g_url_paste_rc, g_url_ok_rc;
static int g_ask_i;
static int g_share_i;
static int g_share_n;
static int g_share_kind[6];
static int g_share_file;
static int g_mv_ch[4];
static int g_mv_n;
static int g_mv_i;
static int g_mv_pick;
static int g_mv_menu;
static int g_mv_opt;
static int g_mv_spawned[4];
static int g_mv_need_start[4];
static int g_in_layout;
static RECT g_mv_rc[4];
static HWND g_tile_wnd[4];
static HWND g_mv_ring;
static int g_search_focus;
static int g_search_from;
static RECT g_search_card;
static RECT g_search_inp;
static RECT g_search_mic;
static int g_search_list_y;
static int g_ask_why;
static int g_pend_ch;
static char g_pend_title[96];
static long long g_pend_s;
static long long g_pend_e;
static int g_from_recs;
static int g_mic_i;
static char g_voice_note[192];
static int g_set_scroll;
static int g_act_x, g_act_y, g_act_w, g_act_row;
static DWORD g_pad_until;
static DWORD g_exit_until;
static DWORD g_esc_at;
static char g_groups[MAX_GROUPS][MAX_NAME];
static int g_group_kind[MAX_GROUPS];
static int g_group_n;
static int g_index_in_group[4096];
static int g_in_group_n;
static HFONT g_font;
static HFONT g_font_sm;
static HFONT g_font_pad;
static HFONT g_font_clock;
static HFONT g_font_day;
static HFONT g_font_tiny;
static int g_setup_i;
static int g_pin_from_group;
static int g_pin_group;
static int g_hold_kind;
static DWORD g_hold_at;
static int g_chm;
static int g_chm_i;
static int g_chm_n;
static int g_fav_bak[MAX_FAV];
static int g_fav_bak_n;
static int g_cur_vis = 1;
static DWORD g_mouse_at;
static POINT g_last_pt;
static int g_pl_boot_fetch;
static DWORD g_persist_at;

static void layout(HWND host);
static int group_hidden(const char *name);
static int group_needs_pin(int gi);
static void fill_round_rect(HDC mem, RECT r, COLORREF c);
static void hi_rgb(int *r, int *g, int *b);
static void theme_rgb(int *r, int *g, int *b);
static void persist_ui(int look, int play, int epg);
static void open_share(int file);
static void apply_share(void);
static void backup_back_to_data(void);
static int start_tile(int slot);
static void hide_tiles(void);
static void close_multi(void);
static void mv_set_ch(int slot, int ch);
static LRESULT CALLBACK tileProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK ringProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void mv_apply_audio(void);
static void mv_place_ring(void);
static int extra_page(void);
static int search_like(void);
static void fill_search(void);
static void do_voice(void);
static void paint_row(HDC mem, RECT item, int hot, const wchar_t *title, const wchar_t *sub);
static void fill_group(int gi);
static void later(const char *msg);
static void rebuild_guide_days(void);
static void sync_prog_to_channel(void);
static int guide_channel(void);
static void set_mode(int mode);
static void remember_group(int gi);
static void restore_group_cursor(void);
static void select_current_in_guide(void);
static void preview_folder(void);
static int enter_folder(void);
static void ask_group_pin(int gi);
static void run_setup_row(void);
static void open_ch_menu(void);
static void activate_channel(void);
static void close_guide(void);
static void open_guide(void);
static void do_backup_export(void);
static void do_backup_import(void);
static int open_context(void);

static HWND make_ui(HWND parent, int vis, int pass) {
  DWORD ex = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
  if (pass) ex |= WS_EX_TRANSPARENT;
  HWND h = CreateWindowExW(ex, kUiClass, L"", WS_POPUP, 0, 0, 10, 10, parent, NULL, GetModuleHandleW(NULL), NULL);
  if (h) SetPropW(h, L"mirefir-ui", (HANDLE)1);
  if (h && vis) ShowWindow(h, SW_SHOWNA);
  return h;
}

static void layer_key(HWND h, BYTE alpha) {
  if (h) SetLayeredWindowAttributes(h, RGB(0, 0, 0), alpha, LWA_COLORKEY | LWA_ALPHA);
}

static void layer_alpha(HWND h, BYTE alpha) {
  if (h) SetLayeredWindowAttributes(h, 0, alpha, LWA_ALPHA);
}

static void raise_ui(HWND h) {
  if (h && IsWindowVisible(h)) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void show_win(HWND h, int on) {
  if (!h) return;
  ShowWindow(h, on ? SW_SHOWNA : SW_HIDE);
  if (on) raise_ui(h);
}

static void place_ui(HWND h, HWND host, int x, int y, int w, int ht) {
  if (!h || !host) return;
  POINT pt = {x, y};
  ClientToScreen(host, &pt);
  SetWindowPos(h, HWND_TOP, pt.x, pt.y, w, ht, SWP_NOACTIVATE);
}

typedef struct {
  HDC mem;
  HBITMAP bm;
  HGDIOBJ old;
  void *bits;
  int w, h;
} Argb;

static int argb_begin(Argb *a, int w, int h) {
  memset(a, 0, sizeof *a);
  a->w = w;
  a->h = h;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  a->mem = CreateCompatibleDC(NULL);
  a->bm = CreateDIBSection(a->mem, &bi, DIB_RGB_COLORS, &a->bits, NULL, 0);
  a->old = SelectObject(a->mem, a->bm);
  if (a->bits) memset(a->bits, 0, (size_t)w * (size_t)h * 4);
  return a->bits != NULL;
}

static void argb_put(Argb *a, int x, int y, int r, int g, int b, int alpha) {
  if (!a->bits || x < 0 || y < 0 || x >= a->w || y >= a->h) return;
  unsigned char *p = (unsigned char *)a->bits + ((size_t)y * a->w + x) * 4;
  p[0] = (unsigned char)(b * alpha / 255);
  p[1] = (unsigned char)(g * alpha / 255);
  p[2] = (unsigned char)(r * alpha / 255);
  p[3] = (unsigned char)alpha;
}

static int in_round(int x, int y, int l, int t, int r, int b, int rad) {
  if (x < l || x >= r || y < t || y >= b) return 0;
  if (rad < 1) return 1;
  int x0 = l + rad, y0 = t + rad, x1 = r - rad - 1, y1 = b - rad - 1;
  if (x >= x0 && x <= x1) return 1;
  if (y >= y0 && y <= y1) return 1;
  int dx, dy;
  if (x < x0 && y < y0) {
    dx = x - x0;
    dy = y - y0;
    return dx * dx + dy * dy <= rad * rad;
  }
  if (x > x1 && y < y0) {
    dx = x - x1;
    dy = y - y0;
    return dx * dx + dy * dy <= rad * rad;
  }
  if (x < x0 && y > y1) {
    dx = x - x0;
    dy = y - y1;
    return dx * dx + dy * dy <= rad * rad;
  }
  dx = x - x1;
  dy = y - y1;
  return dx * dx + dy * dy <= rad * rad;
}

static void argb_fill(Argb *a, RECT rc, int rad, int r, int g, int b, int alpha) {
  if (rc.left < 0) rc.left = 0;
  if (rc.top < 0) rc.top = 0;
  if (rc.right > a->w) rc.right = a->w;
  if (rc.bottom > a->h) rc.bottom = a->h;
  for (int y = rc.top; y < rc.bottom; y++) {
    for (int x = rc.left; x < rc.right; x++) {
      if (in_round(x, y, rc.left, rc.top, rc.right, rc.bottom, rad)) argb_put(a, x, y, r, g, b, alpha);
    }
  }
}

static void argb_alpha_fix(Argb *a, RECT rc, int alpha) {
  int x0 = rc.left < 0 ? 0 : rc.left;
  int y0 = rc.top < 0 ? 0 : rc.top;
  int x1 = rc.right > a->w ? a->w : rc.right;
  int y1 = rc.bottom > a->h ? a->h : rc.bottom;
  unsigned char *bits = a->bits;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      unsigned char *p = bits + ((size_t)y * a->w + x) * 4;
      if (p[3] == 0 && (p[0] | p[1] | p[2])) {
        int bb = p[0], gg = p[1], rr = p[2];
        p[0] = (unsigned char)(bb * alpha / 255);
        p[1] = (unsigned char)(gg * alpha / 255);
        p[2] = (unsigned char)(rr * alpha / 255);
        p[3] = (unsigned char)alpha;
      }
    }
  }
}

static void argb_text(Argb *a, RECT rc, const wchar_t *text, HFONT font, int r, int g, int b, int alpha, UINT fmt) {
  if (!text || !text[0]) return;
  SetBkMode(a->mem, TRANSPARENT);
  SetTextColor(a->mem, RGB(r, g, b));
  SelectObject(a->mem, font);
  DrawTextW(a->mem, text, -1, &rc, fmt | DT_NOPREFIX);
  argb_alpha_fix(a, rc, alpha);
}

static int text_px(Argb *a, HFONT font, const wchar_t *text) {
  SIZE sz;
  if (!a || !text || !text[0]) return 0;
  if (font) SelectObject(a->mem, font);
  if (!GetTextExtentPoint32W(a->mem, text, (int)wcslen(text), &sz)) return 0;
  return sz.cx;
}

static void argb_marquee(Argb *a, RECT rc, const wchar_t *text, HFONT font, int r, int g, int b, int alpha) {
  if (!text || !text[0]) return;
  int rw = rc.right - rc.left;
  if (rw < 8 || rc.bottom <= rc.top) {
    argb_text(a, rc, text, font, r, g, b, alpha, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return;
  }
  int tw = text_px(a, font, text);
  if (tw <= rw) {
    argb_text(a, rc, text, font, r, g, b, alpha, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  g_marquee = 1;
  int gap = 48;
  int span = tw + gap;
  if (span < 1) span = 1;
  int shift = (int)((GetTickCount() / 30u) % (unsigned)span);
  int clip_l = rc.left < 0 ? 0 : rc.left;
  int clip_t = rc.top < 0 ? 0 : rc.top;
  int clip_r = rc.right > a->w ? a->w : rc.right;
  int clip_b = rc.bottom > a->h ? a->h : rc.bottom;
  if (clip_r <= clip_l || clip_b <= clip_t) return;
  HRGN rgn = CreateRectRgn(clip_l, clip_t, clip_r, clip_b);
  SelectClipRgn(a->mem, rgn);
  SetBkMode(a->mem, TRANSPARENT);
  SetTextColor(a->mem, RGB(r, g, b));
  if (font) SelectObject(a->mem, font);
  RECT d = rc;
  d.left = rc.left - shift;
  d.right = d.left + tw + 8;
  DrawTextW(a->mem, text, -1, &d, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
  RECT d2 = d;
  d2.left += span;
  d2.right += span;
  DrawTextW(a->mem, text, -1, &d2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
  SelectClipRgn(a->mem, NULL);
  DeleteObject(rgn);
  RECT vis = {clip_l, clip_t, clip_r, clip_b};
  argb_alpha_fix(a, vis, alpha);
}

static void argb_fit(Argb *a, RECT rc, const wchar_t *text, HFONT font, int r, int g, int b, int alpha, int scroll) {
  if (scroll) argb_marquee(a, rc, text, font, r, g, b, alpha);
  else argb_text(a, rc, text, font, r, g, b, alpha, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void argb_commit(HWND hwnd, Argb *a) {
  POINT src = {0, 0};
  SIZE sz = {a->w, a->h};
  POINT dst;
  RECT wr;
  GetWindowRect(hwnd, &wr);
  dst.x = wr.left;
  dst.y = wr.top;
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  HDC screen = GetDC(NULL);
  UpdateLayeredWindow(hwnd, screen, &dst, &sz, a->mem, &src, 0, &blend, ULW_ALPHA);
  ReleaseDC(NULL, screen);
  SelectObject(a->mem, a->old);
  DeleteObject(a->bm);
  DeleteDC(a->mem);
}

static int group_kind_of(int gi) {
  if (gi < 0 || gi >= g_group_n) return GK_PL;
  return g_group_kind[gi];
}

static int group_find_kind(int kind) {
  for (int i = 0; i < g_group_n; i++) {
    if (g_group_kind[i] == kind) return i;
  }
  return -1;
}

static int group_count(int gi) {
  int kind = group_kind_of(gi);
  if (kind == GK_ALL) return g_pl ? g_pl->count : 0;
  if (kind == GK_FAV) return g_st ? g_st->fav_n : 0;
  if (kind == GK_RECENT) return g_st ? g_st->recent_n : 0;
  int n = 0;
  if (!g_pl || gi < 0 || gi >= g_group_n) return 0;
  const char *name = g_groups[gi];
  for (int i = 0; i < g_pl->count; i++) {
    const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
    if (strcmp(g, name) == 0) n++;
  }
  return n;
}

static int channel_is_fav(int index) {
  if (!g_st) return 0;
  for (int i = 0; i < g_st->fav_n; i++) {
    if (g_st->fav[i] == index) return 1;
  }
  return 0;
}

static void toggle_fav_index(int idx) {
  if (!g_st || !g_pl || idx < 0 || idx >= g_pl->count) return;
  int on = 1;
  for (int i = 0; i < g_st->fav_n; i++) {
    if (g_st->fav[i] == idx) {
      if (i < g_st->fav_n - 1) memmove(&g_st->fav[i], &g_st->fav[i + 1], (size_t)(g_st->fav_n - i - 1) * sizeof(int));
      g_st->fav_n--;
      on = 0;
      break;
    }
  }
  if (on) {
    if (g_st->fav_n >= MAX_FAV) return;
    g_st->fav[g_st->fav_n++] = idx;
  }
  persist_ui(0, 0, 0);
  later(i18n_u(on ? "favAdd" : "favDel"));
  if (group_kind_of(g_group_i) == GK_FAV) fill_group(g_group_i);
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
}

static int sys_folder_hidden(const char *id, const char *alt) {
  if (group_hidden(id)) return 1;
  if (alt && alt[0] && group_hidden(alt)) return 1;
  return 0;
}

static void add_group(const char *name, int kind) {
  if (!name || g_group_n >= MAX_GROUPS) return;
  snprintf(g_groups[g_group_n], MAX_NAME, "%s", name);
  g_group_kind[g_group_n] = kind;
  g_group_n++;
}

static void rebuild_groups(void) {
  g_group_n = 0;
  if (!sys_folder_hidden("all", i18n_u("allChannels"))) add_group(i18n_u("allChannels"), GK_ALL);
  if (!sys_folder_hidden("favorites", i18n_u("favorites"))) add_group(i18n_u("favorites"), GK_FAV);
  if (!sys_folder_hidden("recent", i18n_u("recent"))) add_group(i18n_u("recent"), GK_RECENT);
  if (!g_pl) return;
  for (int i = 0; i < g_pl->count && g_group_n < MAX_GROUPS; i++) {
    const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
    int found = 0;
    for (int k = 0; k < g_group_n; k++) {
      if (g_group_kind[k] == GK_PL && strcmp(g_groups[k], g) == 0) {
        found = 1;
        break;
      }
    }
    if (!found && !group_hidden(g)) add_group(g, GK_PL);
  }
  if (g_group_i >= g_group_n) g_group_i = g_group_n ? 0 : 0;
}

static int cmp_ch_name(const void *a, const void *b) {
  int ia = *(const int *)a, ib = *(const int *)b;
  if (!g_pl) return 0;
  return _stricmp(g_pl->items[ia].name, g_pl->items[ib].name);
}
static int cmp_ch_num(const void *a, const void *b) {
  int ia = *(const int *)a, ib = *(const int *)b;
  if (!g_pl) return 0;
  int na = g_pl->items[ia].number ? g_pl->items[ia].number : ia + 1;
  int nb = g_pl->items[ib].number ? g_pl->items[ib].number : ib + 1;
  return na - nb;
}
static void sort_group(void) {
  if (!g_st || g_in_group_n < 2) return;
  if (strcmp(g_st->channel_sort, "name") == 0) qsort(g_index_in_group, (size_t)g_in_group_n, sizeof(int), cmp_ch_name);
  else if (strcmp(g_st->channel_sort, "number") == 0) qsort(g_index_in_group, (size_t)g_in_group_n, sizeof(int), cmp_ch_num);
}

static void fill_group(int gi) {
  g_in_group_n = 0;
  if (!g_pl || gi < 0 || gi >= g_group_n) return;
  int kind = group_kind_of(gi);
  if (kind == GK_ALL) {
    for (int i = 0; i < g_pl->count && g_in_group_n < 4096; i++) {
      const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
      if (group_hidden(g)) continue;
      g_index_in_group[g_in_group_n++] = i;
    }
    sort_group();
    return;
  }
  if (kind == GK_FAV) {
    if (!g_st) return;
    for (int i = 0; i < g_st->fav_n && g_in_group_n < 4096; i++) g_index_in_group[g_in_group_n++] = g_st->fav[i];
    return;
  }
  if (kind == GK_RECENT) {
    if (!g_st) return;
    for (int i = 0; i < g_st->recent_n && g_in_group_n < 4096; i++) g_index_in_group[g_in_group_n++] = g_st->recent[i];
    return;
  }
  if (group_needs_pin(gi)) {
    g_in_group_n = 0;
    return;
  }
  const char *name = g_groups[gi];
  for (int i = 0; i < g_pl->count && g_in_group_n < 4096; i++) {
    const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
    if (strcmp(g, name) == 0) g_index_in_group[g_in_group_n++] = i;
  }
  sort_group();
}

static void group_id_of(int gi, char *out, int max) {
  if (!out || max < 2) return;
  int kind = group_kind_of(gi);
  if (kind == GK_ALL) snprintf(out, max, "all");
  else if (kind == GK_FAV) snprintf(out, max, "favorites");
  else if (kind == GK_RECENT) snprintf(out, max, "recent");
  else if (gi >= 0 && gi < g_group_n) snprintf(out, max, "%s", g_groups[gi]);
  else {
    out[0] = 0;
  }
}

static int group_index_of_id(const char *id) {
  if (!id || !id[0] || strcmp(id, "all") == 0 || strcmp(id, "Все каналы") == 0) return group_find_kind(GK_ALL);
  if (strcmp(id, "favorites") == 0 || strcmp(id, "Избранное") == 0) return group_find_kind(GK_FAV);
  if (strcmp(id, "recent") == 0 || strcmp(id, "Недавние") == 0) return group_find_kind(GK_RECENT);
  for (int i = 0; i < g_group_n; i++) {
    if (g_group_kind[i] == GK_PL && strcmp(g_groups[i], id) == 0) return i;
  }
  return -1;
}

static int channel_in_recent(int index) {
  if (!g_st) return 0;
  for (int i = 0; i < g_st->recent_n; i++) {
    if (g_st->recent[i] == index) return 1;
  }
  return 0;
}

static void remember_group(int gi) {
  if (gi < 0 || gi >= g_group_n) return;
  g_group_i = gi;
  if (g_st) group_id_of(gi, g_st->last_group, MAX_NAME);
}

static void restore_group_cursor(void) {
  int gi = g_st ? group_index_of_id(g_st->last_group) : -1;
  if (gi < 0) gi = group_find_kind(GK_ALL);
  if (gi < 0) gi = 0;
  g_group_i = gi;
  fill_group(gi);
  select_current_in_guide();
  rebuild_guide_days();
  sync_prog_to_channel();
}

static void preview_folder(void) {
  if (group_needs_pin(g_group_i)) {
    g_in_group_n = 0;
    return;
  }
  fill_group(g_group_i);
  int home = g_st ? group_index_of_id(g_st->last_group) : -1;
  if (home < 0) home = 0;
  if (g_group_i == home) select_current_in_guide();
  else g_chan_i = 0;
  rebuild_guide_days();
  sync_prog_to_channel();
}

static int enter_folder(void) {
  if (group_needs_pin(g_group_i)) {
    later(i18n_u("pinGroup"));
    ask_group_pin(g_group_i);
    return 0;
  }
  preview_folder();
  g_guide_view = VIEW_CATEGORIES;
  g_focus_col = COL_CHANNELS;
  return 1;
}

static void apply_playing_group(int ch) {
  const char *cur = g_st && g_st->last_group[0] ? g_st->last_group : "all";
  int in_fav = channel_is_fav(ch);
  int in_recent = channel_in_recent(ch);
  int fav_i = group_find_kind(GK_FAV);
  int rec_i = group_find_kind(GK_RECENT);
  int all_i = group_find_kind(GK_ALL);
  if ((strcmp(cur, "favorites") == 0 || strcmp(cur, "Избранное") == 0) && in_fav && fav_i >= 0) {
    remember_group(fav_i);
    fill_group(fav_i);
    return;
  }
  if ((strcmp(cur, "recent") == 0 || strcmp(cur, "Недавние") == 0) && in_recent && rec_i >= 0) {
    remember_group(rec_i);
    fill_group(rec_i);
    return;
  }
  int gi = group_index_of_id(cur);
  if (gi >= 0 && group_kind_of(gi) == GK_FAV && in_fav) {
    remember_group(gi);
    fill_group(gi);
    return;
  }
  if (gi >= 0 && group_kind_of(gi) == GK_RECENT && in_recent) {
    remember_group(gi);
    fill_group(gi);
    return;
  }
  if (gi >= 0 && group_kind_of(gi) == GK_ALL) {
    remember_group(gi);
    fill_group(gi);
    return;
  }
  if (gi >= 0 && group_kind_of(gi) == GK_PL && g_pl && ch >= 0 && ch < g_pl->count) {
    const char *g = g_pl->items[ch].group[0] ? g_pl->items[ch].group : "Другие";
    if (strcmp(g, g_groups[gi]) == 0) {
      remember_group(gi);
      fill_group(gi);
      return;
    }
  }
  if (g_pl && ch >= 0 && ch < g_pl->count) {
    const char *g = g_pl->items[ch].group[0] ? g_pl->items[ch].group : "Другие";
    for (int i = 0; i < g_group_n; i++) {
      if (g_group_kind[i] == GK_PL && strcmp(g_groups[i], g) == 0) {
        remember_group(i);
        fill_group(i);
        return;
      }
    }
  }
  if (all_i >= 0) {
    remember_group(all_i);
    fill_group(all_i);
    return;
  }
  if (g_group_n) {
    remember_group(0);
    fill_group(0);
  }
}

static void select_channel_row(int ch) {
  g_chan_i = 0;
  for (int i = 0; i < g_in_group_n; i++) {
    if (g_index_in_group[i] == ch) {
      g_chan_i = i;
      break;
    }
  }
}

static void select_current_in_guide(void) {
  int cur = player_current_index();
  if (g_mv_pick && g_mv_i >= 0 && g_mv_i < g_mv_n && g_mv_ch[g_mv_i] >= 0) cur = g_mv_ch[g_mv_i];
  select_channel_row(cur);
}

static int guide_channel(void) {
  if (g_chan_i >= 0 && g_chan_i < g_in_group_n) return g_index_in_group[g_chan_i];
  return player_current_index();
}

static int menu_channel(void) {
  if (g_mode == UI_GUIDE && g_in_group_n) return guide_channel();
  return player_current_index();
}

static void paint_ch_overlay(Argb *a, RECT rc) {
  if (g_chm != CHM_MENU && g_chm != CHM_MOVE) return;
  int mw = 320, mh = g_chm == CHM_MOVE ? 110 : (g_chm_n * 48 + 24);
  RECT box = {rc.right / 2 - mw / 2, rc.bottom / 2 - mh / 2, rc.right / 2 + mw / 2, rc.bottom / 2 + mh / 2};
  argb_fill(a, box, 12, 10, 14, 22, 230);
  if (g_chm == CHM_MOVE) {
    RECT t = {box.left + 16, box.top + 16, box.right - 16, box.bottom - 16};
    argb_text(a, t, i18n("favMoveHint"), g_font_sm, 255, 255, 255, 240, DT_LEFT | DT_WORDBREAK);
    return;
  }
  RECT r0 = {box.left + 12, box.top + 12, box.right - 12, box.top + 52};
  int ch = menu_channel();
  int fav = channel_is_fav(ch);
  if (g_chm_i == 0) argb_fill(a, r0, 8, 47, 124, 246, 200);
  argb_text(a, r0, fav ? i18n("favDelAction") : i18n("favAddAction"), g_font_sm, 255, 255, 255, 255,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (g_chm_n > 1) {
    RECT r1 = {box.left + 12, box.top + 56, box.right - 12, box.top + 96};
    if (g_chm_i == 1) argb_fill(a, r1, 8, 47, 124, 246, 200);
    argb_text(a, r1, i18n("favMove"), g_font_sm, 255, 255, 255, 255, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
}

static void rebuild_guide_days(void) {
  long long now = epg_now_ms();
  long long today = epg_day_start(now);
  long long day = 24LL * 60 * 60 * 1000;
  int past = 7, future = 7;
  if (g_st) {
    future = g_st->epg_days > 0 ? g_st->epg_days : 7;
    past = future;
    if (g_st->archive_enabled && g_st->archive_days > past) past = g_st->archive_days;
  }
  g_day_n = 0;
  g_day_i = 0;
  for (int i = -past; i <= future && g_day_n < MAX_GUIDE_DAYS; i++) {
    if (i == 0) g_day_i = g_day_n;
    g_days[g_day_n++] = today + (long long)i * day;
  }
}

static void sync_day_from_program(int ch, int slot) {
  long long start = 0;
  if (!epg_list(ch, slot, NULL, 0, NULL, 0, &start, NULL) || !start) return;
  long long day = epg_day_start(start);
  for (int i = 0; i < g_day_n; i++) {
    if (g_days[i] == day) {
      g_day_i = i;
      return;
    }
  }
}

static void sync_prog_to_channel(void) {
  int ch = guide_channel();
  long long day = (g_day_i >= 0 && g_day_i < g_day_n) ? g_days[g_day_i] : epg_day_start(epg_now_ms());
  int n = epg_count(ch);
  g_prog_i = epg_index_for_day(ch, day);
  if (g_prog_i < 0) g_prog_i = 0;
  if (n && g_prog_i >= n) g_prog_i = n - 1;
  sync_day_from_program(ch, g_prog_i);
}

static void format_day_long(long long ms, wchar_t *out, int max) {
  SYSTEMTIME st;
  epg_local_fields(ms, &st);
  int w = st.wDayOfWeek;
  int m = st.wMonth >= 1 && st.wMonth <= 12 ? st.wMonth - 1 : 0;
  if (w < 0 || w > 6) w = 0;
  swprintf(out, max, L"%s, %u %s", i18n_wd_long(w), (unsigned)st.wDay, i18n_mo_long(m));
}

static void paint_panel(HWND hwnd, const wchar_t *title) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ oldb = SelectObject(mem, bm);
  HBRUSH bg = CreateSolidBrush(RGB(18, 22, 32));
  FillRect(mem, &rc, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(240, 242, 248));
  if (title) {
    RECT top = {20, 16, rc.right - 20, 56};
    SelectObject(mem, g_font);
    DrawTextW(mem, title, -1, &top, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldb);
  DeleteObject(bm);
  DeleteDC(mem);
  EndPaint(hwnd, &ps);
}

static void draw_item(HDC hdc, RECT *rc, int hot, const wchar_t *title, const wchar_t *hint) {
  if (hot) {
    int r, g, b;
    hi_rgb(&r, &g, &b);
    HBRUSH br = CreateSolidBrush(RGB(r, g, b));
    FillRect(hdc, rc, br);
    DeleteObject(br);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(255, 255, 255));
  RECT a = *rc;
  a.left += 20;
  a.right -= 16;
  a.top += 6;
  a.bottom = a.top + 28;
  SelectObject(hdc, g_font);
  DrawTextW(hdc, title, -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  if (hint) {
    SetTextColor(hdc, hot ? RGB(220, 230, 255) : RGB(140, 150, 165));
    a.top = a.bottom - 4;
    a.bottom = rc->bottom - 6;
    SelectObject(hdc, g_font_sm);
    DrawTextW(hdc, hint, -1, &a, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
}

static void refresh_clock(void) {
  if (!g_clock || !IsWindowVisible(g_clock)) return;
  RECT rc;
  GetClientRect(g_clock, &rc);
  int w = rc.right, h = rc.bottom;
  if (w < 8 || h < 8) return;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HDC screen = GetDC(NULL);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bm = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ old = SelectObject(mem, bm);
  if (bits) memset(bits, 0, (size_t)w * (size_t)h * 4);
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t buf[16];
  swprintf(buf, 16, L"%02d:%02d", st.wHour, st.wMinute);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font_clock);
  DrawTextW(mem, buf, -1, &rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  unsigned char *p = bits;
  int n = w * h;
  for (int i = 0; i < n && p; i++, p += 4) {
    int b = p[0], g = p[1], r = p[2];
    if (r | g | b) {
      int a = g_st && g_st->clock_opacity > 0 ? g_st->clock_opacity * 255 / 100 : 230;
      if (a < 40) a = 40;
      if (a > 255) a = 255;
      p[0] = (unsigned char)(b * a / 255);
      p[1] = (unsigned char)(g * a / 255);
      p[2] = (unsigned char)(r * a / 255);
      p[3] = (unsigned char)a;
    } else {
      p[3] = 0;
    }
  }
  POINT src = {0, 0};
  SIZE sz = {w, h};
  POINT dst;
  RECT wr;
  GetWindowRect(g_clock, &wr);
  dst.x = wr.left;
  dst.y = wr.top;
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(g_clock, screen, &dst, &sz, mem, &src, 0, &blend, ULW_ALPHA);
  SelectObject(mem, old);
  DeleteObject(bm);
  DeleteDC(mem);
  ReleaseDC(NULL, screen);
}

static int upd_shown(void) {
  return update_ready() && g_mode != UI_MENU && g_mode != UI_GUIDE && g_mode != UI_EXIT;
}

static void paint_upd(HWND hwnd) {
  PAINTSTRUCT ps;
  BeginPaint(hwnd, &ps);
  EndPaint(hwnd, &ps);
  if (!upd_shown()) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  Argb a;
  if (!argb_begin(&a, rc.right, rc.bottom)) return;
  int split = rc.right > 8 ? rc.right * UPD_APPLY_W / UPD_W : rc.right / 2;
  if (split < 1) split = rc.right / 2;
  if (split >= rc.right) split = rc.right / 2;
  int gap = rc.right - split > 10 ? 6 : 0;
  RECT apply = {0, 0, split, rc.bottom};
  RECT later = {split + gap, 0, rc.right, rc.bottom};
  argb_fill(&a, apply, 12, 37, 99, 235, 235);
  argb_fill(&a, later, 12, 26, 34, 48, 230);
  argb_text(&a, apply, i18n("update"), g_font ? g_font : g_font_sm, 255, 255, 255, 255,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  argb_text(&a, later, i18n("updateLater"), g_font ? g_font : g_font_sm, 236, 240, 248, 255,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  argb_commit(hwnd, &a);
}

typedef struct {
  RECT row;
  RECT btn[PAD_N];
  RECT timer;
  RECT bar;
} PadGeom;

static void pad_geom(RECT rc, PadGeom *g) {
  memset(g, 0, sizeof *g);
  int gap = 6, inset = 8, row_h = 72;
  int play_w = 56, btn_w = 58, btn_h = 36;
  int nbtn = PAD_N > 1 ? PAD_N - 1 : 1;
  int avail = rc.right - inset * 2;
  if (avail < 40) avail = rc.right > 8 ? rc.right - 4 : 40;
  int need = play_w + btn_w * nbtn + gap * nbtn;
  if (need > avail) {
    gap = 3;
    need = play_w + btn_w * nbtn + gap * nbtn;
  }
  if (need > avail) {
    int rest = avail - gap * nbtn;
    if (rest < PAD_N * 18) rest = PAD_N * 18;
    play_w = rest * 14 / (14 + 10 * nbtn);
    if (play_w < 20) play_w = 20;
    if (play_w > 56) play_w = 56;
    btn_w = (rest - play_w) / nbtn;
    if (btn_w < 16) btn_w = 16;
    if (btn_h > btn_w) btn_h = btn_w;
  }
  int total = inset * 2 + play_w + btn_w * nbtn + gap * nbtn;
  int x0 = (rc.right - total) / 2;
  if (x0 < 0) x0 = 0;
  g->row.left = x0;
  g->row.top = 0;
  g->row.right = x0 + total;
  g->row.bottom = row_h;
  int x = x0 + inset;
  for (int i = 0; i < PAD_N; i++) {
    int w = i == PLAY_PAD ? play_w : btn_w;
    int h = i == PLAY_PAD ? play_w : btn_h;
    int y = (row_h - h) / 2;
    g->btn[i].left = x;
    g->btn[i].top = y;
    g->btn[i].right = x + w;
    g->btn[i].bottom = y + h;
    x += w + gap;
  }
  g->timer.left = 12;
  g->timer.top = row_h + 4;
  g->timer.right = rc.right - 12;
  g->timer.bottom = row_h + 22;
  g->bar.left = 12;
  g->bar.top = row_h + 28;
  g->bar.right = rc.right - 12;
  g->bar.bottom = row_h + 30;
}

static void format_hms(long long ms, wchar_t *out, int max) {
  if (ms < 0) ms = 0;
  int sec = (int)(ms / 1000);
  swprintf(out, max, L"%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
}

static int pad_bounds(long long *elapsed, long long *total, double *ratio) {
  long long start = 0, end = 0, pos = 0;
  if (player_is_archive()) {
    if (!player_archive_bounds(&start, &end, &pos)) return 0;
  } else {
    int ch = player_current_index();
    if (!epg_now(ch, NULL, 0, NULL, 0, &start, &end) || end <= start) return 0;
    pos = epg_now_ms();
    if (pos < start) pos = start;
    if (pos > end) pos = end;
  }
  if (elapsed) *elapsed = pos - start;
  if (total) *total = end - start;
  if (ratio) *ratio = end > start ? (double)(pos - start) / (double)(end - start) : 0;
  return 1;
}

static void paint_pad(HWND hwnd) {
  PAINTSTRUCT ps;
  BeginPaint(hwnd, &ps);
  EndPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  Argb a;
  if (!argb_begin(&a, rc.right, rc.bottom)) return;
  PadGeom g;
  pad_geom(rc, &g);
  int row_a = g_pad_focus == PAD_BUTTONS ? 178 : 140;
  argb_fill(&a, g.row, 16, 0, 0, 0, row_a);
  if (g_pad_focus == PAD_BUTTONS) {
    RECT ring = {g.row.left + 1, g.row.top + 1, g.row.right - 1, g.row.bottom - 1};
    argb_fill(&a, ring, 16, 47, 124, 246, 90);
    RECT inner = {g.row.left + 3, g.row.top + 3, g.row.right - 3, g.row.bottom - 3};
    argb_fill(&a, inner, 14, 0, 0, 0, row_a);
  }
  int archive = player_is_archive();
  for (int i = 0; i < PAD_N; i++) {
    RECT b = g.btn[i];
    int hot = g_pad_focus == PAD_BUTTONS && i == g_pad_i;
    int dim = (i == 5 && !archive);
    if (i == PLAY_PAD) {
      if (hot) {
        RECT o = {b.left - 3, b.top - 3, b.right + 3, b.bottom + 3};
        argb_fill(&a, o, 31, 47, 124, 246, 220);
      }
      argb_fill(&a, b, 28, 255, 255, 255, 255);
      int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
      if (player_paused()) {
        for (int py = -10; py <= 10; py++) {
          int span = (10 - (py < 0 ? -py : py)) / 2 + 5;
          for (int px = -5; px <= span; px++) argb_put(&a, cx + px - 2, cy + py, 0, 0, 0, 255);
        }
      } else {
        for (int py = -10; py <= 10; py++) {
          for (int px = -6; px <= -2; px++) argb_put(&a, cx + px, cy + py, 0, 0, 0, 255);
          for (int px = 2; px <= 6; px++) argb_put(&a, cx + px, cy + py, 0, 0, 0, 255);
        }
      }
    } else {
      if (hot && !dim) argb_fill(&a, b, 12, 47, 124, 246, 255);
      else if (dim) argb_fill(&a, b, 12, 70, 74, 82, 255);
      else argb_fill(&a, b, 12, 96, 102, 112, 255);
      int tr = dim ? 190 : 255, tg = dim ? 190 : 255, tb = dim ? 190 : 255;
      argb_text(&a, b, pad_lab(i), g_font_pad ? g_font_pad : g_font_sm, tr, tg, tb, 255,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  }
  long long elapsed = 0, total = 0;
  double ratio = 0;
  int has = pad_bounds(&elapsed, &total, &ratio);
  if (has) {
    wchar_t clock[40], el[16], tot[16];
    format_hms(elapsed, el, 16);
    format_hms(total, tot, 16);
    swprintf(clock, 40, L"%s/%s", el, tot);
    argb_text(&a, g.timer, clock, g_font_pad ? g_font_pad : g_font_sm, 230, 230, 230, 230,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  if (g_pad_focus == PAD_BAR) {
    RECT hi = {g.bar.left - 2, g.bar.top - 2, g.bar.right + 2, g.bar.bottom + 2};
    argb_fill(&a, hi, 6, 255, 255, 255, 70);
  }
  argb_fill(&a, g.bar, 6, 255, 255, 255, 38);
  if (has && ratio > 0) {
    int bw = g.bar.right - g.bar.left;
    int fw = (int)(bw * (ratio < 0 ? 0 : ratio > 1 ? 1 : ratio));
    RECT fill = {g.bar.left, g.bar.top, g.bar.left + fw, g.bar.bottom};
    argb_fill(&a, fill, 6, 47, 124, 246, 255);
  }
  if (g_pad_focus == PAD_BAR) {
    int bw = g.bar.right - g.bar.left;
    int t = (int)(bw * (ratio < 0 ? 0 : ratio > 1 ? 1 : ratio));
    int cx = g.bar.left + t;
    int cy = (g.bar.top + g.bar.bottom) / 2;
    RECT knob = {cx - 8, cy - 8, cx + 8, cy + 8};
    argb_fill(&a, knob, 8, 255, 255, 255, 255);
    RECT inner = {cx - 6, cy - 6, cx + 6, cy + 6};
    argb_fill(&a, inner, 6, 47, 124, 246, 255);
  }
  argb_commit(hwnd, &a);
}

enum {
  SID_INFO, SID_CLOCK, SID_NUMBERS, SID_CONFIRM, SID_VOICE, SID_DECODER, SID_BUFFER, SID_ASPECT,
  SID_ARCHIVE, SID_ADAYS, SID_EPGDAYS, SID_OFFSET, SID_EPGAUTO, SID_EPGHOURS, SID_DESC,
  SID_ADD_URL, SID_ADD_FILE, SID_LOAD_EPG, SID_PL, SID_EPG, SID_UA, SID_PLHOURS, SID_SORT,
  SID_XT_SRV, SID_XT_USER, SID_XT_PASS, SID_XT_ADD, SID_THEME, SID_HI, SID_PANEL, SID_FONT,
  SID_LOGO, SID_GROWS, SID_TWOLINE, SID_CLOCKPOS, SID_CLOCKSZ, SID_CLOCKOP, SID_PROG,
  SID_AUTOPLAY, SID_REMVOL, SID_EXTPL, SID_AUDIO, SID_REC, SID_RECPATH, SID_PARENTAL,
  SID_LOCKSET, SID_PIN, SID_LANG, SID_CLOUD_COPY, SID_CLOUD_PASTE, SID_EXPORT, SID_IMPORT,
  SID_RESET, SID_KEY, SID_HIDEGRP, SID_LOCKGRP, SID_CLOUD_SHARE, SID_BACKUP_SHARE, SID_ADD_PRI, SID_ADD_FB, SID_UPDATE,
  SID_HEAD
};

static int g_sid[SET_ROW_MAX];
static int g_sidx[SET_ROW_MAX];
static wchar_t g_sval[SET_ROW_MAX][96];
static wchar_t g_stitlebuf[SET_ROW_MAX][96];
static const wchar_t *g_stitle[SET_ROW_MAX];
static int g_sn;

static void set_note(const wchar_t *text) {
  wcsncpy(g_set_note, text ? text : L"", 159);
  g_set_note[159] = 0;
  g_set_note_at = GetTickCount();
}

static void url_hint(const char *url, wchar_t *out, int max) {
  wchar_t w[MAX_URL];
  utf8_to_wide(url ? url : "", w, MAX_URL);
  int n = (int)wcslen(w);
  if (n < max - 1) {
    wcsncpy(out, w, max - 1);
    out[max - 1] = 0;
    return;
  }
  int keep = max - 2;
  if (keep < 8) keep = max - 1;
  wcsncpy(out, w + n - keep, keep);
  out[keep] = 0;
}

static void add_row_i(int id, const wchar_t *title, const wchar_t *val, int idx) {
  if (g_sn >= SET_ROW_MAX) return;
  g_sid[g_sn] = id;
  g_sidx[g_sn] = idx;
  wcsncpy(g_stitlebuf[g_sn], title ? title : L"", 95);
  g_stitlebuf[g_sn][95] = 0;
  g_stitle[g_sn] = g_stitlebuf[g_sn];
  wcsncpy(g_sval[g_sn], val ? val : L"", 95);
  g_sval[g_sn][95] = 0;
  g_sn++;
}

static void add_row(int id, const wchar_t *title, const wchar_t *val) { add_row_i(id, title, val, -1); }

static const wchar_t *onoff(int v) { return v ? i18n("on") : i18n("off"); }

static void cycle_str(char *v, int max, const char **opts, int n) {
  if (!v || !opts || n < 1 || max < 2) return;
  int i = 0;
  for (; i < n; i++) {
    if (strcmp(v, opts[i]) == 0) break;
  }
  snprintf(v, max, "%s", opts[(i + 1) % n]);
}

static void urlenc(const char *in, char *out, int max) {
  static const char hex[] = "0123456789ABCDEF";
  int o = 0;
  if (!out || max < 2) return;
  for (int i = 0; in && in[i] && o < max - 4; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

static int group_hidden(const char *name) {
  if (!g_st || !name) return 0;
  for (int i = 0; i < g_st->hidden_n; i++) {
    if (strcmp(g_st->hidden[i], name) == 0) return 1;
  }
  return 0;
}

static int group_locked(const char *name) {
  if (!g_st || !g_st->parental_enabled || !name || !name[0]) return 0;
  for (int i = 0; i < g_st->locked_n; i++) {
    if (strcmp(g_st->locked[i], name) == 0) return 1;
  }
  return 0;
}

static int group_needs_pin(int gi) {
  if (gi < 0 || gi >= g_group_n || group_kind_of(gi) != GK_PL) return 0;
  return group_locked(g_groups[gi]) && !g_pin_ok;
}

static int pl_group_nth(int want, char *out, int max) {
  char seen[MAX_GROUPS][MAX_NAME];
  int seen_n = 0;
  if (!g_pl) return 0;
  for (int i = 0; i < g_pl->count && seen_n < MAX_GROUPS; i++) {
    const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
    int dup = 0;
    for (int k = 0; k < seen_n; k++) {
      if (strcmp(seen[k], g) == 0) {
        dup = 1;
        break;
      }
    }
    if (dup) continue;
    if (seen_n == want) {
      if (out && max > 0) snprintf(out, max, "%s", g);
      return 1;
    }
    snprintf(seen[seen_n++], MAX_NAME, "%s", g);
  }
  return 0;
}

static int needs_setup(void) { return !g_pl || g_pl->count < 1; }

static void cursor_show(int on) {
  if (on) {
    if (!g_cur_vis) {
      int c;
      while ((c = ShowCursor(TRUE)) < 0) {
      }
      g_cur_vis = 1;
    }
  } else if (g_cur_vis) {
    int c;
    while ((c = ShowCursor(FALSE)) >= 0) {
    }
    g_cur_vis = 0;
  }
}

static void name_list_remove(char (*list)[MAX_NAME], int *n, const char *name) {
  if (!list || !n || !name || !name[0]) return;
  for (int i = 0; i < *n; i++) {
    if (strcmp(list[i], name) == 0) {
      if (i < *n - 1) memmove(list[i], list[i + 1], (size_t)(*n - i - 1) * MAX_NAME);
      (*n)--;
      memset(list[*n], 0, MAX_NAME);
      return;
    }
  }
}

static void toggle_name_list(char (*list)[MAX_NAME], int *n, int maxn, const char *name) {
  if (!list || !n || !name || !name[0]) return;
  for (int i = 0; i < *n; i++) {
    if (strcmp(list[i], name) == 0) {
      if (i < *n - 1) memmove(list[i], list[i + 1], (size_t)(*n - i - 1) * MAX_NAME);
      (*n)--;
      memset(list[*n], 0, MAX_NAME);
      return;
    }
  }
  if (*n >= maxn) return;
  snprintf(list[*n], MAX_NAME, "%s", name);
  (*n)++;
}

static int hide_folder_id(int idx, char *out, int max, const char **alt) {
  if (alt) *alt = NULL;
  if (idx == 0) {
    if (out && max > 0) snprintf(out, max, "all");
    if (alt) *alt = i18n_u("allChannels");
    return 1;
  }
  if (idx == 1) {
    if (out && max > 0) snprintf(out, max, "favorites");
    if (alt) *alt = i18n_u("favorites");
    return 1;
  }
  if (idx == 2) {
    if (out && max > 0) snprintf(out, max, "recent");
    if (alt) *alt = i18n_u("recent");
    return 1;
  }
  return pl_group_nth(idx - 3, out, max);
}

static int folder_shown(int idx) {
  char id[MAX_NAME];
  const char *alt = NULL;
  if (!hide_folder_id(idx, id, MAX_NAME, &alt)) return 1;
  return !group_hidden(id) && !(alt && group_hidden(alt));
}

static void toggle_hidden_folder(int idx) {
  char id[MAX_NAME];
  const char *alt = NULL;
  if (!g_st || !hide_folder_id(idx, id, MAX_NAME, &alt)) return;
  if (folder_shown(idx)) {
    toggle_name_list(g_st->hidden, &g_st->hidden_n, MAX_HIDDEN, id);
  } else {
    name_list_remove(g_st->hidden, &g_st->hidden_n, id);
    if (alt) name_list_remove(g_st->hidden, &g_st->hidden_n, alt);
  }
}

static void rebuild_fonts(void);
static void apply_panel_alpha(void);

static void persist_ui(int look, int play, int epg) {
  if (!g_st) return;
  i18n_set(g_st->language);
  profile_remember_lists(g_st, g_pl);
  profile_save_settings(g_st);
  if (look) {
    rebuild_fonts();
    apply_panel_alpha();
    layout(g_host);
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    if (g_pad) InvalidateRect(g_pad, NULL, FALSE);
    if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
    if (g_exit) InvalidateRect(g_exit, NULL, FALSE);
  }
  if (play) player_apply_settings(g_st);
  if (epg) epg_apply_settings(g_st);
}

static void vk_name(WPARAM k, char *out, int max) {
  if (!out || max < 2) return;
  if (k == VK_SPACE) {
    snprintf(out, max, " ");
    return;
  }
  if (k == VK_RETURN) {
    snprintf(out, max, "Enter");
    return;
  }
  if (k == VK_LEFT) {
    snprintf(out, max, "ArrowLeft");
    return;
  }
  if (k == VK_RIGHT) {
    snprintf(out, max, "ArrowRight");
    return;
  }
  if (k == VK_UP) {
    snprintf(out, max, "ArrowUp");
    return;
  }
  if (k == VK_DOWN) {
    snprintf(out, max, "ArrowDown");
    return;
  }
  if (k == VK_ESCAPE) {
    snprintf(out, max, "Escape");
    return;
  }
  if (k == VK_F11) {
    snprintf(out, max, "F11");
    return;
  }
  if (k >= VK_F1 && k <= VK_F12) {
    snprintf(out, max, "F%d", (int)(k - VK_F1 + 1));
    return;
  }
  if (k >= 'A' && k <= 'Z') {
    snprintf(out, max, "%c", (char)(k - 'A' + 'a'));
    return;
  }
  if (k >= '0' && k <= '9') {
    snprintf(out, max, "%c", (char)k);
    return;
  }
  snprintf(out, max, "#%u", (unsigned)k);
}

static int name_vk(const char *s) {
  if (!s || !s[0]) return 0;
  if (!strcmp(s, "Enter") || !strcmp(s, "enter")) return VK_RETURN;
  if (!strcmp(s, " ") || !strcmp(s, "Space") || !strcmp(s, "space")) return VK_SPACE;
  if (!strcmp(s, "ArrowLeft")) return VK_LEFT;
  if (!strcmp(s, "ArrowRight")) return VK_RIGHT;
  if (!strcmp(s, "ArrowUp")) return VK_UP;
  if (!strcmp(s, "ArrowDown")) return VK_DOWN;
  if (!strcmp(s, "Escape")) return VK_ESCAPE;
  if (!strcmp(s, "F11")) return VK_F11;
  if (s[0] == 'F' && s[1] >= '1' && s[1] <= '9') {
    int n = atoi(s + 1);
    if (n >= 1 && n <= 12) return VK_F1 + n - 1;
  }
  if (s[0] == '#' ) return atoi(s + 1);
  if (s[1] == 0) {
    char c = s[0];
    if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
    if (c >= 'A' && c <= 'Z') return c;
    if (c >= '0' && c <= '9') return c;
  }
  return 0;
}

static int key_is(const char *name, WPARAM key) {
  int vk = name_vk(name);
  return vk && (WPARAM)vk == key;
}

static void hi_rgb(int *r, int *g, int *b) {
  *r = 37;
  *g = 99;
  *b = 235;
  if (!g_st) return;
  if (strcmp(g_st->highlight, "orange") == 0) {
    *r = 234;
    *g = 88;
    *b = 12;
  } else if (strcmp(g_st->highlight, "teal") == 0) {
    *r = 13;
    *g = 148;
    *b = 136;
  }
}

static void theme_rgb(int *r, int *g, int *b) {
  *r = 18;
  *g = 22;
  *b = 32;
  if (!g_st) return;
  if (strcmp(g_st->theme, "oled") == 0) {
    *r = *g = *b = 0;
  } else if (strcmp(g_st->theme, "graphite") == 0) {
    *r = 28;
    *g = 32;
    *b = 38;
  }
}

static int looks_like_url(const char *s) {
  if (!s || !s[0]) return 0;
  if (istarts(s, "http://") || istarts(s, "https://") || istarts(s, "file:")) return 1;
  if (s[0] == '/' || s[0] == '\\' || (s[0] && s[1] == ':')) return 1;
  return 0;
}

static void act_add(int id, const wchar_t *title) {
  if (g_act_n >= 8) return;
  g_act_id[g_act_n] = id;
  g_act_title[g_act_n] = title;
  g_act_n++;
}

static void close_act(void) {
  g_act = ACT_NONE;
  g_act_n = 0;
  g_act_cur = 0;
}

static void open_pl_act(int i) {
  if (!g_st || i < 0 || i >= g_st->pl_n) return;
  g_act = ACT_PL;
  g_act_i = i;
  g_act_cur = 0;
  g_act_n = 0;
  int current = strcmp(g_st->playlists[i].id, g_st->active_pl) == 0;
  if (!current) act_add(AID_USE, L"Включить");
  act_add(AID_PASTE, L"Заменить из буфера");
  act_add(AID_COPY, L"Копировать ссылку");
  act_add(AID_DEL, L"Удалить");
}

static void open_epg_act(int i) {
  if (!g_st || i < 0 || i >= g_st->epg_n) return;
  g_act = ACT_EPG;
  g_act_i = i;
  g_act_cur = 0;
  g_act_n = 0;
  int role = sources_epg_role(g_st, i);
  if (role != 1) act_add(AID_PRI, L"Основной источник");
  if (role != 2) act_add(AID_FB, L"Дополнительный источник");
  act_add(AID_PASTE, L"Заменить из буфера");
  act_add(AID_COPY, L"Копировать ссылку");
  act_add(AID_DEL, L"Удалить");
}

static int paste_into(char *out, int max) {
  if (!clipboard_read_utf8(out, max) || !out[0]) {
    set_note(L"Скопируйте ссылку и повторите");
    return 0;
  }
  if (!looks_like_url(out)) {
    set_note(i18n("noClipUrl"));
    return 0;
  }
  return 1;
}

static void apply_playlist(int i) {
  if (!g_st || !g_pl || i < 0 || i >= g_st->pl_n) return;
  set_note(i18n("plLoading"));
  if (g_menu) {
    InvalidateRect(g_menu, NULL, FALSE);
    UpdateWindow(g_menu);
  }
  if (!profile_fetch_playlist(g_pl, g_st->playlists[i].url)) {
    set_note(i18n("plFail"));
    return;
  }
  snprintf(g_st->active_pl, MAX_ID, "%s", g_st->playlists[i].id);
  profile_mark_playlist_fetched(g_st);
  profile_restore_lists(g_st, g_pl);
  profile_save_sources(g_st);
  persist_ui(0, 0, 0);
  rebuild_groups();
  remember_group(0);
  fill_group(0);
  int idx = 0;
  if (g_st->last_name[0]) {
    int found = playlist_find_name(g_pl, g_st->last_name);
    if (found >= 0) idx = found;
  }
  if (!player_alive() && g_host) player_start(g_host);
  if (g_zap) g_zap(idx);
  else player_play_channel(g_pl, g_st, idx);
  rec_load(g_pl, g_st);
  epg_start(g_pl, g_st);
  set_note(i18n("plOn"));
  if (g_menu_page == MENU_SETUP && g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void add_playlist_from(const char *url) {
  if (!g_st || !url) return;
  int empty = g_st->pl_n == 0;
  if (!sources_add_playlist(g_st, url)) {
    set_note(i18n("alreadyInList"));
    return;
  }
  profile_save_sources(g_st);
  set_note(i18n("plAdded"));
  if (empty) apply_playlist(g_st->pl_n - 1);
}

static void add_epg_from(const char *url) {
  if (!g_st || !url) return;
  if (!sources_add_epg(g_st, url)) {
    set_note(i18n("alreadyInList"));
    return;
  }
  profile_save_sources(g_st);
  set_note(L"Источник добавлен");
}

static void utf8_del_last(char *s) {
  int n = s ? (int)strlen(s) : 0;
  if (n <= 0) return;
  n--;
  while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}

static void url_close(void) {
  close_act();
  g_url_sel = 0;
  g_menu_page = g_url_back ? g_url_back : MENU_SETTINGS;
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void open_url_edit(int kind) {
  g_url_kind = kind;
  g_url_back = g_menu_page;
  g_url_btn = 0;
  g_url_sel = 0;
  g_url_edit[0] = 0;
  char clip[MAX_URL];
  if (clipboard_read_utf8(clip, MAX_URL) && looks_like_url(clip)) snprintf(g_url_edit, MAX_URL, "%s", clip);
  g_menu_page = MENU_URL;
  close_act();
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static int url_paste_field(void) {
  char clip[MAX_URL];
  if (!clipboard_read_utf8(clip, MAX_URL) || !clip[0]) {
    set_note(i18n("noClipUrl"));
    return 0;
  }
  snprintf(g_url_edit, MAX_URL, "%s", clip);
  g_url_sel = 0;
  g_url_btn = 0;
  set_note(i18n("urlPasted"));
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  return 1;
}

static void url_submit(void) {
  if (!g_url_edit[0]) {
    set_note(i18n("urlEmpty"));
    return;
  }
  if (!looks_like_url(g_url_edit)) {
    set_note(i18n("noClipUrl"));
    return;
  }
  if (g_url_kind == 0) {
    int n = g_st ? g_st->pl_n : 0;
    add_playlist_from(g_url_edit);
    if (g_st && g_st->pl_n > n) url_close();
    else if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  int n = g_st ? g_st->epg_n : 0;
  add_epg_from(g_url_edit);
  if (!g_st || g_st->epg_n <= n) {
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  if (g_url_kind == 2) sources_set_epg_role(g_st, g_st->epg_n - 1, 1);
  if (g_url_kind == 3) sources_set_epg_role(g_st, g_st->epg_n - 1, 2);
  profile_save_sources(g_st);
  persist_ui(0, 0, 1);
  if (g_pl) epg_reload(g_pl, g_st);
  url_close();
}

static void url_activate(void) {
  if (g_url_btn == 1) {
    url_paste_field();
    return;
  }
  if (g_url_btn == 2) {
    url_submit();
    return;
  }
  if (g_url_edit[0] && looks_like_url(g_url_edit)) url_submit();
  else url_paste_field();
}

static void open_url_field_menu(void) {
  g_act = ACT_URL;
  g_act_i = 0;
  g_act_cur = 0;
  g_act_n = 0;
  act_add(AID_PASTE, i18n("urlPaste"));
  act_add(AID_COPY, i18n("copy"));
  act_add(AID_SEL, i18n("selAll"));
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void open_search_field_menu(void) {
  g_act = ACT_SEARCH;
  g_act_i = 0;
  g_act_cur = 0;
  g_act_n = 0;
  act_add(AID_PASTE, i18n("urlPaste"));
  act_add(AID_COPY, i18n("copy"));
  act_add(AID_SEL, i18n("selAll"));
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void save_epg_change(int reload) {
  if (!g_st) return;
  profile_sync_epg(g_st);
  profile_save_sources(g_st);
  if (reload && g_pl) epg_reload(g_pl, g_st);
}

static void run_act(int which) {
  if (which < 0 || which >= g_act_n) return;
  int id = g_act_id[which];
  if (g_act == ACT_URL || g_act == ACT_SEARCH) {
    if (g_act == ACT_SEARCH) {
      if (id == AID_PASTE) {
        char clip[sizeof g_search];
        if (clipboard_read_utf8(clip, (int)sizeof clip) && clip[0]) {
          snprintf(g_search, sizeof g_search, "%s", clip);
          fill_search();
        }
      } else if (id == AID_COPY) {
        if (g_search[0]) {
          clipboard_write_utf8(g_search);
          set_note(L"Скопировано");
        }
      } else if (id == AID_SEL) {
        g_search_focus = 0;
        g_hit_i = -1;
      }
    } else if (id == AID_PASTE) url_paste_field();
    else if (id == AID_COPY) {
      if (g_url_edit[0]) {
        clipboard_write_utf8(g_url_edit);
        set_note(L"Ссылка скопирована");
      }
    } else if (id == AID_SEL) {
      g_url_sel = g_url_edit[0] ? 1 : 0;
      g_url_btn = 0;
    }
    close_act();
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  if (!g_st) return;
  int i = g_act_i;
  char clip[MAX_URL];
  clip[0] = 0;
  if (g_act == ACT_PL) {
    if (i < 0 || i >= g_st->pl_n) {
      close_act();
      return;
    }
    SavedLink *L = &g_st->playlists[i];
    if (id == AID_USE) {
      close_act();
      apply_playlist(i);
      return;
    }
    if (id == AID_PASTE) {
      if (!paste_into(clip, MAX_URL)) return;
      sources_replace_url(L, clip);
      profile_save_sources(g_st);
      if (strcmp(L->id, g_st->active_pl) == 0) apply_playlist(i);
      else set_note(L"Ссылка заменена");
      close_act();
      return;
    }
    if (id == AID_COPY) {
      clipboard_write_utf8(L->url);
      set_note(L"Ссылка скопирована");
      close_act();
      return;
    }
    if (id == AID_DEL) {
      int was_active = strcmp(L->id, g_st->active_pl) == 0;
      sources_remove_playlist(g_st, i);
      profile_save_sources(g_st);
      close_act();
      set_note(L"Плейлист удалён");
      if (was_active && g_st->pl_n) apply_playlist(0);
      return;
    }
  }
  if (g_act == ACT_EPG) {
    if (i < 0 || i >= g_st->epg_n) {
      close_act();
      return;
    }
    SavedLink *L = &g_st->epg_src[i];
    if (id == AID_PRI) {
      sources_set_epg_role(g_st, i, 1);
      save_epg_change(1);
      set_note(L"Основной источник выбран");
      close_act();
      return;
    }
    if (id == AID_FB) {
      sources_set_epg_role(g_st, i, 2);
      save_epg_change(1);
      set_note(sources_epg_role(g_st, i) == 2 ? L"Дополнительный источник выбран" : L"Это теперь основной — добавьте ещё ссылку");
      close_act();
      return;
    }
    if (id == AID_PASTE) {
      if (!paste_into(clip, MAX_URL)) return;
      sources_replace_url(L, clip);
      save_epg_change(1);
      set_note(L"Ссылка заменена");
      close_act();
      return;
    }
    if (id == AID_COPY) {
      clipboard_write_utf8(L->url);
      set_note(L"Ссылка скопирована");
      close_act();
      return;
    }
    if (id == AID_DEL) {
      sources_remove_epg(g_st, i);
      save_epg_change(1);
      set_note(i18n("srcRemoved"));
      close_act();
      return;
    }
  }
}

static void fill_set_rows(void) {
  g_sn = 0;
  if (!g_st) {
    add_row(SID_INFO, i18n("setNoSettings"), L"");
    return;
  }
  switch (g_set_tab) {
    case 0: {
      if (!g_st->pl_n) add_row(SID_INFO, i18n("setEmpty"), i18n("setNeedPl"));
      for (int i = 0; i < g_st->pl_n; i++) {
        wchar_t title[96], hint[96], name[64];
        utf8_to_wide(g_st->playlists[i].name[0] ? g_st->playlists[i].name : i18n_u("setPlaylist"), name, 64);
        int on = strcmp(g_st->playlists[i].id, g_st->active_pl) == 0;
        swprintf(title, 96, L"%s%s", name, on ? i18n("setActive") : L"");
        url_hint(g_st->playlists[i].url, hint, 90);
        add_row_i(SID_PL, title, hint, i);
      }
      add_row(SID_ADD_URL, i18n("setAddUrl"), i18n("setAddM3uClip"));
      add_row(SID_ADD_FILE, i18n("setAddM3u"), i18n("setAddM3uHint"));
      {
        wchar_t hrs[24], ua[64];
        swprintf(hrs, 24, L"%d %s", g_st->playlist_update_hours, i18n("hoursShort"));
        utf8_to_wide(g_st->user_agent, ua, 64);
        const wchar_t *sm = strcmp(g_st->channel_sort, "name") == 0 ? i18n("sortName")
                           : strcmp(g_st->channel_sort, "number") == 0 ? i18n("sortNumber")
                                                                      : i18n("sortPlaylist");
        add_row(SID_PLHOURS, i18n("setInterval"), hrs);
        add_row(SID_UA, i18n("setUa"), ua[0] ? ua : i18n("setPasteBuf"));
        add_row(SID_SORT, i18n("setSort"), sm);
        add_row(SID_XT_SRV, i18n("setXtSrv"), g_xt_server[0] ? i18n("setReady") : i18n("setPasteBuf"));
        add_row(SID_XT_USER, i18n("setXtUser"), g_xt_user[0] ? i18n("setReady") : i18n("setPasteBuf"));
        add_row(SID_XT_PASS, i18n("setXtPass"), g_xt_pass[0] ? i18n("setReady") : i18n("setPasteBuf"));
        add_row(SID_XT_ADD, i18n("setXtAdd"), i18n("setXtHint"));
      }
      add_row(SID_HEAD, i18n("showInGuide"), L"");
      add_row_i(SID_HIDEGRP, i18n("allChannels"), onoff(folder_shown(0)), 0);
      add_row_i(SID_HIDEGRP, i18n("favorites"), onoff(folder_shown(1)), 1);
      add_row_i(SID_HIDEGRP, i18n("recent"), onoff(folder_shown(2)), 2);
      if (g_pl) {
        char seen[MAX_GROUPS][MAX_NAME];
        int seen_n = 0;
        for (int i = 0; i < g_pl->count && g_sn < SET_ROW_MAX - 1; i++) {
          const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
          int dup = 0;
          for (int k = 0; k < seen_n; k++) {
            if (strcmp(seen[k], g) == 0) {
              dup = 1;
              break;
            }
          }
          if (dup) continue;
          snprintf(seen[seen_n++], MAX_NAME, "%s", g);
          wchar_t title[96];
          utf8_to_wide(g, title, 96);
          add_row_i(SID_HIDEGRP, title, onoff(folder_shown(seen_n + 2)), seen_n + 2);
        }
      }
      break;
    }
    case 1: {
      int pri = -1, fb = -1;
      for (int i = 0; i < g_st->epg_n; i++) {
        int role = sources_epg_role(g_st, i);
        if (role == 1 && pri < 0) pri = i;
        if (role == 2 && fb < 0) fb = i;
      }
      if (pri >= 0) {
        wchar_t hint[96];
        url_hint(g_st->epg_src[pri].url, hint, 90);
        add_row_i(SID_EPG, i18n("setEpgMain"), hint, pri);
      } else {
        add_row(SID_ADD_PRI, i18n("setEpgMain"), i18n("setEpgPasteOk"));
      }
      if (fb >= 0) {
        wchar_t hint[96];
        url_hint(g_st->epg_src[fb].url, hint, 90);
        add_row_i(SID_EPG, i18n("setEpgExtra"), hint, fb);
      } else {
        add_row(SID_ADD_FB, i18n("setEpgExtra"), i18n("setEpgPasteOk"));
      }
      for (int i = 0; i < g_st->epg_n; i++) {
        if (i == pri || i == fb) continue;
        wchar_t title[96], hint[96], name[64];
        utf8_to_wide(g_st->epg_src[i].name[0] ? g_st->epg_src[i].name : i18n_u("setSource"), name, 64);
        swprintf(title, 96, L"%s", name);
        url_hint(g_st->epg_src[i].url, hint, 90);
        add_row_i(SID_EPG, title, hint, i);
      }
      add_row(SID_ADD_URL, i18n("setAddUrl"), i18n("setAddXmltv"));
      add_row(SID_LOAD_EPG, i18n("setLoadEpg"), (g_st->epg_n || g_st->epg_url[0]) ? i18n("setLoadEpgHint") : i18n("setNeedEpg"));
      {
        wchar_t off[24], days[24], hrs[24];
        swprintf(off, 24, L"%+d %s", g_st->epg_offset_hours, i18n("hoursShort"));
        swprintf(days, 24, L"%d %s", g_st->epg_days, i18n("daysShort"));
        swprintf(hrs, 24, L"%d %s", g_st->epg_update_hours, i18n("hoursShort"));
        add_row(SID_OFFSET, i18n("setOffset"), off);
        add_row(SID_EPGDAYS, i18n("setEpgDays"), days);
        add_row(SID_EPGAUTO, i18n("setEpgAuto"), onoff(g_st->epg_auto_update));
        add_row(SID_EPGHOURS, i18n("setInterval"), hrs);
        add_row(SID_DESC, i18n("setDesc"), onoff(g_st->show_program_desc));
      }
      break;
    }
    case 2: {
      wchar_t panel[24], rows[24], cop[24];
      swprintf(panel, 24, L"%d %%", g_st->panel_opacity);
      swprintf(rows, 24, L"%d", g_st->guide_rows);
      swprintf(cop, 24, L"%d %%", g_st->clock_opacity);
      const wchar_t *theme = strcmp(g_st->theme, "oled") == 0 ? i18n("themeOled")
                             : strcmp(g_st->theme, "graphite") == 0 ? i18n("themeGraphite")
                                                                   : i18n("themeDark");
      const wchar_t *hi = strcmp(g_st->highlight, "orange") == 0 ? i18n("accentOrange")
                          : strcmp(g_st->highlight, "teal") == 0   ? i18n("accentTeal")
                                                                  : i18n("accentBlue");
      const wchar_t *fsz = strcmp(g_st->font_size, "sm") == 0 ? i18n("fontSm")
                           : strcmp(g_st->font_size, "lg") == 0 ? i18n("fontLg")
                                                               : i18n("fontMd");
      const wchar_t *lsz = strcmp(g_st->logo_size, "sm") == 0 ? i18n("logoSm")
                           : strcmp(g_st->logo_size, "lg") == 0 ? i18n("logoLg")
                                                               : i18n("logoMd");
      const wchar_t *cpos = strcmp(g_st->clock_pos, "top-left") == 0       ? i18n("clockTL")
                            : strcmp(g_st->clock_pos, "bottom-left") == 0  ? i18n("clockBL")
                            : strcmp(g_st->clock_pos, "bottom-right") == 0 ? i18n("clockBR")
                                                                          : i18n("clockTR");
      const wchar_t *csz = strcmp(g_st->clock_size, "sm") == 0 ? i18n("clockSm")
                           : strcmp(g_st->clock_size, "lg") == 0 ? i18n("clockLg")
                                                                : i18n("clockMd");
      add_row(SID_THEME, i18n("setTheme"), theme);
      add_row(SID_HI, i18n("setAccent"), hi);
      add_row(SID_PANEL, i18n("setPanel"), panel);
      add_row(SID_FONT, i18n("setFont"), fsz);
      add_row(SID_LOGO, i18n("setLogo"), lsz);
      add_row(SID_GROWS, i18n("setGuideRows"), rows);
      add_row(SID_TWOLINE, i18n("setTwoLines"), onoff(g_st->two_line_titles));
      add_row(SID_CLOCK, i18n("setClock"), onoff(g_st->clock_enabled));
      add_row(SID_CLOCKPOS, i18n("setClockPos"), cpos);
      add_row(SID_CLOCKSZ, i18n("setClockSize"), csz);
      add_row(SID_CLOCKOP, i18n("setClockOp"), cop);
      add_row(SID_NUMBERS, i18n("setNumbers"), onoff(g_st->show_numbers));
      add_row(SID_PROG, i18n("setProgress"), onoff(g_st->show_epg_progress));
      break;
    }
    case 3: {
      wchar_t buf[32], asp[32], aud[16];
      const wchar_t *dec = strcmp(g_st->decoder, "hardware") == 0 ? i18n("decoderHw")
                           : strcmp(g_st->decoder, "software") == 0 ? i18n("decoderSw")
                                                                   : i18n("decoderAuto");
      swprintf(buf, 32, L"%d %s", g_st->buffer_sec, i18n("secShort"));
      utf8_to_wide(g_st->aspect[0] ? g_st->aspect : "auto", asp, 32);
      utf8_to_wide(g_st->audio[0] ? g_st->audio : "ru", aud, 16);
      add_row(SID_AUTOPLAY, i18n("setAutoplay"), onoff(g_st->autoplay));
      add_row(SID_REMVOL, i18n("setRememberVol"), onoff(g_st->remember_volume));
      add_row(SID_CONFIRM, i18n("setConfirmExit"), onoff(g_st->confirm_exit));
      add_row(SID_VOICE, i18n("setMic"), onoff(g_st->voice_enabled));
      add_row(SID_DECODER, i18n("setDecoder"), dec);
      add_row(SID_BUFFER, i18n("setBuffer"), buf);
      add_row(SID_ASPECT, i18n("setAspect"), asp);
      add_row(SID_EXTPL, i18n("setExternal"), onoff(g_st->external_player));
      add_row(SID_AUDIO, i18n("setAudioLang"), aud);
      break;
    }
    case 4: {
      wchar_t path[80];
      utf8_to_wide(g_st->rec_path, path, 80);
      add_row(SID_REC, i18n("setRec"), onoff(g_st->recordings_enabled));
      add_row(SID_RECPATH, i18n("setRecPath"), path[0] ? path : i18n("setPickFolder"));
      add_row(SID_INFO, i18n("setFilesTimer"), i18n("setRecHint"));
      break;
    }
    case 5: {
      wchar_t days[24];
      swprintf(days, 24, L"%d %s", g_st->archive_days, i18n("daysShort"));
      add_row(SID_ARCHIVE, i18n("archive"), onoff(g_st->archive_enabled));
      add_row(SID_ADAYS, i18n("setArchiveDays"), days);
      break;
    }
    case 6: {
      add_row(SID_PARENTAL, i18n("setParental"), onoff(g_st->parental_enabled));
      add_row(SID_LOCKSET, i18n("setLockSettings"), onoff(g_st->lock_settings));
      add_row(SID_PIN, i18n("setPin"), i18n("setPinHint"));
      if (g_pl) {
        char seen[MAX_GROUPS][MAX_NAME];
        int seen_n = 0;
        for (int i = 0; i < g_pl->count && g_sn < SET_ROW_MAX - 1; i++) {
          const char *g = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
          int dup = 0;
          for (int k = 0; k < seen_n; k++) {
            if (strcmp(seen[k], g) == 0) {
              dup = 1;
              break;
            }
          }
          if (dup) continue;
          snprintf(seen[seen_n++], MAX_NAME, "%s", g);
          wchar_t title[96];
          utf8_to_wide(g, title, 96);
          add_row_i(SID_LOCKGRP, title, group_locked(g) ? i18n("setLocked") : i18n("setUnlocked"), seen_n - 1);
        }
      }
      break;
    }
    case 7: {
      wchar_t v[32];
      const wchar_t *wait = i18n("pressKey");
      utf8_to_wide(g_st->key_menu, v, 32);
      add_row_i(SID_KEY, i18n("keyMenu"), g_wait_key == 1 ? wait : v, 1);
      utf8_to_wide(g_st->key_mute, v, 32);
      add_row_i(SID_KEY, i18n("keyMute"), g_wait_key == 2 ? wait : v, 2);
      utf8_to_wide(g_st->key_fav, v, 32);
      if (!v[0]) wcscpy(v, L"Space");
      add_row_i(SID_KEY, i18n("keyFav"), g_wait_key == 3 ? wait : v, 3);
      utf8_to_wide(g_st->key_fs, v, 32);
      add_row_i(SID_KEY, i18n("keyFs"), g_wait_key == 4 ? wait : v, 4);
      utf8_to_wide(g_st->key_guide, v, 32);
      add_row_i(SID_KEY, i18n("keyGuide"), g_wait_key == 5 ? wait : v, 5);
      utf8_to_wide(g_st->key_search, v, 32);
      add_row_i(SID_KEY, i18n("keySearch"), g_wait_key == 6 ? wait : v, 6);
      utf8_to_wide(g_st->key_rec, v, 32);
      add_row_i(SID_KEY, i18n("keyRec"), g_wait_key == 7 ? wait : v, 7);
      utf8_to_wide(g_st->key_pip, v, 32);
      add_row_i(SID_KEY, i18n("keyPip"), g_wait_key == 8 ? wait : v, 8);
      utf8_to_wide(g_st->key_voice, v, 32);
      add_row_i(SID_KEY, i18n("keyVoice"), g_wait_key == 9 ? wait : v, 9);
      utf8_to_wide(g_st->key_live, v, 32);
      add_row_i(SID_KEY, i18n("keyLive"), g_wait_key == 10 ? wait : v, 10);
      break;
    }
    case 8:
      add_row(SID_LANG, i18n("setLang"), strcmp(g_st->language, "en") == 0 ? L"English" : L"Русский");
      break;
    case 9:
      add_row(SID_CLOUD_COPY, i18n("setCloudCopy"), i18n("setCloudHint"));
      add_row(SID_CLOUD_SHARE, i18n("setCloudShare"), i18n("setCloudShareHint"));
      add_row(SID_BACKUP_SHARE, i18n("setBackupShare"), i18n("setBackupShareHint"));
      add_row(SID_CLOUD_PASTE, i18n("setCloudPaste"), L"");
      add_row(SID_EXPORT, i18n("setExport"), i18n("setExportHint"));
      add_row(SID_IMPORT, i18n("setImport"), i18n("setImportHint"));
      add_row(SID_RESET, i18n("setReset"), i18n("setResetHint"));
      break;
    case 10: {
      wchar_t ver[48];
      swprintf(ver, 48, L"%s  %hs", i18n("setNativeWin"), MIREFIR_VERSION);
      add_row(SID_INFO, L"MirEfir", ver);
      if (update_has_file()) add_row(SID_UPDATE, i18n("update"), i18n("updateReadyHint"));
      break;
    }
    default:
      add_row(SID_INFO, L"Этот раздел", L"будет на следующем этапе");
      break;
  }
}

static void cycle_int(int *v, const int *opts, int n) {
  if (!v || !opts || n < 1) return;
  int i = 0;
  for (; i < n; i++) {
    if (*v == opts[i]) break;
  }
  *v = opts[(i + 1) % n];
}

static void activate_set_row(void) {
  if (!g_st || g_set_i < 0 || g_set_i >= g_sn) return;
  if (g_act) {
    run_act(g_act_cur);
    return;
  }
  int id = g_sid[g_set_i];
  static const int buf[] = {8, 15, 30, 60};
  static const int aday[] = {1, 3, 5, 7, 14};
  static const int eday[] = {1, 2, 3, 5, 7, 14};
  static const int ehrs[] = {3, 6, 12, 24};
  switch (id) {
    case SID_PL:
      open_pl_act(g_sidx[g_set_i]);
      return;
    case SID_EPG:
      open_epg_act(g_sidx[g_set_i]);
      return;
    case SID_ADD_URL:
    case SID_ADD_PRI:
    case SID_ADD_FB: {
      int kind = 0;
      if (g_set_tab != 0) kind = id == SID_ADD_PRI ? 2 : id == SID_ADD_FB ? 3 : 1;
      open_url_edit(kind);
      return;
    }
    case SID_ADD_FILE: {
      char path[MAX_URL];
      if (!pick_m3u_file(path, MAX_URL)) {
        if (g_host) SetFocus(g_host);
        set_note(L"Файл не выбран");
        break;
      }
      if (g_host) SetFocus(g_host);
      add_playlist_from(path);
      break;
    }
    case SID_LOAD_EPG:
      if (!g_st->epg_n && !g_st->epg_url[0]) {
        set_note(L"Укажите основной источник телепрограммы");
        break;
      }
      set_note(L"Загрузка телепрограммы…");
      if (g_menu) {
        InvalidateRect(g_menu, NULL, FALSE);
        UpdateWindow(g_menu);
      }
      if (g_pl) epg_reload(g_pl, g_st);
      set_note(L"Телепрограмма обновляется");
      break;
    case SID_CLOCK:
      g_st->clock_enabled = !g_st->clock_enabled;
      persist_ui(1, 0, 0);
      break;
    case SID_NUMBERS:
      g_st->show_numbers = !g_st->show_numbers;
      persist_ui(1, 0, 0);
      break;
    case SID_CONFIRM:
      g_st->confirm_exit = !g_st->confirm_exit;
      persist_ui(0, 0, 0);
      break;
    case SID_VOICE:
      g_st->voice_enabled = !g_st->voice_enabled;
      persist_ui(0, 0, 0);
      break;
    case SID_ARCHIVE:
      g_st->archive_enabled = !g_st->archive_enabled;
      persist_ui(0, 0, 1);
      break;
    case SID_DESC:
      g_st->show_program_desc = !g_st->show_program_desc;
      persist_ui(0, 0, 0);
      break;
    case SID_EPGAUTO:
      g_st->epg_auto_update = !g_st->epg_auto_update;
      persist_ui(0, 0, 1);
      break;
    case SID_DECODER: {
      static const char *opts[] = {"auto", "hardware", "software"};
      cycle_str(g_st->decoder, (int)sizeof g_st->decoder, opts, 3);
      persist_ui(0, 1, 0);
      break;
    }
    case SID_ASPECT: {
      static const char *opts[] = {"auto", "16:9", "4:3", "zoom"};
      cycle_str(g_st->aspect, (int)sizeof g_st->aspect, opts, 4);
      persist_ui(0, 1, 0);
      break;
    }
    case SID_BUFFER:
      cycle_int(&g_st->buffer_sec, buf, 4);
      persist_ui(0, 1, 0);
      break;
    case SID_ADAYS:
      cycle_int(&g_st->archive_days, aday, 5);
      persist_ui(0, 0, 1);
      break;
    case SID_EPGDAYS:
      cycle_int(&g_st->epg_days, eday, 6);
      persist_ui(0, 0, 1);
      break;
    case SID_EPGHOURS:
      cycle_int(&g_st->epg_update_hours, ehrs, 4);
      persist_ui(0, 0, 1);
      break;
    case SID_OFFSET:
      g_st->epg_offset_hours += 1;
      if (g_st->epg_offset_hours > 12) g_st->epg_offset_hours = -12;
      persist_ui(0, 0, 0);
      if (g_pl) epg_reload(g_pl, g_st);
      break;
    case SID_UA:
      if (!clipboard_read_utf8(g_st->user_agent, MAX_UA) || !g_st->user_agent[0]) {
        set_note(i18n("uaNeed"));
        break;
      }
      persist_ui(0, 1, 0);
      set_note(i18n("uaSaved"));
      break;
    case SID_PLHOURS: {
      static const int ph[] = {1, 3, 6, 12, 24};
      cycle_int(&g_st->playlist_update_hours, ph, 5);
      persist_ui(0, 0, 0);
      break;
    }
    case SID_SORT: {
      static const char *opts[] = {"playlist", "name", "number"};
      cycle_str(g_st->channel_sort, (int)sizeof g_st->channel_sort, opts, 3);
      persist_ui(0, 0, 0);
      fill_group(g_group_i);
      break;
    }
    case SID_XT_SRV:
      if (!paste_into(g_xt_server, MAX_URL)) break;
      set_note(L"Сервер Xtream задан");
      break;
    case SID_XT_USER:
      if (!clipboard_read_utf8(g_xt_user, (int)sizeof g_xt_user) || !g_xt_user[0]) {
        set_note(L"Скопируйте логин и повторите");
        break;
      }
      set_note(L"Логин задан");
      break;
    case SID_XT_PASS:
      if (!clipboard_read_utf8(g_xt_pass, (int)sizeof g_xt_pass) || !g_xt_pass[0]) {
        set_note(L"Скопируйте пароль и повторите");
        break;
      }
      set_note(L"Пароль задан");
      break;
    case SID_XT_ADD: {
      if (!g_xt_server[0] || !g_xt_user[0] || !g_xt_pass[0]) {
        set_note(L"Сначала сервер, логин и пароль");
        break;
      }
      char base[MAX_URL];
      snprintf(base, MAX_URL, "%s", g_xt_server);
      int bl = (int)strlen(base);
      while (bl > 0 && (base[bl - 1] == '/' || base[bl - 1] == '\\')) base[--bl] = 0;
      char u[192], p[192], url[MAX_URL];
      urlenc(g_xt_user, u, (int)sizeof u);
      urlenc(g_xt_pass, p, (int)sizeof p);
      snprintf(url, MAX_URL, "%s/get.php?username=%s&password=%s&type=m3u_plus", base, u, p);
      add_playlist_from(url);
      break;
    }
    case SID_THEME: {
      static const char *opts[] = {"dark", "oled", "graphite"};
      cycle_str(g_st->theme, (int)sizeof g_st->theme, opts, 3);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_HI: {
      static const char *opts[] = {"blue", "orange", "teal"};
      cycle_str(g_st->highlight, (int)sizeof g_st->highlight, opts, 3);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_PANEL: {
      static const int op[] = {70, 80, 90, 100};
      cycle_int(&g_st->panel_opacity, op, 4);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_FONT: {
      static const char *opts[] = {"sm", "md", "lg"};
      cycle_str(g_st->font_size, (int)sizeof g_st->font_size, opts, 3);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_LOGO: {
      static const char *opts[] = {"sm", "md", "lg"};
      cycle_str(g_st->logo_size, (int)sizeof g_st->logo_size, opts, 3);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_GROWS:
      g_st->guide_rows += 1;
      if (g_st->guide_rows > 9) g_st->guide_rows = 4;
      persist_ui(1, 0, 0);
      break;
    case SID_TWOLINE:
      g_st->two_line_titles = !g_st->two_line_titles;
      persist_ui(1, 0, 0);
      break;
    case SID_CLOCKPOS: {
      static const char *opts[] = {"top-right", "top-left", "bottom-left", "bottom-right"};
      cycle_str(g_st->clock_pos, (int)sizeof g_st->clock_pos, opts, 4);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_CLOCKSZ: {
      static const char *opts[] = {"sm", "md", "lg"};
      cycle_str(g_st->clock_size, (int)sizeof g_st->clock_size, opts, 3);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_CLOCKOP: {
      static const int op[] = {60, 75, 90, 100};
      cycle_int(&g_st->clock_opacity, op, 4);
      persist_ui(1, 0, 0);
      break;
    }
    case SID_PROG:
      g_st->show_epg_progress = !g_st->show_epg_progress;
      persist_ui(1, 0, 0);
      break;
    case SID_AUTOPLAY:
      g_st->autoplay = !g_st->autoplay;
      persist_ui(0, 0, 0);
      break;
    case SID_REMVOL:
      g_st->remember_volume = !g_st->remember_volume;
      persist_ui(0, 0, 0);
      break;
    case SID_EXTPL:
      g_st->external_player = !g_st->external_player;
      persist_ui(0, 1, 0);
      if (g_st->external_player) {
        int ch = player_current_index();
        if (g_zap && ch >= 0) g_zap(ch);
        set_note(i18n("extOn"));
      } else {
        int ch = player_current_index();
        if (g_zap && ch >= 0) g_zap(ch);
        set_note(i18n("extOff"));
      }
      break;
    case SID_AUDIO: {
      static const char *opts[] = {"ru", "en", "orig"};
      cycle_str(g_st->audio, (int)sizeof g_st->audio, opts, 3);
      persist_ui(0, 1, 0);
      break;
    }
    case SID_REC:
      g_st->recordings_enabled = !g_st->recordings_enabled;
      persist_ui(0, 0, 0);
      break;
    case SID_RECPATH: {
      char path[MAX_PATHU];
      if (!pick_folder(path, MAX_PATHU)) {
        if (g_host) SetFocus(g_host);
        if (g_st->rec_path[0]) {
          g_st->rec_path[0] = 0;
          persist_ui(0, 0, 0);
          set_note(L"Папка сброшена");
        } else set_note(i18n("folderNone"));
        break;
      }
      if (g_host) SetFocus(g_host);
      snprintf(g_st->rec_path, MAX_PATHU, "%s", path);
      g_st->recordings_enabled = 1;
      persist_ui(0, 0, 0);
      {
        int err = rec_path_error(g_st->rec_path);
        if (err == -3) set_note(i18n("recNoSpace"));
        else if (err == -4) set_note(i18n("recNoDisk"));
        else if (err == -5) set_note(i18n("recNoPerm"));
        else set_note(i18n("folderSaved"));
      }
      break;
    }
    case SID_PARENTAL:
      g_st->parental_enabled = !g_st->parental_enabled;
      if (!g_st->parental_enabled) g_pin_ok = 1;
      persist_ui(0, 0, 0);
      break;
    case SID_LOCKSET:
      g_st->lock_settings = !g_st->lock_settings;
      persist_ui(0, 0, 0);
      break;
    case SID_PIN: {
      char pin[32];
      if (!clipboard_read_utf8(pin, (int)sizeof pin) || !pin[0]) {
        set_note(L"Скопируйте PIN и повторите");
        break;
      }
      int n = 0;
      char clean[MAX_PIN + 1];
      for (int i = 0; pin[i] && n < MAX_PIN; i++) {
        if (pin[i] >= '0' && pin[i] <= '9') clean[n++] = pin[i];
      }
      clean[n] = 0;
      if (n < 1) {
        set_note(i18n("pinDigitsOnly"));
        break;
      }
      snprintf(g_st->parental_pin, sizeof g_st->parental_pin, "%s", clean);
      persist_ui(0, 0, 0);
      set_note(i18n("pinSaved"));
      break;
    }
    case SID_LANG:
      snprintf(g_st->language, sizeof g_st->language, strcmp(g_st->language, "en") == 0 ? "ru" : "en");
      persist_ui(1, 0, 0);
      rebuild_groups();
      fill_group(g_group_i);
      break;
    case SID_CLOUD_COPY:
      if (cloud_copy_code(g_st, g_pl)) set_note(i18n("codeCopied"));
      else set_note(i18n("codeCopyFail"));
      break;
    case SID_CLOUD_SHARE:
      open_share(0);
      break;
    case SID_BACKUP_SHARE:
      open_share(1);
      break;
    case SID_CLOUD_PASTE: {
      char *code = (char *)malloc(65536);
      if (!code) break;
      if (!clipboard_read_utf8_full(code, 65536) || !code[0]) {
        free(code);
        set_note(i18n("codeNeed"));
        break;
      }
      set_note(i18n("codeApply"));
      if (!cloud_apply_code(g_st, g_pl, code)) {
        free(code);
        set_note(i18n("codeBad"));
        break;
      }
      free(code);
      rebuild_groups();
      remember_group(0);
      fill_group(0);
      if (g_zap) g_zap(0);
      if (g_pl) epg_start(g_pl, g_st);
      persist_ui(1, 1, 1);
      set_note(i18n("codeOk"));
      break;
    }
    case SID_EXPORT:
      do_backup_export();
      break;
    case SID_IMPORT:
      do_backup_import();
      break;
    case SID_RESET:
      profile_reset_settings(g_st);
      persist_ui(1, 1, 1);
      set_note(L"Настройки сброшены");
      break;
    case SID_UPDATE:
      if (g_st) profile_save_session(g_st, player_current_index(), g_st->last_name);
      persist_ui(0, 0, 0);
      if (!update_apply(g_host)) set_note(L"Не удалось установить обновление");
      return;
    case SID_KEY:
      g_wait_key = g_sidx[g_set_i];
      set_note(L"Нажмите кнопку на пульте…");
      break;
    case SID_HEAD:
      return;
    case SID_HIDEGRP: {
      toggle_hidden_folder(g_sidx[g_set_i]);
      persist_ui(0, 0, 0);
      rebuild_groups();
      break;
    }
    case SID_LOCKGRP: {
      char g[MAX_NAME];
      if (!pl_group_nth(g_sidx[g_set_i], g, MAX_NAME)) break;
      toggle_name_list(g_st->locked, &g_st->locked_n, MAX_HIDDEN, g);
      persist_ui(0, 0, 0);
      break;
    }
    default:
      break;
  }
}

static void paint_stub(HDC mem, RECT rc) {
  int mw = g_menu_w > 0 ? g_menu_w : 400;
  if (mw < 420) mw = 420;
  RECT panel = {0, 0, mw, rc.bottom};
  int tr, tg, tb;
  theme_rgb(&tr, &tg, &tb);
  HBRUSH bg = CreateSolidBrush(RGB(tr, tg, tb));
  FillRect(mem, &panel, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  RECT t = {24, 24, mw - 24, 70};
  int idx = g_stub_i;
  if (idx < 0 || idx >= MENU_N) idx = 0;
  DrawTextW(mem, menu_title(idx), -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
  t.top = 72;
  t.bottom = 110;
  SetTextColor(mem, RGB(160, 170, 185));
  SelectObject(mem, g_font_sm);
  DrawTextW(mem, menu_hint(idx), -1, &t, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
  RECT body = {24, 130, mw - 24, rc.bottom - 80};
  SetTextColor(mem, RGB(230, 230, 230));
  SelectObject(mem, g_font);
  DrawTextW(mem, L"Этот раздел будет на следующем этапе.\nНазад — вернёт в меню.", -1, &body,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static int set_row_selectable(int i) { return i >= 0 && i < g_sn && g_sid[i] != SID_HEAD; }

static int set_row_step(int i) {
  if (i < 0 || i >= g_sn) return SET_ROW_H;
  if (g_sid[i] == SID_HEAD) return SET_HEAD_GAP + SET_HEAD_H;
  return SET_ROW_H;
}

static void set_move(int d) {
  if (!g_sn) return;
  int i = g_set_i;
  for (int n = 0; n < g_sn; n++) {
    i = (i + g_sn + d) % g_sn;
    if (set_row_selectable(i)) {
      g_set_i = i;
      return;
    }
  }
}

static void draw_toggle_mark(HDC hdc, RECT rc, int on) {
  RECT pill = {rc.right - 78, (rc.top + rc.bottom) / 2 - 12, rc.right - 16, (rc.top + rc.bottom) / 2 + 12};
  int r = 40, g = 46, b = 58;
  if (on) hi_rgb(&r, &g, &b);
  fill_round_rect(hdc, pill, RGB(r, g, b));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, on ? RGB(255, 255, 255) : RGB(160, 168, 180));
  SelectObject(hdc, g_font_sm);
  DrawTextW(hdc, onoff(on), -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static int menu_panel_w(int client_w) {
  if (client_w < 1) client_w = 1;
  int mw = client_w * 86 / 100;
  if (mw > 820) mw = 820;
  if (mw < 560) mw = client_w;
  if (mw > client_w) mw = client_w;
  if (mw < 1) mw = 1;
  return mw;
}

static int settings_nav_w(int panel_w) {
  int tw = 220;
  if (panel_w < 640) tw = panel_w * 36 / 100;
  if (tw < 112) tw = panel_w > 180 ? 112 : panel_w / 2;
  if (panel_w > 160 && tw > panel_w - 96) tw = panel_w - 96;
  if (tw < 64) tw = panel_w > 2 ? panel_w / 2 : 64;
  return tw;
}

static int settings_tab_h(int client_h) {
  int tab_h = 40;
  int need = 60 + SET_TAB_N * tab_h;
  if (client_h > 80 && need > client_h - 8) {
    tab_h = (client_h - 68) / SET_TAB_N;
    if (tab_h < 22) tab_h = 22;
  }
  return tab_h;
}

static void paint_settings(HDC mem, RECT rc) {
  int mw = menu_panel_w(rc.right);
  int tr, tg, tb, hr, hg, hb;
  theme_rgb(&tr, &tg, &tb);
  hi_rgb(&hr, &hg, &hb);
  RECT panel = {0, 0, mw, rc.bottom};
  HBRUSH bg = CreateSolidBrush(RGB(tr, tg, tb));
  FillRect(mem, &panel, bg);
  DeleteObject(bg);
  int tw = settings_nav_w(mw);
  int tab_h = settings_tab_h(rc.bottom);
  RECT nav = {0, 0, tw, rc.bottom};
  HBRUSH nb = CreateSolidBrush(RGB(tr * 3 / 4, tg * 3 / 4, tb * 3 / 4));
  FillRect(mem, &nav, nb);
  DeleteObject(nb);
  SetBkMode(mem, TRANSPARENT);
  RECT head = {16, 16, tw - 12, 52};
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  DrawTextW(mem, i18n("backMenu"), -1, &head, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  int y = 60;
  for (int i = 0; i < SET_TAB_N; i++) {
    RECT item = {0, y, tw, y + tab_h};
    int hot = g_set_pane == 0 && i == g_set_tab;
    int on = i == g_set_tab;
    if (hot) {
      HBRUSH b = CreateSolidBrush(RGB(hr, hg, hb));
      FillRect(mem, &item, b);
      DeleteObject(b);
    } else if (on) {
      HBRUSH b = CreateSolidBrush(RGB(32, 40, 54));
      FillRect(mem, &item, b);
      DeleteObject(b);
    }
    RECT tr = item;
    tr.left += 16;
    SetTextColor(mem, RGB(255, 255, 255));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, set_tab(i), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += tab_h;
  }
  RECT cap = {tw + 24, 18, mw - 24, 56};
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  DrawTextW(mem, set_tab(g_set_tab), -1, &cap, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  fill_set_rows();
  if (g_set_i >= g_sn) g_set_i = g_sn ? g_sn - 1 : 0;
  if (g_set_i < 0) g_set_i = 0;
  if (!set_row_selectable(g_set_i)) set_move(1);
  int note_h = (g_set_note[0] && GetTickCount() - g_set_note_at < 5000) ? 36 : 0;
  int area = rc.bottom - 70 - note_h - 8;
  if (area < SET_ROW_H) area = SET_ROW_H;
  if (g_set_i < g_set_scroll) g_set_scroll = g_set_i;
  for (;;) {
    int used = 0, last = g_set_scroll;
    for (int i = g_set_scroll; i < g_sn; i++) {
      int step = set_row_step(i);
      if (used && used + step > area) break;
      used += step;
      last = i;
    }
    if (g_set_i <= last) break;
    g_set_scroll++;
    if (g_set_scroll > g_set_i) {
      g_set_scroll = g_set_i;
      break;
    }
  }
  if (g_set_scroll < 0) g_set_scroll = 0;
  y = 70;
  for (int i = g_set_scroll; i < g_sn && y < rc.bottom - note_h; i++) {
    int gap = g_sid[i] == SID_HEAD ? SET_HEAD_GAP : 0;
    int h = g_sid[i] == SID_HEAD ? SET_HEAD_H : SET_ROW_H;
    y += gap;
    RECT item = {tw, y, mw, y + h};
    int hot = g_act == ACT_NONE && g_set_pane == 1 && i == g_set_i && set_row_selectable(i);
    if (g_sid[i] == SID_HEAD) {
      SetBkMode(mem, TRANSPARENT);
      SetTextColor(mem, RGB(180, 190, 205));
      SelectObject(mem, g_font_sm);
      RECT t = item;
      t.left += 20;
      t.right -= 16;
      DrawTextW(mem, g_stitle[i], -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (g_sid[i] == SID_HIDEGRP) {
      draw_item(mem, &item, hot, g_stitle[i], NULL);
      draw_toggle_mark(mem, item, wcscmp(g_sval[i], i18n("on")) == 0);
    } else {
      draw_item(mem, &item, hot, g_stitle[i], g_sval[i][0] ? g_sval[i] : NULL);
    }
    y += h;
  }
  if (note_h) {
    RECT nr = {tw + 20, rc.bottom - note_h - 8, mw - 16, rc.bottom - 8};
    SetTextColor(mem, RGB(250, 204, 21));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, g_set_note, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  if (g_act && g_act_n) {
    int pane = mw - tw;
    if (pane < 40) pane = 40;
    int aw = 380;
    if (aw > pane - 12) aw = pane - 12;
    if (aw < 80) aw = pane > 8 ? pane - 8 : pane;
    int ah = 16 + g_act_n * 44;
    int ax = tw + (pane - aw) / 2;
    if (ax < tw) ax = tw;
    int ay = 88;
    if (ay + ah > rc.bottom - 12) ay = rc.bottom - ah - 12;
    if (ay < 48) ay = 48;
    if (ay + ah > rc.bottom && rc.bottom > ay) ah = rc.bottom - ay;
    g_act_x = ax;
    g_act_y = ay;
    g_act_w = aw;
    g_act_row = 44;
    RECT box = {ax, ay, ax + aw, ay + ah};
    HBRUSH dim = CreateSolidBrush(RGB(8, 10, 16));
    FillRect(mem, &box, dim);
    DeleteObject(dim);
    HBRUSH edge = CreateSolidBrush(RGB(50, 62, 82));
    FrameRect(mem, &box, edge);
    DeleteObject(edge);
    for (int i = 0; i < g_act_n; i++) {
      RECT item = {ax, ay + 8 + i * 44, ax + aw, ay + 8 + (i + 1) * 44};
      draw_item(mem, &item, i == g_act_cur, g_act_title[i], NULL);
    }
  }
}

static int search_like(void) {
  return g_menu_page == MENU_SEARCH || g_menu_page == MENU_MICPERM || (g_menu_page == MENU_MULTI && g_mv_pick);
}

static void fill_round_rect(HDC mem, RECT r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  HPEN p = CreatePen(PS_SOLID, 1, c);
  HGDIOBJ ob = SelectObject(mem, b);
  HGDIOBJ op = SelectObject(mem, p);
  RoundRect(mem, r.left, r.top, r.right, r.bottom, 16, 16);
  SelectObject(mem, ob);
  SelectObject(mem, op);
  DeleteObject(b);
  DeleteObject(p);
}

static void paint_mic_icon(HDC mem, RECT r, COLORREF fg) {
  int cx = (r.left + r.right) / 2;
  int cy = (r.top + r.bottom) / 2 - 1;
  HPEN p = CreatePen(PS_SOLID, 2, fg);
  HBRUSH b = CreateSolidBrush(fg);
  HGDIOBJ op = SelectObject(mem, p);
  HGDIOBJ ob = SelectObject(mem, b);
  Ellipse(mem, cx - 5, cy - 11, cx + 6, cy + 2);
  SelectObject(mem, GetStockObject(NULL_BRUSH));
  Arc(mem, cx - 10, cy - 6, cx + 11, cy + 12, cx + 11, cy + 2, cx - 10, cy + 2);
  MoveToEx(mem, cx, cy + 12, NULL);
  LineTo(mem, cx, cy + 16);
  MoveToEx(mem, cx - 7, cy + 16, NULL);
  LineTo(mem, cx + 8, cy + 16);
  SelectObject(mem, op);
  SelectObject(mem, ob);
  DeleteObject(p);
  DeleteObject(b);
}

static int mv_opt_n(void) { return 4; }

static const wchar_t *mv_opt_lab(int i) {
  if (i == 0) return i18n("mvAdd");
  if (i == 1) return i18n("mvFind");
  if (i == 2) return i18n("mvChange");
  return i18n("mvDel");
}

static void paint_search_card(HDC mem, RECT rc) {
  int hr, hg, hb;
  hi_rgb(&hr, &hg, &hb);
  HBRUSH dim = CreateSolidBrush(RGB(6, 8, 12));
  FillRect(mem, &rc, dim);
  DeleteObject(dim);
  int cw = rc.right - 80;
  if (cw > 720) cw = 720;
  if (cw < 380) cw = rc.right > 40 ? rc.right - 32 : rc.right;
  int ch = rc.bottom - 80;
  if (ch > 560) ch = 560;
  if (ch < 240) ch = rc.bottom > 40 ? rc.bottom - 24 : rc.bottom;
  int cx = (rc.right - cw) / 2;
  int cy = (rc.bottom - ch) / 2;
  if (cy < 20) cy = 20;
  RECT card = {cx, cy, cx + cw, cy + ch};
  g_search_card = card;
  fill_round_rect(mem, card, RGB(16, 21, 30));
  HPEN edge = CreatePen(PS_SOLID, 1, RGB(48, 56, 72));
  HGDIOBJ op = SelectObject(mem, edge);
  SelectObject(mem, GetStockObject(NULL_BRUSH));
  RoundRect(mem, card.left, card.top, card.right, card.bottom, 16, 16);
  SelectObject(mem, op);
  DeleteObject(edge);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  RECT cap = {card.left + 22, card.top + 14, card.right - 22, card.top + 46};
  DrawTextW(mem, i18n("searchTitle"), -1, &cap, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  int micw = 52;
  RECT inp = {card.left + 22, card.top + 56, card.right - 22 - micw - 10, card.top + 104};
  RECT mic = {inp.right + 10, inp.top, card.right - 22, inp.bottom};
  g_search_inp = inp;
  g_search_mic = mic;
  int in_hot = g_hit_i < 0 && g_search_focus == 0;
  int mic_hot = g_hit_i < 0 && g_search_focus == 1;
  fill_round_rect(mem, inp, RGB(8, 10, 16));
  HPEN ip = CreatePen(PS_SOLID, 2, in_hot ? RGB(hr, hg, hb) : RGB(42, 50, 64));
  op = SelectObject(mem, ip);
  SelectObject(mem, GetStockObject(NULL_BRUSH));
  RoundRect(mem, inp.left, inp.top, inp.right, inp.bottom, 14, 14);
  SelectObject(mem, op);
  DeleteObject(ip);
  wchar_t q[96];
  utf8_to_wide(g_search, q, 96);
  RECT it = inp;
  it.left += 14;
  it.right -= 10;
  SetTextColor(mem, q[0] ? RGB(255, 255, 255) : RGB(140, 150, 165));
  SelectObject(mem, g_font_sm);
  DrawTextW(mem, q[0] ? q : i18n("searchType"), -1, &it, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  COLORREF micbg = voice_busy() ? RGB(200, 48, 48) : (mic_hot ? RGB(hr, hg, hb) : RGB(40, 46, 58));
  fill_round_rect(mem, mic, micbg);
  paint_mic_icon(mem, mic, RGB(255, 255, 255));
  RECT note = {card.left + 22, inp.bottom + 8, card.right - 22, inp.bottom + 32};
  SetTextColor(mem, RGB(160, 190, 230));
  SelectObject(mem, g_font_sm);
  wchar_t hint[160];
  hint[0] = 0;
  if (voice_busy() && voice_status()[0]) utf8_to_wide(voice_status(), hint, 160);
  else if (g_voice_note[0]) utf8_to_wide(g_voice_note, hint, 160);
  if (hint[0]) DrawTextW(mem, hint, -1, &note, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  int y0 = hint[0] ? note.bottom + 6 : inp.bottom + 14;
  g_search_list_y = y0;
  if (g_menu_page == MENU_MICPERM) {
    RECT a = {card.left + 22, y0, card.right - 22, y0 + 48};
    RECT b = {card.left + 22, y0 + 54, card.right - 22, y0 + 102};
    RECT c = {card.left + 22, y0 + 108, card.right - 22, y0 + 156};
    paint_row(mem, a, g_mic_i == 0, i18n("micAllow"), i18n("micAllowHint"));
    paint_row(mem, b, g_mic_i == 1, i18n("micWin"), i18n("micWinHint"));
    paint_row(mem, c, g_mic_i == 2, i18n("micOffRow"), i18n("micOffHint"));
    return;
  }
  int row = 56, vis = (card.bottom - 16 - y0) / row;
  if (vis < 1) vis = 1;
  int s0 = g_hit_i < 0 ? 0 : g_hit_i - vis / 2;
  if (s0 < 0) s0 = 0;
  if (g_hit_n && s0 > g_hit_n - vis) s0 = g_hit_n - vis;
  if (s0 < 0) s0 = 0;
  if (!g_hit_n) {
    RECT empty = {card.left + 22, y0, card.right - 22, y0 + 40};
    SetTextColor(mem, RGB(160, 170, 185));
    DrawTextW(mem, g_search[0] ? i18n("noMatches") : i18n("searchHint"), -1, &empty, DT_LEFT | DT_TOP | DT_NOPREFIX);
    return;
  }
  for (int i = 0; i < vis && s0 + i < g_hit_n; i++) {
    int ch = g_hits[s0 + i];
    RECT item = {card.left + 10, y0 + i * row, card.right - 10, y0 + (i + 1) * row - 4};
    wchar_t title[220], sub[160];
    char line[MAX_NAME + 24], nowt[96];
    int num = g_pl->items[ch].number ? g_pl->items[ch].number : ch + 1;
    snprintf(line, sizeof line, "%d  %s", num, g_pl->items[ch].name);
    utf8_to_wide(line, title, 220);
    if (!epg_now(ch, nowt, (int)sizeof nowt, NULL, 0, NULL, NULL) || !nowt[0]) snprintf(nowt, sizeof nowt, "%s", i18n_u("liveAir"));
    utf8_to_wide(nowt, sub, 160);
    paint_row(mem, item, s0 + i == g_hit_i, title, sub);
  }
  if (g_act == ACT_SEARCH && g_act_n) {
    int aw = 320, ah = 16 + g_act_n * 44;
    int ax = card.left + 24, ay = card.top + 112;
    if (ay + ah > card.bottom - 8) ay = card.bottom - ah - 8;
    g_act_x = ax;
    g_act_y = ay;
    g_act_w = aw;
    g_act_row = 44;
    RECT box = {ax, ay, ax + aw, ay + ah};
    HBRUSH pop = CreateSolidBrush(RGB(8, 10, 16));
    FillRect(mem, &box, pop);
    DeleteObject(pop);
    HBRUSH edge = CreateSolidBrush(RGB(50, 62, 82));
    FrameRect(mem, &box, edge);
    DeleteObject(edge);
    for (int i = 0; i < g_act_n; i++) {
      RECT item = {ax, ay + 8 + i * 44, ax + aw, ay + 8 + (i + 1) * 44};
      draw_item(mem, &item, i == g_act_cur, g_act_title[i], NULL);
    }
  }
}

static void paint_multi_overlay(HDC mem, RECT rc) {
  HBRUSH bg = CreateSolidBrush(RGB(16, 21, 30));
  FillRect(mem, &rc, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(230, 235, 245));
  SelectObject(mem, g_font_sm);
  if (!g_mv_menu) {
    DrawTextW(mem, i18n("mvOkHint"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }
  int nopt = mv_opt_n();
  for (int i = 0; i < nopt; i++) {
    RECT item = {8, 10 + i * 48, rc.right - 8, 10 + (i + 1) * 48 - 4};
    paint_row(mem, item, i == g_mv_opt, mv_opt_lab(i), NULL);
  }
}

static void paint_extra_list(HDC mem, RECT rc, const wchar_t *title, const wchar_t *hint) {
  int tr, tg, tb, hr, hg, hb;
  theme_rgb(&tr, &tg, &tb);
  hi_rgb(&hr, &hg, &hb);
  int mw = g_menu_w > 0 ? g_menu_w : 480;
  if (mw < 420) mw = 420;
  if (mw > 640) mw = 640;
  RECT panel = {0, 0, mw, rc.bottom};
  HBRUSH bg = CreateSolidBrush(RGB(tr, tg, tb));
  FillRect(mem, &panel, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  RECT cap = {20, 16, mw - 20, 52};
  DrawTextW(mem, title, -1, &cap, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (hint) {
    SetTextColor(mem, RGB(160, 170, 185));
    SelectObject(mem, g_font_sm);
    RECT h = {20, 50, mw - 20, 78};
    DrawTextW(mem, hint, -1, &h, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
}

static void paint_row(HDC mem, RECT item, int hot, const wchar_t *title, const wchar_t *sub) {
  int hr, hg, hb;
  hi_rgb(&hr, &hg, &hb);
  if (hot) {
    HBRUSH b = CreateSolidBrush(RGB(hr, hg, hb));
    FillRect(mem, &item, b);
    DeleteObject(b);
  }
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font_sm);
  RECT a = item;
  a.left += 12;
  a.right -= 12;
  int h = item.bottom - item.top;
  int two = sub && sub[0] && h >= 44;
  a.top += two ? 3 : 0;
  a.bottom = two ? a.top + (h >= 52 ? 22 : 18) : item.bottom;
  DrawTextW(mem, title, -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  if (two) {
    SetTextColor(mem, hot ? RGB(230, 235, 255) : RGB(140, 150, 165));
    a.top = a.bottom + 1;
    a.bottom = item.bottom - 3;
    DrawTextW(mem, sub, -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
}

static void paint_extra(HDC mem, RECT rc) {
  int mw = g_menu_w > 0 ? g_menu_w : 480;
  if (mw < 420) mw = 420;
  if (mw > 640) mw = 640;
  if (g_menu_page == MENU_SEARCH) {
    wchar_t q[96];
    utf8_to_wide(g_search, q, 96);
    wchar_t hint[128];
    if (voice_busy() && voice_status()[0]) utf8_to_wide(voice_status(), hint, 128);
    else if (g_voice_note[0]) utf8_to_wide(g_voice_note, hint, 128);
    else
      swprintf(hint, 128, L"%s  ·  %d", q[0] ? q : i18n("searchType"), g_hit_n);
    paint_extra_list(mem, rc, i18n("search"), hint);
    int y = 88, row = 48, vis = (rc.bottom - y - 16) / row;
    if (vis < 1) vis = 1;
    int s0 = g_hit_i - vis / 2;
    if (s0 < 0) s0 = 0;
    if (g_hit_n && s0 > g_hit_n - vis) s0 = g_hit_n - vis;
    if (s0 < 0) s0 = 0;
    if (!g_hit_n) {
      RECT empty = {20, 100, mw - 20, 160};
      SetTextColor(mem, RGB(160, 170, 185));
      DrawTextW(mem, i18n("noMatches"), -1, &empty, DT_LEFT | DT_TOP | DT_NOPREFIX);
    }
    for (int i = 0; i < vis && s0 + i < g_hit_n; i++) {
      int ch = g_hits[s0 + i];
      RECT item = {8, y + i * row, mw - 8, y + (i + 1) * row - 4};
      wchar_t title[220], sub[160];
      char line[MAX_NAME + 24], nowt[96];
      int num = g_pl->items[ch].number ? g_pl->items[ch].number : ch + 1;
      snprintf(line, sizeof line, "%d  %s", num, g_pl->items[ch].name);
      utf8_to_wide(line, title, 220);
      if (!epg_now(ch, nowt, (int)sizeof nowt, NULL, 0, NULL, NULL) || !nowt[0]) snprintf(nowt, sizeof nowt, "%s", i18n_u("liveAir"));
      utf8_to_wide(nowt, sub, 160);
      paint_row(mem, item, s0 + i == g_hit_i, title, sub);
    }
    return;
  }
  if (g_menu_page == MENU_HISTORY) {
    paint_extra_list(mem, rc, i18n("history"), i18n("historyHint2"));
    int n = g_st ? g_st->recent_n : 0;
    if (n > MAX_RECENT) n = MAX_RECENT;
    int y = 88, row = 52;
    if (!n) {
      RECT empty = {20, 110, mw - 20, 200};
      SetTextColor(mem, RGB(160, 170, 185));
      SelectObject(mem, g_font_sm);
      DrawTextW(mem, i18n("historyEmpty"), -1, &empty, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    for (int i = 0; i < n; i++) {
      int ch = g_st->recent[i];
      RECT item = {8, y + i * row, mw - 8, y + (i + 1) * row - 4};
      RECT body = item;
      body.right -= 44;
      RECT del = {item.right - 44, item.top, item.right - 4, item.bottom};
      int on_row = i == g_hist_i;
      int on_del = on_row && g_hist_del;
      wchar_t title[220], sub[160];
      char nowt[96];
      utf8_to_wide(g_pl && ch >= 0 && ch < g_pl->count ? g_pl->items[ch].name : i18n_u("channel"), title, 220);
      if (!epg_now(ch, nowt, (int)sizeof nowt, NULL, 0, NULL, NULL) || !nowt[0]) snprintf(nowt, sizeof nowt, "%s", i18n_u("liveAir"));
      utf8_to_wide(nowt, sub, 160);
      paint_row(mem, body, on_row && !g_hist_del, title, sub);
      if (on_del) {
        int hr, hg, hb;
        hi_rgb(&hr, &hg, &hb);
        HBRUSH b = CreateSolidBrush(RGB(hr, hg, hb));
        FillRect(mem, &del, b);
        DeleteObject(b);
      }
      SetBkMode(mem, TRANSPARENT);
      SetTextColor(mem, on_del ? RGB(255, 255, 255) : RGB(170, 178, 190));
      SelectObject(mem, g_font);
      DrawTextW(mem, L"×", -1, &del, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (n) {
      RECT clr = {8, y + n * row, mw - 8, y + (n + 1) * row - 4};
      paint_row(mem, clr, g_hist_i == n, i18n("historyClear"), NULL);
    }
    RECT foot = {20, rc.bottom - 48, mw - 20, rc.bottom - 16};
    SetTextColor(mem, RGB(200, 205, 215));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, i18n("historyFoot"), -1, &foot, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }
  if (g_menu_page == MENU_RECS) {
    paint_extra_list(mem, rc, i18n("recordings"), i18n("recsHint"));
    int y = 88, row = 52;
    int tch = 0, stt = 0;
    char ttitle[96], tchname[MAX_NAME];
    long long ts = 0, te = 0;
    if (rec_timer_get(&tch, ttitle, 96, tchname, MAX_NAME, &ts, &te, &stt)) {
      wchar_t line[220], sub[160];
      utf8_to_wide(ttitle, line, 220);
      utf8_to_wide(tchname, sub, 160);
      RECT item = {8, y, mw - 8, y + row - 4};
      paint_row(mem, item, g_file_i == -1, line, stt == REC_RUN ? i18n("recRun") : i18n("recSched"));
      y += row;
    }
    if (!g_file_n) {
      RECT empty = {20, y + 8, mw - 20, y + 90};
      SetTextColor(mem, RGB(160, 170, 185));
      SelectObject(mem, g_font_sm);
      DrawTextW(mem, i18n("recsEmpty"), -1, &empty, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    int vis = (rc.bottom - y - 56) / row;
    if (vis < 1) vis = 1;
    int s0 = g_file_i < 0 ? 0 : g_file_i - vis / 2;
    if (s0 < 0) s0 = 0;
    if (g_file_n && s0 > g_file_n - vis) s0 = g_file_n - vis;
    for (int i = 0; i < vis && s0 + i < g_file_n; i++) {
      RecFile *f = &g_files[s0 + i];
      RECT item = {8, y + i * row, mw - 8, y + (i + 1) * row - 4};
      wchar_t title[220], sub[80];
      utf8_to_wide(f->title, title, 220);
      swprintf(sub, 80, L"%d %s", (int)(f->bytes / (1024 * 1024)), i18n("mb"));
      paint_row(mem, item, g_file_i == s0 + i, title, sub);
    }
    RECT foot = {20, rc.bottom - 48, mw - 20, rc.bottom - 16};
    SetTextColor(mem, RGB(200, 205, 215));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, i18n("recsFoot"), -1, &foot, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }
  if (g_menu_page == MENU_URL) {
    paint_extra_list(mem, rc, i18n("setAddUrl"), i18n("urlEditHint"));
    int hr, hg, hb;
    hi_rgb(&hr, &hg, &hb);
    RECT field = {20, 110, mw - 20, 170};
    g_url_field_rc = field;
    int field_hot = g_url_btn == 0 && !g_act;
    HBRUSH fb = CreateSolidBrush(g_url_sel ? RGB(hr, hg, hb) : RGB(8, 10, 16));
    FillRect(mem, &field, fb);
    DeleteObject(fb);
    if (field_hot && !g_url_sel) {
      HPEN p = CreatePen(PS_SOLID, 2, RGB(hr, hg, hb));
      HGDIOBJ op = SelectObject(mem, p);
      HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
      Rectangle(mem, field.left, field.top, field.right, field.bottom);
      SelectObject(mem, ob);
      SelectObject(mem, op);
      DeleteObject(p);
    }
    wchar_t wurl[MAX_URL];
    utf8_to_wide(g_url_edit, wurl, MAX_URL);
    RECT ft = {field.left + 12, field.top + 8, field.right - 12, field.bottom - 8};
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, g_url_sel ? RGB(255, 255, 255) : (wurl[0] ? RGB(240, 242, 248) : RGB(140, 150, 165)));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, wurl[0] ? wurl : i18n("urlEmpty"), -1, &ft,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (field_hot && wurl[0] && !g_url_sel) {
      SIZE sz;
      GetTextExtentPoint32W(mem, wurl, (int)wcslen(wurl), &sz);
      int cx = ft.left + sz.cx + 2;
      if (cx > ft.right - 2) cx = ft.right - 2;
      HPEN cp = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
      HGDIOBJ oc = SelectObject(mem, cp);
      MoveToEx(mem, cx, ft.top + 6, NULL);
      LineTo(mem, cx, ft.bottom - 6);
      SelectObject(mem, oc);
      DeleteObject(cp);
    }
    RECT paste = {20, 186, (mw / 2) - 8, 234};
    RECT openb = {(mw / 2) + 8, 186, mw - 20, 234};
    g_url_paste_rc = paste;
    g_url_ok_rc = openb;
    paint_row(mem, paste, g_url_btn == 1 && !g_act, i18n("urlPaste"), NULL);
    paint_row(mem, openb, g_url_btn == 2 && !g_act, i18n("urlOpen"), NULL);
    if (g_act == ACT_URL && g_act_n) {
      int aw = 320, ah = 16 + g_act_n * 44;
      int ax = (mw - aw) / 2, ay = 250;
      g_act_x = ax;
      g_act_y = ay;
      g_act_w = aw;
      g_act_row = 44;
      RECT box = {ax, ay, ax + aw, ay + ah};
      HBRUSH dim = CreateSolidBrush(RGB(8, 10, 16));
      FillRect(mem, &box, dim);
      DeleteObject(dim);
      HBRUSH edge = CreateSolidBrush(RGB(50, 62, 82));
      FrameRect(mem, &box, edge);
      DeleteObject(edge);
      for (int i = 0; i < g_act_n; i++) {
        RECT item = {ax, ay + 8 + i * 44, ax + aw, ay + 8 + (i + 1) * 44};
        draw_item(mem, &item, i == g_act_cur, g_act_title[i], NULL);
      }
    }
    return;
  }
  if (g_menu_page == MENU_SETUP) {
    paint_extra_list(mem, rc, i18n("welcome"), i18n("setupHint"));
    RECT a = {20, 110, mw - 20, 158};
    RECT b = {20, 166, mw - 20, 214};
    RECT c = {20, 222, mw - 20, 270};
    RECT d = {20, 278, mw - 20, 326};
    paint_row(mem, a, g_setup_i == 0, i18n("setupOpen"), i18n("setAddM3uClip"));
    paint_row(mem, b, g_setup_i == 1, i18n("setupFile"), i18n("setAddM3uHint"));
    paint_row(mem, c, g_setup_i == 2, i18n("setupEpg"), i18n("setAddXmltv"));
    paint_row(mem, d, g_setup_i == 3, i18n("setupClose"), i18n("setupCloseHint"));
    return;
  }
  if (g_menu_page == MENU_ASKREC) {
    const wchar_t *title = g_ask_why == 2 ? i18n("askBackup") : g_ask_why == 3 ? i18n("askRestore") : i18n("askRec");
    const wchar_t *hint = g_ask_why >= 2 ? i18n("askBackupHint") : i18n("askRecHint");
    const wchar_t *ih = g_ask_why >= 2 ? i18n("askBackupInternalHint") : i18n("askInternalHint");
    paint_extra_list(mem, rc, title, hint);
    RECT a = {20, 100, mw - 20, 148};
    RECT b = {20, 156, mw - 20, 204};
    paint_row(mem, a, g_ask_i == 0, i18n("askInternal"), ih);
    paint_row(mem, b, g_ask_i == 1, i18n("askDisk"), i18n("askDiskHint"));
    return;
  }
  if (g_menu_page == MENU_SHARE) {
    paint_extra_list(mem, rc, i18n(g_share_file ? "shareTitleFile" : "shareTitle"),
                     i18n(g_share_file ? "shareHintFile" : "shareHint"));
    for (int i = 0; i < g_share_n; i++) {
      RECT item = {20, 100 + i * 56, mw - 20, 148 + i * 56};
      int kind = g_share_kind[i];
      const wchar_t *lab = i18n("shareCopy");
      if (kind == 1) lab = i18n("shareTg");
      else if (kind == 2) lab = i18n("shareWa");
      else if (kind == 3) lab = i18n("shareMax");
      else if (kind == 4) lab = i18n("shareMail");
      paint_row(mem, item, i == g_share_i, lab, NULL);
    }
    return;
  }
  if (g_menu_page == MENU_MICPERM) {
    paint_extra_list(mem, rc, i18n("micNeed"), i18n("micNeedHint"));
    RECT a = {20, 100, mw - 20, 148};
    RECT b = {20, 156, mw - 20, 204};
    RECT c = {20, 212, mw - 20, 260};
    paint_row(mem, a, g_mic_i == 0, i18n("micAllow"), i18n("micAllowHint"));
    paint_row(mem, b, g_mic_i == 1, i18n("micWin"), i18n("micWinHint"));
    paint_row(mem, c, g_mic_i == 2, i18n("micOffRow"), i18n("micOffHint"));
    return;
  }
  if (g_menu_page == MENU_MULTI) {
    paint_extra_list(mem, rc, i18n("multiview"), g_mv_pick ? i18n("mvPick") : i18n("mvHint"));
    if (g_mv_pick) {
      int y = 88, row = 44, vis = (rc.bottom - y - 16) / row;
      if (vis < 1) vis = 1;
      int s0 = g_hit_i - vis / 2;
      if (s0 < 0) s0 = 0;
      for (int i = 0; i < vis && s0 + i < g_hit_n; i++) {
        int ch = g_hits[s0 + i];
        RECT item = {8, y + i * row, mw - 8, y + (i + 1) * row - 4};
        wchar_t title[220];
        utf8_to_wide(g_pl->items[ch].name, title, 220);
        paint_row(mem, item, s0 + i == g_hit_i, title, NULL);
      }
      return;
    }
    int y = 88, row = 52;
    for (int i = 0; i < g_mv_n; i++) {
      int ch = g_mv_ch[i];
      RECT item = {8, y + i * row, mw - 8, y + (i + 1) * row - 4};
      wchar_t title[220], sub[80];
          utf8_to_wide(g_pl && ch >= 0 && ch < g_pl->count ? g_pl->items[ch].name : i18n_u("channel"), title, 220);
      swprintf(sub, 80, i == g_mv_i ? L"звук" : L"без звука");
      paint_row(mem, item, i == g_mv_i && g_mv_i < g_mv_n, title, sub);
    }
    if (g_mv_n < 4) {
      RECT add = {8, y + g_mv_n * row, mw - 8, y + (g_mv_n + 1) * row - 4};
      paint_row(mem, add, g_mv_i == g_mv_n, L"+ Добавить окно", L"поиск по имени или номеру");
    }
    RECT foot = {20, rc.bottom - 48, mw - 20, rc.bottom - 16};
    SetTextColor(mem, RGB(200, 205, 215));
    SelectObject(mem, g_font_sm);
    DrawTextW(mem, L"ОК звук  ·  Delete убрать  ·  Назад — меню", -1, &foot, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
}

static void paint_menu(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ oldb = SelectObject(mem, bm);
  HBRUSH dim = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(mem, &rc, dim);
  DeleteObject(dim);
  if (g_menu_page == MENU_SEARCH || g_menu_page == MENU_HISTORY || g_menu_page == MENU_RECS ||
      g_menu_page == MENU_MULTI || g_menu_page == MENU_ASKREC || g_menu_page == MENU_SHARE ||
      g_menu_page == MENU_MICPERM || g_menu_page == MENU_SETUP || g_menu_page == MENU_URL) {
    if (g_menu_page == MENU_MULTI && !g_mv_pick) paint_multi_overlay(mem, rc);
    else if (search_like()) paint_search_card(mem, rc);
    else paint_extra(mem, rc);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return;
  }
  if (g_menu_page == MENU_SETTINGS) {
    paint_settings(mem, rc);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return;
  }
  if (g_menu_page == MENU_PIN) {
    int tr, tg, tb, hr, hg, hb;
    theme_rgb(&tr, &tg, &tb);
    hi_rgb(&hr, &hg, &hb);
    int mw = g_menu_w > 0 ? g_menu_w : 400;
    if (mw < 420 && rc.right >= 420) mw = 420;
    if (mw > rc.right) mw = rc.right;
    if (mw < 1) mw = 1;
    RECT panel = {0, 0, mw, rc.bottom};
    HBRUSH bg = CreateSolidBrush(RGB(tr, tg, tb));
    FillRect(mem, &panel, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    SelectObject(mem, g_font);
    RECT t = {24, 28, mw - 24, 80};
    DrawTextW(mem, g_pin_from_group ? i18n("pinGroup") : i18n("pinTitle"), -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    wchar_t dots[16];
    int n = (int)strlen(g_pin_buf);
    for (int i = 0; i < n && i < MAX_PIN; i++) dots[i] = L'•';
    dots[n] = 0;
    RECT d = {24, 90, mw - 24, 140};
    SelectObject(mem, g_font);
    DrawTextW(mem, n ? dots : i18n("pinDigits"), -1, &d, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    RECT okb = {24, 170, mw / 2 - 8, 214};
    RECT back = {mw / 2 + 8, 170, mw - 24, 214};
    HBRUSH hot = CreateSolidBrush(RGB(hr, hg, hb));
    HBRUSH idle = CreateSolidBrush(RGB(40, 46, 60));
    FillRect(mem, &okb, hot);
    FillRect(mem, &back, idle);
    DeleteObject(hot);
    DeleteObject(idle);
    DrawTextW(mem, i18n("pinEnter"), -1, &okb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem, i18n("back"), -1, &back, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return;
  }
  if (g_menu_page == MENU_STUB) {
    paint_stub(mem, rc);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return;
  }
  int mw = g_menu_w > 0 ? g_menu_w : 400;
  RECT panel = {0, 0, mw, rc.bottom};
  HBRUSH bg = CreateSolidBrush(RGB(18, 22, 32));
  FillRect(mem, &panel, bg);
  DeleteObject(bg);
  RECT head = {20, 18, mw - 20, 70};
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  SelectObject(mem, g_font);
  DrawTextW(mem, i18n("menu"), -1, &head, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
  head.top = 42;
  SetTextColor(mem, RGB(140, 150, 165));
  SelectObject(mem, g_font_sm);
  DrawTextW(mem, i18n("menuHint"), -1, &head, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
  int y = 78;
  int row = 56;
  for (int i = 0; i < MENU_N; i++) {
    RECT item = {0, y, mw, y + row};
    const wchar_t *hint = menu_hint(i);
    if (i == 5) hint = g_st && g_st->voice_enabled ? i18n("micOn") : i18n("micOff");
    draw_item(mem, &item, i == g_menu_i, menu_title(i), hint);
    y += row;
  }
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldb);
  DeleteObject(bm);
  DeleteDC(mem);
  EndPaint(hwnd, &ps);
}

typedef struct GuideBox {
  int gw, cw, pw, dw;
  int y0, row, vis;
} GuideBox;

static void guide_box(int w, int h, GuideBox *b) {
  int show_groups = g_guide_view == VIEW_CATEGORIES || g_guide_view == VIEW_GROUPS;
  int show_programs = g_guide_view == VIEW_CHANNELS || g_guide_view == VIEW_SCHEDULE;
  int show_days = show_programs && g_days_open && g_day_n > 0;
  int gw = show_groups ? 220 : 0;
  int cw = 280;
  int pw = show_programs ? 260 : 0;
  int dw = show_days ? 84 : 0;
  int avail = w > 0 ? w : gw + cw + pw + dw;
  int want = gw + cw + pw + dw;
  if (want > avail && want > 0) {
    int mg = show_groups ? 96 : 0;
    int mc = 140;
    int mp = show_programs ? 120 : 0;
    int md = show_days ? 52 : 0;
    int floor = mg + mc + mp + md;
    if (floor < 1) floor = 1;
    if (avail <= floor) {
      gw = show_groups ? avail * mg / floor : 0;
      cw = avail * mc / floor;
      pw = show_programs ? avail * mp / floor : 0;
      dw = show_days ? avail - gw - cw - pw : 0;
      if (dw < 0) dw = 0;
    } else {
      int extra = avail - floor;
      int flex = (show_groups ? (220 - 96) : 0) + (280 - 140) + (show_programs ? (260 - 120) : 0) + (show_days ? (84 - 52) : 0);
      if (flex < 1) flex = 1;
      gw = show_groups ? mg + (220 - 96) * extra / flex : 0;
      cw = mc + (280 - 140) * extra / flex;
      pw = show_programs ? mp + (260 - 120) * extra / flex : 0;
      dw = show_days ? md + (84 - 52) * extra / flex : 0;
      int sum = gw + cw + pw + dw;
      if (sum < avail) cw += avail - sum;
      if (sum > avail && cw > sum - avail) cw -= sum - avail;
    }
  }
  if (cw < 1) cw = avail > 1 ? avail : 1;
  if (gw + cw + pw + dw > avail && avail > 0) {
    int over = gw + cw + pw + dw - avail;
    if (pw >= over) pw -= over;
    else if (cw > over) cw -= over;
  }
  int rows = g_st && g_st->guide_rows ? g_st->guide_rows : 6;
  if (rows < 1) rows = 6;
  int y0 = 50;
  int row = (h - y0) / rows;
  int min_row = h < 420 ? 36 : 46;
  if (row < min_row) row = min_row;
  if (h > y0 && row > h - y0) row = h - y0;
  if (row < 28) row = 28;
  if (h > 0 && h < y0 + row) {
    y0 = h > 40 ? 8 : 0;
    row = h - y0;
    if (row < 24) row = 24;
  }
  int vis = (h - y0) / row;
  if (vis < 1) vis = 1;
  b->gw = gw;
  b->cw = cw;
  b->pw = pw;
  b->dw = dw;
  b->y0 = y0;
  b->row = row;
  b->vis = vis;
}

static void paint_guide(HWND hwnd) {
  PAINTSTRUCT ps;
  BeginPaint(hwnd, &ps);
  EndPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  Argb a;
  if (!argb_begin(&a, rc.right, rc.bottom)) return;
  if (g_chm && g_mode != UI_GUIDE) {
    paint_ch_overlay(&a, rc);
    argb_commit(hwnd, &a);
    return;
  }
  GuideBox box;
  guide_box(rc.right, rc.bottom, &box);
  g_marquee = 0;
  int gw = box.gw, cw = box.cw, pw = box.pw, dw = box.dw;
  int show_groups = gw > 0;
  int show_programs = pw > 0;
  int show_days = dw > 0;
  if (show_groups) {
    RECT col = {0, 0, gw, rc.bottom};
    argb_fill(&a, col, 0, 0, 0, 0, 102);
  }
  {
    RECT col = {gw, 0, gw + cw, rc.bottom};
    argb_fill(&a, col, 0, 0, 0, 0, 102);
  }
  if (show_programs) {
    RECT col = {gw + cw, 0, gw + cw + pw, rc.bottom};
    argb_fill(&a, col, 0, 0, 0, 0, 89);
  }
  if (show_days) {
    RECT col = {gw + cw + pw, 0, gw + cw + pw + dw, rc.bottom};
    argb_fill(&a, col, 0, 0, 0, 0, 76);
  }
  wchar_t gname[256];
  utf8_to_wide(g_group_n ? g_groups[g_group_i] : "Каналы", gname, 256);
  if (show_groups) {
    RECT t = {16, 12, gw - 8, 44};
    argb_text(&a, t, i18n("folders"), g_font_sm, 125, 200, 255, 230, DT_LEFT | DT_SINGLELINE);
  }
  RECT cap = {gw + 16, 12, gw + cw - 12, 44};
  argb_text(&a, cap, gname, g_font, 255, 255, 255, 240, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  int row = box.row;
  int y = box.y0;
  int vis = box.vis;
  if (show_groups) {
    int g0 = g_group_i - vis / 2;
    if (g0 < 0) g0 = 0;
    for (int i = 0; i < vis && g0 + i < g_group_n; i++) {
      int idx = g0 + i;
      RECT item = {8, y + i * row, gw - 8, y + (i + 1) * row - 4};
      int hot = (g_focus_col == COL_GROUPS && idx == g_group_i);
      int active = idx == g_group_i;
      if (hot) argb_fill(&a, item, 8, 47, 124, 246, 178);
      wchar_t title[256], cnt[16];
      utf8_to_wide(g_groups[idx], title, 256);
      RECT tr = item;
      tr.right -= 36;
      int tr_r = hot ? 255 : (active ? 125 : 255);
      int tr_g = hot ? 255 : (active ? 211 : 255);
      int tr_b = hot ? 255 : (active ? 252 : 255);
      argb_text(&a, tr, title, g_font_sm, tr_r, tr_g, tr_b, hot ? 255 : 210, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      swprintf(cnt, 16, L"%d", group_count(idx));
      RECT cr = item;
      cr.left = item.right - 40;
      argb_text(&a, cr, cnt, g_font_sm, 180, 185, 195, 160, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
  }
  int c0 = g_chan_i - vis / 2;
  if (c0 < 0) c0 = 0;
  int watching = player_current_index();
  long long nowms = epg_now_ms();
  for (int i = 0; i < vis && c0 + i < g_in_group_n; i++) {
    int idx = c0 + i;
    int ch = g_index_in_group[idx];
    RECT item = {gw + 8, y + i * row, gw + cw - 8, y + (i + 1) * row - 4};
    int hot = idx == g_chan_i;
    int on = ch == watching;
    if (hot) argb_fill(&a, item, 8, 47, 124, 246, 178);
    else if (on) argb_fill(&a, item, 8, 255, 255, 255, 26);
    const Channel *c = &g_pl->items[ch];
    char icon[MAX_REF];
    icon[0] = 0;
    epg_channel_icon(ch, icon, MAX_REF);
    int num = group_kind_of(g_group_i) == GK_FAV ? idx + 1 : (c->number ? c->number : ch + 1);
    wchar_t prefix[48], wname[MAX_NAME + 4];
    prefix[0] = 0;
    if (g_st && g_st->show_numbers) swprintf(prefix, 48, on ? L"> %d  " : L"%d  ", num);
    else if (on) swprintf(prefix, 48, L"> ");
    utf8_to_wide(c->name, wname, MAX_NAME + 4);
    int name_h = 20, sub_h = 18;
    int block_top = item.top + 3;
    int max_block = item.bottom - item.top - 6;
    if (name_h + sub_h > max_block) {
      int bh = max_block > 16 ? max_block : 16;
      name_h = bh * 52 / 100;
      if (name_h < 12) name_h = 12;
      sub_h = bh - name_h;
      if (sub_h < 10) sub_h = 10;
    }
    int block_bot = block_top + name_h + sub_h;
    int lsz = name_h + sub_h;
    int text_x = 8;
    if (icon[0] && lsz >= 16) {
      logo_blit((unsigned char *)a.bits, a.w, a.h, item.left + 6, block_top, lsz, icon);
      text_x = 6 + lsz + 8;
    }
    int star_w = channel_is_fav(ch) ? 22 : 0;
    int text_r = item.right - 8 - star_w;
    int prefix_w = text_px(&a, g_font_sm, prefix);
    if (prefix_w > text_r - (item.left + text_x) - 8) prefix_w = text_r - (item.left + text_x) - 8;
    if (prefix_w < 0) prefix_w = 0;
    if (prefix[0] && prefix_w > 0) {
      RECT pr = {item.left + text_x, block_top, item.left + text_x + prefix_w, block_top + name_h};
      argb_text(&a, pr, prefix, g_font_sm, 255, 255, 255, 240, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RECT nr = {item.left + text_x + prefix_w, block_top, text_r, block_top + name_h};
    int scroll = hot && g_focus_col == COL_CHANNELS;
    if (nr.right > nr.left + 4) argb_fit(&a, nr, wname, g_font_sm, 255, 255, 255, 240, scroll);
    char nowt[96];
    long long ns = 0, ne = 0;
    if (!epg_now(ch, nowt, (int)sizeof nowt, NULL, 0, &ns, &ne) || !nowt[0]) snprintf(nowt, sizeof nowt, "%s", i18n_u("liveAir"));
    wchar_t sub[160];
    utf8_to_wide(nowt, sub, 160);
    RECT sr = {item.left + text_x + prefix_w, block_top + name_h, text_r, block_bot};
    if (sr.right > sr.left + 4) argb_fit(&a, sr, sub, g_font_sm, 200, 205, 215, 180, scroll);
    if (g_st && g_st->show_epg_progress && (hot || on) && ne > ns && nowms >= ns && nowms < ne) {
      int old_gap = (item.bottom - 7) - (item.top + 42);
      if (old_gap < 0) old_gap = 0;
      int bar_top = block_bot + old_gap / 3;
      if (bar_top + 3 > item.bottom - 2) bar_top = item.bottom - 5;
      if (bar_top < block_bot) bar_top = block_bot;
      RECT track = {item.left + 8, bar_top, item.right - 10 - star_w, bar_top + 3};
      if (track.right > track.left + 4) {
        argb_fill(&a, track, 2, 255, 255, 255, 38);
        int fillw = (int)((track.right - track.left) * (nowms - ns) / (ne - ns));
        RECT fill = track;
        fill.right = fill.left + fillw;
        argb_fill(&a, fill, 2, 255, 255, 255, 204);
      }
    }
    if (star_w) {
      RECT star = {item.right - 22, block_top, item.right - 2, block_bot};
      argb_text(&a, star, L"*", g_font_sm, 252, 211, 77, 230, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
  if (show_programs) {
    int ch = guide_channel();
    const Channel *c = (g_pl && ch >= 0 && ch < g_pl->count) ? &g_pl->items[ch] : NULL;
    RECT phead = {gw + cw + 12, 12, gw + cw + pw - 8, 44};
    if (c) {
      char head[MAX_NAME + 32];
      int num = group_kind_of(g_group_i) == GK_FAV ? g_chan_i + 1 : (c->number ? c->number : ch + 1);
      snprintf(head, sizeof head, "%d  %s", num, c->name);
      wchar_t wh[320];
      utf8_to_wide(head, wh, 320);
      argb_text(&a, phead, wh, g_font_sm, 255, 255, 255, 240, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    int pn = epg_count(ch);
    int prow = 34;
    int py0 = 50;
    int pvis = (rc.bottom - py0 - 8) / prow;
    if (pvis < 1) pvis = 1;
    int p0 = g_prog_i - pvis / 2;
    if (p0 < 0) p0 = 0;
    if (pn && p0 > pn - pvis) p0 = pn - pvis;
    if (p0 < 0) p0 = 0;
    int any = 0;
    long long prev_day = -1;
    int py = py0;
    for (int i = p0; i < pn && py + prow < rc.bottom; i++) {
      char ptitle[96];
      long long ps = 0, pe = 0;
      if (!epg_list(ch, i, ptitle, (int)sizeof ptitle, NULL, 0, &ps, &pe)) break;
      any = 1;
      long long day = epg_day_start(ps);
      if (day != prev_day) {
        wchar_t dlong[80];
        format_day_long(day, dlong, 80);
        RECT hr = {gw + cw + 12, py, gw + cw + pw - 8, py + 22};
        argb_text(&a, hr, dlong, g_font_tiny, 125, 211, 252, 230, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        py += 22;
        prev_day = day;
        if (py + prow >= rc.bottom) break;
      }
      RECT item = {gw + cw + 8, py, gw + cw + pw - 8, py + prow - 2};
      int hot = g_focus_col == COL_PROGRAMS && i == g_prog_i;
      int selected = i == g_prog_i;
      int current = ps <= nowms && nowms < pe;
      if (hot) argb_fill(&a, item, 6, 47, 124, 246, 190);
      else if (selected) argb_fill(&a, item, 6, 255, 255, 255, 31);
      SYSTEMTIME lt;
      epg_local_fields(ps, &lt);
      wchar_t wtitle[128], hhmm[16];
      utf8_to_wide(ptitle, wtitle, 128);
      swprintf(hhmm, 16, L"%02u:%02u", (unsigned)lt.wHour, (unsigned)lt.wMinute);
      int tr = 255, tg = 255, tb = 255, ta = 210;
      if (current && !hot) {
        tr = 125;
        tg = 211;
        tb = 252;
        ta = 240;
      } else if (!hot && !selected) {
        tr = 220;
        tg = 220;
        tb = 220;
        ta = 191;
      }
      RECT time_rc = {item.left + 8, item.top, item.left + 52, item.bottom};
      RECT title_rc = {item.left + 54, item.top, item.right - 8, item.bottom};
      int ttr = (current && !hot) ? 125 : 180;
      int ttg = (current && !hot) ? 211 : 185;
      int ttb = (current && !hot) ? 252 : 195;
      argb_text(&a, time_rc, hhmm, g_font_sm, ttr, ttg, ttb, 220, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      int archive = pe <= nowms && program_has_archive(c, g_st, ps, pe);
      int timed = rec_has_timer_for(ch, ps, pe);
      if (archive || timed) title_rc.right -= 22;
      if (title_rc.left > title_rc.right - 8) title_rc.left = item.left + 4;
      argb_fit(&a, title_rc, wtitle, g_font_sm, tr, tg, tb, ta, hot);
      if (timed) {
        RECT ar = {item.right - 22, item.top, item.right - 4, item.bottom};
        argb_text(&a, ar, L"●", g_font_sm, 248, 113, 113, 230, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      } else if (archive) {
        RECT ar = {item.right - 22, item.top, item.right - 4, item.bottom};
        argb_text(&a, ar, L"↺", g_font_sm, 220, 220, 220, 140, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      py += prow;
    }
    if (!any) {
      RECT empty = {gw + cw + 16, 56, gw + cw + pw - 12, 120};
      argb_text(&a, empty, i18n("noProgram"), g_font_sm, 180, 185, 195, 200, DT_LEFT | DT_WORDBREAK);
    }
    if (show_days) {
      RECT dcap = {gw + cw + pw, 12, gw + cw + pw + dw, 36};
      argb_text(&a, dcap, L"ДНИ", g_font_tiny, 180, 185, 195, 90, DT_CENTER | DT_SINGLELINE);
      int drow = 58;
      int dy = 40;
      int dvis = (rc.bottom - dy - 8) / drow;
      if (dvis < 1) dvis = 1;
      int d0 = g_day_i - dvis / 2;
      if (d0 < 0) d0 = 0;
      if (g_day_n && d0 > g_day_n - dvis) d0 = g_day_n - dvis;
      if (d0 < 0) d0 = 0;
      for (int i = 0; i < dvis && d0 + i < g_day_n; i++) {
        int idx = d0 + i;
        RECT item = {gw + cw + pw + 4, dy + i * drow, gw + cw + pw + dw - 4, dy + (i + 1) * drow - 4};
        int hot = g_focus_col == COL_DAYS && idx == g_day_i;
        int selected = idx == g_day_i;
        if (hot) argb_fill(&a, item, 6, 47, 124, 246, 190);
        SYSTEMTIME ds;
        epg_local_fields(g_days[idx], &ds);
        int w = ds.wDayOfWeek;
        int m = ds.wMonth >= 1 && ds.wMonth <= 12 ? ds.wMonth - 1 : 0;
        if (w < 0 || w > 6) w = 0;
        wchar_t num[8];
        swprintf(num, 8, L"%u", (unsigned)ds.wDay);
        int cr = 255, cg = 255, cb = 255, ca = 178;
        if (hot) {
          cr = cg = cb = 255;
          ca = 255;
        } else if (selected) {
          cr = 125;
          cg = 211;
          cb = 252;
          ca = 230;
        }
        RECT nr = {item.left, item.top + 4, item.right, item.top + 24};
        RECT mr = {item.left, item.top + 22, item.right, item.top + 36};
        RECT wr = {item.left, item.top + 34, item.right, item.bottom - 2};
        argb_text(&a, nr, num, g_font_day, cr, cg, cb, ca, DT_CENTER | DT_SINGLELINE);
        argb_text(&a, mr, i18n_mo_short(m), g_font_tiny, 255, 255, 255, selected && !hot ? 128 : 90, DT_CENTER | DT_SINGLELINE);
        argb_text(&a, wr, i18n_wd_short(w), g_font_sm, cr, cg, cb, ca, DT_CENTER | DT_SINGLELINE);
      }
    }
    char dtitle[96], ddesc[400];
    long long ds = 0, de = 0;
    dtitle[0] = ddesc[0] = 0;
    int has = epg_list(ch, g_prog_i, dtitle, (int)sizeof dtitle, ddesc, (int)sizeof ddesc, &ds, &de);
    if (!has || !dtitle[0]) has = epg_now(ch, dtitle, (int)sizeof dtitle, ddesc, (int)sizeof ddesc, &ds, &de);
    int used = gw + cw + pw + dw;
    int room = rc.right - used - 36;
    int cardh = 300;
    if (cardh > rc.bottom - 72) cardh = rc.bottom - 72;
    int cardw = 340;
    if (cardw > room) cardw = room;
    if (room >= 160 && cardh >= 160) {
    int card_top = 40;
    if (card_top + cardh > rc.bottom - 8) card_top = rc.bottom - 8 - cardh;
    if (card_top < 8) card_top = 8;
    RECT card = {rc.right - cardw - 16, card_top, rc.right - 16, card_top + cardh};
    argb_fill(&a, card, 16, 0, 0, 0, 140);
    if (g_focus_col == COL_DETAIL) {
      RECT ring = {card.left - 2, card.top - 2, card.right + 2, card.bottom + 2};
      argb_fill(&a, ring, 18, 255, 255, 255, 220);
      argb_fill(&a, card, 16, 0, 0, 0, 140);
    }
    RECT ct = {card.left + 16, card.top + 12, card.right - 16, card.top + 52};
    int show_desc = !g_st || g_st->show_program_desc;
    if (has && dtitle[0]) {
      wchar_t wt[128];
      utf8_to_wide(dtitle, wt, 128);
      argb_text(&a, ct, wt, g_font, 255, 255, 255, 255, DT_LEFT | DT_WORDBREAK);
      if (ds && de) {
        SYSTEMTIME a1, a2;
        epg_local_fields(ds, &a1);
        epg_local_fields(de, &a2);
        wchar_t rng[64];
        swprintf(rng, 64, L"%02u:%02u – %02u:%02u", (unsigned)a1.wHour, (unsigned)a1.wMinute, (unsigned)a2.wHour,
                 (unsigned)a2.wMinute);
        RECT rr = {card.left + 16, card.top + 56, card.right - 16, card.top + 78};
        argb_text(&a, rr, rng, g_font_sm, 200, 205, 215, 200, DT_LEFT | DT_SINGLELINE);
        if (nowms >= ds && nowms < de && de > ds) {
          RECT track = {card.left + 16, card.top + 84, card.right - 16, card.top + 87};
          argb_fill(&a, track, 2, 255, 255, 255, 40);
          int fillw = (int)((track.right - track.left) * (nowms - ds) / (de - ds));
          RECT fill = track;
          fill.right = fill.left + fillw;
          argb_fill(&a, fill, 2, 255, 255, 255, 220);
        }
      }
      if (show_desc) {
        RECT dr = {card.left + 16, card.top + 96, card.right - 16, card.bottom - (de > nowms ? 52 : 12)};
        wchar_t wd[420];
        utf8_to_wide(ddesc[0] ? ddesc : "Нет описания", wd, 420);
        argb_text(&a, dr, wd, g_font_sm, 210, 215, 220, 200, DT_LEFT | DT_WORDBREAK);
      }
      if (de > nowms) {
        RECT btn = {card.left + 16, card.bottom - 44, card.right - 16, card.bottom - 12};
        argb_fill(&a, btn, 8, 47, 124, 246, 220);
        argb_text(&a, btn, rec_has_timer_for(ch, ds, de) ? i18n("cancelRec") : i18n("record"), g_font_sm, 255, 255, 255,
                  255, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
    } else {
      argb_text(&a, ct, show_desc ? L"Нет описания" : L"", g_font, 200, 205, 215, 200, DT_LEFT | DT_WORDBREAK);
    }
    }
  }
  paint_ch_overlay(&a, rc);
  argb_commit(hwnd, &a);
}

static void paint_exit(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  HBRUSH bg = CreateSolidBrush(RGB(18, 22, 32));
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(255, 255, 255));
  SelectObject(hdc, g_font);
  RECT t = {16, 16, rc.right - 16, 70};
  DrawTextW(hdc, i18n("exitAgain"), -1, &t, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
  RECT a = {40, rc.bottom - 58, rc.right / 2 - 8, rc.bottom - 16};
  RECT b = {rc.right / 2 + 8, rc.bottom - 58, rc.right - 40, rc.bottom - 16};
  HBRUSH hot = CreateSolidBrush(RGB(37, 99, 235));
  HBRUSH idle = CreateSolidBrush(RGB(40, 46, 60));
  FillRect(hdc, &a, g_exit_i == 0 ? hot : idle);
  FillRect(hdc, &b, g_exit_i == 1 ? hot : idle);
  DeleteObject(hot);
  DeleteObject(idle);
  DrawTextW(hdc, i18n("quit"), -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  DrawTextW(hdc, i18n("cancel"), -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  EndPaint(hwnd, &ps);
}

static void rebuild_fonts(void) {
  int d = 0;
  if (g_st && strcmp(g_st->font_size, "sm") == 0) d = -2;
  else if (g_st && strcmp(g_st->font_size, "lg") == 0) d = 3;
  int cd = 0;
  if (g_st && strcmp(g_st->clock_size, "sm") == 0) cd = -4;
  else if (g_st && strcmp(g_st->clock_size, "lg") == 0) cd = 8;
  if (g_font) DeleteObject(g_font);
  if (g_font_sm) DeleteObject(g_font_sm);
  if (g_font_pad) DeleteObject(g_font_pad);
  if (g_font_clock) DeleteObject(g_font_clock);
  if (g_font_day) DeleteObject(g_font_day);
  if (g_font_tiny) DeleteObject(g_font_tiny);
  g_font = CreateFontW(20 + d, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
  g_font_sm = CreateFontW(15 + d, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
  g_font_pad = CreateFontW(13 + d, 0, 0, 0, 500, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
  g_font_clock = CreateFontW(22 + cd, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Segoe UI");
  g_font_day = CreateFontW(18 + d, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
  g_font_tiny = CreateFontW(11 + d, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void apply_panel_alpha(void) {
  int pct = g_st && g_st->panel_opacity > 0 ? g_st->panel_opacity : 90;
  if (pct < 50) pct = 50;
  if (pct > 100) pct = 100;
  BYTE a = (BYTE)(pct * 255 / 100);
  if (a < 80) a = 80;
  layer_alpha(g_menu, a);
  layer_alpha(g_exit, 235);
}

static void layout(HWND host) {
  if (g_in_layout) return;
  g_in_layout = 1;
  if (!host) host = g_host;
  if (!host) {
    g_in_layout = 0;
    return;
  }
  if (IsIconic(host)) {
    show_win(g_clock, 0);
    show_win(g_pad, 0);
    show_win(g_menu, 0);
    show_win(g_guide, 0);
    show_win(g_exit, 0);
    show_win(g_upd, 0);
    for (int i = 0; i < 4; i++) {
      if (g_tile_wnd[i]) ShowWindow(g_tile_wnd[i], SW_HIDE);
    }
    g_in_layout = 0;
    return;
  }
  RECT rc;
  GetClientRect(host, &rc);
  int w = rc.right, h = rc.bottom;
  if (g_clock) {
    int cw = 110, ch = 36;
    if (g_st && strcmp(g_st->clock_size, "sm") == 0) {
      cw = 90;
      ch = 30;
    } else if (g_st && strcmp(g_st->clock_size, "lg") == 0) {
      cw = 150;
      ch = 48;
    }
    int x = w - cw - 16, y = 16;
    if (g_st) {
      if (strcmp(g_st->clock_pos, "top-left") == 0) {
        x = 16;
        y = 16;
      } else if (strcmp(g_st->clock_pos, "bottom-left") == 0) {
        x = 16;
        y = h - ch - 16;
      } else if (strcmp(g_st->clock_pos, "bottom-right") == 0) {
        x = w - cw - 16;
        y = h - ch - 16;
      }
    }
    if (cw > w - 8 && w > 8) cw = w - 8;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w > 0 && x + cw > w) x = w - cw;
    if (h > 0 && y + ch > h) y = h - ch;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    place_ui(g_clock, host, x, y, cw, ch);
    show_win(g_clock, g_st ? g_st->clock_enabled : 1);
    refresh_clock();
  }
  if (g_pad) {
    int ph = 128;
    int px = 12;
    int pw = w - 24;
    int py = h - ph - 64;
    if (pw < 1) pw = w > 0 ? w : 1;
    if (px + pw > w && w > 0) {
      px = 0;
      pw = w;
    }
    if (py < 8) py = 8;
    if (h > 0 && py + ph > h) {
      py = 4;
      ph = h > 12 ? h - 8 : h;
    }
    place_ui(g_pad, host, px, py, pw, ph);
    show_win(g_pad, g_mode == UI_PAD);
  }
  if (g_menu) {
    int mw = w * 86 / 100;
    if (mw > 400) mw = 400;
    if (mw < 280) mw = 280;
    if (w > 0 && mw > w) mw = w;
    if (mw < 1) mw = 1;
    g_menu_w = mw;
    int multi_ui = g_mode == UI_MENU && g_menu_page == MENU_MULTI && !g_mv_pick;
    if (multi_ui) {
      if (g_mv_menu) {
        int nopt = mv_opt_n();
        int aw = 300, ah = 16 + nopt * 48;
        if (w > 16 && aw > w - 12) aw = w - 12;
        if (aw < 80) aw = w > 0 ? w : 80;
        if (h > 16 && ah > h - 12) ah = h - 12;
        if (ah < 40) ah = h > 0 ? h : 40;
        int ax = w - aw - 12;
        if (ax < 0) ax = 0;
        int ay = (h - ah) / 2;
        if (ay < 8) ay = 8;
        g_menu_w = aw;
        place_ui(g_menu, host, ax, ay, aw, ah);
        layer_alpha(g_menu, 245);
      } else {
        place_ui(g_menu, host, 0, h - 52, w, 52);
        g_menu_w = w;
        layer_alpha(g_menu, 230);
      }
    } else {
      place_ui(g_menu, host, 0, 0, w, h);
      apply_panel_alpha();
    }
    show_win(g_menu, g_mode == UI_MENU);
  }
  {
    int multi = (g_mode == UI_MENU && g_menu_page == MENU_MULTI) || (g_mode == UI_GUIDE && g_mv_pick && g_mv_n > 0);
    int bar = (g_mode == UI_MENU && g_menu_page == MENU_MULTI && !g_mv_menu && !g_mv_pick) ? 52 : 0;
    int gap = 8, fr = 4;
    int area_h = h - bar;
    if (area_h < 80) area_h = h;
    if (multi && g_mv_n > 0) {
      for (int i = 0; i < 4; i++) {
        RECT *o = &g_mv_rc[i];
        o->left = o->top = o->right = o->bottom = 0;
        if (i >= g_mv_n) continue;
        if (g_mv_n <= 1) {
          int vw = w * 56 / 100;
          int vh = vw * 9 / 16;
          if (vh > area_h * 70 / 100) {
            vh = area_h * 70 / 100;
            vw = vh * 16 / 9;
          }
          if (vw < 240) vw = w > 280 ? w - 40 : w;
          if (vh < 140) vh = area_h > 160 ? area_h - 20 : area_h;
          o->left = (w - vw) / 2;
          o->top = (area_h - vh) / 2;
          o->right = o->left + vw;
          o->bottom = o->top + vh;
        } else if (g_mv_n == 2) {
          int hw = w / 2;
          if (i == 0) {
            o->left = gap;
            o->top = gap;
            o->right = hw - gap / 2;
            o->bottom = area_h - gap;
          } else {
            o->left = hw + gap / 2;
            o->top = gap;
            o->right = w - gap;
            o->bottom = area_h - gap;
          }
        } else if (g_mv_n == 3) {
          int hw = w / 2, hh = area_h / 2;
          if (i == 0) {
            o->left = gap;
            o->top = gap;
            o->right = hw - gap / 2;
            o->bottom = hh - gap / 2;
          } else if (i == 1) {
            o->left = hw + gap / 2;
            o->top = gap;
            o->right = w - gap;
            o->bottom = hh - gap / 2;
          } else {
            o->left = gap;
            o->top = hh + gap / 2;
            o->right = w - gap;
            o->bottom = area_h - gap;
          }
        } else {
          int hw = w / 2, hh = area_h / 2;
          int col = i % 2, row = i / 2;
          o->left = col * hw + gap;
          o->top = row * hh + gap;
          o->right = (col + 1) * hw - gap;
          o->bottom = (row + 1) * hh - gap;
        }
      }
      RECT *m = &g_mv_rc[0];
      if (g_mv_ch[0] >= 0) {
        int mw = m->right - m->left, mh = m->bottom - m->top;
        int ix = m->left + fr, iy = m->top + fr, iw = mw - fr * 2, ih = mh - fr * 2;
        if (iw < 32) {
          ix = m->left;
          iw = mw;
        }
        if (ih < 32) {
          iy = m->top;
          ih = mh;
        }
        player_set_slot(ix, iy, iw, ih);
      }
      for (int i = 1; i < 4; i++) {
        if (i < g_mv_n && g_tile_wnd[i - 1]) {
          RECT *c = &g_mv_rc[i];
          int empty = g_mv_ch[i] < 0;
          int tw = c->right - c->left;
          int th = c->bottom - c->top;
          int tx = c->left + fr, ty = c->top + fr, twi = tw - fr * 2, thi = th - fr * 2;
          if (twi < 32) {
            tx = c->left;
            twi = tw;
          }
          if (thi < 32) {
            ty = c->top;
            thi = th;
          }
          SetWindowPos(g_tile_wnd[i - 1], HWND_TOP, tx, ty, twi, thi, SWP_NOACTIVATE | SWP_SHOWWINDOW);
          InvalidateRect(g_tile_wnd[i - 1], NULL, FALSE);
          if (empty) {
            g_mv_need_start[i] = 0;
            if (g_mv_spawned[i]) {
              g_mv_spawned[i] = 0;
              player_tile_stop(i - 1);
            }
          } else if (!g_mv_spawned[i]) {
            g_mv_need_start[i] = 1;
          }
        } else if (g_tile_wnd[i - 1]) {
          ShowWindow(g_tile_wnd[i - 1], SW_HIDE);
        }
      }
      mv_place_ring();
      mv_apply_audio();
      if (g_host) SetFocus(g_host);
    } else {
      if (g_mv_ring) ShowWindow(g_mv_ring, SW_HIDE);
      for (int i = 0; i < 4; i++) {
        if (g_tile_wnd[i]) ShowWindow(g_tile_wnd[i], SW_HIDE);
      }
    }
  }
  if (g_guide) {
    place_ui(g_guide, host, 0, 0, w, h);
    show_win(g_guide, g_mode == UI_GUIDE || g_chm);
  }
  if (g_exit) {
    int ew = 420, eh = 150;
    if (w > 24 && ew > w - 16) ew = w - 16;
    if (h > 24 && eh > h - 16) eh = h - 16;
    int ex = (w - ew) / 2;
    int ey = (h - eh) / 2;
    if (ex < 0) ex = 0;
    if (ey < 0) ey = 0;
    place_ui(g_exit, host, ex, ey, ew, eh);
    show_win(g_exit, g_mode == UI_EXIT);
  }
  if (g_upd) {
    int on = upd_shown();
    int uw = UPD_W, uh = UPD_H;
    if (w > 20 && uw > w - 16) uw = w - 16;
    if (uw < 1) uw = w > 0 ? w : 1;
    int x = 16, y = h - uh - 16;
    if (g_st && g_st->clock_enabled && strcmp(g_st->clock_pos, "bottom-left") == 0) {
      int cw = 110;
      if (strcmp(g_st->clock_size, "sm") == 0) cw = 90;
      else if (strcmp(g_st->clock_size, "lg") == 0) cw = 150;
      x = 16 + cw + 12;
    }
    if (g_mode == UI_PAD) y = h - 128 - 64 - uh - 8;
    if (x + uw > w) x = w - uw;
    if (x < 4) x = 4;
    if (y < 4) y = 4;
    if (h > 0 && y + uh > h) y = h > uh ? h - uh : 0;
    place_ui(g_upd, host, x, y, uw, uh);
    show_win(g_upd, on);
    if (on) {
      raise_ui(g_upd);
      paint_upd(g_upd);
    }
  }
  if (g_mode == UI_MENU && g_menu) raise_ui(g_menu);
  if (g_clock && (g_st ? g_st->clock_enabled : 1)) raise_ui(g_clock);
  if (g_upd && upd_shown()) raise_ui(g_upd);
  g_in_layout = 0;
}

static void set_mode(int mode) {
  int old = g_mode;
  g_mode = mode;
  if (old == UI_GUIDE && mode != UI_GUIDE) restore_group_cursor();
  if (mode == UI_PAD) {
    g_pad_until = GetTickCount() + 6500;
    g_pad_focus = PAD_NONE;
    g_pad_i = PLAY_PAD;
  }
  if (mode == UI_EXIT) g_exit_until = GetTickCount() + 4000;
  layout(g_host);
  if (g_pad) InvalidateRect(g_pad, NULL, FALSE);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
  if (g_exit) InvalidateRect(g_exit, NULL, FALSE);
}

static void open_guide(void) {
  rebuild_groups();
  {
    int ch = player_current_index();
    if (g_mv_pick && g_mv_i >= 0 && g_mv_i < g_mv_n && g_mv_ch[g_mv_i] >= 0) ch = g_mv_ch[g_mv_i];
    apply_playing_group(ch);
  }
  if (g_in_group_n < 1) {
    remember_group(0);
    fill_group(0);
  }
  select_current_in_guide();
  rebuild_guide_days();
  g_days_open = 0;
  g_guide_view = VIEW_CHANNELS;
  g_focus_col = COL_CHANNELS;
  sync_prog_to_channel();
  g_resume_guide = 0;
  set_mode(UI_GUIDE);
}

static int contains_i(const char *hay, const char *needle) {
  if (!needle || !needle[0]) return 1;
  if (!hay) return 0;
  wchar_t a[256], b[128];
  utf8_to_wide(hay, a, 256);
  utf8_to_wide(needle, b, 128);
  CharLowerBuffW(a, (DWORD)wcslen(a));
  CharLowerBuffW(b, (DWORD)wcslen(b));
  return b[0] && wcsstr(a, b) != NULL;
}

static int ch_is_fav(int idx) {
  if (!g_st) return 0;
  for (int i = 0; i < g_st->fav_n; i++) {
    if (g_st->fav[i] == idx) return 1;
  }
  return 0;
}

static void compact_copy(const char *in, char *out, int max) {
  int o = 0;
  for (int i = 0; in && in[i] && o < max - 1; i++) {
    if (in[i] != ' ') out[o++] = in[i];
  }
  out[o] = 0;
}

static int family_score(const char *fam, const char *q) {
  if (!fam || !fam[0] || !q || !q[0]) return 0;
  if (!strcmp(fam, q)) return 300;
  size_t ql = strlen(q), fl = strlen(fam);
  if (ql >= 2 && !strncmp(fam, q, ql) && (fam[ql] == 0 || fam[ql] == ' ')) {
    int extra = (int)(fl - ql);
    if (extra < 0) extra = 0;
    if (extra > 40) extra = 40;
    return 220 - extra;
  }
  char fc[192], qc[192];
  compact_copy(fam, fc, 192);
  compact_copy(q, qc, 192);
  if (!qc[0]) return 0;
  if (!strcmp(fc, qc)) return 260;
  size_t qcl = strlen(qc), fcl = strlen(fc);
  if (qcl >= 2 && !strncmp(fc, qc, qcl)) {
    int extra = (int)(fcl - qcl);
    if (extra < 0) extra = 0;
    if (extra > 40) extra = 40;
    return 180 - extra;
  }
  if (qcl >= 2 && strstr(fc, qc)) return 80;
  return 0;
}

typedef struct HitSc {
  int idx;
  int score;
} HitSc;

static int hit_cmp(const void *a, const void *b) {
  const HitSc *x = (const HitSc *)a;
  const HitSc *y = (const HitSc *)b;
  if (y->score != x->score) return y->score - x->score;
  return x->idx - y->idx;
}

static int mv_used(int ch) {
  for (int i = 0; i < g_mv_n; i++) {
    if (g_mv_ch[i] == ch) return 1;
  }
  return 0;
}

static void fill_search(void) {
  g_hit_n = 0;
  if (!g_pl) return;
  char qf[MAX_FAMILY];
  fold_family(g_search, qf, MAX_FAMILY);
  int digits = g_search[0] && strspn(g_search, "0123456789") == strlen(g_search);
  int want = digits ? atoi(g_search) : 0;
  HitSc tmp[40];
  int n = 0;
  for (int i = 0; i < g_pl->count; i++) {
    const Channel *c = &g_pl->items[i];
    const char *grp = c->group[0] ? c->group : "Другие";
    if (group_hidden(grp)) continue;
    if (g_mv_pick == 1 && mv_used(i)) continue;
    int score = 0;
    if (!g_search[0]) {
      score = 10 + (ch_is_fav(i) ? 100 : 0);
    } else {
      int num = c->number ? c->number : i + 1;
      if (digits && want == num) score = 280;
      int s = family_score(c->family, qf);
      char tf[MAX_FAMILY];
      fold_family(c->tvg_name, tf, MAX_FAMILY);
      int s2 = family_score(tf, qf);
      if (s2 > s) s = s2;
      if (s > score) score = s;
      if (score < 80 && (contains_i(c->name, g_search) || contains_i(c->tvg_name, g_search))) score = 50;
      if (score > 0 && ch_is_fav(i)) score += 50;
    }
    if (score <= 0) continue;
    if (n < 40) {
      tmp[n].idx = i;
      tmp[n].score = score;
      n++;
    } else {
      int worst = 0;
      for (int k = 1; k < 40; k++) {
        if (tmp[k].score < tmp[worst].score) worst = k;
      }
      if (score > tmp[worst].score) {
        tmp[worst].idx = i;
        tmp[worst].score = score;
      }
    }
  }
  qsort(tmp, (size_t)n, sizeof tmp[0], hit_cmp);
  g_hit_n = n;
  for (int i = 0; i < n; i++) g_hits[i] = tmp[i].idx;
  if (g_hit_i >= g_hit_n) g_hit_i = g_hit_n ? g_hit_n - 1 : -1;
  if (g_hit_i < -1) g_hit_i = -1;
}

static void push_recent(int index) {
  if (!g_st || index < 0) return;
  for (int i = 0; i < g_st->recent_n; i++) {
    if (g_st->recent[i] == index) {
      memmove(&g_st->recent[i], &g_st->recent[i + 1], (size_t)(g_st->recent_n - i - 1) * sizeof(int));
      g_st->recent_n--;
      break;
    }
  }
  if (g_st->recent_n >= MAX_RECENT) g_st->recent_n = MAX_RECENT - 1;
  memmove(&g_st->recent[1], &g_st->recent[0], (size_t)g_st->recent_n * sizeof(int));
  g_st->recent[0] = index;
  g_st->recent_n++;
}

static void open_search(void) {
  if (g_mode == UI_MENU && g_menu_page == MENU_MULTI) g_search_from = 2;
  else if (g_mode == UI_MENU && g_menu_page == MENU_MAIN) g_search_from = 0;
  else if (g_mode != UI_MENU) g_search_from = 1;
  g_menu_page = MENU_SEARCH;
  g_search[0] = 0;
  g_voice_note[0] = 0;
  g_hit_i = -1;
  g_search_focus = 0;
  fill_search();
  set_mode(UI_MENU);
}

static void hist_remove_at(int i) {
  if (!g_st || i < 0 || i >= g_st->recent_n) return;
  if (i < g_st->recent_n - 1)
    memmove(&g_st->recent[i], &g_st->recent[i + 1], (size_t)(g_st->recent_n - i - 1) * sizeof(int));
  g_st->recent_n--;
  persist_ui(0, 0, 0);
  int n = g_st->recent_n;
  int total = n + (n ? 1 : 0);
  if (g_hist_i >= total) g_hist_i = total ? total - 1 : 0;
  if (g_hist_i >= n) g_hist_del = 0;
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void hist_clear(void) {
  if (!g_st) return;
  g_st->recent_n = 0;
  g_st->recent_id_n = 0;
  persist_ui(0, 0, 0);
  g_hist_i = 0;
  g_hist_del = 0;
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void hist_play(int i) {
  if (!g_st || i < 0 || i >= g_st->recent_n) return;
  int ch = g_st->recent[i];
  if (g_pl && ch >= 0 && ch < g_pl->count && g_zap) g_zap(ch);
  set_mode(UI_LIVE);
}

static void open_history(void) {
  g_menu_page = MENU_HISTORY;
  g_hist_i = 0;
  g_hist_del = 0;
  set_mode(UI_MENU);
}

static void open_recs(void) {
  g_menu_page = MENU_RECS;
  g_file_n = rec_scan(g_st, g_files, MAX_REC_FILES);
  g_file_i = rec_timer_get(NULL, NULL, 0, NULL, 0, NULL, NULL, NULL) ? -1 : 0;
  if (g_file_i == 0 && !g_file_n) g_file_i = rec_timer_get(NULL, NULL, 0, NULL, 0, NULL, NULL, NULL) ? -1 : 0;
  set_mode(UI_MENU);
}

static void hide_tiles(void) {
  player_tile_stop_all();
  for (int i = 0; i < 4; i++) {
    if (g_tile_wnd[i]) ShowWindow(g_tile_wnd[i], SW_HIDE);
  }
}

static const char *channel_ua(int ch) {
  if (!g_pl || ch < 0 || ch >= g_pl->count) return g_st ? g_st->user_agent : "";
  return g_pl->items[ch].ua[0] ? g_pl->items[ch].ua : (g_st ? g_st->user_agent : "");
}

static int mv_start_q(void) {
  if (g_mv_n >= 3) return 1;
  if (g_mv_n >= 2) return 2;
  return 4;
}

static int channel_url(int ch, char *out, int max) {
  if (!out || max < 2 || !g_pl || ch < 0 || ch >= g_pl->count) return 0;
  char urls[12][MAX_URL];
  int n = 0;
  build_play_urls(g_pl, ch, mv_start_q(), urls, &n, 12);
  if (n > 0 && urls[0][0]) {
    snprintf(out, max, "%s", urls[0]);
    return 1;
  }
  if (!g_pl->items[ch].url[0]) return 0;
  snprintf(out, max, "%s", g_pl->items[ch].url);
  return 1;
}

static void mv_place_ring(void) {
  if (!g_mv_ring || g_mv_n < 1) return;
  int i = (g_mv_i >= 0 && g_mv_i < g_mv_n) ? g_mv_i : 0;
  RECT *act = &g_mv_rc[i];
  int rw = act->right - act->left, rh = act->bottom - act->top;
  int fr = 4;
  if (rw < 8 || rh < 8) return;
  SetWindowPos(g_mv_ring, HWND_TOP, act->left, act->top, rw, rh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (rw > fr * 2 + 2 && rh > fr * 2 + 2) {
    HRGN outer = CreateRectRgn(0, 0, rw, rh);
    HRGN inner = CreateRectRgn(fr, fr, rw - fr, rh - fr);
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    SetWindowRgn(g_mv_ring, outer, TRUE);
  } else {
    SetWindowRgn(g_mv_ring, NULL, TRUE);
  }
  InvalidateRect(g_mv_ring, NULL, FALSE);
}

static void mv_retune(void) {
  if (g_mv_n < 2) return;
  for (int i = 1; i < g_mv_n; i++) {
    if (g_mv_ch[i] < 0) continue;
    g_mv_need_start[i] = 1;
  }
}

static void mv_focus(int i) {
  if (i < 0 || i >= g_mv_n || i == g_mv_i) return;
  g_mv_i = i;
  mv_apply_audio();
  mv_place_ring();
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  if (g_host) SetFocus(g_host);
}

static int start_tile(int slot) {
  if (slot < 1 || slot >= g_mv_n || !g_tile_wnd[slot - 1]) return 0;
  if (g_mv_ch[slot] < 0) {
    player_tile_stop(slot - 1);
    return 1;
  }
  char url[MAX_URL];
  if (!channel_url(g_mv_ch[slot], url, MAX_URL)) return 0;
  player_set_multiview(1);
  return player_tile_start(slot - 1, g_tile_wnd[slot - 1], url, channel_ua(g_mv_ch[slot]));
}

static void mv_apply_audio(void) {
  if (!g_st) return;
  player_set_pause(0);
  int duck = g_mv_n > 1 && g_mv_i != 0;
  player_set_volume(g_st->volume, g_st->muted || duck);
  for (int i = 1; i < g_mv_n; i++) {
    player_tile_mute(i - 1, i != g_mv_i);
    player_tile_pause(i - 1, 0);
  }
}

static int mv_next_free(void) {
  if (!g_pl || g_pl->count < 1) return -1;
  int start = (g_mv_i >= 0 && g_mv_i < g_mv_n) ? g_mv_ch[g_mv_i] : player_current_index();
  if (start < 0) start = 0;
  for (int k = 1; k <= g_pl->count; k++) {
    int i = (start + k) % g_pl->count;
    const char *grp = g_pl->items[i].group[0] ? g_pl->items[i].group : "Другие";
    if (group_hidden(grp)) continue;
    if (!mv_used(i)) return i;
  }
  return -1;
}

static int mv_add_ch(int ch) {
  if (g_mv_n >= 4) {
    later(i18n_u("mvMax"));
    return 0;
  }
  if (ch >= 0 && (!g_pl || ch >= g_pl->count)) return 0;
  g_mv_ch[g_mv_n] = ch;
  g_mv_spawned[g_mv_n] = 0;
  g_mv_n++;
  g_mv_i = g_mv_n - 1;
  g_mv_menu = 0;
  g_mv_pick = 0;
  layout(g_host);
  mv_retune();
  if (g_host) SetFocus(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  return 1;
}

static int mv_place_ch(int ch) {
  for (int i = 0; i < g_mv_n; i++) {
    if (g_mv_ch[i] < 0) {
      mv_set_ch(i, ch);
      return 1;
    }
  }
  return mv_add_ch(ch);
}

static void mv_set_ch(int slot, int ch) {
  if (slot < 0 || slot >= g_mv_n || !g_pl || ch < 0 || ch >= g_pl->count) return;
  g_mv_ch[slot] = ch;
  if (g_mode != UI_GUIDE) {
    g_mv_pick = 0;
    g_mv_menu = 0;
  }
  if (slot == 0) {
    if (g_st) player_play_at(g_pl, g_st, ch, mv_start_q());
    shell_on_play(ch);
  } else {
    g_mv_need_start[slot] = 1;
  }
  layout(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
}

static void mv_remove(int slot) {
  if (g_mv_n <= 1) {
    close_multi();
    set_mode(UI_LIVE);
    return;
  }
  if (slot < 0 || slot >= g_mv_n) return;
  player_tile_stop_all();
  memset(g_mv_spawned, 0, sizeof g_mv_spawned);
  for (int i = slot; i < g_mv_n - 1; i++) g_mv_ch[i] = g_mv_ch[i + 1];
  g_mv_n--;
  g_mv_spawned[0] = 1;
  if (g_mv_i >= g_mv_n) g_mv_i = g_mv_n - 1;
  g_mv_menu = 0;
  player_set_multiview(g_mv_n > 1);
  layout(g_host);
  if (g_mv_n > 1) mv_retune();
  else if (g_pl && g_st && g_mv_ch[0] >= 0) player_play_at(g_pl, g_st, g_mv_ch[0], 4);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void mv_show_menu(int on) {
  g_mv_menu = on ? 1 : 0;
  if (on) g_mv_opt = 0;
  layout(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void mv_move(int dx, int dy) {
  if (g_mv_n <= 1) return;
  int step = 0;
  if (dx) step = dx > 0 ? 1 : -1;
  else if (dy) step = dy > 0 ? 1 : -1;
  if (!step) return;
  mv_focus((g_mv_i + g_mv_n + step) % g_mv_n);
}

static void mv_open_search(int pick) {
  g_mv_pick = pick;
  g_search[0] = 0;
  g_voice_note[0] = 0;
  g_hit_i = -1;
  g_search_focus = pick == 1 ? 1 : 0;
  g_search_from = 2;
  fill_search();
  layout(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void run_mv_opt(void) {
  int i = g_mv_opt;
  if (i == 0) {
    mv_add_ch(-1);
    return;
  }
  if (i == 1) {
    int empty = 0;
    for (int s = 0; s < g_mv_n; s++) {
      if (g_mv_ch[s] < 0) empty = 1;
    }
    if (!empty && g_mv_n >= 4) {
      later(i18n_u("mvMax"));
      return;
    }
    mv_open_search(empty ? 2 : 1);
    do_voice();
    return;
  }
  if (i == 2) {
    g_mv_menu = 0;
    g_mv_pick = 2;
    open_guide();
    return;
  }
  mv_remove(g_mv_i);
}

static void open_multi(void) {
  player_pip_stop();
  int cur = player_current_index();
  if (cur < 0) {
    later(i18n_u("pipFail"));
    return;
  }
  g_menu_page = MENU_MULTI;
  g_mv_pick = 0;
  g_mv_menu = 0;
  g_mv_opt = 0;
  g_mv_n = 1;
  g_mv_i = 0;
  g_mv_ch[0] = cur;
  memset(g_mv_spawned, 0, sizeof g_mv_spawned);
  memset(g_mv_need_start, 0, sizeof g_mv_need_start);
  g_mv_spawned[0] = 1;
  player_set_pause(0);
  set_mode(UI_MENU);
}

static void close_multi(void) {
  int idx = g_mv_ch[0] >= 0 ? g_mv_ch[0] : player_current_index();
  player_set_multiview(0);
  hide_tiles();
  memset(g_mv_need_start, 0, sizeof g_mv_need_start);
  if (g_mv_ring) ShowWindow(g_mv_ring, SW_HIDE);
  g_mv_n = 0;
  g_mv_pick = 0;
  g_mv_menu = 0;
  if (g_menu_page == MENU_MULTI) g_menu_page = MENU_MAIN;
  player_clear_slot();
  player_set_pause(0);
  if (g_st) player_set_volume(g_st->volume, g_st->muted);
  if (g_host) layout(g_host);
  if (g_pl && g_st && idx >= 0) player_play_at(g_pl, g_st, idx, 4);
}

static void later(const char *msg) { player_show_text(msg, 2000); }

static void do_pip(void) {
  player_pip_stop();
  if (g_mode == UI_MENU && g_menu_page == MENU_MULTI) {
    close_multi();
    set_mode(UI_LIVE);
    return;
  }
  open_multi();
}

static void apply_voice_result(const char *text, int ch, int do_switch) {
  g_voice_note[0] = 0;
  snprintf(g_search, sizeof g_search, "%s", text && text[0] ? text : "");
  fill_search();
  g_hit_i = g_hit_n ? 0 : -1;
  g_search_focus = 0;
  int pick = g_hit_n ? g_hits[0] : ch;
  if (g_mv_pick) {
    if (do_switch && pick >= 0) {
      if (g_mv_pick == 2) mv_set_ch(g_mv_i, pick);
      else mv_place_ch(pick);
      mv_apply_audio();
      return;
    }
    if (!g_hit_n) snprintf(g_voice_note, sizeof g_voice_note, "Канал не найден. Скажите «переключи на» и название из списка.");
    g_menu_page = MENU_MULTI;
    layout(g_host);
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  if (do_switch && pick >= 0 && g_zap) {
    g_zap(pick);
    set_mode(UI_LIVE);
    return;
  }
  g_menu_page = MENU_SEARCH;
  if (!g_hit_n) snprintf(g_voice_note, sizeof g_voice_note, "Канал не найден. Скажите «переключи на» и название из списка.");
  set_mode(UI_MENU);
}

static void do_voice(void) {
  if (g_st && !g_st->voice_enabled) {
    later(i18n_u("micMenuOff"));
    if (g_mode != UI_MENU || g_menu_page != MENU_SEARCH) open_search();
    snprintf(g_voice_note, sizeof g_voice_note, "Микрофон выключен в меню. Включите пункт «Микрофон».");
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  g_voice_note[0] = 0;
  if (g_mode == UI_MENU && g_menu_page == MENU_MULTI) {
    if (!g_mv_pick) {
      int fill = (g_mv_i >= 0 && g_mv_i < g_mv_n && g_mv_ch[g_mv_i] < 0) ? 2 : (g_mv_n < 4 ? 1 : 2);
      mv_open_search(fill);
    }
    g_search_focus = 1;
    g_hit_i = -1;
  } else if (g_mode != UI_MENU || g_menu_page != MENU_SEARCH) {
    if (g_mode != UI_MENU) g_search_from = 1;
    open_search();
    g_search_focus = 1;
  } else {
    g_search_focus = 1;
  }
  voice_start(g_pl, g_st);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static int do_live_rec(void);
static int do_timer(int ch, const char *title, long long s, long long e);

static void finish_ask(void) {
  int why = g_ask_why;
  if (why == 1) {
    do_timer(g_pend_ch, g_pend_title, g_pend_s, g_pend_e);
    open_recs();
  } else {
    do_live_rec();
    set_mode(UI_LIVE);
  }
}

static int proto_has(const wchar_t *name) {
  HKEY k = NULL;
  if (!name || !*name) return 0;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
  RegCloseKey(k);
  return 1;
}

static int pct_enc(const char *in, char *out, int max) {
  static const char *hex = "0123456789ABCDEF";
  int o = 0;
  if (!in || !out || max < 2) return 0;
  for (int i = 0; in[i] && o + 4 < max; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
  return o;
}

static int share_open(const wchar_t *url) {
  if (!url || !url[0]) return 0;
  INT_PTR r = (INT_PTR)ShellExecuteW(g_host ? g_host : NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
  return r > 32;
}

static void share_fill(void) {
  g_share_n = 0;
  g_share_kind[g_share_n++] = SHARE_COPY;
  g_share_kind[g_share_n++] = SHARE_TG;
  g_share_kind[g_share_n++] = SHARE_WA;
  g_share_kind[g_share_n++] = SHARE_MAX;
  g_share_kind[g_share_n++] = SHARE_MAIL;
  if (g_share_i < 0 || g_share_i >= g_share_n) g_share_i = 0;
}

static int share_mail_file(const char *path) {
  wchar_t w[MAX_PATH];
  if (!utf8_to_wide(path, w, MAX_PATH) || !w[0]) return 0;
  char apath[MAX_PATH * 2];
  if (!WideCharToMultiByte(CP_ACP, 0, w, -1, apath, (int)sizeof apath, NULL, NULL)) return 0;
  MapiFileDesc file;
  memset(&file, 0, sizeof file);
  file.nPosition = (ULONG)-1;
  file.lpszPathName = apath;
  file.lpszFileName = "mirefir-backup.json";
  MapiMessage msg;
  memset(&msg, 0, sizeof msg);
  msg.lpszSubject = "MirEfir";
  msg.lpszNoteText = "mirefir-backup.json";
  msg.nFileCount = 1;
  msg.lpFiles = &file;
  ULONG r = MAPISendMail(0, (ULONG_PTR)(g_host ? g_host : NULL), &msg, MAPI_DIALOG | MAPI_LOGON_UI, 0);
  return r == SUCCESS_SUCCESS;
}

static int share_send_file(int kind) {
  char path[MAX_URL];
  if (!profile_backup_internal_path(path, MAX_URL) || !profile_export_backup(g_st, g_pl, path)) return 0;
  clipboard_write_file_drop(path);
  if (kind == SHARE_COPY) {
    set_note(i18n("backupCopied"));
    return 1;
  }
  if (kind == SHARE_MAIL && share_mail_file(path)) {
    set_note(i18n("shareSent"));
    return 1;
  }
  wchar_t url[128];
  url[0] = 0;
  if (kind == SHARE_TG) wcsncpy(url, proto_has(L"tg") ? L"tg://" : L"https://t.me/", 127);
  else if (kind == SHARE_WA) wcsncpy(url, proto_has(L"whatsapp") ? L"whatsapp://" : L"https://web.whatsapp.com/", 127);
  else if (kind == SHARE_MAX) wcsncpy(url, proto_has(L"max") ? L"max://" : L"https://max.ru/", 127);
  else wcsncpy(url, L"mailto:?subject=MirEfir", 127);
  url[127] = 0;
  int ok = share_open(url);
  set_note(ok ? i18n("sharePasteFile") : i18n("shareFailFile"));
  return ok;
}

static int share_send(int kind) {
  char *code = cloud_make_code(g_st, g_pl);
  if (!code) return 0;
  clipboard_write_utf8(code);
  if (kind == SHARE_COPY) {
    free(code);
    set_note(i18n("codeCopied"));
    return 1;
  }
  char *msg = (char *)malloc(16384);
  char *enc = (char *)malloc(49152);
  wchar_t *wenc = (wchar_t *)malloc(24576 * sizeof(wchar_t));
  wchar_t *url = (wchar_t *)malloc(32768 * sizeof(wchar_t));
  if (!msg || !enc || !wenc || !url) {
    free(msg);
    free(enc);
    free(wenc);
    free(url);
    free(code);
    return 0;
  }
  snprintf(msg, 16384, "MirEfir код:\n%s", code);
  free(code);
  pct_enc(msg, enc, 49152);
  int n = 0;
  for (; enc[n] && n < 24575; n++) wenc[n] = (wchar_t)(unsigned char)enc[n];
  wenc[n] = 0;
  url[0] = 0;
  int long_txt = n > 3500;
  if (kind == SHARE_TG) {
    if (long_txt) wcsncpy(url, proto_has(L"tg") ? L"tg://" : L"https://t.me/", 32767);
    else if (proto_has(L"tg")) swprintf(url, 32768, L"tg://msg?text=%s", wenc);
    else swprintf(url, 32768, L"https://t.me/share/url?text=%s", wenc);
  } else if (kind == SHARE_WA) {
    if (long_txt) wcsncpy(url, proto_has(L"whatsapp") ? L"whatsapp://" : L"https://web.whatsapp.com/", 32767);
    else if (proto_has(L"whatsapp")) swprintf(url, 32768, L"whatsapp://send?text=%s", wenc);
    else swprintf(url, 32768, L"https://wa.me/?text=%s", wenc);
  } else if (kind == SHARE_MAX) {
    if (long_txt) wcsncpy(url, proto_has(L"max") ? L"max://" : L"https://max.ru/", 32767);
    else if (proto_has(L"max")) swprintf(url, 32768, L"max://share?text=%s", wenc);
    else swprintf(url, 32768, L"https://max.ru/:share?text=%s", wenc);
  } else {
    swprintf(url, 32768, L"mailto:?subject=MirEfir&body=%s", wenc);
  }
  url[32767] = 0;
  int ok = share_open(url);
  free(msg);
  free(enc);
  free(wenc);
  free(url);
  if (!ok) return 0;
  set_note(long_txt ? i18n("sharePaste") : i18n("shareSent"));
  return 1;
}

static void open_share(int file) {
  g_share_file = file ? 1 : 0;
  share_fill();
  g_share_i = 0;
  g_menu_page = MENU_SHARE;
  layout(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void apply_share(void) {
  if (g_share_i < 0 || g_share_i >= g_share_n) return;
  int ok = g_share_file ? share_send_file(g_share_kind[g_share_i]) : share_send(g_share_kind[g_share_i]);
  if (ok) backup_back_to_data();
  else set_note(i18n(g_share_file ? "shareFailFile" : "shareFail"));
}

static void backup_back_to_data(void) {
  g_menu_page = MENU_SETTINGS;
  g_set_tab = 9;
  g_set_pane = 1;
  layout(g_host);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static void note_file(const wchar_t *title, const char *path) {
  wchar_t w[MAX_PATHU];
  utf8_to_wide(path ? path : "", w, MAX_PATHU);
  if (!w[0]) {
    set_note(title);
    return;
  }
  wchar_t msg[160];
  int n = (int)wcslen(w);
  const wchar_t *show = n > 72 ? w + n - 72 : w;
  swprintf(msg, 160, L"%s: %s", title, show);
  set_note(msg);
}

static void do_backup_export(void) {
  char path[MAX_URL];
  path[0] = 0;
  if (!pick_json_save(path, MAX_URL)) {
    if (g_host) SetFocus(g_host);
    set_note(i18n("fileNone"));
    return;
  }
  if (g_host) SetFocus(g_host);
  if (profile_export_backup(g_st, g_pl, path)) note_file(i18n("backupSaved"), path);
  else set_note(i18n("backupFail"));
}

static void apply_backup_file(const char *path) {
  if (!path || !path[0] || !profile_import_backup(g_st, g_pl, path)) {
    set_note(i18n("backupFail"));
    return;
  }
  rebuild_groups();
  remember_group(0);
  fill_group(0);
  if (g_zap) g_zap(0);
  if (g_pl) epg_start(g_pl, g_st);
  persist_ui(1, 1, 1);
  set_note(i18n("backupLoaded"));
}

static void do_backup_import(void) {
  char path[MAX_URL];
  path[0] = 0;
  if (!pick_json_open(path, MAX_URL)) {
    if (g_host) SetFocus(g_host);
    set_note(i18n("fileNone"));
    return;
  }
  if (g_host) SetFocus(g_host);
  apply_backup_file(path);
}

static int apply_backup_ask(void) {
  if (g_ask_why == 2) {
    do_backup_export();
    backup_back_to_data();
    return 1;
  }
  do_backup_import();
  backup_back_to_data();
  return 1;
}

static int apply_ask(void) {
  if (!g_st) return 0;
  if (g_ask_why >= 2) return apply_backup_ask();
  if (g_ask_i == 0) {
    rec_internal_dir(g_st->rec_path, MAX_PATHU);
    g_st->recordings_enabled = 1;
    persist_ui(0, 0, 0);
    later(i18n_u("recInternal"));
    finish_ask();
    return 1;
  }
  char path[MAX_PATHU];
  if (!pick_folder(path, MAX_PATHU) || !path[0]) {
    if (g_host) SetFocus(g_host);
    later(i18n_u("folderNone"));
    return 0;
  }
  if (g_host) SetFocus(g_host);
  snprintf(g_st->rec_path, MAX_PATHU, "%s", path);
  g_st->recordings_enabled = 1;
  persist_ui(0, 0, 0);
  later(i18n_u("folderSaved"));
  finish_ask();
  return 1;
}

static int do_live_rec(void) {
  if (!g_st || !g_pl) return 0;
  if (!g_st->recordings_enabled) {
    later(i18n_u("recOff"));
    return 0;
  }
  if (player_playing_file()) {
    later(i18n_u("recNeedLive"));
    return 0;
  }
  if (player_recording()) {
    rec_live_stop();
    later(i18n_u("recStopped"));
    return 1;
  }
  int idx = player_current_index();
  if (idx < 0) return 0;
  if (!g_st->rec_path[0]) {
    g_ask_why = 0;
    g_ask_i = 0;
    g_menu_page = MENU_ASKREC;
    set_mode(UI_MENU);
    return 0;
  }
  int r = rec_live_start(g_pl, g_st, idx);
  if (r == 1) later(i18n_u("recOn"));
  else if (r == 2) later(i18n_u("recStopped"));
  else if (r == -1) later(i18n_u("recOff"));
  else if (r == -2) {
    g_ask_why = 0;
    g_ask_i = 0;
    g_menu_page = MENU_ASKREC;
    set_mode(UI_MENU);
    return 0;
  } else if (r == -3) later(i18n_u("recNoSpace"));
  else if (r == -4) later(i18n_u("recNoDisk"));
  else if (r == -5) later(i18n_u("recNoPerm"));
  else later(i18n_u("recFail"));
  return 1;
}

static int do_timer(int ch, const char *title, long long s, long long e) {
  if (!g_st || !g_pl) return 0;
  if (!g_st->recordings_enabled) {
    later(i18n_u("recOff"));
    return 0;
  }
  if (!g_st->rec_path[0]) {
    g_ask_why = 1;
    g_pend_ch = ch;
    snprintf(g_pend_title, 96, "%s", title && title[0] ? title : "Запись");
    g_pend_s = s;
    g_pend_e = e;
    g_ask_i = 0;
    g_menu_page = MENU_ASKREC;
    set_mode(UI_MENU);
    return 0;
  }
  int r = rec_timer_toggle(g_pl, g_st, ch, title, s, e);
  if (r == 1) later(i18n_u("recTimerOn"));
  else if (r == 2) later(i18n_u("recTimerOff"));
  else later(i18n_u("recTimerFail"));
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  return r != 0;
}

static void restore_live(void) {
  int idx = player_current_index();
  if (g_zap && idx >= 0) g_zap(idx);
}

static int extra_page(void) {
  return g_menu_page == MENU_SEARCH || g_menu_page == MENU_HISTORY || g_menu_page == MENU_RECS ||
         g_menu_page == MENU_MULTI || g_menu_page == MENU_ASKREC || g_menu_page == MENU_SHARE ||
         g_menu_page == MENU_MICPERM || g_menu_page == MENU_SETUP || g_menu_page == MENU_URL;
}

static int is_ok_key(WPARAM key) {
  return key == VK_RETURN || key == VK_SELECT || key == VK_EXECUTE || key == VK_GAMEPAD_A;
}

static int typing_field(void) {
  return g_mode == UI_MENU && (g_menu_page == MENU_SEARCH || g_menu_page == MENU_URL || g_menu_page == MENU_PIN ||
                              (g_menu_page == MENU_MULTI && g_mv_pick));
}

int shell_typing(void) { return typing_field(); }

static int letter_key(WPARAM key) {
  return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == VK_SPACE;
}

WPARAM shell_map_key(WPARAM key) {
  switch (key) {
    case VK_GAMEPAD_A:
      return VK_RETURN;
    case VK_GAMEPAD_B:
    case VK_GAMEPAD_VIEW:
    case VK_BROWSER_BACK:
      return VK_ESCAPE;
    case VK_GAMEPAD_DPAD_UP:
      return VK_UP;
    case VK_GAMEPAD_DPAD_DOWN:
      return VK_DOWN;
    case VK_GAMEPAD_DPAD_LEFT:
      return VK_LEFT;
    case VK_GAMEPAD_DPAD_RIGHT:
      return VK_RIGHT;
    case VK_GAMEPAD_MENU:
    case VK_LAUNCH_APP1:
      return VK_APPS;
    default:
      return key;
  }
}

static int play_archive_at(int ch, long long at, long long origin_s, long long origin_e) {
  if (!g_pl || !g_st || ch < 0 || ch >= g_pl->count) return 0;
  if (at < origin_s) at = origin_s;
  if (origin_e > origin_s && at > origin_e - 400) at = origin_e - 400;
  long long now = epg_now_ms();
  if (at >= now - 1200) {
    if (g_zap) g_zap(ch);
    return 1;
  }
  if (!can_play_archive(&g_pl->items[ch], g_st, at, origin_e)) return 0;
  char urls[12][MAX_URL];
  int n = catchup_url_candidates(&g_pl->items[ch], at, origin_e > at ? origin_e : at + 60LL * 60 * 1000, urls, 12);
  if (!n) return 0;
  return player_play_archive(g_pl, g_st, ch, urls, n, at, origin_s, origin_e);
}

static int pad_seek_to(long long at) {
  int ch = player_current_index();
  long long start = 0, end = 0, pos = 0;
  if (player_is_archive()) {
    if (!player_archive_bounds(&start, &end, &pos)) return 0;
    return play_archive_at(ch, at, start, end);
  }
  if (!epg_now(ch, NULL, 0, NULL, 0, &start, &end) || end <= start) return 0;
  long long now = epg_now_ms();
  if (at > now - 1200) {
    later(i18n_u("liveAir"));
    return 1;
  }
  if (at < start) at = start;
  if (!play_archive_at(ch, at, start, end)) {
    later(i18n_u("archiveOff"));
    return 0;
  }
  return 1;
}

static void pad_nudge(int seconds) {
  long long start = 0, end = 0, pos = 0;
  if (player_is_archive()) {
    if (!player_archive_bounds(&start, &end, &pos)) {
      later(i18n_u("archiveOff"));
      return;
    }
    if (!pad_seek_to(pos + (long long)seconds * 1000)) later(i18n_u("archiveOff"));
    return;
  }
  if (seconds > 0) {
    later(i18n_u("liveAir"));
    return;
  }
  if (!pad_bounds(NULL, NULL, NULL)) {
    later(i18n_u("archiveOff"));
    return;
  }
  int ch = player_current_index();
  if (!epg_now(ch, NULL, 0, NULL, 0, &start, &end) || end <= start) {
    later(i18n_u("archiveOff"));
    return;
  }
  if (!pad_seek_to(epg_now_ms() + (long long)seconds * 1000)) later(i18n_u("archiveOff"));
}

static void pad_seek_ratio(double ratio) {
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  long long start = 0, end = 0, pos = 0;
  if (player_is_archive()) {
    if (!player_archive_bounds(&start, &end, &pos) || end <= start) return;
    pad_seek_to(start + (long long)(ratio * (double)(end - start)));
    return;
  }
  int ch = player_current_index();
  if (!epg_now(ch, NULL, 0, NULL, 0, &start, &end) || end <= start) return;
  pad_seek_to(start + (long long)(ratio * (double)(end - start)));
}

static int activate_focused_program(void) {
  int ch = guide_channel();
  if (!g_pl || ch < 0 || ch >= g_pl->count) return 0;
  long long ps = 0, pe = 0;
  if (!epg_list(ch, g_prog_i, NULL, 0, NULL, 0, &ps, &pe) || !ps) {
    if (ch != player_current_index() && g_zap) g_zap(ch);
    set_mode(UI_LIVE);
    return 1;
  }
  long long now = epg_now_ms();
  if (ps <= now && now < pe) {
    if (ch != player_current_index() && g_zap) g_zap(ch);
    set_mode(UI_LIVE);
    return 1;
  }
  if (ps > now) {
    char title[96];
    title[0] = 0;
    epg_list(ch, g_prog_i, title, (int)sizeof title, NULL, 0, NULL, NULL);
    do_timer(ch, title, ps, pe);
    return 1;
  }
  if (!can_play_archive(&g_pl->items[ch], g_st, ps, pe)) {
    later(i18n_u("archiveOff"));
    return 0;
  }
  if (!play_archive_at(ch, ps, ps, pe)) {
    later(i18n_u("archiveOff"));
    return 0;
  }
  set_mode(UI_LIVE);
  return 1;
}

static void ask_exit(void) {
  if (g_st && !g_st->confirm_exit) {
    if (g_quit) g_quit();
    return;
  }
  g_exit_i = 0;
  set_mode(UI_EXIT);
}

static void run_pad(int i) {
  g_pad_until = GetTickCount() + 6500;
  switch (i) {
    case 0:
      g_menu_i = 0;
      g_menu_page = MENU_MAIN;
      set_mode(UI_MENU);
      break;
    case 1:
      open_guide();
      break;
    case 2:
      do_voice();
      break;
    case 3:
      pad_nudge(-30);
      InvalidateRect(g_pad, NULL, FALSE);
      break;
    case 4:
      player_toggle_pause();
      InvalidateRect(g_pad, NULL, FALSE);
      break;
    case 5:
      pad_nudge(30);
      InvalidateRect(g_pad, NULL, FALSE);
      break;
    case 6:
      if (player_is_archive() && g_zap) g_zap(player_current_index());
      else later(i18n_u("liveAir"));
      break;
    case 7:
      player_cycle_audio();
      later(i18n_u("audioTrack"));
      break;
    case 8:
      do_pip();
      break;
    case 9:
      do_live_rec();
      InvalidateRect(g_pad, NULL, FALSE);
      break;
  }
}

static void open_settings_tab(int tab) {
  g_set_tab = tab;
  if (g_set_tab < 0) g_set_tab = 0;
  if (g_set_tab >= SET_TAB_N) g_set_tab = SET_TAB_N - 1;
  g_set_pane = 0;
  g_set_i = 0;
  g_set_scroll = 0;
  close_act();
  if (g_st && g_st->parental_enabled && g_st->lock_settings && !g_pin_ok) {
    g_menu_page = MENU_PIN;
    g_pin_buf[0] = 0;
  } else {
    g_menu_page = MENU_SETTINGS;
  }
  InvalidateRect(g_menu, NULL, FALSE);
}

static void open_stub(int i) {
  g_menu_page = MENU_STUB;
  g_stub_i = i;
  InvalidateRect(g_menu, NULL, FALSE);
}

static void run_menu(int i) {
  const char *id = kMenuId[i];
  if (strcmp(id, "live") == 0) {
    g_resume_guide = 0;
    g_menu_page = MENU_MAIN;
    set_mode(UI_LIVE);
    return;
  }
  if (strcmp(id, "microphone") == 0) {
    if (g_st) {
      g_st->voice_enabled = !g_st->voice_enabled;
      persist_ui(0, 0, 0);
    }
    InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  if (strcmp(id, "exit") == 0) {
    if (g_quit) g_quit();
    return;
  }
  if (strcmp(id, "guide") == 0) {
    open_settings_tab(1);
    return;
  }
  if (strcmp(id, "playlists") == 0) {
    open_settings_tab(0);
    return;
  }
  if (strcmp(id, "settings") == 0) {
    open_settings_tab(2);
    return;
  }
  if (strcmp(id, "search") == 0) {
    open_search();
    return;
  }
  if (strcmp(id, "history") == 0) {
    open_history();
    return;
  }
  if (strcmp(id, "recordings") == 0) {
    open_recs();
    return;
  }
  if (strcmp(id, "multiview") == 0) {
    open_multi();
    layout(g_host);
    InvalidateRect(g_menu, NULL, FALSE);
    return;
  }
  open_stub(i);
}

static void go_back(void) {
  DWORD now = GetTickCount();
  if (now - g_esc_at < 250) return;
  g_esc_at = now;
  if (g_chm == CHM_MOVE) {
    if (g_st) {
      memcpy(g_st->fav, g_fav_bak, sizeof g_st->fav);
      g_st->fav_n = g_fav_bak_n;
      fill_group(1);
    }
    g_chm = CHM_OFF;
    layout(g_host);
    if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
    return;
  }
  if (g_chm) {
    g_chm = CHM_OFF;
    layout(g_host);
    if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
    return;
  }
  if (g_mode == UI_EXIT) {
    if (g_quit) g_quit();
    return;
  }
  if (g_mode == UI_NOTE) {
    set_mode(UI_MENU);
    return;
  }
  if (g_mode == UI_GUIDE) {
    if (g_guide_view == VIEW_GROUPS) {
      restore_group_cursor();
      g_guide_view = VIEW_CHANNELS;
      g_focus_col = COL_CHANNELS;
      g_days_open = 0;
      InvalidateRect(g_guide, NULL, FALSE);
      layout(g_host);
      return;
    }
    if (g_guide_view == VIEW_SCHEDULE || g_focus_col == COL_PROGRAMS || g_focus_col == COL_DAYS ||
        g_focus_col == COL_DETAIL) {
      g_guide_view = VIEW_CHANNELS;
      g_focus_col = COL_CHANNELS;
      g_days_open = 0;
      InvalidateRect(g_guide, NULL, FALSE);
      layout(g_host);
      return;
    }
    if (g_guide_view == VIEW_CATEGORIES) {
      g_guide_view = VIEW_GROUPS;
      g_focus_col = COL_GROUPS;
      InvalidateRect(g_guide, NULL, FALSE);
      layout(g_host);
      return;
    }
    close_guide();
    return;
  }
  if (g_mode == UI_MENU) {
    if (g_wait_key) {
      g_wait_key = 0;
      set_note(L"Отмена");
      InvalidateRect(g_menu, NULL, FALSE);
      return;
    }
    if (g_menu_page == MENU_PIN && g_pin_from_group) {
      g_pin_from_group = 0;
      g_menu_page = MENU_MAIN;
      set_mode(UI_GUIDE);
      return;
    }
    if (g_menu_page == MENU_SETTINGS && g_act) {
      close_act();
      InvalidateRect(g_menu, NULL, FALSE);
      return;
    }
    if (g_menu_page == MENU_URL) {
      if (g_act) {
        close_act();
        InvalidateRect(g_menu, NULL, FALSE);
        return;
      }
      url_close();
      return;
    }
    if (g_menu_page == MENU_SETTINGS && g_set_pane == 1) {
      g_set_pane = 0;
      InvalidateRect(g_menu, NULL, FALSE);
      return;
    }
    if (g_menu_page == MENU_SETUP) {
      g_menu_page = MENU_MAIN;
      if (needs_setup()) set_mode(UI_MENU);
      else set_mode(UI_LIVE);
      return;
    }
    if (g_menu_page != MENU_MAIN) {
      if (g_menu_page == MENU_SEARCH || g_menu_page == MENU_MICPERM || (g_menu_page == MENU_MULTI && g_mv_pick))
        voice_stop();
      if (g_menu_page == MENU_MICPERM) {
        g_menu_page = MENU_SEARCH;
        InvalidateRect(g_menu, NULL, FALSE);
        return;
      }
      if (g_menu_page == MENU_MULTI && g_mv_pick) {
        g_mv_pick = 0;
        g_search[0] = 0;
        g_voice_note[0] = 0;
        InvalidateRect(g_menu, NULL, FALSE);
        layout(g_host);
        return;
      }
      if (g_menu_page == MENU_MULTI && g_mv_menu) {
        mv_show_menu(0);
        return;
      }
      if (g_menu_page == MENU_MULTI) {
        close_multi();
        set_mode(UI_LIVE);
        return;
      }
      if (g_menu_page == MENU_ASKREC && g_ask_why >= 2) {
        backup_back_to_data();
        return;
      }
      if (g_menu_page == MENU_SHARE) {
        backup_back_to_data();
        return;
      }
      if (g_menu_page == MENU_SEARCH) {
        if (g_search_from == 2) {
          g_menu_page = MENU_MULTI;
          g_mv_pick = 0;
          layout(g_host);
          if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
          return;
        }
        if (g_search_from == 1) {
          set_mode(UI_LIVE);
          return;
        }
      }
      g_menu_page = MENU_MAIN;
      InvalidateRect(g_menu, NULL, FALSE);
      layout(g_host);
      return;
    }
    if (g_resume_guide) {
      g_resume_guide = 0;
      g_guide_view = VIEW_GROUPS;
      g_focus_col = COL_GROUPS;
      set_mode(UI_GUIDE);
      return;
    }
    set_mode(UI_LIVE);
    return;
  }
  if (g_mode == UI_PAD) {
    set_mode(UI_LIVE);
    return;
  }
  if (player_playing_file()) {
    int back_recs = g_from_recs;
    g_from_recs = 0;
    restore_live();
    if (back_recs) {
      open_recs();
      return;
    }
    set_mode(UI_LIVE);
    return;
  }
  if (g_fs && g_fs(2)) return;
  ask_exit();
}

static LRESULT CALLBACK uiProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      if (g_host) SetFocus(g_host);
      return MA_NOACTIVATE;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_APPCOMMAND:
      if (g_host) PostMessageW(g_host, msg, wp, lp);
      return 0;
    case WM_RBUTTONDOWN:
      if (g_host) SetFocus(g_host);
      shell_context();
      return 0;
    case WM_TIMER:
      if (hwnd == g_guide && g_marquee && g_mode == UI_GUIDE) InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    case WM_PAINT:
      if (hwnd == g_clock) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        refresh_clock();
        return 0;
      } else if (hwnd == g_pad) paint_pad(hwnd);
      else if (hwnd == g_menu) paint_menu(hwnd);
      else if (hwnd == g_guide) paint_guide(hwnd);
      else if (hwnd == g_exit) paint_exit(hwnd);
      else if (hwnd == g_upd) paint_upd(hwnd);
      else paint_panel(hwnd, NULL);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN: {
      SetFocus(g_host);
      int x = LOWORD(lp), y = HIWORD(lp);
      if (hwnd == g_upd) {
        RECT urc;
        GetClientRect(hwnd, &urc);
        int split = urc.right > 8 ? urc.right * UPD_APPLY_W / UPD_W : urc.right / 2;
        if (x < split) {
          if (g_st) profile_save_session(g_st, player_current_index(), g_st->last_name);
          persist_ui(0, 0, 0);
          if (!update_apply(g_host)) set_note(L"Не удалось установить обновление");
        } else {
          update_later();
          layout(g_host);
        }
        return 0;
      }
      if (hwnd == g_menu) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (extra_page()) {
          if (g_menu_page == MENU_URL) {
            POINT pt = {x, y};
            if (g_act == ACT_URL && g_act_n) {
              if (x >= g_act_x && x < g_act_x + g_act_w && y >= g_act_y && y < g_act_y + 8 + g_act_n * g_act_row) {
                int i = (y - g_act_y - 8) / (g_act_row ? g_act_row : 44);
                if (i >= 0 && i < g_act_n) {
                  g_act_cur = i;
                  run_act(i);
                }
                return 0;
              }
              close_act();
              InvalidateRect(g_menu, NULL, FALSE);
              return 0;
            }
            if (PtInRect(&g_url_paste_rc, pt)) {
              g_url_btn = 1;
              url_paste_field();
              return 0;
            }
            if (PtInRect(&g_url_ok_rc, pt)) {
              g_url_btn = 2;
              url_submit();
              return 0;
            }
            if (PtInRect(&g_url_field_rc, pt)) {
              g_url_btn = 0;
              InvalidateRect(g_menu, NULL, FALSE);
              return 0;
            }
            go_back();
            return 0;
          }
          if (g_menu_page == MENU_SETUP) {
            if (x > g_menu_w) {
              g_menu_page = MENU_MAIN;
              if (needs_setup()) set_mode(UI_MENU);
              else set_mode(UI_LIVE);
              return 0;
            }
            int i = (y - 110) / 56;
            if (i >= 0 && i < 4) {
              g_setup_i = i;
              if (i == 3) {
                g_menu_page = MENU_MAIN;
                if (needs_setup()) set_mode(UI_MENU);
                else set_mode(UI_LIVE);
                return 0;
              }
              run_setup_row();
            }
            return 0;
          }
          if (search_like()) {
            POINT pt = {x, y};
            if (g_act == ACT_SEARCH && g_act_n) {
              if (x >= g_act_x && x < g_act_x + g_act_w && y >= g_act_y && y < g_act_y + 8 + g_act_n * g_act_row) {
                int i = (y - g_act_y - 8) / (g_act_row ? g_act_row : 44);
                if (i >= 0 && i < g_act_n) {
                  g_act_cur = i;
                  run_act(i);
                }
                return 0;
              }
              close_act();
              InvalidateRect(g_menu, NULL, FALSE);
              return 0;
            }
            if (PtInRect(&g_search_mic, pt)) {
              g_search_focus = 1;
              g_hit_i = -1;
              do_voice();
              return 0;
            }
            if (PtInRect(&g_search_inp, pt)) {
              g_search_focus = 0;
              g_hit_i = -1;
              InvalidateRect(g_menu, NULL, FALSE);
              return 0;
            }
            if (PtInRect(&g_search_card, pt) && g_hit_n && g_menu_page != MENU_MICPERM) {
              int row = 56;
              int y0 = g_search_list_y ? g_search_list_y : g_search_inp.bottom + 14;
              int i = (y - y0) / row;
              if (i >= 0 && i < g_hit_n) {
                g_hit_i = i;
                int ch = g_hits[g_hit_i];
                if (g_menu_page == MENU_MULTI) {
                  if (g_mv_pick == 2) mv_set_ch(g_mv_i, ch);
                  else mv_place_ch(ch);
                } else if (g_zap) {
                  g_zap(ch);
                  set_mode(UI_LIVE);
                }
              }
              return 0;
            }
            go_back();
            return 0;
          }
          if (g_menu_page == MENU_MULTI) {
            if (g_mv_menu) {
              int nopt = mv_opt_n();
              int i = (y - 10) / 48;
              if (i >= 0 && i < nopt) {
                g_mv_opt = i;
                run_mv_opt();
              }
              return 0;
            }
            mv_show_menu(1);
            return 0;
          }
          if (g_menu_page == MENU_HISTORY) {
            int mw = g_menu_w > 0 ? g_menu_w : 480;
            if (mw < 420) mw = 420;
            if (mw > 640) mw = 640;
            if (x > mw) {
              g_resume_guide = 0;
              g_menu_page = MENU_MAIN;
              set_mode(UI_LIVE);
              return 0;
            }
            int n = g_st ? g_st->recent_n : 0;
            if (n > MAX_RECENT) n = MAX_RECENT;
            int y0 = 88, row = 52;
            if (y >= y0 && n) {
              int i = (y - y0) / row;
              if (i >= 0 && i < n) {
                g_hist_i = i;
                if (x >= mw - 52) {
                  g_hist_del = 1;
                  hist_remove_at(i);
                } else {
                  g_hist_del = 0;
                  hist_play(i);
                }
                return 0;
              }
              if (i == n) {
                g_hist_i = n;
                g_hist_del = 0;
                hist_clear();
                return 0;
              }
            }
            return 0;
          }
          if (x > g_menu_w) {
            g_resume_guide = 0;
            g_menu_page = MENU_MAIN;
            set_mode(UI_LIVE);
          }
          return 0;
        }
        if (g_menu_page == MENU_STUB) {
          go_back();
          return 0;
        }
        if (g_menu_page == MENU_SETTINGS) {
          int mw = menu_panel_w(rc.right);
          int tw = settings_nav_w(mw);
          if (g_act && x >= g_act_x && x < g_act_x + g_act_w && y >= g_act_y && y < g_act_y + 8 + g_act_n * g_act_row) {
            int i = (y - g_act_y - 8) / (g_act_row ? g_act_row : 44);
            if (i >= 0 && i < g_act_n) {
              g_act_cur = i;
              run_act(i);
              InvalidateRect(g_menu, NULL, FALSE);
            }
            return 0;
          }
          if (g_act) {
            close_act();
            InvalidateRect(g_menu, NULL, FALSE);
            return 0;
          }
          if (x > mw) {
            g_resume_guide = 0;
            g_menu_page = MENU_MAIN;
            set_mode(UI_LIVE);
            return 0;
          }
          if (x < tw) {
            if (y < 56) {
              go_back();
              return 0;
            }
            int i = (y - 60) / settings_tab_h(rc.bottom);
            if (i >= 0 && i < SET_TAB_N) {
              g_set_tab = i;
              g_set_pane = 0;
              g_set_i = 0;
              g_set_scroll = 0;
              close_act();
              InvalidateRect(g_menu, NULL, FALSE);
            }
            return 0;
          }
          fill_set_rows();
          int i = -1, yy = 70;
          for (int k = g_set_scroll; k < g_sn; k++) {
            int gap = g_sid[k] == SID_HEAD ? SET_HEAD_GAP : 0;
            int h = g_sid[k] == SID_HEAD ? SET_HEAD_H : SET_ROW_H;
            if (y >= yy + gap && y < yy + gap + h) {
              i = k;
              break;
            }
            yy += gap + h;
          }
          if (i >= 0 && i < g_sn && set_row_selectable(i)) {
            g_set_pane = 1;
            g_set_i = i;
            activate_set_row();
            InvalidateRect(g_menu, NULL, FALSE);
          }
          return 0;
        }
        if (x > g_menu_w) {
          set_mode(UI_LIVE);
          return 0;
        }
        int i = (y - 78) / 56;
        if (i >= 0 && i < MENU_N) {
          g_menu_i = i;
          run_menu(i);
        }
        return 0;
      }
      if (hwnd == g_guide) {
        RECT grc;
        GetClientRect(hwnd, &grc);
        GuideBox b;
        guide_box(grc.right, grc.bottom, &b);
        if (x >= b.gw + b.cw + b.pw + b.dw) {
          close_guide();
          return 0;
        }
        int slot = y < b.y0 ? -1 : (y - b.y0) / b.row;
        if (x < b.gw) {
          g_focus_col = COL_GROUPS;
          int g0 = g_group_i - b.vis / 2;
          if (g0 < 0) g0 = 0;
          int idx = g0 + slot;
          if (slot >= 0 && idx >= 0 && idx < g_group_n) {
            g_group_i = idx;
            if (!enter_folder()) return 0;
          }
        } else if (x < b.gw + b.cw) {
          g_focus_col = COL_CHANNELS;
          int c0 = g_chan_i - b.vis / 2;
          if (c0 < 0) c0 = 0;
          int idx = c0 + slot;
          if (slot >= 0 && idx >= 0 && idx < g_in_group_n) {
            g_chan_i = idx;
            activate_channel();
          }
        } else if (x < b.gw + b.cw + b.pw) {
          g_focus_col = COL_PROGRAMS;
        } else if (b.dw > 0) {
          int drow = 58, dy = 40;
          int dvis = (grc.bottom - dy - 8) / drow;
          if (dvis < 1) dvis = 1;
          int d0 = g_day_i - dvis / 2;
          if (d0 < 0) d0 = 0;
          if (g_day_n && d0 > g_day_n - dvis) d0 = g_day_n - dvis;
          if (d0 < 0) d0 = 0;
          int i = y >= dy ? (y - dy) / drow : -1;
          int idx = d0 + i;
          g_focus_col = COL_DAYS;
          if (i >= 0 && idx >= 0 && idx < g_day_n) {
            g_day_i = idx;
            g_prog_i = epg_index_for_day(guide_channel(), g_days[g_day_i]);
          }
        }
        layout(g_host);
        InvalidateRect(g_guide, NULL, FALSE);
        return 0;
      }
      if (hwnd == g_pad) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        PadGeom g;
        pad_geom(rc, &g);
        if (y >= g.bar.top - 8) {
          int bw = g.bar.right - g.bar.left;
          double ratio = bw > 0 ? (double)(x - g.bar.left) / (double)bw : 0;
          g_pad_focus = PAD_BAR;
          pad_seek_ratio(ratio);
          g_pad_until = GetTickCount() + 6500;
          InvalidateRect(g_pad, NULL, FALSE);
          return 0;
        }
        for (int i = 0; i < PAD_N; i++) {
          if (x >= g.btn[i].left && x < g.btn[i].right && y >= g.btn[i].top && y < g.btn[i].bottom) {
            g_pad_focus = PAD_BUTTONS;
            g_pad_i = i;
            run_pad(i);
            InvalidateRect(g_pad, NULL, FALSE);
            return 0;
          }
        }
        return 0;
      }
      if (hwnd == g_exit) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        g_exit_i = (x < rc.right / 2) ? 0 : 1;
        if (g_exit_i == 0) {
          if (g_quit) g_quit();
        } else set_mode(UI_LIVE);
        return 0;
      }
      break;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void shell_request_exit(void) {
  if (g_mode == UI_EXIT) {
    if (g_quit) g_quit();
    return;
  }
  ask_exit();
}

static LRESULT CALLBACK ringProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int r = 37, g = 99, b = 235;
    hi_rgb(&r, &g, &b);
    HBRUSH br = CreateSolidBrush(RGB(r, g, b));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_ERASEBKGND) return 1;
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR ||
       msg == WM_APPCOMMAND) &&
      g_host) {
    PostMessageW(g_host, msg, wp, lp);
    return 0;
  }
  if (msg == WM_RBUTTONDOWN) {
    if (g_host) SetFocus(g_host);
    shell_context();
    return 0;
  }
  if (msg == WM_LBUTTONDOWN && g_host) {
    PostMessageW(g_host, WM_LBUTTONDOWN, wp, lp);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK tileProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH br = CreateSolidBrush(RGB(8, 10, 14));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    int slot = (int)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (slot >= 1 && slot < g_mv_n && g_mv_ch[slot] < 0) {
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(170, 178, 190));
      if (g_font_sm) SelectObject(hdc, g_font_sm);
      DrawTextW(hdc, i18n("mvEmpty"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_LBUTTONDOWN) {
    int slot = (int)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    mv_focus(slot);
    return 0;
  }
  if (msg == WM_RBUTTONDOWN) {
    if (g_host) SetFocus(g_host);
    shell_context();
    return 0;
  }
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR ||
       msg == WM_APPCOMMAND) &&
      g_host) {
    PostMessageW(g_host, msg, wp, lp);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void shell_init(HWND host, Playlist *pl, Settings *st, void (*zap)(int), void (*quit)(void), int (*fullscreen)(int)) {
  g_host = host;
  g_pl = pl;
  g_st = st;
  g_zap = zap;
  g_quit = quit;
  g_fs = fullscreen;
  WNDCLASSW wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = uiProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hbrBackground = GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kUiClass;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassW(&wc);
  rebuild_fonts();
  g_clock = make_ui(host, 1, 1);
  g_pad = make_ui(host, 0, 0);
  g_menu = make_ui(host, 0, 0);
  g_guide = make_ui(host, 0, 0);
  if (g_guide) SetTimer(g_guide, 1, 50, NULL);
  g_exit = make_ui(host, 0, 0);
  g_upd = make_ui(host, 0, 0);
  {
    WNDCLASSW twc;
    memset(&twc, 0, sizeof twc);
    twc.lpfnWndProc = tileProc;
    twc.hInstance = GetModuleHandleW(NULL);
    twc.hbrBackground = GetStockObject(BLACK_BRUSH);
    twc.lpszClassName = L"MirEfirTile";
    RegisterClassW(&twc);
    for (int i = 0; i < 4; i++) {
      g_tile_wnd[i] = CreateWindowExW(0, L"MirEfirTile", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 16,
                                     16, host, NULL, GetModuleHandleW(NULL), NULL);
      if (g_tile_wnd[i]) {
        SetPropW(g_tile_wnd[i], L"mirefir-tile", (HANDLE)1);
        SetWindowLongPtrW(g_tile_wnd[i], GWLP_USERDATA, (LONG_PTR)(i + 1));
      }
    }
    WNDCLASSW rwc;
    memset(&rwc, 0, sizeof rwc);
    rwc.lpfnWndProc = ringProc;
    rwc.hInstance = GetModuleHandleW(NULL);
    rwc.hbrBackground = GetStockObject(BLACK_BRUSH);
    rwc.lpszClassName = L"MirEfirRing";
    RegisterClassW(&rwc);
    g_mv_ring = CreateWindowExW(0, L"MirEfirRing", L"", WS_CHILD, 0, 0, 16, 16, host, NULL, GetModuleHandleW(NULL),
                                NULL);
    if (g_mv_ring) SetPropW(g_mv_ring, L"mirefir-tile", (HANDLE)1);
  }
  if (st) i18n_set(st->language);
  apply_panel_alpha();
  logo_init();
  rec_load(g_pl, g_st);
  rebuild_groups();
  apply_playing_group(g_st ? g_st->last_index : 0);
  if (g_in_group_n < 1) {
    remember_group(0);
    fill_group(0);
  }
  GetCursorPos(&g_last_pt);
  g_mouse_at = GetTickCount();
  g_pl_boot_fetch = 1;
  layout(host);
  update_start(host);
  if (needs_setup()) {
    g_setup_i = 0;
    g_menu_page = MENU_SETUP;
    set_mode(UI_MENU);
  }
}

void shell_shutdown(void) {
  persist_ui(0, 0, 0);
  update_stop();
  rec_save(g_pl, g_st);
  logo_shutdown();
  profile_playlist_refresh_shutdown();
  voice_stop();
  hide_tiles();
  if (g_mv_ring) {
    DestroyWindow(g_mv_ring);
    g_mv_ring = NULL;
  }
  cursor_show(1);
  for (int i = 0; i < 4; i++) {
    if (g_tile_wnd[i]) {
      DestroyWindow(g_tile_wnd[i]);
      g_tile_wnd[i] = NULL;
    }
  }
  if (g_upd) {
    DestroyWindow(g_upd);
    g_upd = NULL;
  }
  if (g_font) DeleteObject(g_font);
  if (g_font_sm) DeleteObject(g_font_sm);
  if (g_font_pad) DeleteObject(g_font_pad);
  if (g_font_clock) DeleteObject(g_font_clock);
  if (g_font_day) DeleteObject(g_font_day);
  if (g_font_tiny) DeleteObject(g_font_tiny);
  g_font = g_font_sm = g_font_pad = g_font_clock = g_font_day = g_font_tiny = NULL;
}

void shell_layout(HWND host) { layout(host); }

void shell_tick(void) {
  static int upd_was;
  int upd_now = update_ready();
  if (upd_now != upd_was) {
    upd_was = upd_now;
    layout(g_host);
    if (g_mode == UI_MENU && g_menu_page == MENU_SETTINGS && g_set_tab == 10 && g_menu)
      InvalidateRect(g_menu, NULL, FALSE);
  }
  DWORD now = GetTickCount();
  if (g_persist_at && now - g_persist_at >= 1200) {
    g_persist_at = 0;
    persist_ui(0, 0, 0);
  }
  rec_tick(g_pl, g_st);
  POINT pt;
  GetCursorPos(&pt);
  if (pt.x != g_last_pt.x || pt.y != g_last_pt.y) {
    g_last_pt = pt;
    shell_mouse_moved();
  }
  if (g_mode == UI_LIVE) {
    if (g_cur_vis && now - g_mouse_at > 4000) cursor_show(0);
  } else cursor_show(1);
  for (int i = 1; i < g_mv_n && i < 4; i++) {
    if (!g_mv_need_start[i]) continue;
    g_mv_need_start[i] = 0;
    if (start_tile(i)) {
      g_mv_spawned[i] = 1;
      mv_apply_audio();
      if (g_host) SetFocus(g_host);
    } else g_mv_need_start[i] = 1;
    break;
  }
  if (player_alive() && !g_logo_hold_set) {
    g_logo_hold_set = 1;
    logo_hold(6000);
  }
  if (!epg_ready() || !g_pl) {
    g_logo_armed = 0;
    g_logo_i = 0;
  } else if (g_logo_armed < 2) {
    if (!g_logo_armed) {
      logo_retain_begin();
      g_logo_armed = 1;
    }
    int end = g_logo_i + 32;
    if (end > g_pl->count) end = g_pl->count;
    for (int i = g_logo_i; i < end; i++) {
      char u[MAX_REF];
      if (!epg_channel_icon(i, u, MAX_REF)) continue;
      logo_retain(u);
      logo_queue(u);
    }
    g_logo_i = end;
    if (g_logo_i >= g_pl->count) {
      logo_retain_end();
      g_logo_armed = 2;
    }
  }
  if (logo_tick() && g_mode == UI_GUIDE && g_guide) InvalidateRect(g_guide, NULL, FALSE);
  if (g_mode == UI_MENU && (g_menu_page == MENU_SEARCH || g_menu_page == MENU_MICPERM ||
                           (g_menu_page == MENU_MULTI && g_mv_pick))) {
    char text[192];
    int ch = -1, sw = 0;
    int kind = voice_poll(text, 192, &ch, &sw);
    if (kind == VOICE_BUSY) {
      if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    } else if (kind == VOICE_NEEDMIC) {
      g_mic_i = 0;
      g_menu_page = MENU_MICPERM;
      snprintf(g_voice_note, sizeof g_voice_note, "%s", text[0] ? text : i18n_u("voiceNeedMic"));
      if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    } else if (kind == VOICE_EMPTY || kind == VOICE_FAIL) {
      snprintf(g_voice_note, sizeof g_voice_note, "%s", text);
      later(text);
      if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    } else if (kind == VOICE_HEARD) {
      apply_voice_result(text, ch, sw);
    }
  }
  refresh_clock();
  if (g_mode == UI_PAD) {
    if (now > g_pad_until) set_mode(UI_LIVE);
    else if (g_pad) InvalidateRect(g_pad, NULL, FALSE);
  }
  if (g_mode == UI_EXIT && now > g_exit_until) set_mode(UI_LIVE);
  if (g_mode == UI_GUIDE && epg_dirty()) {
    epg_clear_dirty();
    if (g_focus_col == COL_CHANNELS) sync_prog_to_channel();
    InvalidateRect(g_guide, NULL, FALSE);
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_SETTINGS && g_set_note[0] && now - g_set_note_at > 5000) {
    g_set_note[0] = 0;
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  }
  if (g_st && g_pl) {
    int force = g_pl_boot_fetch;
    if (profile_playlist_refresh_start(g_st, force) || force) g_pl_boot_fetch = 0;
    char name[MAX_NAME] = {0}, url[MAX_URL] = {0};
    int cur = player_current_index();
    if (cur >= 0 && cur < g_pl->count) {
      snprintf(name, MAX_NAME, "%s", g_pl->items[cur].name);
      snprintf(url, MAX_URL, "%s", g_pl->items[cur].url);
    }
    int r = profile_playlist_refresh_take(g_pl);
    if (r != 0) {
      profile_mark_playlist_fetched(g_st);
      profile_save_settings(g_st);
    }
    if (r == 1) {
      profile_restore_lists(g_st, g_pl);
      rec_load(g_pl, g_st);
      rebuild_groups();
      int idx = 0;
      if (name[0]) {
        int found = playlist_find_name(g_pl, name);
        if (found >= 0) idx = found;
      }
      int same = idx >= 0 && idx < g_pl->count && url[0] && strcmp(g_pl->items[idx].url, url) == 0;
      if (!same) {
        if (g_zap) g_zap(idx);
        else player_play_channel(g_pl, g_st, idx);
      } else g_st->last_index = idx;
      epg_start(g_pl, g_st);
      later(i18n_u("plUpdated"));
    }
  }
}

void shell_on_play(int index) {
  push_recent(index);
  g_persist_at = GetTickCount();
  if (!g_persist_at) g_persist_at = 1;
  if (g_mode == UI_GUIDE) {
    select_current_in_guide();
    if (g_focus_col == COL_CHANNELS) sync_prog_to_channel();
    InvalidateRect(g_guide, NULL, FALSE);
    return;
  }
  apply_playing_group(index);
  if (g_in_group_n < 1) {
    remember_group(0);
    fill_group(0);
  }
}

int shell_next_channel(int delta) {
  restore_group_cursor();
  fill_group(g_group_i);
  if (g_in_group_n < 1) {
    remember_group(0);
    fill_group(0);
  }
  if (g_in_group_n < 1) return -1;
  int cur = player_current_index();
  int pos = 0;
  for (int i = 0; i < g_in_group_n; i++) {
    if (g_index_in_group[i] == cur) {
      pos = i;
      break;
    }
  }
  int n = g_in_group_n;
  pos = (pos + n + (delta % n) + n) % n;
  remember_group(g_group_i);
  return g_index_in_group[pos];
}

int shell_appcommand(int cmd) {
  switch (cmd) {
    case APPCOMMAND_BROWSER_BACKWARD:
    case APPCOMMAND_MEDIA_STOP:
      go_back();
      return 1;
    case APPCOMMAND_BROWSER_SEARCH:
      if (!typing_field()) {
        open_search();
        return 1;
      }
      return 0;
    case APPCOMMAND_MEDIA_CHANNEL_UP: {
      int ch = shell_next_channel(-1);
      if (ch >= 0 && g_zap) g_zap(ch);
      return 1;
    }
    case APPCOMMAND_MEDIA_CHANNEL_DOWN: {
      int ch = shell_next_channel(1);
      if (ch >= 0 && g_zap) g_zap(ch);
      return 1;
    }
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
    case APPCOMMAND_MEDIA_PLAY:
    case APPCOMMAND_MEDIA_PAUSE:
      if (g_mode == UI_LIVE || g_mode == UI_PAD) {
        player_toggle_pause();
        return 1;
      }
      return 0;
    case APPCOMMAND_MEDIA_REWIND:
      if (g_mode == UI_LIVE || g_mode == UI_PAD) {
        pad_nudge(-30);
        return 1;
      }
      return 0;
    case APPCOMMAND_MEDIA_FAST_FORWARD:
      if (g_mode == UI_LIVE || g_mode == UI_PAD) {
        pad_nudge(30);
        return 1;
      }
      return 0;
    case APPCOMMAND_MEDIA_RECORD:
      if (!typing_field()) {
        do_live_rec();
        return 1;
      }
      return 0;
    case APPCOMMAND_VOLUME_MUTE:
      if (g_st && !typing_field()) {
        g_st->muted = !g_st->muted;
        player_set_volume(g_st->volume, g_st->muted);
        profile_save_session(g_st, player_current_index(), g_st->last_name);
        return 1;
      }
      return 0;
    case APPCOMMAND_BROWSER_HOME:
      if (g_mode == UI_LIVE || g_mode == UI_PAD) {
        g_menu_i = 0;
        g_menu_page = MENU_MAIN;
        set_mode(UI_MENU);
        return 1;
      }
      return 0;
    default:
      return 0;
  }
}

int shell_host_click(void) {
  if (g_mode == UI_LIVE) {
    g_pad_i = 0;
    set_mode(UI_PAD);
    return 1;
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_SETUP) {
    g_menu_page = MENU_MAIN;
    if (needs_setup()) set_mode(UI_MENU);
    else set_mode(UI_LIVE);
    return 1;
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_MULTI) {
    POINT pt;
    GetCursorPos(&pt);
    if (g_host) ScreenToClient(g_host, &pt);
    for (int i = 0; i < g_mv_n; i++) {
      RECT *r = &g_mv_rc[i];
      if (pt.x >= r->left && pt.x < r->right && pt.y >= r->top && pt.y < r->bottom) {
        mv_focus(i);
        return 1;
      }
    }
    return 1;
  }
  if (g_mode == UI_MENU && (g_menu_page == MENU_SEARCH || g_menu_page == MENU_MICPERM)) {
    go_back();
    return 1;
  }
  if (g_mode == UI_GUIDE || g_mode == UI_MENU || g_mode == UI_PAD) {
    if (g_menu_page == MENU_MULTI) close_multi();
    set_mode(UI_LIVE);
    return 1;
  }
  return 0;
}

static char *key_slot(int which) {
  if (!g_st) return NULL;
  switch (which) {
    case 1: return g_st->key_menu;
    case 2: return g_st->key_mute;
    case 3: return g_st->key_fav;
    case 4: return g_st->key_fs;
    case 5: return g_st->key_guide;
    case 6: return g_st->key_search;
    case 7: return g_st->key_rec;
    case 8: return g_st->key_pip;
    case 9: return g_st->key_voice;
    case 10: return g_st->key_live;
    default: return NULL;
  }
}

static void toggle_fav_playing(void) { toggle_fav_index(player_current_index()); }

static void ask_group_pin(int gi) {
  g_pin_from_group = 1;
  g_pin_group = gi;
  g_pin_buf[0] = 0;
  g_menu_page = MENU_PIN;
  set_mode(UI_MENU);
}

static void open_ch_menu(void) {
  int ch = menu_channel();
  if (!g_pl || ch < 0 || ch >= g_pl->count) return;
  int fav = group_kind_of(g_group_i) == GK_FAV;
  if (fav) {
    fill_group(g_group_i);
    g_chan_i = 0;
    for (int i = 0; i < g_in_group_n; i++) {
      if (g_index_in_group[i] == ch) {
        g_chan_i = i;
        break;
      }
    }
  }
  g_chm = CHM_MENU;
  g_chm_i = 0;
  g_chm_n = fav ? 2 : 1;
  layout(g_host);
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
}

static void run_ch_menu(void) {
  int ch = menu_channel();
  if (g_chm_i == 0) {
    toggle_fav_index(ch);
    g_chm = CHM_OFF;
    layout(g_host);
  } else if (g_chm_i == 1 && g_st) {
    memcpy(g_fav_bak, g_st->fav, sizeof g_st->fav);
    g_fav_bak_n = g_st->fav_n;
    g_chm = CHM_MOVE;
    later(i18n_u("favMoveHint"));
  }
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
}

static int open_context(void) {
  if (g_act) {
    close_act();
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return 1;
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_URL) {
    open_url_field_menu();
    return 1;
  }
  if (g_mode == UI_MENU && (g_menu_page == MENU_SEARCH || (g_menu_page == MENU_MULTI && g_mv_pick))) {
    open_search_field_menu();
    return 1;
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_SETTINGS && g_set_pane == 1) {
    fill_set_rows();
    if (g_set_i < 0 || g_set_i >= g_sn) return 1;
    int id = g_sid[g_set_i];
    if (id == SID_PL) {
      open_pl_act(g_sidx[g_set_i]);
      if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (id == SID_EPG) {
      open_epg_act(g_sidx[g_set_i]);
      if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
  }
  if (g_mode == UI_LIVE || g_mode == UI_PAD || g_mode == UI_GUIDE) {
    open_ch_menu();
    return 1;
  }
  return 1;
}

int shell_context(void) { return open_context(); }

static void move_fav(int d) {
  if (!g_st || g_st->fav_n < 2) return;
  int a = g_chan_i;
  int b = (a + g_st->fav_n + d) % g_st->fav_n;
  int t = g_st->fav[a];
  g_st->fav[a] = g_st->fav[b];
  g_st->fav[b] = t;
  g_chan_i = b;
  fill_group(1);
  if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
}

static void close_guide(void) {
  if (g_mv_pick) {
    g_mv_pick = 0;
    g_mv_menu = 0;
    g_menu_page = MENU_MULTI;
    set_mode(UI_MENU);
    return;
  }
  set_mode(UI_LIVE);
}

static void activate_channel(void) {
  if (!g_in_group_n) return;
  remember_group(g_group_i);
  int ch = guide_channel();
  if (g_mv_pick) {
    int cur = (g_mv_i >= 0 && g_mv_i < g_mv_n) ? g_mv_ch[g_mv_i] : -1;
    if (ch != cur) {
      if (g_mv_pick == 2) mv_set_ch(g_mv_i, ch);
      else mv_place_ch(ch);
    } else {
      close_guide();
    }
    return;
  }
  if (ch != player_current_index()) {
    if (g_zap) g_zap(ch);
    if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
    return;
  }
  close_guide();
}

static void run_setup_row(void) {
  if (g_setup_i == 3) {
    g_menu_page = MENU_MAIN;
    if (needs_setup()) set_mode(UI_MENU);
    else set_mode(UI_LIVE);
    return;
  }
  if (g_setup_i == 0) {
    char url[MAX_URL];
    if (paste_into(url, MAX_URL)) add_playlist_from(url);
    else later(i18n_u("setupNeed"));
  } else if (g_setup_i == 1) {
    char path[MAX_URL];
    if (pick_m3u_file(path, MAX_URL)) {
      if (g_host) SetFocus(g_host);
      add_playlist_from(path);
    } else if (g_host) SetFocus(g_host);
  } else {
    char url[MAX_URL];
    if (paste_into(url, MAX_URL)) {
      add_epg_from(url);
      if (g_pl && g_pl->count) epg_reload(g_pl, g_st);
    }
  }
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
}

static int recs_has_timer(void) {
  return rec_timer_get(NULL, NULL, 0, NULL, 0, NULL, NULL, NULL);
}

static int handle_extra_keys(WPARAM key) {
  if (g_st && key_is(g_st->key_pip, key) && g_menu_page == MENU_MULTI) {
    do_pip();
    return 1;
  }
  if (key == VK_LEFT && !search_like() && g_menu_page != MENU_MULTI && g_menu_page != MENU_HISTORY &&
      g_menu_page != MENU_URL) {
    go_back();
    return 1;
  }
  if (g_menu_page == MENU_URL) {
    if (g_act == ACT_URL) {
      if (key == VK_UP || key == VK_DOWN) {
        int d = key == VK_DOWN ? 1 : -1;
        if (g_act_n) g_act_cur = (g_act_cur + g_act_n + d) % g_act_n;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (is_ok_key(key) || key == VK_RIGHT) {
        run_act(g_act_cur);
        return 1;
      }
      if (key == VK_ESCAPE || key == VK_LEFT || key == VK_BACK) {
        close_act();
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      return 1;
    }
    if (key == VK_UP) {
      g_url_btn = (g_url_btn + 3 - 1) % 3;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_DOWN) {
      g_url_btn = (g_url_btn + 1) % 3;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_LEFT) {
      if (g_url_btn == 2) g_url_btn = 1;
      else if (g_url_btn == 1) g_url_btn = 0;
      else {
        go_back();
        return 1;
      }
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_RIGHT) {
      if (g_url_btn < 2) g_url_btn++;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_BACK) {
      if (g_url_sel) {
        g_url_edit[0] = 0;
        g_url_sel = 0;
        g_url_btn = 0;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (g_url_edit[0]) {
        utf8_del_last(g_url_edit);
        g_url_btn = 0;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      go_back();
      return 1;
    }
    if (key == VK_INSERT || (key == 'V' && GetKeyState(VK_CONTROL) < 0)) {
      url_paste_field();
      return 1;
    }
    if (key == VK_F2 || key == VK_APPS || (g_st && key_is(g_st->key_menu, key) && key != VK_RETURN)) {
      open_url_field_menu();
      return 1;
    }
    if (is_ok_key(key)) {
      g_hold_kind = HOLD_URL;
      g_hold_at = GetTickCount();
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_SETUP) {
    if (key == VK_UP || key == VK_DOWN) {
      g_setup_i = (g_setup_i + 4 + (key == VK_DOWN ? 1 : -1)) % 4;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      run_setup_row();
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_MICPERM) {
    if (key == VK_UP || key == VK_DOWN) {
      g_mic_i = (g_mic_i + 3 + (key == VK_DOWN ? 1 : -1)) % 3;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      if (g_mic_i == 0) {
        g_menu_page = MENU_SEARCH;
        do_voice();
      } else if (g_mic_i == 1) {
        ShellExecuteW(NULL, L"open", L"ms-settings:privacy-microphone", NULL, NULL, SW_SHOWNORMAL);
      } else if (g_st) {
        g_st->voice_enabled = 0;
        persist_ui(0, 0, 0);
        g_menu_page = MENU_SEARCH;
        snprintf(g_voice_note, sizeof g_voice_note, "Микрофон выключен в меню. Включите пункт «Микрофон».");
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_ASKREC) {
    if (key == VK_UP || key == VK_DOWN) {
      g_ask_i = !g_ask_i;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      apply_ask();
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_SHARE) {
    if (key == VK_UP || key == VK_DOWN) {
      if (g_share_n) {
        int d = key == VK_DOWN ? 1 : -1;
        g_share_i = (g_share_i + g_share_n + d) % g_share_n;
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      apply_share();
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_SEARCH || (g_menu_page == MENU_MULTI && g_mv_pick)) {
    if (g_act == ACT_SEARCH) {
      if (key == VK_UP || key == VK_DOWN) {
        int d = key == VK_DOWN ? 1 : -1;
        if (g_act_n) g_act_cur = (g_act_cur + g_act_n + d) % g_act_n;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (is_ok_key(key) || key == VK_RIGHT) {
        run_act(g_act_cur);
        return 1;
      }
      if (key == VK_ESCAPE || key == VK_LEFT || key == VK_BACK) {
        close_act();
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      return 1;
    }
    if (key == VK_F2 || key == VK_APPS) {
      open_search_field_menu();
      return 1;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
      g_hit_i = -1;
      g_search_focus = key == VK_RIGHT ? 1 : 0;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_DOWN) {
      if (g_hit_n) {
        if (g_hit_i < 0) g_hit_i = 0;
        else g_hit_i = (g_hit_i + 1) % g_hit_n;
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (key == VK_UP) {
      if (g_hit_i <= 0) {
        g_hit_i = -1;
      } else {
        g_hit_i -= 1;
      }
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_BACK) {
      int n = (int)strlen(g_search);
      if (n > 0) {
        g_search[n - 1] = 0;
        fill_search();
        InvalidateRect(g_menu, NULL, FALSE);
      } else go_back();
      return 1;
    }
    if (key == VK_INSERT || (key == 'V' && GetKeyState(VK_CONTROL) < 0)) {
      char clip[80];
      if (clipboard_read_utf8(clip, 80) && clip[0]) {
        snprintf(g_search, sizeof g_search, "%s", clip);
        fill_search();
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (g_st && key_is(g_st->key_voice, key) && !letter_key(key)) {
      do_voice();
      return 1;
    }
    if (is_ok_key(key)) {
      if (g_hit_i < 0 && g_search_focus == 1) {
        do_voice();
        return 1;
      }
      if (g_hit_i < 0) {
        char clip[80];
        if (!g_hit_n && clipboard_read_utf8(clip, 80) && clip[0]) {
          snprintf(g_search, sizeof g_search, "%s", clip);
          fill_search();
          InvalidateRect(g_menu, NULL, FALSE);
        }
        return 1;
      }
      if (!g_hit_n) return 1;
      int ch = g_hits[g_hit_i];
      if (g_menu_page == MENU_MULTI) {
        if (g_mv_pick == 2) mv_set_ch(g_mv_i, ch);
        else mv_place_ch(ch);
        return 1;
      }
      if (g_zap) g_zap(ch);
      set_mode(UI_LIVE);
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_HISTORY) {
    int n = g_st ? g_st->recent_n : 0;
    if (n > MAX_RECENT) n = MAX_RECENT;
    int total = n + (n ? 1 : 0);
    if (key == VK_UP || key == VK_DOWN) {
      if (total) {
        int d = key == VK_DOWN ? 1 : -1;
        g_hist_i = (g_hist_i + total + d) % total;
        if (g_hist_i >= n) g_hist_del = 0;
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (key == VK_RIGHT) {
      if (n && g_hist_i < n) {
        g_hist_del = 1;
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (key == VK_LEFT) {
      if (g_hist_del) {
        g_hist_del = 0;
        InvalidateRect(g_menu, NULL, FALSE);
      } else {
        go_back();
      }
      return 1;
    }
    if (key == VK_DELETE) {
      if (n && g_hist_i < n) hist_remove_at(g_hist_i);
      else if (n) hist_clear();
      return 1;
    }
    if (is_ok_key(key) && n && g_st) {
      if (g_hist_i == n) hist_clear();
      else if (g_hist_del) hist_remove_at(g_hist_i);
      else hist_play(g_hist_i);
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_RECS) {
    int has_tm = recs_has_timer();
    int lo = has_tm ? -1 : 0;
    int hi = g_file_n ? g_file_n - 1 : lo;
    if (key == VK_UP || key == VK_DOWN) {
      if (lo <= hi) {
        int d = key == VK_DOWN ? 1 : -1;
        g_file_i += d;
        if (g_file_i < lo) g_file_i = hi;
        if (g_file_i > hi) g_file_i = lo;
        InvalidateRect(g_menu, NULL, FALSE);
      }
      return 1;
    }
    if (key == 'D') {
      open_settings_tab(4);
      return 1;
    }
    if (is_ok_key(key)) {
      if (g_file_i < 0) {
        rec_timer_cancel();
        later(i18n_u("recTimerOff"));
        g_file_n = rec_scan(g_st, g_files, MAX_REC_FILES);
        g_file_i = g_file_n ? 0 : -1;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (g_file_i >= 0 && g_file_i < g_file_n) {
        g_from_recs = 1;
        if (player_play_file(g_files[g_file_i].path)) {
          later(g_files[g_file_i].title);
          set_mode(UI_LIVE);
        } else later(i18n_u("recOpenFail"));
      }
      return 1;
    }
    return 1;
  }
  if (g_menu_page == MENU_MULTI) {
    if (g_st && key_is(g_st->key_voice, key)) {
      do_voice();
      return 1;
    }
    if (g_mv_menu) {
      int nopt = mv_opt_n();
      if (key == VK_UP || key == VK_DOWN) {
        int d = key == VK_DOWN ? 1 : -1;
        g_mv_opt = (g_mv_opt + nopt + d) % nopt;
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (is_ok_key(key) || key == VK_RIGHT) {
        run_mv_opt();
        return 1;
      }
      if (key == VK_LEFT) {
        mv_show_menu(0);
        return 1;
      }
      return 1;
    }
    if (key == VK_LEFT) {
      mv_move(-1, 0);
      return 1;
    }
    if (key == VK_RIGHT) {
      mv_move(1, 0);
      return 1;
    }
    if (key == VK_UP) {
      mv_move(0, -1);
      return 1;
    }
    if (key == VK_DOWN) {
      mv_move(0, 1);
      return 1;
    }
    if (key == VK_DELETE && g_mv_n > 1) {
      mv_remove(g_mv_i);
      return 1;
    }
    if (is_ok_key(key)) {
      mv_show_menu(1);
      return 1;
    }
    return 1;
  }
  return 0;
}

int shell_keydown(WPARAM key) {
  key = shell_map_key(key);
  if (is_ok_key(key) && g_hold_kind) return 1;
  if (g_chm == CHM_MOVE) {
    if (key == VK_UP || key == VK_DOWN) {
      move_fav(key == VK_DOWN ? 1 : -1);
      return 1;
    }
    if (is_ok_key(key)) {
      persist_ui(0, 0, 0);
      g_chm = CHM_OFF;
      layout(g_host);
      if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
      return 1;
    }
    if (key == VK_ESCAPE || key == VK_BACK) {
      go_back();
      return 1;
    }
    return 1;
  }
  if (g_chm == CHM_MENU) {
    if (key == VK_UP || key == VK_DOWN) {
      if (g_chm_n) g_chm_i = (g_chm_i + g_chm_n + (key == VK_DOWN ? 1 : -1)) % g_chm_n;
      if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      run_ch_menu();
      return 1;
    }
    if (key == VK_ESCAPE || key == VK_BACK) {
      go_back();
      return 1;
    }
    return 1;
  }
  if (g_wait_key && g_mode == UI_MENU) {
    if (key == VK_ESCAPE) {
      g_wait_key = 0;
      set_note(L"Отмена");
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    char *slot = key_slot(g_wait_key);
    char name[MAX_KEY];
    vk_name(key, name, MAX_KEY);
    if (slot && name[0]) snprintf(slot, MAX_KEY, "%s", name);
    g_wait_key = 0;
    persist_ui(0, 0, 0);
    set_note(L"Клавиша назначена");
    InvalidateRect(g_menu, NULL, FALSE);
    return 1;
  }
  if (g_mode == UI_MENU && g_menu_page == MENU_PIN) {
    if (key == VK_ESCAPE || key == VK_LEFT || key == VK_BACK) {
      go_back();
      return 1;
    }
    if (key >= '0' && key <= '9') {
      int n = (int)strlen(g_pin_buf);
      if (n < MAX_PIN) {
        g_pin_buf[n] = (char)key;
        g_pin_buf[n + 1] = 0;
      }
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
      int n = (int)strlen(g_pin_buf);
      if (n < MAX_PIN) {
        g_pin_buf[n] = (char)('0' + (key - VK_NUMPAD0));
        g_pin_buf[n + 1] = 0;
      }
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_RIGHT) {
      if (g_st && g_st->parental_pin[0] && strcmp(g_pin_buf, g_st->parental_pin) == 0) {
        g_pin_ok = 1;
        g_pin_buf[0] = 0;
        if (g_pin_from_group) {
          int gi = g_pin_group;
          g_pin_from_group = 0;
          g_group_i = gi;
          preview_folder();
          g_guide_view = VIEW_CATEGORIES;
          g_focus_col = COL_CHANNELS;
          set_mode(UI_GUIDE);
          return 1;
        }
        g_menu_page = MENU_SETTINGS;
      } else {
        g_pin_buf[0] = 0;
      }
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    return 1;
  }
  if (g_st && (g_mode == UI_LIVE || g_mode == UI_PAD) && !typing_field()) {
    if (key_is(g_st->key_search, key)) {
      open_search();
      return 1;
    }
    if (key_is(g_st->key_rec, key)) {
      do_live_rec();
      return 1;
    }
    if (key_is(g_st->key_pip, key)) {
      do_pip();
      return 1;
    }
    if (key_is(g_st->key_voice, key)) {
      do_voice();
      return 1;
    }
    if (key_is(g_st->key_fav, key) && key != VK_RETURN) {
      toggle_fav_playing();
      return 1;
    }
    if (key_is(g_st->key_mute, key) && key != 'M') {
      g_st->muted = !g_st->muted;
      player_set_volume(g_st->volume, g_st->muted);
      profile_save_session(g_st, player_current_index(), g_st->last_name);
      return 1;
    }
    if (key_is(g_st->key_fs, key) && key != VK_RETURN && g_fs) {
      if (!g_fs(2)) g_fs(1);
      return 1;
    }
  }
  if (key == VK_F2 || key == VK_APPS || (g_st && key_is(g_st->key_menu, key) && key != VK_RETURN)) {
    if (g_mode == UI_GUIDE) {
      if (group_kind_of(g_group_i) == GK_FAV) open_ch_menu();
      else {
        g_resume_guide = 1;
        g_menu_i = 0;
        g_menu_page = MENU_MAIN;
        set_mode(UI_MENU);
      }
      return 1;
    }
    if (g_mode == UI_LIVE || g_mode == UI_PAD) {
      if (group_kind_of(g_group_i) == GK_FAV) {
        open_ch_menu();
        return 1;
      }
      g_menu_i = 0;
      g_menu_page = MENU_MAIN;
      set_mode(UI_MENU);
      return 1;
    }
  }
  if (!typing_field() && (key == 'G' || key == VK_F1 || (g_st && key_is(g_st->key_guide, key)))) {
    if (g_mode == UI_GUIDE) close_guide();
    else open_guide();
    return 1;
  }
  if (key == VK_ESCAPE || (key == VK_BACK && !typing_field())) {
    go_back();
    return 1;
  }
  if (g_mode == UI_EXIT) {
    if (key == VK_LEFT || key == VK_RIGHT) {
      g_exit_i = !g_exit_i;
      InvalidateRect(g_exit, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key)) {
      if (g_exit_i == 0) {
        if (g_quit) g_quit();
      } else set_mode(UI_LIVE);
      return 1;
    }
    return 1;
  }
  if (g_mode == UI_MENU) {
    if (g_menu_page == MENU_PIN) return 1;
    if (extra_page()) return handle_extra_keys(key);
    if (g_menu_page == MENU_STUB) {
      if (is_ok_key(key) || key == VK_LEFT || key == VK_RIGHT || key == VK_BACK) go_back();
      return 1;
    }
    if (g_menu_page == MENU_SETTINGS) {
      fill_set_rows();
      if (g_act) {
        if (key == VK_UP || key == VK_DOWN) {
          int d = key == VK_DOWN ? 1 : -1;
          if (g_act_n) g_act_cur = (g_act_cur + g_act_n + d) % g_act_n;
          InvalidateRect(g_menu, NULL, FALSE);
          return 1;
        }
        if (is_ok_key(key) || key == VK_RIGHT) {
          run_act(g_act_cur);
          InvalidateRect(g_menu, NULL, FALSE);
          return 1;
        }
        if (key == VK_LEFT || key == VK_BACK) {
          close_act();
          InvalidateRect(g_menu, NULL, FALSE);
          return 1;
        }
        return 1;
      }
      if (key == VK_UP || key == VK_DOWN) {
        int d = key == VK_DOWN ? 1 : -1;
        if (g_set_pane == 0) {
          g_set_tab = (g_set_tab + SET_TAB_N + d) % SET_TAB_N;
          g_set_i = 0;
          g_set_scroll = 0;
        } else if (g_sn) {
          set_move(d);
        }
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (key == VK_RIGHT) {
        if (g_set_pane == 0) {
          g_set_pane = 1;
          g_set_i = 0;
          InvalidateRect(g_menu, NULL, FALSE);
        } else activate_set_row();
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      if (key == VK_LEFT || key == VK_BACK) {
        go_back();
        return 1;
      }
      if (is_ok_key(key)) {
        if (g_set_pane == 0) {
          g_set_pane = 1;
          g_set_i = 0;
        } else activate_set_row();
        InvalidateRect(g_menu, NULL, FALSE);
        return 1;
      }
      return 1;
    }
    if (key == VK_UP) {
      g_menu_i = (g_menu_i + MENU_N - 1) % MENU_N;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_DOWN) {
      g_menu_i = (g_menu_i + 1) % MENU_N;
      InvalidateRect(g_menu, NULL, FALSE);
      return 1;
    }
    if (key == VK_LEFT) {
      ask_exit();
      return 1;
    }
    if (key == VK_RIGHT || key == VK_BACK) {
      go_back();
      return 1;
    }
    if (is_ok_key(key)) {
      run_menu(g_menu_i);
      return 1;
    }
    return 1;
  }
  if (g_mode == UI_GUIDE) {
    if (g_chm == CHM_MOVE) {
      if (key == VK_UP || key == VK_DOWN) {
        move_fav(key == VK_DOWN ? 1 : -1);
        return 1;
      }
      if (is_ok_key(key)) {
        persist_ui(0, 0, 0);
        g_chm = CHM_OFF;
        layout(g_host);
        if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
        return 1;
      }
      if (key == VK_ESCAPE || key == VK_BACK) {
        go_back();
        return 1;
      }
      return 1;
    }
    if (g_chm == CHM_MENU) {
      if (key == VK_UP || key == VK_DOWN) {
        if (g_chm_n) g_chm_i = (g_chm_i + g_chm_n + (key == VK_DOWN ? 1 : -1)) % g_chm_n;
        if (g_guide) InvalidateRect(g_guide, NULL, FALSE);
        return 1;
      }
      if (is_ok_key(key) || key == VK_RIGHT) {
        run_ch_menu();
        return 1;
      }
      if (key == VK_ESCAPE || key == VK_BACK) {
        go_back();
        return 1;
      }
      return 1;
    }
    if (g_st && key_is(g_st->key_rec, key)) {
      int ch = guide_channel();
      char title[96];
      long long ps = 0, pe = 0;
      title[0] = 0;
      if (epg_list(ch, g_prog_i, title, (int)sizeof title, NULL, 0, &ps, &pe) && pe > epg_now_ms())
        do_timer(ch, title, ps, pe);
      else later(i18n_u("recOnlyNow"));
      return 1;
    }
    if (key == VK_LEFT) {
      if (g_guide_view == VIEW_CHANNELS || g_guide_view == VIEW_SCHEDULE) {
        if (g_focus_col == COL_DETAIL) {
          g_focus_col = (g_days_open && g_day_n) ? COL_DAYS : COL_PROGRAMS;
        } else if (g_focus_col == COL_DAYS) {
          g_focus_col = COL_PROGRAMS;
        } else if (g_focus_col == COL_PROGRAMS) {
          g_focus_col = COL_CHANNELS;
          g_guide_view = VIEW_CHANNELS;
          g_days_open = 0;
        } else {
          g_guide_view = VIEW_GROUPS;
          g_focus_col = COL_GROUPS;
          g_days_open = 0;
        }
      } else if (g_guide_view == VIEW_CATEGORIES) {
        g_guide_view = VIEW_GROUPS;
        g_focus_col = COL_GROUPS;
      } else {
        if (g_mv_pick) {
          close_guide();
          return 1;
        }
        g_resume_guide = 1;
        g_menu_i = 0;
        g_menu_page = MENU_MAIN;
        set_mode(UI_MENU);
        return 1;
      }
      layout(g_host);
      InvalidateRect(g_guide, NULL, FALSE);
      return 1;
    }
    if (key == VK_RIGHT) {
      if (g_guide_view == VIEW_GROUPS) {
        if (!enter_folder()) return 1;
      } else if (g_guide_view == VIEW_CATEGORIES) {
        g_guide_view = VIEW_CHANNELS;
        g_focus_col = COL_CHANNELS;
      } else if (g_focus_col == COL_CHANNELS) {
        g_guide_view = VIEW_SCHEDULE;
        g_focus_col = COL_PROGRAMS;
        g_days_open = 1;
        rebuild_guide_days();
        sync_prog_to_channel();
      } else if (g_focus_col == COL_PROGRAMS && g_day_n) {
        g_days_open = 1;
        g_focus_col = COL_DAYS;
      } else if (g_focus_col == COL_DAYS || g_focus_col == COL_PROGRAMS) {
        g_focus_col = COL_DETAIL;
      }
      layout(g_host);
      InvalidateRect(g_guide, NULL, FALSE);
      return 1;
    }
    if (key == VK_UP || key == VK_DOWN) {
      int d = key == VK_DOWN ? 1 : -1;
      if (g_focus_col == COL_GROUPS && g_group_n) {
        g_group_i = (g_group_i + g_group_n + d) % g_group_n;
        preview_folder();
      } else if (g_focus_col == COL_CHANNELS && g_in_group_n) {
        g_chan_i = (g_chan_i + g_in_group_n + d) % g_in_group_n;
        rebuild_guide_days();
        sync_prog_to_channel();
      } else if (g_focus_col == COL_PROGRAMS) {
        int n = epg_count(guide_channel());
        if (n) {
          int next = g_prog_i + d;
          if (next < 0) next = 0;
          if (next > n - 1) next = n - 1;
          g_prog_i = next;
          sync_day_from_program(guide_channel(), g_prog_i);
        }
      } else if (g_focus_col == COL_DAYS && g_day_n) {
        int next = g_day_i + d;
        if (next < 0) next = 0;
        if (next > g_day_n - 1) next = g_day_n - 1;
        g_day_i = next;
        g_prog_i = epg_index_for_day(guide_channel(), g_days[g_day_i]);
      }
      InvalidateRect(g_guide, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key) || key == VK_SPACE) {
      if (g_focus_col == COL_GROUPS) {
        if (!enter_folder()) return 1;
        layout(g_host);
        InvalidateRect(g_guide, NULL, FALSE);
      } else if (g_focus_col == COL_DETAIL) {
        int ch = guide_channel();
        char title[96];
        long long ps = 0, pe = 0;
        title[0] = 0;
        if (epg_list(ch, g_prog_i, title, (int)sizeof title, NULL, 0, &ps, &pe) && pe > epg_now_ms())
          do_timer(ch, title, ps, pe);
        else activate_focused_program();
      } else if (g_focus_col == COL_PROGRAMS || g_focus_col == COL_DAYS) {
        activate_focused_program();
      } else if (g_focus_col == COL_CHANNELS && g_in_group_n) {
        g_hold_kind = HOLD_CHAN;
        g_hold_at = GetTickCount();
      }
      return 1;
    }
    return 1;
  }
  if (g_mode == UI_PAD) {
    if (key == VK_DOWN) {
      if (g_pad_focus == PAD_NONE) g_pad_focus = PAD_BUTTONS;
      else if (g_pad_focus == PAD_BUTTONS) g_pad_focus = PAD_BAR;
      g_pad_until = GetTickCount() + 6500;
      InvalidateRect(g_pad, NULL, FALSE);
      return 1;
    }
    if (key == VK_UP) {
      if (g_pad_focus == PAD_BAR) g_pad_focus = PAD_BUTTONS;
      else if (g_pad_focus == PAD_BUTTONS) g_pad_focus = PAD_NONE;
      g_pad_until = GetTickCount() + 6500;
      InvalidateRect(g_pad, NULL, FALSE);
      return 1;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
      if (g_pad_focus == PAD_BAR) {
        pad_nudge(key == VK_RIGHT ? 15 : -15);
        g_pad_until = GetTickCount() + 6500;
        InvalidateRect(g_pad, NULL, FALSE);
        return 1;
      }
      if (g_pad_focus == PAD_NONE) return 1;
      g_pad_i = (g_pad_i + PAD_N + (key == VK_RIGHT ? 1 : -1)) % PAD_N;
      g_pad_until = GetTickCount() + 6500;
      InvalidateRect(g_pad, NULL, FALSE);
      return 1;
    }
    if (is_ok_key(key)) {
      if (g_pad_focus == PAD_BUTTONS) {
        if (g_pad_i == 7) {
          g_hold_kind = HOLD_PAD_SUB;
          g_hold_at = GetTickCount();
          g_pad_until = GetTickCount() + 6500;
          return 1;
        }
        run_pad(g_pad_i);
        return 1;
      }
      set_mode(UI_LIVE);
      return 1;
    }
    if (key == VK_OEM_COMMA || key == 0xB1) {
      pad_nudge(-30);
      return 1;
    }
    if (key == VK_OEM_PERIOD || key == 0xB0) {
      pad_nudge(30);
      return 1;
    }
    return 1;
  }
  if (is_ok_key(key)) {
    g_hold_kind = HOLD_LIVE;
    g_hold_at = GetTickCount();
    return 1;
  }
  if (key == VK_OEM_COMMA || key == 0xB1) {
    pad_nudge(-30);
    return 1;
  }
  if (key == VK_OEM_PERIOD || key == 0xB0) {
    pad_nudge(30);
    return 1;
  }
  if (key == VK_LEFT) {
    open_guide();
    return 1;
  }
  return 0;
}

int shell_char(unsigned ch) {
  if (g_mode != UI_MENU) return 0;
  if (voice_busy()) return 1;
  if (g_act) return 1;
  if (g_menu_page == MENU_URL) {
    if (g_act) return 1;
    if (ch < 32 || ch == 127) return 1;
    wchar_t w[2];
    w[0] = (wchar_t)ch;
    w[1] = 0;
    char u[8];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, (int)sizeof u, NULL, NULL);
    if (n < 2) return 1;
    if (g_url_sel) {
      g_url_edit[0] = 0;
      g_url_sel = 0;
    }
    int len = (int)strlen(g_url_edit);
    if (len + n - 1 >= (int)sizeof g_url_edit) return 1;
    memcpy(g_url_edit + len, u, (size_t)(n - 1));
    g_url_edit[len + n - 1] = 0;
    g_url_btn = 0;
    if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
    return 1;
  }
  if (g_menu_page != MENU_SEARCH && !(g_menu_page == MENU_MULTI && g_mv_pick)) return 0;
  if (ch < 32 || ch == 127) return 1;
  g_search_focus = 0;
  g_hit_i = -1;
  wchar_t w[2];
  w[0] = (wchar_t)ch;
  w[1] = 0;
  char u[8];
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, (int)sizeof u, NULL, NULL);
  if (n < 2) return 1;
  int len = (int)strlen(g_search);
  if (len + n - 1 >= (int)sizeof g_search) return 1;
  memcpy(g_search + len, u, (size_t)(n - 1));
  g_search[len + n - 1] = 0;
  fill_search();
  if (g_menu) InvalidateRect(g_menu, NULL, FALSE);
  return 1;
}

int shell_keyup(WPARAM key) {
  key = shell_map_key(key);
  if (!is_ok_key(key) || !g_hold_kind) return 0;
  int kind = g_hold_kind;
  int held = (GetTickCount() - g_hold_at) >= HOLD_MS;
  g_hold_kind = HOLD_NONE;
  if (kind == HOLD_PAD_SUB) {
    if (held) {
      player_cycle_sub();
      later(i18n_u("subTrack"));
    } else {
      player_cycle_audio();
      later(i18n_u("audioTrack"));
    }
    g_pad_until = GetTickCount() + 6500;
    if (g_pad) InvalidateRect(g_pad, NULL, FALSE);
    return 1;
  }
  if (kind == HOLD_CHAN) {
    if (held) open_ch_menu();
    else activate_channel();
    return 1;
  }
  if (kind == HOLD_LIVE) {
    if (held) open_ch_menu();
    else {
      g_pad_i = 0;
      set_mode(UI_PAD);
    }
    return 1;
  }
  if (kind == HOLD_URL) {
    if (held && g_url_btn == 0) open_url_field_menu();
    else url_activate();
    return 1;
  }
  return 1;
}

void shell_mouse_moved(void) {
  g_mouse_at = GetTickCount();
  cursor_show(1);
}

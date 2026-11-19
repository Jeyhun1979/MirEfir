#define COBJMACROS
#include "app.h"
#include <wincodec.h>

enum { LOGO_SIDE = 48, LOGO_BYTES = 2 * 1024 * 1024, LOGO_SRC_MAX = 1600 };

typedef struct LogoItem {
  char url[MAX_REF];
  unsigned char *px;
  int w, h;
  int fail;
  int queued;
  DWORD fail_tick;
} LogoItem;

typedef struct LogoJob {
  char url[MAX_REF];
  struct LogoJob *next;
} LogoJob;

static LogoItem *g_item;
static int g_n;
static int g_cap;
static int g_co;
static HANDLE g_th;
static volatile LONG g_busy;
static volatile LONG g_fresh;
static volatile LONG g_stop;
static LogoJob *g_qhead;
static LogoJob *g_qtail;
static CRITICAL_SECTION g_cs;
static int g_cs_on;
static unsigned *g_keep;
static int g_keep_n;
static int g_keep_cap;
static volatile DWORD g_not_before;

int logo_px(const Settings *st) {
  if (!st) return 30;
  if (strcmp(st->logo_size, "sm") == 0) return 22;
  if (strcmp(st->logo_size, "lg") == 0) return 40;
  return 30;
}

static void logo_lock(void) {
  if (g_cs_on) EnterCriticalSection(&g_cs);
}
static void logo_unlock(void) {
  if (g_cs_on) LeaveCriticalSection(&g_cs);
}

static unsigned logo_hash(const char *url) {
  unsigned h = 2166136261u;
  for (int i = 0; url && url[i]; i++) h = (h ^ (unsigned char)url[i]) * 16777619u;
  return h;
}

static void cache_path(const char *url, wchar_t *out, int max) {
  wchar_t dir[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  wchar_t logos[MAX_PATH];
  swprintf(logos, MAX_PATH, L"%s\\logos", dir);
  CreateDirectoryW(logos, NULL);
  swprintf(out, max, L"%s\\%08x.bin", logos, logo_hash(url));
}

static int write_cached(const wchar_t *path, const unsigned char *px, int w, int h) {
  if (!path || !px || w < 1 || h < 1 || w > LOGO_SIDE || h > LOGO_SIDE) return 0;
  HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hf == INVALID_HANDLE_VALUE) return 0;
  unsigned char hdr[8];
  hdr[0] = 'L';
  hdr[1] = 'G';
  hdr[2] = '0';
  hdr[3] = '1';
  hdr[4] = (unsigned char)(w & 255);
  hdr[5] = (unsigned char)((w >> 8) & 255);
  hdr[6] = (unsigned char)(h & 255);
  hdr[7] = (unsigned char)((h >> 8) & 255);
  DWORD wr = 0;
  int ok = WriteFile(hf, hdr, 8, &wr, NULL) && wr == 8;
  DWORD nbytes = (DWORD)w * (DWORD)h * 4;
  ok = ok && WriteFile(hf, px, nbytes, &wr, NULL) && wr == nbytes;
  CloseHandle(hf);
  if (!ok) DeleteFileW(path);
  return ok;
}

static int read_cached(const wchar_t *path, unsigned char **out, int *ow, int *oh) {
  *out = NULL;
  *ow = *oh = 0;
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  unsigned char hdr[8];
  DWORD got = 0;
  int ok = ReadFile(h, hdr, 8, &got, NULL) && got == 8 && hdr[0] == 'L' && hdr[1] == 'G' && hdr[2] == '0' && hdr[3] == '1';
  int w = 0, ht = 0;
  if (ok) {
    w = hdr[4] | (hdr[5] << 8);
    ht = hdr[6] | (hdr[7] << 8);
    ok = w > 0 && ht > 0 && w <= LOGO_SIDE && ht <= LOGO_SIDE;
  }
  unsigned char *px = NULL;
  if (ok) {
    DWORD nbytes = (DWORD)w * (DWORD)ht * 4;
    px = (unsigned char *)malloc(nbytes);
    ok = px && ReadFile(h, px, nbytes, &got, NULL) && got == nbytes;
    if (!ok) {
      free(px);
      px = NULL;
    }
  }
  CloseHandle(h);
  if (!ok) return 0;
  *out = px;
  *ow = w;
  *oh = ht;
  return 1;
}

static unsigned char *fit_side(const unsigned char *src, int sw, int sh, int *ow, int *oh) {
  int dw = sw, dh = sh;
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;
  if (dw > LOGO_SIDE || dh > LOGO_SIDE) {
    if (sw >= sh) {
      dw = LOGO_SIDE;
      dh = sh * LOGO_SIDE / sw;
      if (dh < 1) dh = 1;
    } else {
      dh = LOGO_SIDE;
      dw = sw * LOGO_SIDE / sh;
      if (dw < 1) dw = 1;
    }
  }
  unsigned char *dst = (unsigned char *)malloc((size_t)dw * (size_t)dh * 4);
  if (!dst) return NULL;
  if (dw == sw && dh == sh) {
    memcpy(dst, src, (size_t)dw * (size_t)dh * 4);
  } else {
    for (int y = 0; y < dh; y++) {
      int y0 = y * sh / dh;
      int y1 = (y + 1) * sh / dh;
      if (y1 <= y0) y1 = y0 + 1;
      if (y1 > sh) y1 = sh;
      for (int x = 0; x < dw; x++) {
        int x0 = x * sw / dw;
        int x1 = (x + 1) * sw / dw;
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > sw) x1 = sw;
        unsigned r = 0, g = 0, b = 0, a = 0, c = 0;
        for (int yy = y0; yy < y1; yy++) {
          const unsigned char *row = src + ((size_t)yy * (size_t)sw) * 4;
          for (int xx = x0; xx < x1; xx++) {
            const unsigned char *p = row + (size_t)xx * 4;
            b += p[0];
            g += p[1];
            r += p[2];
            a += p[3];
            c++;
          }
        }
        unsigned char *o = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4;
        if (!c) c = 1;
        o[0] = (unsigned char)(b / c);
        o[1] = (unsigned char)(g / c);
        o[2] = (unsigned char)(r / c);
        o[3] = (unsigned char)(a / c);
      }
    }
  }
  *ow = dw;
  *oh = dh;
  return dst;
}

static int decode_image(const wchar_t *path, unsigned char **out, int *ow, int *oh) {
  *out = NULL;
  *ow = *oh = 0;
  if (read_cached(path, out, ow, oh)) return 1;
  IWICImagingFactory *fac = NULL;
  IWICBitmapDecoder *dec = NULL;
  IWICBitmapFrameDecode *frame = NULL;
  IWICFormatConverter *conv = NULL;
  HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&fac);
  if (FAILED(hr) || !fac) return 0;
  hr = IWICImagingFactory_CreateDecoderFromFilename(fac, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
  if (FAILED(hr) || !dec) {
    IWICImagingFactory_Release(fac);
    return 0;
  }
  hr = IWICBitmapDecoder_GetFrame(dec, 0, &frame);
  if (FAILED(hr) || !frame) {
    IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(fac);
    return 0;
  }
  hr = IWICImagingFactory_CreateFormatConverter(fac, &conv);
  if (FAILED(hr) || !conv) {
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(fac);
    return 0;
  }
  hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                      NULL, 0.0, WICBitmapPaletteTypeCustom);
  UINT w = 0, h = 0;
  if (SUCCEEDED(hr)) IWICFormatConverter_GetSize(conv, &w, &h);
  int ok = 0;
  if (SUCCEEDED(hr) && w > 0 && h > 0 && w <= LOGO_SRC_MAX && h <= LOGO_SRC_MAX) {
    unsigned char *full = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (full) {
      UINT stride = w * 4;
      if (SUCCEEDED(IWICFormatConverter_CopyPixels(conv, NULL, stride, stride * h, full))) {
        *out = fit_side(full, (int)w, (int)h, ow, oh);
        ok = *out != NULL;
      }
      free(full);
    }
  }
  IWICFormatConverter_Release(conv);
  IWICBitmapFrameDecode_Release(frame);
  IWICBitmapDecoder_Release(dec);
  IWICImagingFactory_Release(fac);
  return ok;
}

static LogoItem *find_item(const char *url, int add) {
  if (!url || !url[0]) return NULL;
  for (int i = 0; i < g_n; i++) {
    if (strcmp(g_item[i].url, url) == 0) return &g_item[i];
  }
  if (!add) return NULL;
  if (g_n >= g_cap) {
    int cap = g_cap ? g_cap * 2 : 64;
    LogoItem *next = (LogoItem *)realloc(g_item, (size_t)cap * sizeof(LogoItem));
    if (!next) return NULL;
    g_item = next;
    g_cap = cap;
  }
  LogoItem *it = &g_item[g_n++];
  memset(it, 0, sizeof *it);
  snprintf(it->url, MAX_REF, "%s", url);
  return it;
}

static void move_front(const char *url) {
  LogoJob *prev = NULL;
  for (LogoJob *j = g_qhead; j; j = j->next) {
    if (strcmp(j->url, url) == 0) {
      if (prev) {
        prev->next = j->next;
        if (g_qtail == j) g_qtail = prev;
        j->next = g_qhead;
        g_qhead = j;
      }
      return;
    }
    prev = j;
  }
}

static int enqueue(const char *url, int front) {
  for (LogoJob *j = g_qhead; j; j = j->next) {
    if (strcmp(j->url, url) == 0) {
      if (front) move_front(url);
      return 1;
    }
  }
  LogoJob *j = (LogoJob *)malloc(sizeof(LogoJob));
  if (!j) return 0;
  snprintf(j->url, MAX_REF, "%s", url);
  if (front || !g_qhead) {
    j->next = g_qhead;
    g_qhead = j;
    if (!g_qtail) g_qtail = j;
  } else {
    j->next = NULL;
    g_qtail->next = j;
    g_qtail = j;
  }
  return 1;
}

static int take_job(char *url, int max) {
  logo_lock();
  LogoJob *j = g_qhead;
  if (!j) {
    logo_unlock();
    return 0;
  }
  g_qhead = j->next;
  if (!g_qhead) g_qtail = NULL;
  snprintf(url, max, "%s", j->url);
  free(j);
  logo_unlock();
  return 1;
}

static void store_px(const char *url, unsigned char *px, int w, int h, int ok) {
  logo_lock();
  LogoItem *it = find_item(url, 0);
  if (it) {
    free(it->px);
    it->px = ok ? px : NULL;
    it->w = w;
    it->h = h;
    it->fail = !ok;
    it->fail_tick = ok ? 0 : GetTickCount();
    it->queued = 0;
    if (!ok) free(px);
  } else free(px);
  logo_unlock();
}

static int url_safe(const char *url) {
  if (!url || !(istarts(url, "http://") || istarts(url, "https://"))) return 0;
  for (const char *p = url; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c <= 32 || c == '"' || c == '&' || c == '|' || c == '>' || c == '<' || c == '^') return 0;
  }
  return 1;
}

static int read_whole(const wchar_t *path, char **body, int *n) {
  *body = NULL;
  *n = 0;
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  DWORD sz = GetFileSize(h, NULL);
  if (sz < 33 || sz > LOGO_BYTES) {
    CloseHandle(h);
    return 0;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  DWORD got = 0;
  int ok = buf && ReadFile(h, buf, sz, &got, NULL) && got == sz;
  CloseHandle(h);
  if (!ok) {
    free(buf);
    return 0;
  }
  buf[sz] = 0;
  *body = buf;
  *n = (int)sz;
  return 1;
}

static int curl_get(const char *url, char **body, int *n) {
  *body = NULL;
  *n = 0;
  if (!url_safe(url)) return 0;
  wchar_t dir[MAX_PATH], out[MAX_PATH], wurl[MAX_REF], cmd[4096];
  if (!GetTempPathW(MAX_PATH, dir)) return 0;
  _snwprintf(out, MAX_PATH, L"%smirefir-%lu.img", dir, GetCurrentThreadId());
  out[MAX_PATH - 1] = 0;
  DeleteFileW(out);
  if (!utf8_to_wide(url, wurl, MAX_REF)) return 0;
  _snwprintf(cmd, 4096,
             L"\"C:\\Windows\\System32\\curl.exe\" -sS -L --connect-timeout 3 --max-time 8 --limit-rate 64k -A \"Mozilla/5.0\" -o \"%s\" \"%s\"",
             out, wurl);
  cmd[4095] = 0;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(L"C:\\Windows\\System32\\curl.exe", cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 0;
  if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  int ok = code == 0 && read_whole(out, body, n);
  DeleteFileW(out);
  return ok;
}

static int download_logo(const char *url, char **body, int *n) {
  const char *rest = NULL;
  if (strncmp(url, "http://epg.one/", 15) == 0) rest = url + 15;
  else if (strncmp(url, "https://epg.one/", 16) == 0) rest = url + 16;
  if (rest && rest[0]) {
    char alt[MAX_REF];
    snprintf(alt, sizeof alt, "http://ru.epg.one/%s", rest);
    if (strcmp(alt, url) != 0 && curl_get(alt, body, n)) return 1;
  }
  return curl_get(url, body, n);
}

static void wait_logo_hold(void) {
  for (;;) {
    if (g_stop) return;
    DWORD until = g_not_before;
    DWORD now = GetTickCount();
    if ((int)(now - until) >= 0) return;
    DWORD left = until - now;
    if (left > 200) left = 200;
    Sleep(left);
  }
}

static void fetch_one(const char *url) {
  wchar_t path[MAX_PATH];
  cache_path(url, path, MAX_PATH);
  unsigned char *px = NULL;
  int w = 0, h = 0;
  if (read_cached(path, &px, &w, &h)) {
    store_px(url, px, w, h, 1);
    return;
  }
  char *body = NULL;
  int n = 0;
  int ok = 0;
  wchar_t tmp[MAX_PATH];
  swprintf(tmp, MAX_PATH, L"%s.dl", path);
  if (download_logo(url, &body, &n)) {
    HANDLE hf = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
      DWORD wr = 0;
      WriteFile(hf, body, (DWORD)n, &wr, NULL);
      CloseHandle(hf);
      if (wr == (DWORD)n) ok = decode_image(tmp, &px, &w, &h);
      DeleteFileW(tmp);
      if (ok) write_cached(path, px, w, h);
    }
  }
  free(body);
  if (!ok) {
    free(px);
    px = NULL;
    w = h = 0;
  }
  store_px(url, px, w, h, ok);
}

static DWORD WINAPI logo_thread(void *arg) {
  (void)arg;
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  for (;;) {
    char url[MAX_REF];
    if (g_stop) break;
    if (!take_job(url, MAX_REF)) break;
    wait_logo_hold();
    if (g_stop) break;
    fetch_one(url);
    InterlockedExchange(&g_fresh, 1);
    Sleep(150);
  }
  CoUninitialize();
  InterlockedExchange(&g_busy, 0);
  if (!g_stop) {
    logo_lock();
    int more = g_qhead != NULL;
    logo_unlock();
    if (more && InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
      HANDLE t = CreateThread(NULL, 0, logo_thread, NULL, 0, NULL);
      if (!t) InterlockedExchange(&g_busy, 0);
      else {
        if (g_th) CloseHandle(g_th);
        g_th = t;
      }
    }
  }
  return 0;
}

static void ensure_worker(void) {
  if (g_stop) return;
  if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
  HANDLE t = CreateThread(NULL, 0, logo_thread, NULL, 0, NULL);
  if (!t) {
    InterlockedExchange(&g_busy, 0);
    return;
  }
  if (g_th) CloseHandle(g_th);
  g_th = t;
}

static void logo_request(const char *url, int front) {
  if (!g_cs_on || !url || !(istarts(url, "http://") || istarts(url, "https://"))) return;
  logo_lock();
  LogoItem *it = find_item(url, 1);
  if (!it || it->px) {
    logo_unlock();
    return;
  }
  if (it->fail) {
    DWORD now = GetTickCount();
    if ((DWORD)(now - it->fail_tick) < 20000) {
      logo_unlock();
      return;
    }
    it->fail = 0;
  }
  if (it->queued) {
    if (front) move_front(url);
    logo_unlock();
    return;
  }
  if (!enqueue(url, front)) {
    logo_unlock();
    return;
  }
  it->queued = 1;
  logo_unlock();
  ensure_worker();
}

void logo_hold(DWORD ms_from_now) {
  DWORD t = GetTickCount() + ms_from_now;
  if ((int)(t - g_not_before) > 0) g_not_before = t;
}

void logo_init(void) {
  g_not_before = GetTickCount() + 6000;
  if (!g_cs_on) {
    InitializeCriticalSection(&g_cs);
    g_cs_on = 1;
  }
  if (!g_co) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    g_co = 1;
  }
}

void logo_shutdown(void) {
  InterlockedExchange(&g_stop, 1);
  if (g_th) {
    WaitForSingleObject(g_th, 15000);
    CloseHandle(g_th);
    g_th = NULL;
  }
  logo_lock();
  for (int i = 0; i < g_n; i++) free(g_item[i].px);
  free(g_item);
  g_item = NULL;
  g_n = g_cap = 0;
  while (g_qhead) {
    LogoJob *n = g_qhead->next;
    free(g_qhead);
    g_qhead = n;
  }
  g_qtail = NULL;
  free(g_keep);
  g_keep = NULL;
  g_keep_n = g_keep_cap = 0;
  logo_unlock();
  if (g_cs_on) {
    DeleteCriticalSection(&g_cs);
    g_cs_on = 0;
  }
  if (g_co) {
    CoUninitialize();
    g_co = 0;
  }
  InterlockedExchange(&g_stop, 0);
  InterlockedExchange(&g_busy, 0);
}

void logo_want(const char *url) { logo_request(url, 1); }
void logo_queue(const char *url) { logo_request(url, 0); }

void logo_retain_begin(void) {
  if (!g_cs_on) return;
  logo_lock();
  g_keep_n = 0;
  logo_unlock();
}

void logo_retain(const char *url) {
  if (!g_cs_on || !url || !url[0]) return;
  unsigned h = logo_hash(url);
  logo_lock();
  for (int i = 0; i < g_keep_n; i++) {
    if (g_keep[i] == h) {
      logo_unlock();
      return;
    }
  }
  if (g_keep_n >= g_keep_cap) {
    int cap = g_keep_cap ? g_keep_cap * 2 : 64;
    unsigned *next = (unsigned *)realloc(g_keep, (size_t)cap * sizeof(unsigned));
    if (!next) {
      logo_unlock();
      return;
    }
    g_keep = next;
    g_keep_cap = cap;
  }
  g_keep[g_keep_n++] = h;
  logo_unlock();
}

void logo_retain_end(void) {
  if (!g_cs_on) return;
  wchar_t dir[MAX_PATH], logos[MAX_PATH], pattern[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(logos, MAX_PATH, L"%s\\logos", dir);
  swprintf(pattern, MAX_PATH, L"%s\\*.bin", logos);
  logo_lock();
  int n = g_keep_n;
  unsigned *keep = NULL;
  if (n > 0) {
    keep = (unsigned *)malloc((size_t)n * sizeof(unsigned));
    if (keep) memcpy(keep, g_keep, (size_t)n * sizeof(unsigned));
    else n = 0;
  }
  logo_unlock();
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      wchar_t *dot = wcsrchr(fd.cFileName, L'.');
      if (!dot || _wcsicmp(dot, L".bin") != 0) continue;
      unsigned id = wcstoul(fd.cFileName, NULL, 16);
      int wanted = 0;
      for (int i = 0; i < n; i++) {
        if (keep[i] == id) {
          wanted = 1;
          break;
        }
      }
      if (!wanted) {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", logos, fd.cFileName);
        DeleteFileW(full);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  free(keep);
}

int logo_tick(void) { return InterlockedExchange(&g_fresh, 0); }

static int cache_into_item(const char *url) {
  wchar_t path[MAX_PATH];
  cache_path(url, path, MAX_PATH);
  unsigned char *px = NULL;
  int w = 0, h = 0;
  if (!read_cached(path, &px, &w, &h)) return 0;
  logo_lock();
  LogoItem *it = find_item(url, 1);
  if (!it) {
    free(px);
    logo_unlock();
    return 0;
  }
  free(it->px);
  it->px = px;
  it->w = w;
  it->h = h;
  it->fail = 0;
  it->queued = 0;
  logo_unlock();
  return 1;
}

static int copy_logo(const char *url, unsigned char *local, int *ow, int *oh) {
  logo_lock();
  LogoItem *it = find_item(url, 0);
  int ok = it && it->px && it->w > 0 && it->h > 0 && it->w <= LOGO_SIDE && it->h <= LOGO_SIDE;
  if (ok) {
    *ow = it->w;
    *oh = it->h;
    memcpy(local, it->px, (size_t)it->w * (size_t)it->h * 4);
  }
  logo_unlock();
  return ok;
}

void logo_blit(unsigned char *bits, int bw, int bh, int x, int y, int size, const char *url) {
  if (!bits || size < 8 || !url || !url[0]) return;
  unsigned char local[LOGO_SIDE * LOGO_SIDE * 4];
  int w = 0, h = 0;
  if (!copy_logo(url, local, &w, &h)) {
    if (!cache_into_item(url) || !copy_logo(url, local, &w, &h)) {
      logo_want(url);
      return;
    }
  }
  for (int dy = 0; dy < size; dy++) {
    int sy = h * dy / size;
    if (sy >= h) sy = h - 1;
    int py = y + dy;
    if (py < 0 || py >= bh) continue;
    for (int dx = 0; dx < size; dx++) {
      int sx = w * dx / size;
      if (sx >= w) sx = w - 1;
      int px0 = x + dx;
      if (px0 < 0 || px0 >= bw) continue;
      unsigned char *s = local + ((size_t)sy * (size_t)w + (size_t)sx) * 4;
      unsigned char a = s[3];
      if (a < 8) continue;
      unsigned char *d = bits + ((size_t)py * (size_t)bw + (size_t)px0) * 4;
      d[0] = (unsigned char)(s[0] * a / 255);
      d[1] = (unsigned char)(s[1] * a / 255);
      d[2] = (unsigned char)(s[2] * a / 255);
      d[3] = a;
    }
  }
}

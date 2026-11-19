#include "app.h"
#define COBJMACROS
#include <dxgi.h>

static int g_ready;
static int g_detected = PLAY_MID;
static int g_floor = PLAY_HIGH;
static int g_hevc;
static int g_class = PLAY_MID;
static int g_stepped;

static int has_w(const wchar_t *s, const wchar_t *n) { return s && n && wcsstr(s, n) != NULL; }

static void fold_w(wchar_t *s) {
  for (; s && *s; s++) {
    if (*s >= L'A' && *s <= L'Z') *s = (wchar_t)(*s + 32);
  }
}

static int cpu_very_weak(void) {
  wchar_t cpu[128];
  cpu[0] = 0;
  DWORD n = sizeof cpu;
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                   RRF_RT_REG_SZ, NULL, cpu, &n) != ERROR_SUCCESS)
    return 0;
  fold_w(cpu);
  return has_w(cpu, L"pentium") || has_w(cpu, L"celeron") || has_w(cpu, L"atom") || has_w(cpu, L"a4-") ||
         has_w(cpu, L"a6-") || has_w(cpu, L"e1-");
}

static int classify(int cores, int ram_mb, int vram_mb, unsigned vendor, const wchar_t *gpu) {
  (void)ram_mb;
  wchar_t name[256];
  wcsncpy(name, gpu ? gpu : L"", 255);
  name[255] = 0;
  fold_w(name);

  int old_intel = has_w(name, L"hd graphics 2000") || has_w(name, L"hd graphics 2500") ||
                  has_w(name, L"hd graphics 3000") || has_w(name, L"hd graphics 4000") || has_w(name, L"gma ") ||
                  (vendor == 0x8086 && has_w(name, L"hd graphics") && !has_w(name, L"uhd") && !has_w(name, L"iris") &&
                   !has_w(name, L"hd graphics 5") && !has_w(name, L"hd graphics 6") && !has_w(name, L"hd graphics 7"));
  int weak_nv = has_w(name, L"gt 610") || has_w(name, L"gt 620") || has_w(name, L"gt 630") || has_w(name, L"gt 640") ||
                has_w(name, L"gt 710") || has_w(name, L"gt 720") || has_w(name, L"gt 730") || has_w(name, L"gt 740");
  int hevc = 0;
  if (vendor == 0x10DE) {
    hevc = has_w(name, L"rtx") || has_w(name, L"gtx 16") || has_w(name, L"gtx 10") || has_w(name, L"gtx 9") ||
           has_w(name, L"quadro") || has_w(name, L"tesla");
    if (has_w(name, L"gtx 9") && !has_w(name, L"gtx 96") && !has_w(name, L"gtx 97") && !has_w(name, L"gtx 98"))
      hevc = 0;
  } else if (vendor == 0x1002) {
    hevc = has_w(name, L"rx ") || has_w(name, L"vega") || has_w(name, L"rdna") || has_w(name, L"radeon vii") ||
           has_w(name, L"radeon pro");
  } else if (vendor == 0x8086) {
    if (old_intel) hevc = 0;
    else
      hevc = has_w(name, L"iris") || has_w(name, L"arc") || has_w(name, L"uhd") || has_w(name, L"hd graphics 5") ||
             has_w(name, L"hd graphics 6") || has_w(name, L"hd graphics 7");
  }
  g_hevc = hevc;

  if (cpu_very_weak() || old_intel || weak_nv || (cores <= 2 && !hevc)) return PLAY_MIN;
  if (hevc && (cores >= 4 || vram_mb >= 1024 || has_w(name, L"rtx") || has_w(name, L"rx ") || has_w(name, L"iris") ||
               has_w(name, L"arc")))
    return PLAY_HIGH;
  if (hevc || (cores >= 4 && ram_mb >= 8192)) return PLAY_MID;
  if (cores >= 4) return PLAY_MID;
  return PLAY_LOW;
}

static void pick_gpu(unsigned *vendor, int *vram_mb, wchar_t *name, int name_max) {
  *vendor = 0;
  *vram_mb = 0;
  if (name && name_max) name[0] = 0;
  IDXGIFactory1 *fac = NULL;
  if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&fac)) || !fac) return;
  IDXGIAdapter1 *best = NULL;
  SIZE_T best_mem = 0;
  for (UINT i = 0;; i++) {
    IDXGIAdapter1 *ad = NULL;
    if (IDXGIFactory1_EnumAdapters1(fac, i, &ad) == DXGI_ERROR_NOT_FOUND) break;
    if (!ad) break;
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof d);
    if (SUCCEEDED(IDXGIAdapter1_GetDesc1(ad, &d)) && d.VendorId != 0x1414 &&
        !has_w(d.Description, L"Basic Render") && !has_w(d.Description, L"extension")) {
      IDXGIOutput *out = NULL;
      int shown = SUCCEEDED(IDXGIAdapter1_EnumOutputs(ad, 0, &out));
      if (out) IDXGIOutput_Release(out);
      SIZE_T mem = d.DedicatedVideoMemory ? d.DedicatedVideoMemory : d.SharedSystemMemory;
      if (shown) mem += (SIZE_T)1 << 40;
      if (!best || mem > best_mem) {
        if (best) IDXGIAdapter1_Release(best);
        best = ad;
        best_mem = mem;
        ad = NULL;
      }
    }
    if (ad) IDXGIAdapter1_Release(ad);
  }
  if (best) {
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof d);
    if (SUCCEEDED(IDXGIAdapter1_GetDesc1(best, &d))) {
      *vendor = d.VendorId;
      *vram_mb = (int)(d.DedicatedVideoMemory / (1024 * 1024));
      if (name && name_max) wcsncpy(name, d.Description, name_max - 1);
    }
    IDXGIAdapter1_Release(best);
  }
  IDXGIFactory1_Release(fac);
}

void device_probe(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int cores = (int)si.dwNumberOfProcessors;
  if (cores < 1) cores = 1;
  MEMORYSTATUSEX mem;
  memset(&mem, 0, sizeof mem);
  mem.dwLength = sizeof mem;
  GlobalMemoryStatusEx(&mem);
  int ram_mb = (int)(mem.ullTotalPhys / (1024 * 1024));
  unsigned vendor = 0;
  int vram_mb = 0;
  wchar_t gpu[128];
  gpu[0] = 0;
  pick_gpu(&vendor, &vram_mb, gpu, 128);
  g_detected = classify(cores, ram_mb, vram_mb, vendor, gpu);
  g_floor = PLAY_HIGH;
  g_stepped = 0;
  g_class = g_detected;
  g_ready = 1;
}

static void ensure(void) {
  if (!g_ready) device_probe();
}

int device_class(void) {
  ensure();
  return g_class;
}

int device_can_hevc(void) {
  ensure();
  return g_hevc && g_class >= PLAY_MID;
}

int device_start_quality(void) {
  ensure();
  /* always the playlist URL the user picked; a dropped network is not a weak GPU */
  return 4;
}

int device_prefer_d3d(void) { return device_class() <= PLAY_LOW; }

void device_sync(Settings *st) {
  ensure();
  if (!st) return;
  st->play_class = g_class;
}

void device_lower(Settings *st) {
  (void)st;
}

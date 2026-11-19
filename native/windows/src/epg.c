#include "app.h"
#include "tinf.h"

enum { EPG_MAX_PROG = 1400, SCAN_CAP = 256 * 1024, HCAP = 8192, FOLD_MAX = 128 };
enum { SEC_NONE, SEC_CH, SEC_PR, SEC_ARR };

typedef struct {
  char title[72];
  char desc[96];
  long long start;
  long long end;
} EpgSlot;

typedef struct {
  char title[96];
  char desc[400];
  long long start;
  long long end;
  EpgSlot *slots;
  int slot_n;
  int slot_cap;
} EpgNow;

typedef struct {
  char epg_id[96];
  int index;
  int rank;
} Bind;

typedef struct {
  char buf[SCAN_CAP];
  int n;
  int section;
  int skip_arr;
  long long now;
  long long offset;
  long long from_ms;
  long long to_ms;
  const Playlist *pl;
  EpgNow *nowv;
  Bind *bind;
  int *owned;
  int bind_n;
  int bind_cap;
  int got_prog;
  int file_prog;
  int saw_xml;
  char cur_key[96];
  char (*fold_name)[FOLD_MAX];
  char (*fold_tvg)[FOLD_MAX];
  int head[HCAP];
  int *chain;
} Scan;

static EpgNow *g_now;
static int g_n;
static volatile int g_ready;
static volatile int g_dirty;
static volatile int g_stop;
static HANDLE g_thread;
static const Playlist *g_pl;
static long long g_offset;
static int g_past_days = 7;
static int g_future_days = 7;
static char g_epg_urls[MAX_SAVED][MAX_URL];
static int g_epg_url_n;
static int g_epg_hours = 6;
static int g_epg_auto = 1;
static int g_epg_force;
static volatile int g_busy;
static long long g_saved_at;
static long long g_next_try;
static int g_file_back;
static int g_file_ahead;
enum { EPG_HTTP_MS = 20000, EPG_KEY_MAX = 80 };
static CRITICAL_SECTION g_lock;
static int g_lock_on;
static char (*g_icon)[MAX_REF];
static int *g_icon_rank;
static int g_epg_ver;
static char g_fetch_base[MAX_URL];

static void epg_lock(void) {
  if (g_lock_on) EnterCriticalSection(&g_lock);
}
static void epg_unlock(void) {
  if (g_lock_on) LeaveCriticalSection(&g_lock);
}

static void epg_note_icon(int index, int rank, const char *url) {
  if (!g_icon || !g_icon_rank || !url || !url[0] || index < 0 || index >= g_n) return;
  if (!(istarts(url, "http://") || istarts(url, "https://"))) return;
  epg_lock();
  if (!g_icon[index][0] || rank < g_icon_rank[index]) {
    snprintf(g_icon[index], MAX_REF, "%s", url);
    g_icon_rank[index] = rank;
  }
  epg_unlock();
}

int epg_channel_icon(int index, char *out, int max) {
  if (!out || max < 2) return 0;
  out[0] = 0;
  epg_lock();
  int ok = 0;
  if (g_icon && index >= 0 && index < g_n && g_icon[index][0]) {
    snprintf(out, max, "%s", g_icon[index]);
    ok = 1;
  }
  epg_unlock();
  return ok;
}

int epg_ready(void) { return g_ready; }
int epg_dirty(void) { return g_dirty; }
void epg_clear_dirty(void) { g_dirty = 0; }

long long epg_now_ms(void) {
  FILETIME ft;
  ULARGE_INTEGER u;
  GetSystemTimeAsFileTime(&ft);
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (long long)(u.QuadPart / 10000ULL - 11644473600000ULL);
}

void epg_local_fields(long long ms, SYSTEMTIME *out) {
  FILETIME utc, loc;
  ULARGE_INTEGER ul;
  if (!out) return;
  memset(out, 0, sizeof *out);
  ul.QuadPart = (unsigned long long)(ms + 11644473600000ULL) * 10000ULL;
  utc.dwLowDateTime = ul.LowPart;
  utc.dwHighDateTime = ul.HighPart;
  if (!FileTimeToLocalFileTime(&utc, &loc)) loc = utc;
  FileTimeToSystemTime(&loc, out);
}

long long epg_day_start(long long ms) {
  SYSTEMTIME st;
  FILETIME loc, utc;
  ULARGE_INTEGER ul;
  epg_local_fields(ms, &st);
  st.wHour = 0;
  st.wMinute = 0;
  st.wSecond = 0;
  st.wMilliseconds = 0;
  if (!SystemTimeToFileTime(&st, &loc)) return ms;
  if (!LocalFileTimeToFileTime(&loc, &utc)) utc = loc;
  ul.LowPart = utc.dwLowDateTime;
  ul.HighPart = utc.dwHighDateTime;
  return (long long)(ul.QuadPart / 10000ULL - 11644473600000ULL);
}

static int live_index_u(int index) {
  if (!g_now || index < 0 || index >= g_n || !g_now[index].slots) return -1;
  EpgNow *e = &g_now[index];
  long long now = epg_now_ms();
  int best = -1;
  for (int i = 0; i < e->slot_n; i++) {
    if (e->slots[i].start <= now && now < e->slots[i].end) {
      if (best < 0 || e->slots[i].start > e->slots[best].start) best = i;
    }
  }
  return best;
}

int epg_now(int index, char *title, int tmax, char *desc, int dmax, long long *start, long long *end) {
  epg_lock();
  if (!g_now || index < 0 || index >= g_n) {
    epg_unlock();
    return 0;
  }
  EpgNow *e = &g_now[index];
  long long now = epg_now_ms();
  int live = live_index_u(index);
  int ok = 0;
  if (live >= 0 && e->slots) {
    EpgSlot *s = &e->slots[live];
    if (title && tmax > 0) snprintf(title, tmax, "%s", s->title);
    if (desc && dmax > 0) snprintf(desc, dmax, "%s", s->desc[0] ? s->desc : e->desc);
    if (start) *start = s->start;
    if (end) *end = s->end;
    ok = 1;
  } else if (e->title[0] && e->start <= now && now < e->end) {
    if (title && tmax > 0) snprintf(title, tmax, "%s", e->title);
    if (desc && dmax > 0) snprintf(desc, dmax, "%s", e->desc);
    if (start) *start = e->start;
    if (end) *end = e->end;
    ok = 1;
  }
  epg_unlock();
  return ok;
}

int epg_count(int index) {
  epg_lock();
  int n = (!g_now || index < 0 || index >= g_n) ? 0 : g_now[index].slot_n;
  epg_unlock();
  return n;
}

int epg_live_index(int index) {
  epg_lock();
  int r = live_index_u(index);
  epg_unlock();
  return r;
}

int epg_index_for_day(int index, long long day) {
  epg_lock();
  if (!g_now || index < 0 || index >= g_n || !g_now[index].slot_n || !g_now[index].slots) {
    epg_unlock();
    return 0;
  }
  EpgNow *e = &g_now[index];
  long long now = epg_now_ms();
  long long today = epg_day_start(now);
  int live = live_index_u(index);
  int first = -1, after = -1, last = e->slot_n - 1;
  if (day == today && live >= 0) {
    epg_unlock();
    return live;
  }
  for (int i = 0; i < e->slot_n; i++) {
    long long d = epg_day_start(e->slots[i].start);
    if (d == day && first < 0) first = i;
    if (d > day && after < 0) after = i;
  }
  int r = first >= 0 ? first : (after >= 0 ? after : last);
  epg_unlock();
  return r;
}

int epg_list(int index, int slot, char *title, int tmax, char *desc, int dmax, long long *start, long long *end) {
  epg_lock();
  if (!g_now || index < 0 || index >= g_n) {
    epg_unlock();
    return 0;
  }
  EpgNow *e = &g_now[index];
  if (!e->slots || slot < 0 || slot >= e->slot_n) {
    epg_unlock();
    return 0;
  }
  if (title && tmax > 0) snprintf(title, tmax, "%s", e->slots[slot].title);
  if (desc && dmax > 0) snprintf(desc, dmax, "%s", e->slots[slot].desc);
  if (start) *start = e->slots[slot].start;
  if (end) *end = e->slots[slot].end;
  epg_unlock();
  return 1;
}

void epg_stop(void) {
  g_stop = 1;
  if (g_thread) {
    WaitForSingleObject(g_thread, 8000);
    CloseHandle(g_thread);
    g_thread = NULL;
  }
  DWORD t0 = GetTickCount();
  while (g_busy && GetTickCount() - t0 < 8000) Sleep(50);
  if (g_now) {
    for (int i = 0; i < g_n; i++) free(g_now[i].slots);
  }
  free(g_now);
  g_now = NULL;
  free(g_icon);
  free(g_icon_rank);
  g_icon = NULL;
  g_icon_rank = NULL;
  g_n = 0;
  g_epg_ver = 0;
  g_ready = 0;
  g_dirty = 0;
  if (g_lock_on) {
    DeleteCriticalSection(&g_lock);
    g_lock_on = 0;
  }
}

static long long json_long(const char *obj, const char *key) {
  const char *v = json_value_of(obj, key);
  if (!v) return 0;
  return strtoll(v, NULL, 10);
}

static const char *end_json(const char *p) {
  if (!p) return NULL;
  if (*p != '{' && *p != '[') return NULL;
  char open = *p, close = (char)(open == '{' ? '}' : ']');
  int depth = 0, str = 0, esc = 0;
  for (; *p; p++) {
    if (str) {
      if (esc) {
        esc = 0;
        continue;
      }
      if (*p == '\\') {
        esc = 1;
        continue;
      }
      if (*p == '"') str = 0;
      continue;
    }
    if (*p == '"') {
      str = 1;
      continue;
    }
    if (*p == open) depth++;
    else if (*p == close) {
      depth--;
      if (!depth) return p + 1;
    }
  }
  return NULL;
}

static int utf8_cp(const char **p) {
  const unsigned char *s = (const unsigned char *)*p;
  if (s[0] < 0x80) {
    *p += 1;
    return s[0];
  }
  if ((s[0] & 0xE0) == 0xC0 && s[1]) {
    *p += 2;
    return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
  }
  if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
    *p += 3;
    return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  }
  *p += 1;
  return s[0];
}

static int put_cp(char *out, int n, int max, int cp) {
  if (cp < 0x80) {
    if (n < max - 1) out[n++] = (char)cp;
    return n;
  }
  if (cp < 0x800) {
    if (n < max - 2) {
      out[n++] = (char)(0xC0 | (cp >> 6));
      out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    return n;
  }
  if (n < max - 3) {
    out[n++] = (char)(0xE0 | (cp >> 12));
    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  }
  return n;
}

static int is_word_drop(const char *w) {
  return !strcmp(w, "uhd") || !strcmp(w, "fhd") || !strcmp(w, "hd") || !strcmp(w, "sd") || !strcmp(w, "4k") ||
         !strcmp(w, "hevc") || !strcmp(w, "hdr") || !strcmp(w, "50") || !strcmp(w, "60");
}

static int name_shifted(const char *s) {
  if (!s) return 0;
  for (; *s; s++) {
    if (*s == '+' && s[1] >= '0' && s[1] <= '9') return 1;
  }
  return 0;
}

static long long i64abs(long long v) { return v < 0 ? -v : v; }

static void fold_epg_key(const char *s, char *out, int max) {
  if (!out || max < 2) return;
  out[0] = 0;
  if (!s) return;
  char tmp[512];
  int n = 0;
  int skip_paren = 0;
  const char *p = s;
  while (*p && n < (int)sizeof(tmp) - 4) {
    if (*p == '+') {
      const char *q = p + 1;
      if (*q >= '0' && *q <= '9') {
        while (*q >= '0' && *q <= '9') q++;
        p = q;
        if (n && tmp[n - 1] != ' ') tmp[n++] = ' ';
        continue;
      }
    }
    int cp = utf8_cp(&p);
    if (cp == '(') {
      skip_paren = 1;
      if (n && tmp[n - 1] != ' ') tmp[n++] = ' ';
      continue;
    }
    if (cp == ')') {
      skip_paren = 0;
      continue;
    }
    if (skip_paren) continue;
    if (cp == 0x401 || cp == 0x451) cp = 0x435;
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
    int keep = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 0x430 && cp <= 0x44F);
    if (!keep) {
      if (n && tmp[n - 1] != ' ') tmp[n++] = ' ';
      continue;
    }
    n = put_cp(tmp, n, (int)sizeof tmp, cp);
  }
  tmp[n] = 0;
  out[0] = 0;
  for (char *tok = tmp; *tok;) {
    while (*tok == ' ') tok++;
    if (!*tok) break;
    char *sp = strchr(tok, ' ');
    if (sp) *sp = 0;
    if (!is_word_drop(tok)) {
      if (out[0]) {
        int o = (int)strlen(out);
        if (o < max - 2) {
          out[o] = ' ';
          out[o + 1] = 0;
        }
      }
      strncat(out, tok, (size_t)max - 1 - strlen(out));
    }
    tok = sp ? sp + 1 : tok + strlen(tok);
  }
}

static unsigned hash_id(const char *s) {
  unsigned h = 2166136261u;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 16777619u;
  }
  return h % HCAP;
}

static int has_bind_id(Scan *s, const char *epg_id) {
  if (!epg_id || !epg_id[0] || !s->bind) return 0;
  unsigned h = hash_id(epg_id);
  for (int b = s->head[h]; b >= 0; b = s->chain[b]) {
    if (strcmp(s->bind[b].epg_id, epg_id) == 0) return 1;
  }
  return 0;
}

static void unhash_bind(Scan *s, int i) {
  unsigned h = hash_id(s->bind[i].epg_id);
  int prev = -1;
  for (int b = s->head[h]; b >= 0; b = s->chain[b]) {
    if (b == i) {
      if (prev < 0) s->head[h] = s->chain[i];
      else s->chain[prev] = s->chain[i];
      s->chain[i] = -1;
      return;
    }
    prev = b;
  }
}

static void hash_bind(Scan *s, int i) {
  unsigned h = hash_id(s->bind[i].epg_id);
  s->chain[i] = s->head[h];
  s->head[h] = i;
}

static void bind_id(Scan *s, const char *epg_id, int index, int rank) {
  if (!epg_id || !epg_id[0] || index < 0 || !s->owned) return;
  int old = s->owned[index];
  if (old >= 0) {
    if (s->bind[old].rank <= rank) return;
    unhash_bind(s, old);
    snprintf(s->bind[old].epg_id, 96, "%s", epg_id);
    s->bind[old].rank = rank;
    hash_bind(s, old);
    return;
  }
  if (s->bind_n >= s->bind_cap) {
    int cap = s->bind_cap ? s->bind_cap * 2 : 256;
    Bind *nb = (Bind *)realloc(s->bind, (size_t)cap * sizeof(Bind));
    int *nc = (int *)realloc(s->chain, (size_t)cap * sizeof(int));
    if (!nb || !nc) {
      free(nb);
      free(nc);
      return;
    }
    s->bind = nb;
    s->chain = nc;
    s->bind_cap = cap;
  }
  int i = s->bind_n;
  snprintf(s->bind[i].epg_id, 96, "%s", epg_id);
  s->bind[i].index = index;
  s->bind[i].rank = rank;
  hash_bind(s, i);
  s->owned[index] = i;
  s->bind_n++;
}

static void resolve_icon(const char *src, char *out, int max) {
  if (!out || max < 8) return;
  out[0] = 0;
  if (!src || !src[0]) return;
  if (istarts(src, "http://") || istarts(src, "https://")) {
    snprintf(out, max, "%s", src);
    return;
  }
  const char *base = g_fetch_base;
  if (!base || !(istarts(base, "http://") || istarts(base, "https://"))) return;
  if (istarts(src, "//")) {
    snprintf(out, max, "%s%s", istarts(base, "https://") ? "https:" : "http:", src);
    return;
  }
  char root[MAX_URL];
  snprintf(root, sizeof root, "%s", base);
  char *q = strchr(root, '?');
  if (q) *q = 0;
  if (src[0] == '/') {
    char *p = strstr(root, "://");
    if (!p) return;
    p += 3;
    char *sl = strchr(p, '/');
    if (sl) *sl = 0;
    snprintf(out, max, "%s%s", root, src);
    return;
  }
  char *sl = strrchr(root, '/');
  if (!sl) return;
  sl[1] = 0;
  snprintf(out, max, "%s%s", root, src);
}

static void note_icon_for_id(Scan *s, const char *epg_id, const char *icon) {
  char url[MAX_REF];
  resolve_icon(icon, url, MAX_REF);
  if (!url[0] || !s || !s->pl || !s->owned || !s->bind || !epg_id || !epg_id[0]) return;
  for (int i = 0; i < s->pl->count; i++) {
    int b = s->owned[i];
    if (b < 0 || b >= s->bind_n) continue;
    if (strcmp(s->bind[b].epg_id, epg_id) != 0) continue;
    epg_note_icon(i, s->bind[b].rank, url);
  }
}

static void bind_channel_obj(Scan *s, const char *epg_id, const char *obj) {
  if (!s->pl || !epg_id) return;
  int shifted = 0;
  for (int i = 0; i < s->pl->count; i++) {
    if (s->pl->items[i].tvg_id[0] && _stricmp(s->pl->items[i].tvg_id, epg_id) == 0) bind_id(s, epg_id, i, 0);
  }
  char id_fold[FOLD_MAX];
  fold_epg_key(epg_id, id_fold, FOLD_MAX);
  if (id_fold[0]) {
    for (int i = 0; i < s->pl->count; i++) {
      if ((s->fold_name[i][0] && !strcmp(s->fold_name[i], id_fold)) || (s->fold_tvg[i][0] && !strcmp(s->fold_tvg[i], id_fold)))
        bind_id(s, epg_id, i, name_shifted(epg_id) ? 2 : 0);
    }
  }
  const char *names = json_value_of(obj, "names");
  if (!names || *names != '[') return;
  names++;
  while (*names && *names != ']') {
    while (*names == ' ' || *names == ',') names++;
    if (*names == '"') {
      char nm[MAX_NAME];
      const char *q = names + 1;
      int o = 0;
      while (*q && *q != '"' && o < MAX_NAME - 1) {
        if (*q == '\\' && q[1]) q++;
        nm[o++] = *q++;
      }
      nm[o] = 0;
      if (name_shifted(nm)) shifted = 1;
      char folded[FOLD_MAX];
      fold_epg_key(nm, folded, FOLD_MAX);
      if (folded[0]) {
        int rank = name_shifted(nm) ? 2 : 0;
        for (int i = 0; i < s->pl->count; i++) {
          if ((s->fold_name[i][0] && !strcmp(s->fold_name[i], folded)) || (s->fold_tvg[i][0] && !strcmp(s->fold_tvg[i], folded)))
            bind_id(s, epg_id, i, rank);
        }
      }
      names = (*q == '"') ? q + 1 : q;
    } else if (*names) names++;
  }
  if (shifted && id_fold[0]) {
    for (int i = 0; i < s->pl->count; i++) {
      if (s->owned && s->owned[i] >= 0 && !strcmp(s->bind[s->owned[i]].epg_id, epg_id) && s->bind[s->owned[i]].rank == 0)
        s->bind[s->owned[i]].rank = 2;
    }
  }
  char icon[MAX_REF];
  icon[0] = 0;
  if (!json_extract_string(obj, "icon", icon, MAX_REF)) json_extract_string(obj, "logo", icon, MAX_REF);
  note_icon_for_id(s, epg_id, icon);
}

static void fill_slot(EpgSlot *slot, const char *title, const char *desc, long long start, long long end) {
  snprintf(slot->title, sizeof slot->title, "%s", title && title[0] ? title : "Программа");
  snprintf(slot->desc, sizeof slot->desc, "%s", desc ? desc : "");
  slot->start = start;
  slot->end = end;
}

static int grow_slots(EpgNow *e) {
  if (e->slot_n < e->slot_cap) return 1;
  if (e->slot_cap >= EPG_MAX_PROG) return 0;
  int cap = e->slot_cap ? e->slot_cap * 2 : 48;
  if (cap > EPG_MAX_PROG) cap = EPG_MAX_PROG;
  EpgSlot *next = (EpgSlot *)realloc(e->slots, (size_t)cap * sizeof(EpgSlot));
  if (!next) return 0;
  e->slots = next;
  e->slot_cap = cap;
  return 1;
}

static void insert_slot(EpgNow *e, const char *title, const char *desc, long long start, long long end, long long now) {
  epg_lock();
  for (int k = 0; k < e->slot_n; k++) {
    if (e->slots[k].start == start) {
      if (desc && desc[0] && !e->slots[k].desc[0]) snprintf(e->slots[k].desc, sizeof e->slots[k].desc, "%s", desc);
      epg_unlock();
      return;
    }
  }
  int pos = e->slot_n;
  for (int k = 0; k < e->slot_n; k++) {
    if (start < e->slots[k].start) {
      pos = k;
      break;
    }
  }
  if (!grow_slots(e)) {
    if (!e->slots || !e->slot_n) {
      epg_unlock();
      return;
    }
    long long today = epg_day_start(now);
    long long day = 24LL * 60 * 60 * 1000;
    long long from = today - (long long)g_past_days * day;
    long long to = today + (long long)(g_future_days + 1) * day;
    if (end < from || start > to) {
      epg_unlock();
      return;
    }
    int drop_first = e->slots[0].end < from || e->slots[0].start < from;
    int drop_last = e->slots[e->slot_n - 1].start >= to;
    if (!drop_first && !drop_last) {
      long long d0 = i64abs(e->slots[0].start - now);
      long long d1 = i64abs(e->slots[e->slot_n - 1].start - now);
      long long dn = i64abs(start - now);
      if (dn >= d0 && dn >= d1) {
        epg_unlock();
        return;
      }
      drop_first = d0 >= d1;
      drop_last = !drop_first;
    }
    if (drop_first) {
      memmove(&e->slots[0], &e->slots[1], (size_t)(e->slot_n - 1) * sizeof(EpgSlot));
      e->slot_n--;
      if (pos > 0) pos--;
      if (pos > e->slot_n) pos = e->slot_n;
    } else {
      if (pos >= e->slot_n) {
        epg_unlock();
        return;
      }
      e->slot_n--;
    }
  }
  for (int k = e->slot_n; k > pos; k--) e->slots[k] = e->slots[k - 1];
  fill_slot(&e->slots[pos], title, desc, start, end);
  e->slot_n++;
  epg_unlock();
}

static void apply_program(Scan *s, const char *epg_id, const char *obj) {
  if (!epg_id || !epg_id[0] || !obj || !s->nowv || s->skip_arr) return;
  long long start = json_long(obj, "start") + s->offset;
  long long end = json_long(obj, "end") + s->offset;
  if (!start || !end || end <= start) return;
  if (end < s->from_ms || start > s->to_ms) return;
  char title[72], desc[96];
  title[0] = desc[0] = 0;
  json_extract_string(obj, "title", title, (int)sizeof title);
  if (!json_extract_string(obj, "description", desc, (int)sizeof desc)) json_extract_string(obj, "desc", desc, (int)sizeof desc);
  int live = start <= s->now && s->now < end;
  unsigned h = hash_id(epg_id);
  for (int b = s->head[h]; b >= 0; b = s->chain[b]) {
    if (strcmp(s->bind[b].epg_id, epg_id) != 0) continue;
    int i = s->bind[b].index;
    if (i < 0 || i >= s->pl->count) continue;
    EpgNow *e = &s->nowv[i];
    if (live) {
      epg_lock();
      snprintf(e->title, sizeof e->title, "%s", title[0] ? title : "Программа");
      snprintf(e->desc, sizeof e->desc, "%s", desc);
      e->start = start;
      e->end = end;
      epg_unlock();
    }
    insert_slot(e, title, desc, start, end, s->now);
    s->got_prog++;
    g_dirty = 1;
  }
}

static const char *skip_ws_c(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
  return p;
}

static void scan_take(Scan *s) {
  char *p = s->buf;
  char *end = s->buf + s->n;
  while (p < end && !g_stop) {
    if (s->section == SEC_ARR) {
      p = (char *)skip_ws_c(p, end);
      if (p >= end) break;
      if (*p == ']') {
        s->section = SEC_PR;
        s->cur_key[0] = 0;
        s->skip_arr = 0;
        p++;
        continue;
      }
      if (*p == '{') {
        const char *fin = end_json(p);
        if (!fin || fin > end) break;
        if (!s->skip_arr) apply_program(s, s->cur_key, p);
        p = (char *)fin;
        continue;
      }
      p++;
      continue;
    }
    if (s->section < SEC_PR) {
      char *hit = strstr(p, "\"programs\"");
      if (hit && hit < end) {
        s->section = SEC_PR;
        p = hit + 10;
        continue;
      }
    }
    if (s->section < SEC_CH) {
      char *hit = strstr(p, "\"channels\"");
      if (hit && hit < end) {
        s->section = SEC_CH;
        p = hit + 10;
        continue;
      }
    }
    char *q = strchr(p, '"');
    if (!q || q >= end) break;
    const char *ke = q + 1;
    while (ke < end && *ke && *ke != '"') ke++;
    if (ke >= end) break;
    char key[96];
    int kn = (int)(ke - (q + 1));
    if (kn > 95) kn = 95;
    memcpy(key, q + 1, (size_t)kn);
    key[kn] = 0;
    const char *col = ke + 1;
    col = skip_ws_c(col, end);
    if (col >= end) break;
    if (*col != ':') {
      p = (char *)ke + 1;
      continue;
    }
    col++;
    col = skip_ws_c(col, end);
    if (col >= end) break;
    if (*col == '{') {
      const char *fin = end_json(col);
      if (!fin || fin > end) break;
      if (s->section == SEC_CH) bind_channel_obj(s, key, col);
      p = (char *)fin;
      continue;
    }
    if (*col == '[') {
      if (s->section == SEC_PR) {
        snprintf(s->cur_key, sizeof s->cur_key, "%s", key);
        s->skip_arr = !has_bind_id(s, key);
        s->section = SEC_ARR;
        p = (char *)col + 1;
        continue;
      }
      const char *fin = end_json(col);
      if (!fin || fin > end) break;
      p = (char *)fin;
      continue;
    }
    p = (char *)ke + 1;
  }
  int keep = (int)(end - p);
  if (keep < 0) keep = 0;
  if (keep > 0 && p != s->buf) memmove(s->buf, p, (size_t)keep);
  s->n = keep;
}

static int epg_feed(const unsigned char *p, unsigned n, void *user) {
  Scan *s = (Scan *)user;
  if (g_stop) return 1;
  unsigned off = 0;
  while (off < n && !g_stop) {
    int room = SCAN_CAP - 1 - s->n;
    if (room < 2048) {
      scan_take(s);
      room = SCAN_CAP - 1 - s->n;
      if (room < 512) {
        if (s->n > 64) {
          memmove(s->buf, s->buf + s->n - 64, 64);
          s->n = 64;
        }
        room = SCAN_CAP - 1 - s->n;
      }
    }
    int take = (int)(n - off);
    if (take > room) take = room;
    memcpy(s->buf + s->n, p + off, (size_t)take);
    s->n += take;
    s->buf[s->n] = 0;
    off += (unsigned)take;
    scan_take(s);
  }
  return g_stop ? 1 : 0;
}

static const unsigned char *gzip_deflate(const unsigned char *p, int n, int *outn) {
  if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) return NULL;
  unsigned flg = p[3];
  p += 10;
  n -= 10;
  if (flg & 4) {
    if (n < 2) return NULL;
    int x = p[0] | (p[1] << 8);
    p += 2 + x;
    n -= 2 + x;
  }
  if (flg & 8) {
    while (n && *p) {
      p++;
      n--;
    }
    if (n) {
      p++;
      n--;
    }
  }
  if (flg & 16) {
    while (n && *p) {
      p++;
      n--;
    }
    if (n) {
      p++;
      n--;
    }
  }
  if (flg & 2) {
    if (n < 2) return NULL;
    p += 2;
    n -= 2;
  }
  if (n < 8) return NULL;
  *outn = n - 8;
  return p;
}

static void xml_decode(const char *in, int n, char *out, int max) {
  int o = 0;
  if (!out || max < 2) return;
  out[0] = 0;
  if (!in || n < 0) return;
  for (int i = 0; i < n && o < max - 1;) {
    if (in[i] == '&') {
      if (i + 4 < n && !strncmp(in + i, "&amp;", 5)) {
        out[o++] = '&';
        i += 5;
      } else if (i + 3 < n && !strncmp(in + i, "&lt;", 4)) {
        out[o++] = '<';
        i += 4;
      } else if (i + 3 < n && !strncmp(in + i, "&gt;", 4)) {
        out[o++] = '>';
        i += 4;
      } else if (i + 5 < n && !strncmp(in + i, "&quot;", 6)) {
        out[o++] = '"';
        i += 6;
      } else if (i + 4 < n && (!strncmp(in + i, "&#39;", 5) || !strncmp(in + i, "&apos;", 6))) {
        out[o++] = '\'';
        i += in[i + 1] == '#' ? 5 : 6;
      } else {
        out[o++] = in[i++];
      }
    } else {
      out[o++] = in[i++];
    }
  }
  out[o] = 0;
}

static int xml_attr(const char *tag, const char *name, char *out, int max) {
  if (out && max) out[0] = 0;
  if (!tag || !name || !out) return 0;
  char key[64];
  snprintf(key, sizeof key, "%s=\"", name);
  const char *hit = strstr(tag, key);
  if (!hit) return 0;
  hit += strlen(key);
  const char *end = strchr(hit, '"');
  if (!end) return 0;
  xml_decode(hit, (int)(end - hit), out, max);
  return out[0] != 0;
}

static int xml_inner(const char *block, const char *tag, char *out, int max) {
  if (out && max) out[0] = 0;
  if (!block || !tag) return 0;
  char open[32], close[32];
  snprintf(open, sizeof open, "<%s", tag);
  snprintf(close, sizeof close, "</%s>", tag);
  const char *a = strstr(block, open);
  if (!a) return 0;
  const char *gt = strchr(a, '>');
  if (!gt || gt[-1] == '/') return 0;
  const char *b = strstr(gt, close);
  if (!b) return 0;
  const char *text = gt + 1;
  if (!strncmp(text, "<![CDATA[", 9)) {
    text += 9;
    const char *ce = strstr(text, "]]>");
    if (ce && ce < b) {
      xml_decode(text, (int)(ce - text), out, max);
      return out[0] != 0;
    }
  }
  xml_decode(text, (int)(b - text), out, max);
  return out[0] != 0;
}

static long long parse_xmltv_time(const char *s) {
  if (!s || strlen(s) < 14) return 0;
  int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
  if (sscanf(s, "%4d%2d%2d%2d%2d%2d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  const char *p = s + 14;
  while (*p == ' ') p++;
  int sign = 0, oh = 0, om = 0;
  if (*p == '+' || *p == '-') {
    sign = (*p == '+') ? 1 : -1;
    sscanf(p + 1, "%2d%2d", &oh, &om);
  }
  SYSTEMTIME st;
  memset(&st, 0, sizeof st);
  st.wYear = (WORD)Y;
  st.wMonth = (WORD)M;
  st.wDay = (WORD)D;
  st.wHour = (WORD)h;
  st.wMinute = (WORD)m;
  st.wSecond = (WORD)sec;
  FILETIME ft;
  if (sign) {
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    long long ms = (long long)(ul.QuadPart / 10000ULL - 11644473600000ULL);
    ms -= (long long)sign * ((long long)oh * 60 + om) * 60 * 1000;
    return ms;
  }
  SYSTEMTIME utc;
  if (!TzSpecificLocalTimeToSystemTime(NULL, &st, &utc)) utc = st;
  if (!SystemTimeToFileTime(&utc, &ft)) return 0;
  ULARGE_INTEGER ul;
  ul.LowPart = ft.dwLowDateTime;
  ul.HighPart = ft.dwHighDateTime;
  return (long long)(ul.QuadPart / 10000ULL - 11644473600000ULL);
}

static void bind_from_prog_id(Scan *s, const char *epg_id) {
  if (!s || !s->pl || !epg_id || !epg_id[0]) return;
  for (int i = 0; i < s->pl->count; i++) {
    if (s->pl->items[i].tvg_id[0] && _stricmp(s->pl->items[i].tvg_id, epg_id) == 0) bind_id(s, epg_id, i, 0);
  }
  if (!s->fold_name || !s->fold_tvg) return;
  char folded[FOLD_MAX];
  fold_epg_key(epg_id, folded, FOLD_MAX);
  if (!folded[0]) return;
  int rank = name_shifted(epg_id) ? 2 : 0;
  for (int i = 0; i < s->pl->count; i++) {
    if ((s->fold_name[i][0] && !strcmp(s->fold_name[i], folded)) || (s->fold_tvg[i][0] && !strcmp(s->fold_tvg[i], folded)))
      bind_id(s, epg_id, i, rank);
  }
}

static void apply_bound(Scan *s, const char *epg_id, const char *title, const char *desc, long long start, long long end) {
  if (!s || !s->nowv || !epg_id || !epg_id[0] || !start || !end || end <= start) return;
  if (end < s->from_ms || start > s->to_ms) return;
  unsigned h = hash_id(epg_id);
  int known = 0;
  for (int b = s->head[h]; b >= 0; b = s->chain[b]) {
    if (strcmp(s->bind[b].epg_id, epg_id) == 0) {
      known = 1;
      break;
    }
  }
  if (!known) bind_from_prog_id(s, epg_id);
  int live = start <= s->now && s->now < end;
  for (int b = s->head[h]; b >= 0; b = s->chain[b]) {
    if (strcmp(s->bind[b].epg_id, epg_id) != 0) continue;
    int i = s->bind[b].index;
    if (i < 0 || i >= s->pl->count) continue;
    EpgNow *e = &s->nowv[i];
    if (live) {
      epg_lock();
      snprintf(e->title, sizeof e->title, "%s", title && title[0] ? title : "Программа");
      snprintf(e->desc, sizeof e->desc, "%s", desc ? desc : "");
      e->start = start;
      e->end = end;
      epg_unlock();
    }
    insert_slot(e, title, desc, start, end, s->now);
    s->got_prog++;
    g_dirty = 1;
  }
}

static void xml_take_block(Scan *s, const char *block, int is_ch) {
  if (is_ch) {
    char id[96];
    if (!xml_attr(block, "id", id, (int)sizeof id) || !s->pl) return;
    for (int i = 0; i < s->pl->count; i++) {
      if (s->pl->items[i].tvg_id[0] && _stricmp(s->pl->items[i].tvg_id, id) == 0) bind_id(s, id, i, 0);
    }
    char id_fold[FOLD_MAX];
    fold_epg_key(id, id_fold, FOLD_MAX);
    if (id_fold[0] && s->fold_name && s->fold_tvg) {
      for (int i = 0; i < s->pl->count; i++) {
        if ((s->fold_name[i][0] && !strcmp(s->fold_name[i], id_fold)) ||
            (s->fold_tvg[i][0] && !strcmp(s->fold_tvg[i], id_fold)))
          bind_id(s, id, i, name_shifted(id) ? 2 : 0);
      }
    }
    const char *p = block;
    while ((p = strstr(p, "<display-name"))) {
      char nm[MAX_NAME];
      if (xml_inner(p, "display-name", nm, MAX_NAME) && nm[0] && s->fold_name && s->fold_tvg) {
        char folded[FOLD_MAX];
        fold_epg_key(nm, folded, FOLD_MAX);
        int rank = name_shifted(nm) ? 2 : 0;
        if (folded[0]) {
          for (int i = 0; i < s->pl->count; i++) {
            if ((s->fold_name[i][0] && !strcmp(s->fold_name[i], folded)) ||
                (s->fold_tvg[i][0] && !strcmp(s->fold_tvg[i], folded)))
              bind_id(s, id, i, rank);
          }
        }
      }
      p += 13;
    }
    char icon[MAX_REF];
    icon[0] = 0;
    const char *ip = strstr(block, "<icon");
    if (ip) xml_attr(ip, "src", icon, MAX_REF);
    note_icon_for_id(s, id, icon);
    return;
  }
  char id[96], st[40], en[40], title[72], desc[96];
  if (!xml_attr(block, "channel", id, (int)sizeof id)) return;
  if (!xml_attr(block, "start", st, (int)sizeof st) || !xml_attr(block, "stop", en, (int)sizeof en)) return;
  xml_inner(block, "title", title, (int)sizeof title);
  if (!xml_inner(block, "desc", desc, (int)sizeof desc)) xml_inner(block, "description", desc, (int)sizeof desc);
  s->file_prog++;
  apply_bound(s, id, title, desc, parse_xmltv_time(st) + s->offset, parse_xmltv_time(en) + s->offset);
}

static void xml_scan_take(Scan *s) {
  char *p = s->buf;
  char *end = s->buf + s->n;
  while (p < end && !g_stop) {
    char *ch = strstr(p, "<channel");
    char *pr = strstr(p, "<programme");
    if ((!ch || ch >= end) && (!pr || pr >= end)) break;
    int is_ch = ch && (!pr || ch < pr);
    s->saw_xml = 1;
    char *start = is_ch ? ch : pr;
    const char *close = is_ch ? "</channel>" : "</programme>";
    char *fin = strstr(start, close);
    if (!fin || fin >= end) {
      p = start;
      break;
    }
    fin += (int)strlen(close);
    char save = *fin;
    *fin = 0;
    xml_take_block(s, start, is_ch);
    *fin = save;
    p = fin;
  }
  int keep = (int)(end - p);
  if (keep < 0) keep = 0;
  if (keep > 0 && p != s->buf) memmove(s->buf, p, (size_t)keep);
  s->n = keep;
}

static int xml_feed(const unsigned char *p, unsigned n, void *user) {
  Scan *s = (Scan *)user;
  if (g_stop) return 1;
  unsigned off = 0;
  while (off < n && !g_stop) {
    int room = SCAN_CAP - 1 - s->n;
    if (room < 2048) {
      xml_scan_take(s);
      room = SCAN_CAP - 1 - s->n;
      if (room < 512) {
        if (s->n > 64) {
          memmove(s->buf, s->buf + s->n - 64, 64);
          s->n = 64;
        }
        room = SCAN_CAP - 1 - s->n;
      }
    }
    int take = (int)(n - off);
    if (take > room) take = room;
    memcpy(s->buf + s->n, p + off, (size_t)take);
    s->n += take;
    s->buf[s->n] = 0;
    off += (unsigned)take;
    xml_scan_take(s);
  }
  return g_stop ? 1 : 0;
}

static int has_ascii(const char *s, const char *needle) {
  if (!s || !needle || !needle[0]) return 0;
  int nlen = (int)strlen(needle);
  for (const char *p = s; *p; p++) {
    int i = 0;
    while (i < nlen && p[i]) {
      char a = p[i], b = needle[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
      i++;
    }
    if (i == nlen) return 1;
  }
  return 0;
}

static int looks_like_xmltv(const unsigned char *p, int n) {
  if (!p || n < 16) return 0;
  int skip = 0;
  if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) skip = 3;
  int take = n - skip;
  if (take > 4096) take = 4096;
  char head[4097];
  memcpy(head, p + skip, (size_t)take);
  head[take] = 0;
  if (has_ascii(head, "<html") || has_ascii(head, "<!doctype html")) return 0;
  if (head[0] == '{' || head[0] == '[') return 0;
  return has_ascii(head, "<tv") || has_ascii(head, "<programme") || has_ascii(head, "<channel");
}

static int epg_has_url(const char *url) {
  if (!url || !url[0]) return 0;
  for (int i = 0; i < g_epg_url_n; i++) {
    if (strcmp(g_epg_urls[i], url) == 0) return 1;
  }
  return 0;
}

static void epg_push_url(const char *url) {
  if (!url || !url[0] || g_epg_url_n >= MAX_SAVED) return;
  if (epg_has_url(url)) return;
  snprintf(g_epg_urls[g_epg_url_n], MAX_URL, "%s", url);
  g_epg_url_n++;
}

static void epg_copy_sources(const Settings *st) {
  g_epg_url_n = 0;
  if (!st) return;
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 1) epg_push_url(st->epg_src[i].url);
  }
  for (int i = 0; i < st->epg_n; i++) {
    if (st->epg_src[i].enabled == 2) epg_push_url(st->epg_src[i].url);
  }
  for (int i = 0; i < st->epg_n; i++) epg_push_url(st->epg_src[i].url);
  epg_push_url(st->epg_url);
  epg_push_url(st->epg_url2);
}

static void scan_clear_binds(Scan *s) {
  if (!s) return;
  memset(s->head, 0xFF, sizeof s->head);
  s->bind_n = 0;
  if (s->owned && s->pl) {
    for (int i = 0; i < s->pl->count; i++) s->owned[i] = -1;
  }
}

static void scan_reset_parse(Scan *s) {
  if (!s) return;
  s->n = 0;
  s->buf[0] = 0;
  s->section = SEC_NONE;
  s->skip_arr = 0;
  s->saw_xml = 0;
  s->file_prog = 0;
}

static int try_https(char *url, int max) {
  if (!url || !istarts(url, "http://")) return 0;
  char tmp[MAX_URL];
  snprintf(tmp, sizeof tmp, "https://%s", url + 7);
  snprintf(url, max, "%s", tmp);
  return 1;
}

static int load_xmltv_url(const char *url, Scan *s) {
  if (!url || !url[0] || !s) return 0;
  char tryu[MAX_URL];
  snprintf(tryu, MAX_URL, "%s", url);
  char *raw = NULL;
  int n = 0;
  int before = s->got_prog;
  scan_reset_parse(s);
  scan_clear_binds(s);
  if (try_https(tryu, MAX_URL)) {
    if (http_get_lim(tryu, &raw, &n, 80 * 1024 * 1024, EPG_HTTP_MS) != 0) {
      free(raw);
      raw = NULL;
      n = 0;
      snprintf(tryu, MAX_URL, "%s", url);
    }
  }
  if (!raw && http_get_lim(tryu, &raw, &n, 80 * 1024 * 1024, EPG_HTTP_MS) != 0) {
    free(raw);
    return 0;
  }
  if (!raw || n < 32) {
    free(raw);
    return 0;
  }
  snprintf(g_fetch_base, sizeof g_fetch_base, "%s", tryu);
  const unsigned char *p = (const unsigned char *)raw;
  int gzip = n >= 3 && p[0] == 0x1f && p[1] == 0x8b;
  if (!gzip && !looks_like_xmltv(p, n)) {
    free(raw);
    return 0;
  }
  if (gzip) {
    int dn = 0;
    const unsigned char *def = gzip_deflate(p, n, &dn);
    if (!def || dn < 8) {
      free(raw);
      return 0;
    }
    tinf_uncompress_cb(xml_feed, s, def, (unsigned)dn);
  } else {
    xml_feed(p, (unsigned)n, s);
  }
  if (!g_stop) xml_scan_take(s);
  free(raw);
  /* Empty/HTML/no programmes = junk. Missing EPG on some playlist channels is normal. */
  if (!s->saw_xml || s->file_prog < 1) {
    scan_clear_binds(s);
    s->got_prog = before;
    return 0;
  }
  if (s->got_prog <= before) {
    scan_clear_binds(s);
    return 0;
  }
  return 1;
}

static void native_epg_path(wchar_t *path, int tmp) {
  wchar_t dir[MAX_PATH];
  profile_dirs(dir, NULL, MAX_PATH);
  swprintf(path, MAX_PATH, tmp ? L"%s\\epg.bin.tmp" : L"%s\\epg.bin", dir);
}

static int put_u8(FILE *f, unsigned v) {
  unsigned char c = (unsigned char)v;
  return fwrite(&c, 1, 1, f) == 1;
}
static int put_u16(FILE *f, unsigned v) {
  unsigned char b[2] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255)};
  return fwrite(b, 1, 2, f) == 2;
}
static int put_u32(FILE *f, unsigned v) {
  unsigned char b[4] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255),
                        (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 24) & 255)};
  return fwrite(b, 1, 4, f) == 4;
}
static int put_i64(FILE *f, long long v) {
  unsigned char b[8];
  unsigned long long u = (unsigned long long)v;
  for (int i = 0; i < 8; i++) b[i] = (unsigned char)((u >> (8 * i)) & 255);
  return fwrite(b, 1, 8, f) == 8;
}
static int put_str(FILE *f, const char *s, int maxl) {
  int n = s ? (int)strlen(s) : 0;
  if (n > maxl) n = maxl;
  if (!put_u8(f, (unsigned)n)) return 0;
  return n < 1 || fwrite(s, 1, (size_t)n, f) == (size_t)n;
}
static int put_str16(FILE *f, const char *s, int maxl) {
  int n = s ? (int)strlen(s) : 0;
  if (n > maxl) n = maxl;
  if (!put_u16(f, (unsigned)n)) return 0;
  return n < 1 || fwrite(s, 1, (size_t)n, f) == (size_t)n;
}

static int get_u8(FILE *f, unsigned *v) {
  unsigned char c;
  if (fread(&c, 1, 1, f) != 1) return 0;
  *v = c;
  return 1;
}
static int get_u16(FILE *f, unsigned *v) {
  unsigned char b[2];
  if (fread(b, 1, 2, f) != 2) return 0;
  *v = (unsigned)b[0] | ((unsigned)b[1] << 8);
  return 1;
}
static int get_u32(FILE *f, unsigned *v) {
  unsigned char b[4];
  if (fread(b, 1, 4, f) != 4) return 0;
  *v = (unsigned)b[0] | ((unsigned)b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
  return 1;
}
static int get_i64(FILE *f, long long *v) {
  unsigned char b[8];
  if (fread(b, 1, 8, f) != 8) return 0;
  unsigned long long u = 0;
  for (int i = 7; i >= 0; i--) u = (u << 8) | b[i];
  *v = (long long)u;
  return 1;
}
static int get_str(FILE *f, char *out, int max) {
  unsigned n = 0;
  if (!get_u8(f, &n) || !out || max < 2) return 0;
  int take = (int)n;
  if (take >= max) take = max - 1;
  if (take > 0 && fread(out, 1, (size_t)take, f) != (size_t)take) return 0;
  out[take] = 0;
  int skip = (int)n - take;
  if (skip > 0) fseek(f, skip, SEEK_CUR);
  return 1;
}
static int get_str16(FILE *f, char *out, int max) {
  unsigned n = 0;
  if (!get_u16(f, &n) || !out || max < 2) return 0;
  int take = (int)n;
  if (take >= max) take = max - 1;
  if (take > 0 && fread(out, 1, (size_t)take, f) != (size_t)take) return 0;
  out[take] = 0;
  int skip = (int)n - take;
  if (skip > 0) fseek(f, skip, SEEK_CUR);
  return 1;
}

static void channel_save_key(int i, char *key, int max) {
  if (!g_pl || i < 0 || i >= g_pl->count || !key || max < 2) return;
  const Channel *ch = &g_pl->items[i];
  if (ch->tvg_id[0]) snprintf(key, max, "%s", ch->tvg_id);
  else snprintf(key, max, "%s", ch->name);
}

static int channel_index_for_key(const char *key) {
  if (!g_pl || !key || !key[0]) return -1;
  for (int i = 0; i < g_pl->count; i++) {
    if (g_pl->items[i].tvg_id[0] && _stricmp(g_pl->items[i].tvg_id, key) == 0) return i;
  }
  char fold[FOLD_MAX];
  fold_epg_key(key, fold, FOLD_MAX);
  if (!fold[0]) return -1;
  for (int i = 0; i < g_pl->count; i++) {
    char a[FOLD_MAX], b[FOLD_MAX];
    fold_epg_key(g_pl->items[i].name, a, FOLD_MAX);
    fold_epg_key(g_pl->items[i].tvg_name[0] ? g_pl->items[i].tvg_name : g_pl->items[i].name, b, FOLD_MAX);
    if ((a[0] && !strcmp(a, fold)) || (b[0] && !strcmp(b, fold))) return i;
  }
  return -1;
}

static void apply_saved_icon(const char *key, const char *icon) {
  if (!g_pl || !key || !key[0] || !icon || !icon[0]) return;
  int n = 0;
  for (int i = 0; i < g_pl->count; i++) {
    const Channel *ch = &g_pl->items[i];
    int exact = (ch->tvg_id[0] && _stricmp(ch->tvg_id, key) == 0) || (ch->name[0] && strcmp(ch->name, key) == 0) ||
                (ch->tvg_name[0] && strcmp(ch->tvg_name, key) == 0);
    if (!exact) continue;
    epg_note_icon(i, 0, icon);
    n++;
  }
  if (n) return;
  int idx = channel_index_for_key(key);
  if (idx >= 0) epg_note_icon(idx, 0, icon);
}

static void epg_window_now(long long *from, long long *to) {
  long long day = 24LL * 60 * 60 * 1000;
  long long today = epg_day_start(epg_now_ms());
  if (from) *from = today - (long long)g_past_days * day;
  if (to) *to = today + (long long)(g_future_days + 1) * day;
}

static void epg_prune_window(void) {
  long long from = 0, to = 0;
  epg_window_now(&from, &to);
  if (!g_now) return;
  epg_lock();
  for (int i = 0; i < g_n; i++) {
    EpgNow *e = &g_now[i];
    int w = 0;
    for (int k = 0; k < e->slot_n; k++) {
      if (e->slots[k].end < from || e->slots[k].start > to) continue;
      if (w != k) e->slots[w] = e->slots[k];
      w++;
    }
    e->slot_n = w;
  }
  epg_unlock();
}

static int native_epg_save(void) {
  if (!g_now || !g_pl || g_n < 1) return 0;
  wchar_t path[MAX_PATH], tmp[MAX_PATH];
  native_epg_path(path, 0);
  native_epg_path(tmp, 1);
  FILE *f = _wfopen(tmp, L"wb");
  if (!f) return 0;
  int ok = 1;
  unsigned nch = 0;
  long long now = epg_now_ms();
  long long from = 0, to = 0;
  epg_window_now(&from, &to);
  epg_lock();
  for (int i = 0; i < g_n; i++) {
    if (g_now[i].slot_n > 0 || (g_icon && g_icon[i][0])) nch++;
  }
  ok = ok && fwrite("MEPG", 1, 4, f) == 4;
  ok = ok && put_u16(f, 2);
  ok = ok && put_u16(f, 1);
  ok = ok && put_i64(f, now);
  ok = ok && put_i64(f, from);
  ok = ok && put_i64(f, to);
  ok = ok && put_u16(f, (unsigned)g_past_days);
  ok = ok && put_u16(f, (unsigned)g_future_days);
  ok = ok && put_u32(f, nch);
  for (int i = 0; i < g_n && ok; i++) {
    EpgNow *e = &g_now[i];
    if (e->slot_n < 1 && !(g_icon && g_icon[i][0])) continue;
    char key[EPG_KEY_MAX];
    key[0] = 0;
    channel_save_key(i, key, EPG_KEY_MAX);
    unsigned slots = (unsigned)e->slot_n;
    if (slots > 0xFFFF) slots = 0xFFFF;
    ok = ok && put_str(f, key, EPG_KEY_MAX - 1);
    ok = ok && put_str16(f, g_icon ? g_icon[i] : "", MAX_REF - 1);
    ok = ok && put_u16(f, slots);
    for (unsigned k = 0; k < slots && ok; k++) {
      ok = ok && put_i64(f, e->slots[k].start);
      ok = ok && put_i64(f, e->slots[k].end);
      ok = ok && put_str(f, e->slots[k].title, 71);
      ok = ok && put_str(f, e->slots[k].desc, 95);
    }
  }
  epg_unlock();
  fclose(f);
  if (!ok) {
    DeleteFileW(tmp);
    return 0;
  }
  if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp);
    return 0;
  }
  DeleteFileW(tmp);
  g_saved_at = now;
  g_file_back = g_past_days;
  g_file_ahead = g_future_days;
  g_epg_ver = 2;
  return 1;
}

static int native_epg_load(void) {
  wchar_t path[MAX_PATH];
  native_epg_path(path, 0);
  FILE *f = _wfopen(path, L"rb");
  if (!f) return 0;
  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MEPG", 4) != 0) {
    fclose(f);
    return 0;
  }
  unsigned ver = 0, flags = 0, nch = 0, back = 0, ahead = 0;
  long long saved = 0, from = 0, to = 0;
  g_epg_ver = 0;
  if (!get_u16(f, &ver) || (ver != 1 && ver != 2) || !get_u16(f, &flags)) {
    fclose(f);
    return 0;
  }
  if (!get_i64(f, &saved) || !get_i64(f, &from) || !get_i64(f, &to)) {
    fclose(f);
    return 0;
  }
  if (!get_u16(f, &back) || !get_u16(f, &ahead) || !get_u32(f, &nch)) {
    fclose(f);
    return 0;
  }
  if (nch > 20000) {
    fclose(f);
    return 0;
  }
  int loaded = 0;
  for (unsigned c = 0; c < nch && !g_stop; c++) {
    char key[EPG_KEY_MAX];
    char icon[MAX_REF];
    unsigned slots = 0;
    icon[0] = 0;
    if (!get_str(f, key, EPG_KEY_MAX)) break;
    if (ver == 2 && !get_str16(f, icon, MAX_REF)) break;
    if (!get_u16(f, &slots)) break;
    if (icon[0]) apply_saved_icon(key, icon);
    int idx = channel_index_for_key(key);
    for (unsigned k = 0; k < slots; k++) {
      long long st = 0, en = 0;
      char title[72], desc[96];
      title[0] = desc[0] = 0;
      if (!get_i64(f, &st) || !get_i64(f, &en) || !get_str(f, title, 72) || !get_str(f, desc, 96)) {
        fclose(f);
        return loaded;
      }
      if (idx < 0 || !g_now || idx >= g_n) continue;
      if (en < from || st > to) continue;
      insert_slot(&g_now[idx], title, desc, st, en, epg_now_ms());
      loaded = 1;
    }
  }
  fclose(f);
  if (loaded) {
    g_saved_at = saved;
    g_file_back = (int)back;
    g_file_ahead = (int)ahead;
    g_epg_ver = (int)ver;
    g_dirty = 1;
  }
  return loaded;
}

static int native_epg_fresh(void) {
  if (!g_saved_at) return 0;
  if (g_file_back != g_past_days || g_file_ahead != g_future_days) return 0;
  long long age = epg_now_ms() - g_saved_at;
  if (age < 0) return 0;
  return age < (long long)g_epg_hours * 3600LL * 1000LL;
}

static int refresh_xmltv(Scan *s) {
  if (!s || g_epg_url_n < 1) return 0;
  player_show_text("Обновляем телепрограмму", 2000);
  int ok = 0;
  for (int i = 0; i < g_epg_url_n && !g_stop; i++) {
    s->got_prog = 0;
    if (load_xmltv_url(g_epg_urls[i], s)) {
      ok = 1;
      break;
    }
  }
  if (!ok) player_show_text("Не удалось обновить телепрограмму", 2500);
  return ok;
}

static long long coverage_end(void) {
  long long have = 0;
  if (!g_now) return 0;
  epg_lock();
  for (int i = 0; i < g_n; i++) {
    EpgNow *e = &g_now[i];
    if (!e->slots || e->slot_n < 1) continue;
    long long end = e->slots[e->slot_n - 1].end;
    if (end > have) have = end;
  }
  epg_unlock();
  return have;
}

static void scan_setup(Scan *s, int slice) {
  memset(s->head, 0xFF, sizeof s->head);
  s->pl = g_pl;
  s->nowv = g_now;
  s->now = epg_now_ms();
  s->offset = g_offset;
  long long day = 24LL * 60 * 60 * 1000;
  long long today = epg_day_start(s->now);
  long long win_from = today - (long long)g_past_days * day;
  long long win_to = today + (long long)(g_future_days + 1) * day;
  s->from_ms = win_from;
  s->to_ms = win_to;
  if (slice) {
    long long have = coverage_end();
    if (have < s->now) have = s->now;
    long long chunk = (long long)g_epg_hours * 3600LL * 1000LL;
    if (chunk < 3600LL * 1000LL) chunk = 3600LL * 1000LL;
    s->from_ms = have - 30LL * 60LL * 1000LL;
    if (s->from_ms < win_from) s->from_ms = win_from;
    s->to_ms = have + chunk;
    if (s->to_ms > win_to) s->to_ms = win_to;
  }
  if (!g_pl || g_pl->count < 1) return;
  s->owned = (int *)malloc((size_t)g_pl->count * sizeof(int));
  s->fold_name = (char (*)[FOLD_MAX])calloc((size_t)g_pl->count, FOLD_MAX);
  s->fold_tvg = (char (*)[FOLD_MAX])calloc((size_t)g_pl->count, FOLD_MAX);
  if (s->owned) {
    for (int i = 0; i < g_pl->count; i++) s->owned[i] = -1;
  }
  if (s->fold_name && s->fold_tvg) {
    for (int i = 0; i < g_pl->count; i++) {
      fold_epg_key(g_pl->items[i].name, s->fold_name[i], FOLD_MAX);
      fold_epg_key(g_pl->items[i].tvg_name[0] ? g_pl->items[i].tvg_name : g_pl->items[i].name, s->fold_tvg[i], FOLD_MAX);
    }
  }
}

static void load_live_cache(Scan *s) {
  wchar_t live[MAX_PATH], path[MAX_PATH];
  profile_dirs(NULL, live, MAX_PATH);
  swprintf(path, MAX_PATH, L"%s\\epg-cache.json.gz", live);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 18 || sz.QuadPart > 80LL * 1024 * 1024) {
    CloseHandle(h);
    return;
  }
  unsigned char *raw = (unsigned char *)malloc((size_t)sz.QuadPart);
  DWORD got = 0;
  if (!raw || !ReadFile(h, raw, (DWORD)sz.QuadPart, &got, NULL)) {
    free(raw);
    CloseHandle(h);
    return;
  }
  CloseHandle(h);
  int dn = 0;
  const unsigned char *def = gzip_deflate(raw, (int)got, &dn);
  if (def && dn >= 8) {
    tinf_uncompress_cb(epg_feed, s, def, (unsigned)dn);
    if (!g_stop) scan_take(s);
  }
  free(raw);
}

static void scan_free(Scan *s) {
  if (!s) return;
  free(s->fold_name);
  free(s->fold_tvg);
  free(s->owned);
  free(s->bind);
  free(s->chain);
  free(s);
}

static DWORD WINAPI epg_thread(void *arg) {
  (void)arg;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  g_busy = 1;
  Scan *s = (Scan *)calloc(1, sizeof(Scan));
  if (!s) {
    g_ready = 1;
    g_busy = 0;
    return 0;
  }
  scan_setup(s, 0);
  int have = native_epg_load();
  if (!have) load_live_cache(s);
  int icon_scan = have && g_epg_ver < 2;
  int need = g_epg_force || !have || !native_epg_fresh() || icon_scan;
  if (need && (g_epg_auto || g_epg_force || !have || icon_scan)) {
    int ok = refresh_xmltv(s);
    if (ok) {
      epg_prune_window();
      native_epg_save();
    } else {
      g_next_try = epg_now_ms() + 30LL * 60LL * 1000LL;
    }
  }
  if (!g_saved_at && g_now) {
    int any = 0;
    for (int i = 0; i < g_n; i++) {
      if (g_now[i].slot_n > 0) {
        any = 1;
        break;
      }
    }
    if (any) {
      epg_prune_window();
      native_epg_save();
    }
  }
  g_epg_force = 0;
  scan_free(s);
  g_dirty = 1;
  g_ready = 1;
  g_busy = 0;
  return 0;
}

static DWORD WINAPI epg_update_thread(void *arg) {
  (void)arg;
  Scan *s = (Scan *)calloc(1, sizeof(Scan));
  if (!s) {
    g_busy = 0;
    return 0;
  }
  scan_setup(s, 1);
  if (s->from_ms >= s->to_ms) {
    epg_prune_window();
    native_epg_save();
    g_dirty = 1;
    g_next_try = 0;
    scan_free(s);
    g_busy = 0;
    return 0;
  }
  int ok = refresh_xmltv(s);
  if (ok) {
    epg_prune_window();
    native_epg_save();
    g_dirty = 1;
    g_next_try = 0;
  } else {
    g_next_try = epg_now_ms() + 30LL * 60LL * 1000LL;
  }
  scan_free(s);
  g_busy = 0;
  return 0;
}

void epg_start(const Playlist *pl, const Settings *st) {
  epg_stop();
  g_stop = 0;
  g_saved_at = 0;
  g_next_try = 0;
  InitializeCriticalSection(&g_lock);
  g_lock_on = 1;
  g_pl = pl;
  g_offset = st ? (long long)st->epg_offset_hours * 60LL * 60LL * 1000LL : 0;
  epg_copy_sources(st);
  g_epg_hours = st && st->epg_update_hours > 0 ? st->epg_update_hours : 6;
  g_epg_auto = !st || st->epg_auto_update;
  g_future_days = st && st->epg_days > 0 ? st->epg_days : 7;
  if (g_future_days < 1) g_future_days = 7;
  if (g_future_days > 14) g_future_days = 14;
  g_past_days = g_future_days;
  if (st && st->archive_enabled && st->archive_days > g_past_days) g_past_days = st->archive_days;
  {
    wchar_t leftover[MAX_PATH];
    native_epg_path(leftover, 1);
    DeleteFileW(leftover);
  }
  if (!pl || pl->count < 1) return;
  g_n = pl->count;
  g_now = (EpgNow *)calloc((size_t)g_n, sizeof(EpgNow));
  if (!g_now) {
    g_n = 0;
    return;
  }
  g_icon = (char (*)[MAX_REF])calloc((size_t)g_n, sizeof(*g_icon));
  g_icon_rank = (int *)malloc((size_t)g_n * sizeof(int));
  if (g_icon_rank) {
    for (int i = 0; i < g_n; i++) g_icon_rank[i] = 127;
  }
  g_busy = 1;
  g_thread = CreateThread(NULL, 0, epg_thread, NULL, 0, NULL);
  if (!g_thread) g_busy = 0;
}

void epg_reload(const Playlist *pl, const Settings *st) {
  g_epg_force = 1;
  g_next_try = 0;
  epg_start(pl, st);
}

void epg_apply_settings(const Settings *st) {
  if (!st) return;
  g_offset = (long long)st->epg_offset_hours * 60LL * 60LL * 1000LL;
  epg_copy_sources(st);
  g_epg_hours = st->epg_update_hours > 0 ? st->epg_update_hours : 6;
  g_epg_auto = st->epg_auto_update;
  g_future_days = st->epg_days > 0 ? st->epg_days : 7;
  if (g_future_days < 1) g_future_days = 7;
  if (g_future_days > 14) g_future_days = 14;
  g_past_days = g_future_days;
  if (st->archive_enabled && st->archive_days > g_past_days) g_past_days = st->archive_days;
  if (g_ready && g_now) {
    epg_prune_window();
    native_epg_save();
    g_dirty = 1;
  }
}

void epg_tick(void) {
  if (!g_epg_auto || !g_ready || g_busy || g_stop) return;
  if (g_next_try && epg_now_ms() < g_next_try) return;
  if (native_epg_fresh()) return;
  g_busy = 1;
  HANDLE t = CreateThread(NULL, 0, epg_update_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
  else g_busy = 0;
}

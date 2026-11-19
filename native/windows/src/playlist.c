#include "app.h"
#include <ctype.h>

static void copy_trunc(char *dst, int max, const char *src) {
  if (!dst || max < 2) return;
  if (!src) {
    dst[0] = 0;
    return;
  }
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

static void trim_copy(char *dst, int max, const char *src) {
  if (!src) {
    if (dst && max) dst[0] = 0;
    return;
  }
  while (*src == ' ' || *src == '\t') src++;
  const char *end = src + strlen(src);
  while (end > src && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
  int n = (int)(end - src);
  if (n > max - 1) n = max - 1;
  memcpy(dst, src, (size_t)n);
  dst[n] = 0;
}

static int utf8_next(const char **p, unsigned *cp) {
  const unsigned char *s = (const unsigned char *)*p;
  if (!*s) {
    *cp = 0;
    return 0;
  }
  if (s[0] < 0x80) {
    *cp = s[0];
    *p += 1;
    return 1;
  }
  if ((s[0] & 0xE0) == 0xC0 && s[1]) {
    *cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    *p += 2;
    return 1;
  }
  if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
    *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    *p += 3;
    return 1;
  }
  *cp = s[0];
  *p += 1;
  return 1;
}

static int is_alnum_cp(unsigned cp) {
  if (cp >= '0' && cp <= '9') return 1;
  if (cp >= 'a' && cp <= 'z') return 1;
  if (cp >= 'A' && cp <= 'Z') return 1;
  if (cp >= 0x410 && cp <= 0x44F) return 1;
  if (cp == 0x401 || cp == 0x451) return 1;
  return 0;
}

void fold_family(const char *name, char *out, int max) {
  if (!out || max < 2) return;
  out[0] = 0;
  if (!name) return;
  char tmp[512];
  int n = 0;
  const char *p = name;
  unsigned cp;
  while (utf8_next(&p, &cp) && n < (int)sizeof(tmp) - 5) {
    if (cp == 0x401 || cp == 0x451) cp = 0x435;
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
    if (!is_alnum_cp(cp)) {
      if (n && tmp[n - 1] != ' ') tmp[n++] = ' ';
      continue;
    }
    if (cp < 0x80) tmp[n++] = (char)cp;
    else if (cp < 0x800) {
      tmp[n++] = (char)(0xC0 | (cp >> 6));
      tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
      tmp[n++] = (char)(0xE0 | (cp >> 12));
      tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      tmp[n++] = (char)(0x80 | (cp & 0x3F));
    }
  }
  tmp[n] = 0;
  const char *words[] = {"uhd", "fhd", "hd", "sd", "4k", "hevc", "hdr", "tv", "тв", "канал", "телеканал", "программа", NULL};
  char folded[512];
  folded[0] = 0;
  for (char *tok = tmp; *tok;) {
    while (*tok == ' ') tok++;
    if (!*tok) break;
    char *sp = strchr(tok, ' ');
    if (sp) *sp = 0;
    int drop = 0;
    for (int i = 0; words[i]; i++) {
      if (strcmp(tok, words[i]) == 0) drop = 1;
    }
    if (!drop) {
      if (folded[0]) strcat(folded, " ");
      strcat(folded, tok);
    }
    tok = sp ? sp + 1 : tok + strlen(tok);
  }
  copy_trunc(out, max, folded);
}

int quality_of(const char *name, const char *url) {
  char text[1024];
  snprintf(text, sizeof text, "%s %s", name ? name : "", url ? url : "");
  for (char *p = text; *p; p++) *p = (char)tolower((unsigned char)*p);
  if (strstr(text, "4k") || strstr(text, "uhd") || strstr(text, "2160")) return 4;
  if (strstr(text, "hevc") || strstr(text, "h265") || strstr(text, "h.265")) return 4;
  if (strstr(text, "fhd") || strstr(text, "1080")) return 3;
  if (strstr(text, "720") || strstr(text, " hd") || strstr(text, "hd ") || strcmp(text, "hd") == 0) return 2;
  if (strstr(text, "sd") || strstr(text, "480") || strstr(text, "576")) return 1;
  return 3;
}

const char *quality_label(int q) {
  if (q >= 4) return "4K";
  if (q == 3) return "FHD";
  if (q == 2) return "HD";
  if (q == 1) return "SD";
  return "FHD";
}

static int is_xtream_live(const char *url) {
  return url && strstr(url, "/live/");
}

static void swap_ext(const char *url, const char *from, const char *to, char *out, int max) {
  copy_trunc(out, max, url);
  char query[1024];
  query[0] = 0;
  char *q = strchr(out, '?');
  if (q) {
    copy_trunc(query, (int)sizeof query, q);
    *q = 0;
  }
  char *dot = strrchr(out, '.');
  char *slash = strrchr(out, '/');
  if (!dot || (slash && dot < slash)) return;
  if (_stricmp(dot, from) != 0) return;
  if ((int)((dot - out) + strlen(to) + strlen(query) + 1) > max) return;
  strcpy(dot, to);
  if (query[0]) strcat(out, query);
}

static int url_listed(char urls[][MAX_URL], int n, const char *url) {
  for (int k = 0; k < n; k++) {
    if (strcmp(urls[k], url) == 0) return 1;
  }
  return 0;
}

static void add_url_pair(char urls[][MAX_URL], int *n, int maxn, const char *url) {
  if (!url || !url[0] || *n >= maxn || url_listed(urls, *n, url)) return;
  char alt[MAX_URL];
  alt[0] = 0;
  if (is_xtream_live(url)) {
    if (strstr(url, ".m3u8") || strstr(url, ".M3U8")) swap_ext(url, ".m3u8", ".ts", alt, MAX_URL);
    else if (strstr(url, ".ts") || strstr(url, ".TS")) swap_ext(url, ".ts", ".m3u8", alt, MAX_URL);
  }
  copy_trunc(urls[(*n)++], MAX_URL, url);
  if (alt[0] && *n < maxn && !url_listed(urls, *n, alt)) copy_trunc(urls[(*n)++], MAX_URL, alt);
}

void build_play_urls(const Playlist *pl, int index, int start_q, char urls[][MAX_URL], int *n, int maxn) {
  (void)start_q;
  *n = 0;
  if (!pl || index < 0 || index >= pl->count || maxn < 1) return;
  add_url_pair(urls, n, maxn, pl->items[index].url);
}

static void to_base36(unsigned v, char *out) {
  char tmp[20];
  int n = 0;
  if (!v) {
    strcpy(out, "0");
    return;
  }
  while (v) {
    unsigned d = v % 36;
    tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    v /= 36;
  }
  for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
  out[n] = 0;
}

static void make_channel_id(int index1, const char *url, const char *name, char *out, int max) {
  char src[MAX_URL + MAX_NAME];
  snprintf(src, sizeof src, "%s%s", url ? url : "", name ? name : "");
  wchar_t w[4096];
  utf8_to_wide(src, w, 4096);
  int32_t h = 0;
  for (int i = 0; w[i]; i++) h = (h << 5) - h + (uint16_t)w[i];
  unsigned v = (h < 0) ? (h == (int32_t)0x80000000 ? 0x80000000u : (unsigned)(-h)) : (unsigned)h;
  char b36[24];
  to_base36(v, b36);
  snprintf(out, max, "%d-%s", index1, b36);
}

static void extract_attr(const char *s, const char *key, char *out, int max) {
  if (out && max) out[0] = 0;
  if (!s || !key || !out) return;
  const char *hit = strstr(s, key);
  if (!hit) return;
  hit += strlen(key);
  if (*hit == '"') {
    hit++;
    const char *end = strchr(hit, '"');
    if (!end) return;
    int n = (int)(end - hit);
    if (n > max - 1) n = max - 1;
    memcpy(out, hit, (size_t)n);
    out[n] = 0;
    return;
  }
  int n = 0;
  while (hit[n] && hit[n] != ' ' && hit[n] != '\t' && hit[n] != ',') n++;
  if (n > max - 1) n = max - 1;
  if (n > 0) {
    memcpy(out, hit, (size_t)n);
    out[n] = 0;
  }
}

static int add_channel(Playlist *pl, Channel *ch) {
  if (pl->count >= pl->cap) {
    int cap = pl->cap ? pl->cap * 2 : 256;
    Channel *next = (Channel *)realloc(pl->items, (size_t)cap * sizeof(Channel));
    if (!next) return 0;
    pl->items = next;
    pl->cap = cap;
  }
  fold_family(ch->name, ch->family, MAX_FAMILY);
  ch->quality = quality_of(ch->name, ch->url);
  pl->items[pl->count++] = *ch;
  return 1;
}

int playlist_parse(Playlist *pl, const char *text) {
  if (!pl) return 0;
  pl->items = NULL;
  pl->count = 0;
  pl->cap = 0;
  if (!text || !text[0]) return 0;

  Channel pending;
  memset(&pending, 0, sizeof pending);
  int have = 0;
  char ua[MAX_UA] = {0};
  char ref[MAX_REF] = {0};
  char last_group[MAX_NAME] = {0};
  char list_catchup[MAX_CATCHUP] = {0};
  char list_catchup_src[MAX_CATCHUP_SRC] = {0};
  int list_catchup_days = 0;

  const char *p = text;
  while (*p) {
    const char *nl = p;
    while (*nl && *nl != '\n' && *nl != '\r') nl++;
    char line[MAX_URL + 64];
    int n = (int)(nl - p);
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
    memcpy(line, p, (size_t)n);
    line[n] = 0;
    while (*nl == '\n' || *nl == '\r') nl++;
    p = nl;

    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) continue;

    if (istarts(s, "#EXTM3U")) {
      extract_attr(s, "catchup=", list_catchup, MAX_CATCHUP);
      if (!list_catchup[0]) extract_attr(s, "catchup-type=", list_catchup, MAX_CATCHUP);
      extract_attr(s, "catchup-source=", list_catchup_src, MAX_CATCHUP_SRC);
      char days[16];
      days[0] = 0;
      extract_attr(s, "catchup-days=", days, (int)sizeof days);
      if (!days[0]) extract_attr(s, "tvg-rec=", days, (int)sizeof days);
      if (!days[0]) extract_attr(s, "timeshift=", days, (int)sizeof days);
      int d = atoi(days);
      if (d > 0 && d <= 40) list_catchup_days = d;
      continue;
    }
    if (istarts(s, "#EXTVLCOPT:")) {
      const char *body = s + 11;
      if (istarts(body, "http-user-agent=")) trim_copy(ua, MAX_UA, body + 16);
      else if (istarts(body, "http-referrer=")) trim_copy(ref, MAX_REF, body + 14);
      else if (istarts(body, "http-referer=")) trim_copy(ref, MAX_REF, body + 13);
      continue;
    }
    if (istarts(s, "#EXTGRP:")) {
      trim_copy(last_group, MAX_NAME, s + 8);
      if (have) copy_trunc(pending.group, MAX_NAME, last_group);
      continue;
    }
    if (istarts(s, "#EXTINF:")) {
      memset(&pending, 0, sizeof pending);
      const char *comma = strrchr(s, ',');
      trim_copy(pending.name, MAX_NAME, comma ? comma + 1 : "Канал");
      if (!pending.name[0]) copy_trunc(pending.name, MAX_NAME, "Канал");
      copy_trunc(pending.ua, MAX_UA, ua);
      copy_trunc(pending.referer, MAX_REF, ref);
      const char *gt = strstr(s, "group-title=\"");
      if (gt) {
        gt += 13;
        const char *end = strchr(gt, '"');
        if (end) {
          int n = (int)(end - gt);
          if (n > MAX_NAME - 1) n = MAX_NAME - 1;
          memcpy(pending.group, gt, (size_t)n);
          pending.group[n] = 0;
        }
      }
      if (!pending.group[0] && last_group[0]) copy_trunc(pending.group, MAX_NAME, last_group);
      if (!pending.group[0]) copy_trunc(pending.group, MAX_NAME, "Другие");
      const char *chno = strstr(s, "tvg-chno=\"");
      if (chno) pending.number = atoi(chno + 10);
      extract_attr(s, "tvg-id=", pending.tvg_id, MAX_TVG);
      extract_attr(s, "tvg-name=", pending.tvg_name, MAX_NAME);
      extract_attr(s, "tvg-logo=", pending.logo, MAX_REF);
      extract_attr(s, "catchup=", pending.catchup, MAX_CATCHUP);
      if (!pending.catchup[0]) extract_attr(s, "catchup-type=", pending.catchup, MAX_CATCHUP);
      extract_attr(s, "catchup-source=", pending.catchup_source, MAX_CATCHUP_SRC);
      char days[16];
      days[0] = 0;
      extract_attr(s, "catchup-days=", days, (int)sizeof days);
      if (!days[0]) extract_attr(s, "tvg-rec=", days, (int)sizeof days);
      if (!days[0]) extract_attr(s, "timeshift=", days, (int)sizeof days);
      int d = atoi(days);
      if (d > 0 && d <= 40) pending.catchup_days = d;
      if (!pending.catchup[0] && list_catchup[0]) copy_trunc(pending.catchup, MAX_CATCHUP, list_catchup);
      if (!pending.catchup_source[0] && list_catchup_src[0]) copy_trunc(pending.catchup_source, MAX_CATCHUP_SRC, list_catchup_src);
      if (!pending.catchup_days && list_catchup_days) pending.catchup_days = list_catchup_days;
      have = 1;
      continue;
    }
    if (s[0] == '#') continue;
    if (!have) continue;
    trim_copy(pending.url, MAX_URL, s);
    if (!pending.url[0]) {
      have = 0;
      continue;
    }
    make_channel_id(pl->count + 1, pending.url, pending.tvg_name[0] ? pending.tvg_name : pending.name, pending.id, MAX_ID);
    add_channel(pl, &pending);
    ua[0] = 0;
    ref[0] = 0;
    have = 0;
    memset(&pending, 0, sizeof pending);
  }
  return pl->count > 0;
}

void playlist_free(Playlist *pl) {
  if (!pl) return;
  free(pl->items);
  pl->items = NULL;
  pl->count = 0;
  pl->cap = 0;
}

int playlist_find_name(const Playlist *pl, const char *name) {
  if (!pl || !name || !name[0]) return -1;
  for (int i = 0; i < pl->count; i++) {
    if (strcmp(pl->items[i].name, name) == 0) return i;
  }
  return -1;
}

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

static int type_off(const char *t) {
  return !t || !t[0] || !_stricmp(t, "0") || !_stricmp(t, "false") || !_stricmp(t, "none") || !_stricmp(t, "off") ||
         !_stricmp(t, "no");
}

int channel_has_catchup(const Channel *ch) {
  if (!ch) return 0;
  if (ch->catchup_days > 0) return 1;
  if (ch->catchup_source[0]) return 1;
  return !type_off(ch->catchup);
}

static int catchup_days(const Channel *ch, const Settings *st) {
  if (!channel_has_catchup(ch)) return 0;
  int own = ch->catchup_days;
  int cap = st && st->archive_days > 0 ? st->archive_days : 0;
  if (own > 0 && cap > 0) return own < cap ? own : cap;
  if (own > 0) return own;
  return cap > 0 ? cap : 7;
}

int program_has_archive(const Channel *ch, const Settings *st, long long start, long long end) {
  (void)start;
  if (!channel_has_catchup(ch) || !end) return 0;
  if (st && !st->archive_enabled) return 0;
  long long now = epg_now_ms();
  if (end > now) return 0;
  int days = catchup_days(ch, st);
  if (!days) return 0;
  return end >= now - (long long)days * 24LL * 60 * 60 * 1000;
}

int can_play_archive(const Channel *ch, const Settings *st, long long start, long long end) {
  if (!channel_has_catchup(ch) || !start) return 0;
  if (st && !st->archive_enabled) return 0;
  long long now = epg_now_ms();
  if (start >= now) return 0;
  int days = catchup_days(ch, st);
  if (!days) return 0;
  long long finish = end > start ? end : start + 60LL * 60 * 1000;
  return finish >= now - (long long)days * 24LL * 60 * 60 * 1000;
}

static void date_parts(long long ms, int utc, int *Y, int *mo, int *d, int *H, int *M, int *S) {
  SYSTEMTIME st;
  memset(&st, 0, sizeof st);
  if (utc) {
    FILETIME ft;
    ULARGE_INTEGER ul;
    ul.QuadPart = (unsigned long long)(ms + 11644473600000ULL) * 10000ULL;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;
    FileTimeToSystemTime(&ft, &st);
  } else {
    epg_local_fields(ms, &st);
  }
  if (Y) *Y = st.wYear;
  if (mo) *mo = st.wMonth;
  if (d) *d = st.wDay;
  if (H) *H = st.wHour;
  if (M) *M = st.wMinute;
  if (S) *S = st.wSecond;
}

static void xtream_stamp(long long ms, int utc, int with_sec, char *out, int max) {
  int Y, mo, d, H, M, S;
  date_parts(ms, utc, &Y, &mo, &d, &H, &M, &S);
  if (with_sec)
    snprintf(out, max, "%04d-%02d-%02d:%02d-%02d-%02d", Y, mo, d, H, M, S);
  else
    snprintf(out, max, "%04d-%02d-%02d:%02d-%02d", Y, mo, d, H, M);
}

static int parse_xtream(const char *url, char *host, int hmax, char *user, int umax, char *pass, int pmax, char *id,
                        int imax) {
  if (!url || !host || !user || !pass || !id) return 0;
  host[0] = user[0] = pass[0] = id[0] = 0;
  const char *scheme = NULL;
  const char *p = url;
  if (istarts(p, "https://")) {
    scheme = "https://";
    p += 8;
  } else if (istarts(p, "http://")) {
    scheme = "http://";
    p += 7;
  } else {
    return 0;
  }
  const char *slash = strchr(p, '/');
  if (!slash) return 0;
  snprintf(host, hmax, "%s%.*s", scheme, (int)(slash - p), p);
  char rest[MAX_URL];
  copy_trunc(rest, MAX_URL, slash + 1);
  char *q = strchr(rest, '?');
  if (q) *q = 0;
  char *parts[8];
  int n = 0;
  char *cur = rest;
  while (cur && *cur && n < 6) {
    parts[n++] = cur;
    char *s = strchr(cur, '/');
    if (!s) break;
    *s = 0;
    cur = s + 1;
  }
  if (n < 3) return 0;
  int i0 = 0;
  if (!_stricmp(parts[0], "live") || !_stricmp(parts[0], "play")) i0 = 1;
  if (n - i0 < 3) return 0;
  if (i0 == 0 && (!_stricmp(parts[0], "timeshift") || !_stricmp(parts[0], "hls") || !_stricmp(parts[0], "streaming") ||
                  !_stricmp(parts[0], "xmltv") || !_stricmp(parts[0], "player_api") || !_stricmp(parts[0], "panel_api")))
    return 0;
  copy_trunc(user, umax, parts[i0]);
  copy_trunc(pass, pmax, parts[i0 + 1]);
  char idbuf[64];
  copy_trunc(idbuf, (int)sizeof idbuf, parts[i0 + 2]);
  char *dot = strrchr(idbuf, '.');
  if (dot) *dot = 0;
  copy_trunc(id, imax, idbuf);
  return user[0] && pass[0] && id[0];
}

static void replace_all(char *text, int max, const char *token, const char *value) {
  if (!text || !token || !token[0] || !value) return;
  char out[MAX_URL];
  int o = 0;
  int tlen = (int)strlen(token);
  int vlen = (int)strlen(value);
  for (int i = 0; text[i] && o < max - 1;) {
    if (strncmp(text + i, token, (size_t)tlen) == 0) {
      if (o + vlen >= max - 1) break;
      memcpy(out + o, value, (size_t)vlen);
      o += vlen;
      i += tlen;
    } else {
      out[o++] = text[i++];
    }
  }
  out[o] = 0;
  copy_trunc(text, max, out);
}

static void fill_template(const char *tmpl, long long start, long long end, char *out, int max) {
  copy_trunc(out, max, tmpl);
  long long utc = start / 1000;
  long long lutc = end / 1000;
  int duration_sec = (int)(lutc - utc);
  if (duration_sec < 1) duration_sec = 1;
  int duration_min = (int)((end - start + 30000) / 60000);
  if (duration_min < 1) duration_min = 1;
  int offset = (int)((epg_now_ms() - start) / 1000);
  if (offset < 0) offset = 0;
  int xtream = strstr(tmpl, "timeshift") != NULL;
  int Y, mo, d, H, M, S, uy, umo, ud, uH, uM, uS;
  date_parts(start, 0, &Y, &mo, &d, &H, &M, &S);
  date_parts(start, 1, &uy, &umo, &ud, &uH, &uM, &uS);
  char buf[48], stamp[40];
  xtream_stamp(start, 0, 0, stamp, (int)sizeof stamp);
  snprintf(buf, sizeof buf, "%lld", utc);
  replace_all(out, max, "{utc}", buf);
  replace_all(out, max, "${utc}", buf);
  replace_all(out, max, "${timestamp}", buf);
  replace_all(out, max, "{start}", xtream ? stamp : buf);
  snprintf(buf, sizeof buf, "%lld", lutc);
  replace_all(out, max, "{lutc}", buf);
  replace_all(out, max, "{utcend}", buf);
  replace_all(out, max, "{end}", buf);
  snprintf(buf, sizeof buf, "%d", xtream ? duration_min : duration_sec);
  replace_all(out, max, "{duration}", buf);
  replace_all(out, max, "${duration}", buf);
  snprintf(buf, sizeof buf, "%d", duration_min);
  replace_all(out, max, "{minutes}", buf);
  snprintf(buf, sizeof buf, "%d", offset);
  replace_all(out, max, "{offset}", buf);
  replace_all(out, max, "${start}", stamp);
  snprintf(buf, sizeof buf, "%04d", Y);
  replace_all(out, max, "{Y}", buf);
  snprintf(buf, sizeof buf, "%02d", mo);
  replace_all(out, max, "{m}", buf);
  snprintf(buf, sizeof buf, "%02d", d);
  replace_all(out, max, "{d}", buf);
  snprintf(buf, sizeof buf, "%02d", H);
  replace_all(out, max, "{H}", buf);
  snprintf(buf, sizeof buf, "%02d", M);
  replace_all(out, max, "{M}", buf);
  snprintf(buf, sizeof buf, "%02d", S);
  replace_all(out, max, "{S}", buf);
  snprintf(buf, sizeof buf, "%04d", uy);
  replace_all(out, max, "{utcY}", buf);
  snprintf(buf, sizeof buf, "%02d", umo);
  replace_all(out, max, "{utcm}", buf);
  snprintf(buf, sizeof buf, "%02d", ud);
  replace_all(out, max, "{utcd}", buf);
  snprintf(buf, sizeof buf, "%02d", uH);
  replace_all(out, max, "{utcH}", buf);
  snprintf(buf, sizeof buf, "%02d", uM);
  replace_all(out, max, "{utcM}", buf);
}

static void catchup_span(long long start, long long end, long long *out_s, long long *out_e) {
  long long now = epg_now_ms();
  long long finish = (end && end > start) ? end : start + 60LL * 60 * 1000;
  if (finish > now - 1500) finish = now - 1500;
  *out_s = start;
  *out_e = finish;
}

static void add_url(char urls[][MAX_URL], int *n, int maxn, const char *url, const char *live) {
  if (!url || !url[0] || *n >= maxn) return;
  if (live && strcmp(url, live) == 0) return;
  for (int i = 0; i < *n; i++) {
    if (strcmp(urls[i], url) == 0) return;
  }
  copy_trunc(urls[(*n)++], MAX_URL, url);
}

static int has_ci(const char *s, const char *p) {
  if (!s || !p) return 0;
  size_t n = strlen(p);
  for (; *s; s++) {
    if (_strnicmp(s, p, n) == 0) return 1;
  }
  return 0;
}

static void append_utc(const char *url, long long start, long long end, char *out, int max) {
  copy_trunc(out, max, url);
  char *q = strchr(out, '?');
  if (q) {
    char *w = q + 1;
    char kept[MAX_URL];
    kept[0] = 0;
    while (*w) {
      char *amp = strchr(w, '&');
      int n = amp ? (int)(amp - w) : (int)strlen(w);
      if (!(_strnicmp(w, "utc=", 4) == 0 || _strnicmp(w, "lutc=", 5) == 0 || _strnicmp(w, "duration=", 9) == 0)) {
        if (kept[0]) strncat(kept, "&", sizeof kept - strlen(kept) - 1);
        strncat(kept, w, n < (int)(sizeof kept - strlen(kept) - 1) ? (size_t)n : sizeof kept - strlen(kept) - 1);
      }
      if (!amp) break;
      w = amp + 1;
    }
    if (kept[0]) {
      q[1] = 0;
      strncat(out, kept, (size_t)max - strlen(out) - 1);
    } else {
      *q = 0;
    }
  }
  char extra[80];
  snprintf(extra, sizeof extra, "%cutc=%lld&lutc=%lld", strchr(out, '?') ? '&' : '?', start / 1000, end / 1000);
  strncat(out, extra, (size_t)max - strlen(out) - 1);
}

int catchup_url_candidates(const Channel *ch, long long start_ms, long long end_ms, char urls[][MAX_URL], int maxn) {
  int n = 0;
  if (!ch || !channel_has_catchup(ch) || !ch->url[0] || !start_ms || maxn < 1) return 0;
  long long start = 0, finish = 0;
  catchup_span(start_ms, end_ms, &start, &finish);
  if (!start || finish <= start) return 0;
  char type[MAX_CATCHUP];
  copy_trunc(type, MAX_CATCHUP, ch->catchup);
  for (char *p = type; *p; p++) *p = (char)tolower((unsigned char)*p);
  int index_pl = has_ci(ch->url, "/index.m3u8");
  int xtream = strstr(type, "xc") || strstr(type, "default") || strstr(type, "shift") || !type[0] || strstr(type, "timeshift");
  int flussonic = strstr(type, "flussonic") || strstr(type, "fs") || index_pl || has_ci(ch->url, "video.m3u8");
  int append = strstr(type, "append") || strstr(type, "shift") || strstr(type, "default") || flussonic || !type[0];
  char built[MAX_URL];
  if (index_pl) {
    append_utc(ch->url, start, epg_now_ms() - 1500, built, MAX_URL);
    add_url(urls, &n, maxn, built, ch->url);
  }
  if (ch->catchup_source[0]) {
    fill_template(ch->catchup_source, start, finish, built, MAX_URL);
    add_url(urls, &n, maxn, built, ch->url);
  }
  if (!index_pl && (flussonic || append || ch->catchup_source[0])) {
    append_utc(ch->url, start, finish, built, MAX_URL);
    add_url(urls, &n, maxn, built, ch->url);
  }
  char host[256], user[128], pass[128], id[64];
  host[0] = user[0] = pass[0] = id[0] = 0;
  int parsed = parse_xtream(ch->url, host, (int)sizeof host, user, (int)sizeof user, pass, (int)sizeof pass, id, (int)sizeof id);
  if (!index_pl && (xtream || !ch->catchup_source[0]) && parsed) {
    int duration = (int)((finish - start + 30000) / 60000);
    if (duration < 1) duration = 1;
    char stamp[40];
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%lld/%s.m3u8", host, user, pass, duration, start / 1000, id);
    add_url(urls, &n, maxn, built, ch->url);
    xtream_stamp(start, 0, 1, stamp, (int)sizeof stamp);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%s/%s.m3u8", host, user, pass, duration, stamp, id);
    add_url(urls, &n, maxn, built, ch->url);
    xtream_stamp(start, 1, 1, stamp, (int)sizeof stamp);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%s/%s.m3u8", host, user, pass, duration, stamp, id);
    add_url(urls, &n, maxn, built, ch->url);
    xtream_stamp(start, 0, 0, stamp, (int)sizeof stamp);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%s/%s.m3u8", host, user, pass, duration, stamp, id);
    add_url(urls, &n, maxn, built, ch->url);
    xtream_stamp(start, 1, 0, stamp, (int)sizeof stamp);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%s/%s.m3u8", host, user, pass, duration, stamp, id);
    add_url(urls, &n, maxn, built, ch->url);
    snprintf(built, MAX_URL, "%s/streaming/timeshift.php?username=%s&password=%s&stream=%s&start=%s&duration=%d", host,
             user, pass, id, stamp, duration);
    add_url(urls, &n, maxn, built, ch->url);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%s/%s.ts", host, user, pass, duration, stamp, id);
    add_url(urls, &n, maxn, built, ch->url);
    snprintf(built, MAX_URL, "%s/timeshift/%s/%s/%d/%lld/%s.ts", host, user, pass, duration, start / 1000, id);
    add_url(urls, &n, maxn, built, ch->url);
  }
  return n;
}

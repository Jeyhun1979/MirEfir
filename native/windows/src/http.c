#include "app.h"
#include <winhttp.h>

#ifndef WINHTTP_DECOMPRESSION_FLAG_GZIP
#define WINHTTP_DECOMPRESSION_FLAG_GZIP 1
#define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 2
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_NEVER
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0
#endif

static void apply_location(wchar_t *cur, int max, const wchar_t *loc) {
  if (!cur || !loc || !loc[0] || max < 8) return;
  if (wcsstr(loc, L"://")) {
    _snwprintf(cur, max, L"%s", loc);
    cur[max - 1] = 0;
    return;
  }
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof uc);
  wchar_t host[256], scheme[32];
  uc.dwStructSize = sizeof uc;
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszScheme = scheme;
  uc.dwSchemeLength = 32;
  if (!WinHttpCrackUrl(cur, 0, 0, &uc) || !host[0] || !scheme[0]) return;
  if (loc[0] == L'/') _snwprintf(cur, max, L"%s://%s%s", scheme, host, loc);
  else _snwprintf(cur, max, L"%s://%s/%s", scheme, host, loc);
  cur[max - 1] = 0;
}

static int http_fetch_once(const wchar_t *wurl, const wchar_t *agent, int max_bytes, int timeout_ms, char **out, int *len,
                           wchar_t *redir, int redir_max) {
  if (out) *out = NULL;
  if (len) *len = 0;
  if (redir && redir_max > 0) redir[0] = 0;
  if (!wurl || !wurl[0]) return -1;
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
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return -1;

  HINTERNET ses = WinHttpOpen(agent && agent[0] ? agent : L"MirEfir/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return -1;
  WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return -1;
  }
  wchar_t full[4096];
  if (extra[0]) _snwprintf(full, 4096, L"%s%s", path[0] ? path : L"/", extra);
  else _snwprintf(full, 4096, L"%s", path[0] ? path : L"/");
  full[4095] = 0;
  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, L"GET", full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return -1;
  }
  DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
  WinHttpSetTimeouts(req, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
  WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof decomp);

  int rc = -1;
  char *buf = NULL;
  int cap = 0;
  int used = 0;
  if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, NULL)) {
    DWORD status = 0, slen = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen,
                        WINHTTP_NO_HEADER_INDEX);
    if (status >= 300 && status < 400 && redir && redir_max > 8) {
      DWORD llen = (DWORD)redir_max * sizeof(wchar_t);
      if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, redir, &llen, WINHTTP_NO_HEADER_INDEX) &&
          redir[0])
        rc = 2;
    } else if (status == 200) {
      int ok = 0;
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (!avail) {
          ok = 1;
          break;
        }
        if (used + (int)avail + 1 > cap) {
          int next = cap ? cap * 2 : 256 * 1024;
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
      if (ok && buf && used > 0) {
        buf[used] = 0;
        if (out) *out = buf;
        if (len) *len = used;
        buf = NULL;
        rc = 0;
      }
    }
  }
  free(buf);
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return rc;
}

int http_get_lim_ua(const char *url, char **out, int *len, int max_bytes, int timeout_ms, const wchar_t *agent) {
  if (out) *out = NULL;
  if (len) *len = 0;
  if (!url || !out) return -1;
  if (!(istarts(url, "http://") || istarts(url, "https://"))) return -2;
  if (max_bytes < 64 * 1024) max_bytes = 32 * 1024 * 1024;
  if (timeout_ms < 3000) timeout_ms = 15000;
  wchar_t cur[4096];
  if (!utf8_to_wide(url, cur, 4096)) return -1;
  for (int hop = 0; hop < 4; hop++) {
    char *body = NULL;
    int n = 0;
    wchar_t loc[4096];
    loc[0] = 0;
    int rc = http_fetch_once(cur, agent, max_bytes, timeout_ms, &body, &n, loc, 4096);
    if (rc == 0) {
      *out = body;
      if (len) *len = n;
      return 0;
    }
    free(body);
    if (rc != 2 || !loc[0]) return -1;
    apply_location(cur, 4096, loc);
  }
  return -1;
}

int http_get_lim(const char *url, char **out, int *len, int max_bytes, int timeout_ms) {
  return http_get_lim_ua(url, out, len, max_bytes, timeout_ms, L"MirEfir/1.0");
}

int http_get(const char *url, char **out, int *len) {
  return http_get_lim(url, out, len, 32 * 1024 * 1024, 15000);
}

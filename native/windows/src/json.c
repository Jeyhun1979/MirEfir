#include "app.h"
#include <ctype.h>

int utf8_to_wide(const char *s, wchar_t *w, int max) {
  if (!s || !w || max < 2) return 0;
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, max);
  if (n <= 0) {
    w[0] = 0;
    return 0;
  }
  return n;
}

int wide_to_utf8(const wchar_t *w, char *s, int max) {
  if (!w || !s || max < 2) return 0;
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, s, max, NULL, NULL);
  if (n <= 0) {
    s[0] = 0;
    return 0;
  }
  return n;
}

int istarts(const char *s, const char *pfx) {
  if (!s || !pfx) return 0;
  while (*pfx) {
    unsigned char a = (unsigned char)*s++;
    unsigned char b = (unsigned char)*pfx++;
    if (tolower(a) != tolower(b)) return 0;
  }
  return 1;
}

char *read_file_utf8(const wchar_t *path, int *len) {
  if (len) *len = 0;
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 48LL * 1024 * 1024) {
    CloseHandle(h);
    return NULL;
  }
  int n = (int)sz.QuadPart;
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    CloseHandle(h);
    return NULL;
  }
  DWORD got = 0;
  if (!ReadFile(h, buf, (DWORD)n, &got, NULL)) {
    free(buf);
    CloseHandle(h);
    return NULL;
  }
  CloseHandle(h);
  buf[got] = 0;
  char *text = buf;
  if ((unsigned char)buf[0] == 0xEF && got >= 3 && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
    text = buf + 3;
    got -= 3;
    memmove(buf, text, (size_t)got + 1);
  }
  if (len) *len = got;
  return buf;
}

int write_file_utf8(const wchar_t *path, const char *text) {
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  DWORD n = (DWORD)strlen(text ? text : "");
  DWORD w = 0;
  BOOL ok = WriteFile(h, text ? text : "", n, &w, NULL);
  CloseHandle(h);
  return ok && w == n;
}

static const char *skip_ws(const char *p) {
  while (p && *p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

static const char *skip_string(const char *p) {
  if (!p || *p != '"') return p;
  p++;
  while (*p) {
    if (*p == '\\' && p[1]) {
      p += 2;
      continue;
    }
    if (*p == '"') return p + 1;
    p++;
  }
  return p;
}

static const char *skip_value(const char *p) {
  p = skip_ws(p);
  if (!p || !*p) return p;
  if (*p == '"') return skip_string(p);
  if (*p == '{' || *p == '[') {
    char open = *p;
    char close = (char)(open == '{' ? '}' : ']');
    int depth = 1;
    p++;
    while (*p && depth) {
      if (*p == '"') {
        p = skip_string(p);
        continue;
      }
      if (*p == open) depth++;
      else if (*p == close) depth--;
      p++;
    }
    return p;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
  return p;
}

static int unescape_to(const char *src, char *dst, int max) {
  int n = 0;
  if (*src != '"') return 0;
  src++;
  while (*src && *src != '"' && n < max - 1) {
    if (*src == '\\' && src[1]) {
      src++;
      char c = *src++;
      if (c == 'n') dst[n++] = '\n';
      else if (c == 'r') dst[n++] = '\r';
      else if (c == 't') dst[n++] = '\t';
      else if (c == 'u') {
        src += 4;
        dst[n++] = '?';
      } else {
        dst[n++] = c;
      }
    } else {
      dst[n++] = *src++;
    }
  }
  dst[n] = 0;
  return n;
}

const char *json_value_of(const char *obj, const char *key) {
  if (!obj || !key) return NULL;
  const char *p = skip_ws(obj);
  if (*p != '{') {
    p = strchr(p, '{');
    if (!p) return NULL;
  }
  p++;
  int depth = 1;
  while (*p && depth > 0) {
    p = skip_ws(p);
    if (*p == '}') {
      depth--;
      p++;
      continue;
    }
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p != '"') {
      p = skip_value(p);
      continue;
    }
    const char *ks = p + 1;
    const char *ke = skip_string(p) - 1;
    int klen = (int)(ke - ks);
    p = skip_ws(ke + 1);
    if (*p != ':') continue;
    p = skip_ws(p + 1);
    int match = (int)strlen(key) == klen && strncmp(ks, key, (size_t)klen) == 0 && depth == 1;
    if (match) return p;
    if (*p == '{') {
      depth++;
      p++;
      continue;
    }
    p = skip_value(p);
  }
  return NULL;
}

int json_parse_string(const char *quoted, char *out, int max) {
  if (out && max > 0) out[0] = 0;
  if (!quoted || *quoted != '"' || !out || max < 2) return 0;
  unescape_to(quoted, out, max);
  return out[0] != 0;
}

int json_extract_string(const char *obj, const char *key, char *out, int max) {
  if (out && max > 0) out[0] = 0;
  const char *v = json_value_of(obj, key);
  if (!v || *v != '"' || !out || max < 2) return 0;
  unescape_to(v, out, max);
  return out[0] != 0;
}

char *json_dup_string(const char *obj, const char *key) {
  const char *v = json_value_of(obj, key);
  if (!v || *v != '"') return NULL;
  size_t cap = strlen(v) + 4;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  unescape_to(v, out, (int)cap);
  return out;
}

int json_extract_int(const char *obj, const char *key, int *out) {
  const char *v = json_value_of(obj, key);
  if (!v || !out) return 0;
  if (*v == '"') {
    char buf[64];
    unescape_to(v, buf, sizeof buf);
    *out = atoi(buf);
    return 1;
  }
  *out = atoi(v);
  return 1;
}

int json_extract_double(const char *obj, const char *key, double *out) {
  const char *v = json_value_of(obj, key);
  if (!v || !out) return 0;
  if (*v == '"') {
    char buf[64];
    unescape_to(v, buf, sizeof buf);
    *out = atof(buf);
    return 1;
  }
  *out = atof(v);
  return 1;
}

int json_extract_bool(const char *obj, const char *key, int *out) {
  const char *v = json_value_of(obj, key);
  if (!v || !out) return 0;
  if (*v == 't') {
    *out = 1;
    return 1;
  }
  if (*v == 'f') {
    *out = 0;
    return 1;
  }
  if (*v == '"') {
    char buf[16];
    unescape_to(v, buf, sizeof buf);
    *out = istarts(buf, "true") || strcmp(buf, "1") == 0;
    return 1;
  }
  *out = atoi(v) != 0;
  return 1;
}

int json_array_strings(const char *v, void (*cb)(const char *s, void *u), void *u) {
  v = skip_ws(v);
  if (!v || *v != '[' || !cb) return 0;
  v++;
  int n = 0;
  while (*v) {
    v = skip_ws(v);
    if (*v == ']') break;
    if (*v == ',') {
      v++;
      continue;
    }
    if (*v == '"') {
      char buf[256];
      unescape_to(v, buf, (int)sizeof buf);
      cb(buf, u);
      n++;
      v = skip_string(v);
    } else {
      v = skip_value(v);
    }
  }
  return n;
}

int json_array_each(const char *v, void (*cb)(const char *item, void *u), void *u) {
  v = skip_ws(v);
  if (!v || *v != '[' || !cb) return 0;
  v++;
  int n = 0;
  while (*v) {
    v = skip_ws(v);
    if (*v == ']') break;
    if (*v == ',') {
      v++;
      continue;
    }
    cb(v, u);
    n++;
    v = skip_value(v);
  }
  return n;
}

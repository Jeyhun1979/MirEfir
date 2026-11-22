#include "app.h"
#include "tinf.h"
#include <stdlib.h>
#include <string.h>

/* gzip (RFC 1952) + deflate stored/fixed. If LZ77 round-trip fails, store uncompressed. */

typedef struct {
  unsigned char *p;
  int n, cap;
  unsigned val;
  int bits;
} BitW;

static int bw_need(BitW *b, int extra) {
  if (b->n + extra + 16 <= b->cap) return 1;
  int cap = b->cap * 2 + extra + 256;
  unsigned char *p = (unsigned char *)realloc(b->p, (size_t)cap);
  if (!p) return 0;
  b->p = p;
  b->cap = cap;
  return 1;
}

static void put_bits(BitW *b, unsigned v, int n) {
  b->val |= v << b->bits;
  b->bits += n;
  while (b->bits >= 8) {
    if (b->n < b->cap) b->p[b->n++] = (unsigned char)b->val;
    b->val >>= 8;
    b->bits -= 8;
  }
}

static unsigned reverse_bits(unsigned v, int n) {
  unsigned r = 0;
  for (int i = 0; i < n; i++) r = (r << 1) | ((v >> i) & 1);
  return r;
}

static void put_fixed_lit(BitW *b, int sym) {
  /* RFC 1951 §3.2.6, codes packed MSB-first into LSB-first stream. */
  if (sym <= 143) put_bits(b, reverse_bits((unsigned)(sym + 48), 8), 8);
  else if (sym <= 255) put_bits(b, reverse_bits((unsigned)(sym - 144 + 400), 9), 9);
  else if (sym <= 279) put_bits(b, reverse_bits((unsigned)(sym - 256), 7), 7);
  else put_bits(b, reverse_bits((unsigned)(sym - 280 + 192), 8), 8);
}

static void put_len_dist(BitW *b, int len, int dist) {
  static const unsigned char lb[29] = {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const unsigned short lo[29] = {
      3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const unsigned char db[30] = {
      0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  static const unsigned short ds[30] = {
      1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  int li = 0;
  while (li < 28 && lo[li + 1] <= len) li++;
  put_fixed_lit(b, 257 + li);
  if (lb[li]) put_bits(b, (unsigned)(len - lo[li]), lb[li]);
  int di = 0;
  while (di < 29 && ds[di + 1] <= dist) di++;
  put_bits(b, reverse_bits((unsigned)di, 5), 5);
  if (db[di]) put_bits(b, (unsigned)(dist - ds[di]), db[di]);
}

static void gzip_header(unsigned char *p) {
  p[0] = 0x1f;
  p[1] = 0x8b;
  p[2] = 8;
  p[3] = 0;
  p[4] = p[5] = p[6] = p[7] = 0;
  p[8] = 0;
  p[9] = 0xff;
}

static void gzip_footer(unsigned char *p, unsigned crc, unsigned n) {
  p[0] = (unsigned char)crc;
  p[1] = (unsigned char)(crc >> 8);
  p[2] = (unsigned char)(crc >> 16);
  p[3] = (unsigned char)(crc >> 24);
  p[4] = (unsigned char)n;
  p[5] = (unsigned char)(n >> 8);
  p[6] = (unsigned char)(n >> 16);
  p[7] = (unsigned char)(n >> 24);
}

static int gzip_stored(const unsigned char *in, int n, unsigned char **out, int *outn) {
  int blocks = n <= 0 ? 1 : (n + 65534) / 65535;
  int cap = 10 + blocks * (5 + 65535) + 8;
  unsigned char *buf = (unsigned char *)malloc((size_t)cap);
  if (!buf) return 0;
  gzip_header(buf);
  int o = 10, left = n, i = 0;
  if (n <= 0) {
    buf[o++] = 1;
    buf[o++] = 0;
    buf[o++] = 0;
    buf[o++] = 0xff;
    buf[o++] = 0xff;
  } else {
    while (left > 0) {
      int chunk = left > 65535 ? 65535 : left;
      int last = chunk == left;
      buf[o++] = (unsigned char)last;
      buf[o++] = (unsigned char)chunk;
      buf[o++] = (unsigned char)(chunk >> 8);
      buf[o++] = (unsigned char)~chunk;
      buf[o++] = (unsigned char)(~chunk >> 8);
      memcpy(buf + o, in + i, (size_t)chunk);
      o += chunk;
      i += chunk;
      left -= chunk;
    }
  }
  gzip_footer(buf + o, tinf_crc32(in, (unsigned)n), (unsigned)n);
  o += 8;
  *out = buf;
  *outn = o;
  return 1;
}

static int gzip_fixed(const unsigned char *in, int n, unsigned char **out, int *outn) {
  BitW b;
  memset(&b, 0, sizeof b);
  b.cap = n + n / 2 + 256;
  b.p = (unsigned char *)malloc((size_t)b.cap);
  if (!b.p) return 0;
  gzip_header(b.p);
  b.n = 10;
  if (!bw_need(&b, n + 32)) {
    free(b.p);
    return 0;
  }
  put_bits(&b, 1, 1); /* BFINAL */
  put_bits(&b, 1, 2); /* BTYPE = fixed */
  int i = 0;
  while (i < n) {
    int best = 0, dist = 0;
    int from = i - 32768;
    if (from < 0) from = 0;
    if (i + 2 < n) {
      for (int j = i - 1; j >= from; j--) {
        if (in[j] != in[i]) continue;
        int k = 1;
        while (i + k < n && j + k < i && in[j + k] == in[i + k] && k < 258) k++;
        if (k >= 3 && k > best) {
          best = k;
          dist = i - j;
          if (best >= 64) break;
        }
      }
    }
    if (!bw_need(&b, 16)) {
      free(b.p);
      return 0;
    }
    if (best >= 3) {
      put_len_dist(&b, best, dist);
      i += best;
    } else {
      put_fixed_lit(&b, in[i]);
      i++;
    }
  }
  put_fixed_lit(&b, 256);
  if (b.bits) put_bits(&b, 0, 8 - b.bits);
  if (!bw_need(&b, 8)) {
    free(b.p);
    return 0;
  }
  gzip_footer(b.p + b.n, tinf_crc32(in, (unsigned)n), (unsigned)n);
  b.n += 8;
  *out = b.p;
  *outn = b.n;
  return 1;
}

static int gzip_ok(const unsigned char *in, int n, const unsigned char *gz, int gn) {
  if (!gz || gn < 18) return 0;
  unsigned dlen = (unsigned)n + 8;
  unsigned char *plain = (unsigned char *)malloc(dlen);
  if (!plain) return 0;
  unsigned got = dlen;
  int ok = tinf_gzip_uncompress(plain, &got, gz, (unsigned)gn) == TINF_OK && (int)got == n &&
           memcmp(plain, in, (size_t)n) == 0;
  free(plain);
  return ok;
}

int gzip_compress(const void *in, int inlen, unsigned char **out, int *outlen) {
  if (!out || !outlen) return 0;
  *out = NULL;
  *outlen = 0;
  const unsigned char *src = (const unsigned char *)(in ? in : "");
  int n = inlen < 0 ? 0 : inlen;
  unsigned char *gz = NULL;
  int gn = 0;
  if (n > 16 && gzip_fixed(src, n, &gz, &gn) && gzip_ok(src, n, gz, gn)) {
    *out = gz;
    *outlen = gn;
    return 1;
  }
  free(gz);
  if (!gzip_stored(src, n, &gz, &gn) || !gzip_ok(src, n, gz, gn)) {
    free(gz);
    return 0;
  }
  *out = gz;
  *outlen = gn;
  return 1;
}

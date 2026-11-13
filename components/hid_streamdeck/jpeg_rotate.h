#pragma once
// Lossless 180 degree rotation of a baseline (sequential Huffman) JPEG, done on the DCT coefficients:
//   - the blocks of every component are visited in reverse order (last block first),
//   - inside a block a coefficient at (u,v) changes sign when u+v is odd (a flip in x and one in y),
//   - the stream is Huffman-coded again with the tables that are already in the file.
// Nothing is decoded to pixels and nothing is quantised again, so there is no quality loss and no extra file size to speak of.
//
// Only what a deck sends is handled: SOF0, one interleaved scan with all components, 8 bit, no restart markers, and an
// image size that is a multiple of the MCU (96x96 is a multiple of 16, so 4:4:4 and 4:2:0 both qualify). Anything else, or a
// Huffman table that lacks a symbol the rotated image needs, makes rotate180() return false and the caller sends the original.
//
// No ESP-IDF or ESPHome dependency, so it can be tested on a PC (tools/test_jpeg_rotate.cpp).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace esphome {
namespace hid_streamdeck {
namespace jpegrot {

static const uint8_t ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                   41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                   30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const int MAX_BLOCKS = 1024;  // 128 KB of coefficients; a 96x96 4:4:4 key is 432 blocks

struct Huff {
  bool present = false;
  uint8_t vals[256];
  int32_t mincode[17], maxcode[17];
  int16_t valptr[17];
  uint16_t ecode[256];
  uint8_t esize[256];  // 0 = symbol not in the table
};

struct Comp {
  uint8_t id = 0, h = 1, v = 1, td = 0, ta = 0;
  int gw = 0, gh = 0;
  int16_t *blocks = nullptr;  // gw*gh blocks of 64 coefficients, in zigzag order
};

struct Reader {
  const uint8_t *p, *end;
  uint32_t acc = 0;
  int nbits = 0;
  bool ok = true;
  int bit() {
    if (nbits == 0) {
      if (p >= end) { ok = false; return 0; }
      uint8_t b = *p++;
      if (b == 0xFF) {
        if (p >= end) { ok = false; return 0; }
        if (*p == 0x00) p++;
        else { ok = false; return 0; }  // a marker inside the data: truncated or restart markers
      }
      acc = b;
      nbits = 8;
    }
    nbits--;
    return (acc >> nbits) & 1;
  }
  int receive(int s) {
    int v = 0;
    for (int i = 0; i < s; i++) v = (v << 1) | bit();
    return v;
  }
  static int extend(int v, int s) { return s == 0 ? 0 : (v < (1 << (s - 1)) ? v - (1 << s) + 1 : v); }
  int decode(const Huff &t) {
    int32_t code = 0;
    for (int l = 1; l <= 16; l++) {
      code = (code << 1) | bit();
      if (!ok) return -1;
      if (t.maxcode[l] >= 0 && code <= t.maxcode[l] && code >= t.mincode[l]) return t.vals[t.valptr[l] + code - t.mincode[l]];
    }
    ok = false;
    return -1;
  }
};

struct Writer {
  std::vector<uint8_t> &out;
  uint32_t acc = 0;
  int nbits = 0;
  explicit Writer(std::vector<uint8_t> &o) : out(o) {}
  void put(uint32_t code, int size) {
    if (size == 0) return;
    acc = (acc << size) | (code & ((1u << size) - 1));
    nbits += size;
    while (nbits >= 8) {
      uint8_t b = (acc >> (nbits - 8)) & 0xFF;
      out.push_back(b);
      if (b == 0xFF) out.push_back(0x00);
      nbits -= 8;
    }
    acc &= (1u << nbits) - 1;
  }
  void flush() {
    if (nbits > 0) put((1u << (8 - nbits)) - 1, 8 - nbits);  // pad with 1 bits
  }
};

static inline int bitlen(int a) {
  int s = 0;
  while (a) { s++; a >>= 1; }
  return s;
}

static inline bool build(Huff &t, const uint8_t *counts, const uint8_t *vals, int nvals) {
  if (nvals > 256) return false;
  memcpy(t.vals, vals, nvals);
  memset(t.esize, 0, sizeof(t.esize));
  int32_t code = 0;
  int k = 0;
  for (int l = 1; l <= 16; l++) {
    t.valptr[l] = k;
    t.mincode[l] = code;
    for (int i = 0; i < counts[l - 1]; i++, k++, code++) {
      if (code >= (1 << l)) return false;
      t.ecode[vals[k]] = (uint16_t) code;
      t.esize[vals[k]] = (uint8_t) l;
    }
    t.maxcode[l] = counts[l - 1] ? code - 1 : -1;
    code <<= 1;
  }
  t.present = true;
  return true;
}

struct Ctx {
  Huff ht[4];  // index: class*2 + id (0,1 = DC, 2,3 = AC)
  Comp comp[4];
};

static inline void free_ctx(Ctx *c) {
  if (c == nullptr) return;
  for (auto &k : c->comp) free(k.blocks);
  delete c;
}

static inline bool encode_block(Writer &w, const int16_t *src, int &pred, const Huff &dc, const Huff &ac) {
  int v0 = src[0];  // DC does not change sign
  int diff = v0 - pred;
  pred = v0;
  int a = diff < 0 ? -diff : diff;
  int s = bitlen(a);
  if (s > 11 || dc.esize[s] == 0) return false;
  w.put(dc.ecode[s], dc.esize[s]);
  if (s) w.put((uint32_t)(diff < 0 ? diff - 1 : diff), s);
  int run = 0;
  for (int z = 1; z < 64; z++) {
    int v = src[z];
    if (((ZIGZAG[z] >> 3) + (ZIGZAG[z] & 7)) & 1) v = -v;
    if (v == 0) { run++; continue; }
    while (run > 15) {
      if (ac.esize[0xF0] == 0) return false;
      w.put(ac.ecode[0xF0], ac.esize[0xF0]);
      run -= 16;
    }
    int av = v < 0 ? -v : v;
    int sz = bitlen(av);
    int sym = (run << 4) | sz;
    if (sz > 10 || ac.esize[sym] == 0) return false;
    w.put(ac.ecode[sym], ac.esize[sym]);
    w.put((uint32_t)(v < 0 ? v - 1 : v), sz);
    run = 0;
  }
  if (run > 0) {
    if (ac.esize[0x00] == 0) return false;
    w.put(ac.ecode[0x00], ac.esize[0x00]);
  }
  return true;
}

/// Rotate `d[0..n)` by 180 degrees into `out`. Returns false (and leaves `out` unspecified) if the file is not of the kind
/// described at the top.
inline bool rotate180(const uint8_t *d, size_t n, std::vector<uint8_t> &out) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
  Ctx *c = new (std::nothrow) Ctx();
  if (c == nullptr) return false;
  int ncomp = 0, width = 0, height = 0;
  bool have_sof = false;
  size_t i = 2, header_end = 0;
  int scan_n = 0;

  for (;;) {
    if (i + 4 > n || d[i] != 0xFF) { free_ctx(c); return false; }
    uint8_t m = d[i + 1];
    if (m == 0xFF) { i++; continue; }
    size_t len = ((size_t) d[i + 2] << 8) | d[i + 3];
    if (len < 2 || i + 2 + len > n) { free_ctx(c); return false; }
    const uint8_t *s = d + i + 4;
    size_t sl = len - 2;
    if (m == 0xC0) {  // baseline DCT
      if (sl < 6 || s[0] != 8) { free_ctx(c); return false; }
      height = (s[1] << 8) | s[2];
      width = (s[3] << 8) | s[4];
      ncomp = s[5];
      if (ncomp < 1 || ncomp > 4 || sl < (size_t)(6 + 3 * ncomp) || width == 0 || height == 0) { free_ctx(c); return false; }
      for (int k = 0; k < ncomp; k++) {
        c->comp[k].id = s[6 + 3 * k];
        c->comp[k].h = s[7 + 3 * k] >> 4;
        c->comp[k].v = s[7 + 3 * k] & 15;
        if (c->comp[k].h < 1 || c->comp[k].h > 4 || c->comp[k].v < 1 || c->comp[k].v > 4) { free_ctx(c); return false; }
      }
      have_sof = true;
    } else if (m == 0xC4) {  // Huffman tables
      size_t o = 0;
      while (o + 17 <= sl) {
        int tc = s[o] >> 4, th = s[o] & 15;
        if (tc > 1 || th > 1) { free_ctx(c); return false; }
        int total = 0;
        for (int k = 0; k < 16; k++) total += s[o + 1 + k];
        if (o + 17 + total > sl) { free_ctx(c); return false; }
        if (!build(c->ht[tc * 2 + th], s + o + 1, s + o + 17, total)) { free_ctx(c); return false; }
        o += 17 + total;
      }
    } else if (m == 0xDD) {  // restart interval: not handled
      if (sl >= 2 && ((s[0] << 8) | s[1]) != 0) { free_ctx(c); return false; }
    } else if (m >= 0xC1 && m <= 0xCF) {  // progressive, lossless, arithmetic, ...
      free_ctx(c);
      return false;
    } else if (m == 0xDA) {  // start of scan
      if (!have_sof || sl < 1) { free_ctx(c); return false; }
      scan_n = s[0];
      if (scan_n != ncomp || sl < (size_t)(1 + 2 * scan_n + 3)) { free_ctx(c); return false; }
      for (int k = 0; k < scan_n; k++) {
        if (s[1 + 2 * k] != c->comp[k].id) { free_ctx(c); return false; }  // components in frame order only
        c->comp[k].td = s[2 + 2 * k] >> 4;
        c->comp[k].ta = s[2 + 2 * k] & 15;
        if (c->comp[k].td > 1 || c->comp[k].ta > 1) { free_ctx(c); return false; }
      }
      const uint8_t *e = s + 1 + 2 * scan_n;
      if (e[0] != 0 || e[1] != 63 || e[2] != 0) { free_ctx(c); return false; }  // not a full sequential scan
      header_end = i + 2 + len;
      break;
    } else if (m == 0xD9 || m == 0xD8) {
      free_ctx(c);
      return false;
    }
    i += 2 + len;
  }

  // Geometry
  int hmax = 1, vmax = 1;
  if (ncomp == 1) { c->comp[0].h = 1; c->comp[0].v = 1; }
  for (int k = 0; k < ncomp; k++) {
    if (c->comp[k].h > hmax) hmax = c->comp[k].h;
    if (c->comp[k].v > vmax) vmax = c->comp[k].v;
  }
  if (width % (8 * hmax) != 0 || height % (8 * vmax) != 0) { free_ctx(c); return false; }  // padding would end up on the wrong side
  int mcux = width / (8 * hmax), mcuy = height / (8 * vmax);
  long total_blocks = 0;
  for (int k = 0; k < ncomp; k++) {
    if (!c->ht[c->comp[k].td].present || !c->ht[2 + c->comp[k].ta].present) { free_ctx(c); return false; }
    c->comp[k].gw = mcux * c->comp[k].h;
    c->comp[k].gh = mcuy * c->comp[k].v;
    total_blocks += (long) c->comp[k].gw * c->comp[k].gh;
  }
  if (total_blocks > MAX_BLOCKS) { free_ctx(c); return false; }
  for (int k = 0; k < ncomp; k++) {
    c->comp[k].blocks = (int16_t *) calloc((size_t) c->comp[k].gw * c->comp[k].gh * 64, sizeof(int16_t));
    if (c->comp[k].blocks == nullptr) { free_ctx(c); return false; }
  }

  // Decode all coefficients
  Reader r;
  r.p = d + header_end;
  r.end = d + n;
  int pred[4] = {0, 0, 0, 0};
  for (int my = 0; my < mcuy; my++) {
    for (int mx = 0; mx < mcux; mx++) {
      for (int k = 0; k < ncomp; k++) {
        Comp &cp = c->comp[k];
        const Huff &dc = c->ht[cp.td], &ac = c->ht[2 + cp.ta];
        for (int by = 0; by < cp.v; by++) {
          for (int bx = 0; bx < cp.h; bx++) {
            int16_t *b = cp.blocks + ((size_t)(my * cp.v + by) * cp.gw + (mx * cp.h + bx)) * 64;
            int s = r.decode(dc);
            if (!r.ok || s > 11) { free_ctx(c); return false; }
            pred[k] += Reader::extend(r.receive(s), s);
            b[0] = (int16_t) pred[k];
            for (int z = 1; z < 64;) {
              int rs = r.decode(ac);
              if (!r.ok) { free_ctx(c); return false; }
              int run = rs >> 4, sz = rs & 15;
              if (sz == 0) {
                if (run == 15) { z += 16; continue; }
                break;  // end of block
              }
              z += run;
              if (z > 63) { free_ctx(c); return false; }
              b[z] = (int16_t) Reader::extend(r.receive(sz), sz);
              z++;
            }
            if (!r.ok) { free_ctx(c); return false; }
          }
        }
      }
    }
  }

  // Write: same headers, blocks taken from the opposite corner
  out.clear();
  out.reserve(n + 64);
  out.insert(out.end(), d, d + header_end);
  Writer w(out);
  for (int k = 0; k < 4; k++) pred[k] = 0;
  for (int my = 0; my < mcuy; my++) {
    for (int mx = 0; mx < mcux; mx++) {
      for (int k = 0; k < ncomp; k++) {
        Comp &cp = c->comp[k];
        const Huff &dc = c->ht[cp.td], &ac = c->ht[2 + cp.ta];
        for (int by = 0; by < cp.v; by++) {
          for (int bx = 0; bx < cp.h; bx++) {
            int gx = mx * cp.h + bx, gy = my * cp.v + by;
            const int16_t *src = cp.blocks + ((size_t)(cp.gh - 1 - gy) * cp.gw + (cp.gw - 1 - gx)) * 64;
            if (!encode_block(w, src, pred[k], dc, ac)) { free_ctx(c); return false; }
          }
        }
      }
    }
  }
  w.flush();
  out.push_back(0xFF);
  out.push_back(0xD9);
  free_ctx(c);
  return true;
}

}  // namespace jpegrot
}  // namespace hid_streamdeck
}  // namespace esphome

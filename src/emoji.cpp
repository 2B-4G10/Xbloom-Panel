// Colour emoji: text mapping and the image font. See include/emoji.h.
#include "emoji.h"

#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

extern "C" {
extern const uint16_t emoji_size;
extern const uint32_t emoji_count;
extern const uint32_t emoji_cps[];
extern const uint16_t emoji_seq[];
extern const uint32_t emoji_at[];
extern const uint8_t emoji_lz4[];
}

namespace emoji {
namespace {

constexpr uint32_t PUA = 0xF0000;  // emoji i is drawn as U+F0000 + i
constexpr uint32_t VS15 = 0xFE0E, VS16 = 0xFE0F, ZWJ = 0x200D;

// ---- UTF-8 ----
uint32_t next_cp(const char *&p) {
  const uint8_t *s = (const uint8_t *)p;
  uint32_t c = s[0];
  int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  if (len > 1) {
    c &= 0x3F >> (len - 1);
    for (int i = 1; i < len; i++) {
      if ((s[i] & 0xC0) != 0x80) { len = i; c = 0xFFFD; break; }  // broken text: one replacement
      c = (c << 6) | (s[i] & 0x3F);
    }
  }
  p += len;
  return c;
}

// Appends `c` if it fits (always leaving room for the final 0).
bool put_cp(char *&o, char *end, uint32_t c) {
  char b[4];
  int len;
  if (c < 0x80) { b[0] = c; len = 1; }
  else if (c < 0x800) { b[0] = 0xC0 | c >> 6; b[1] = 0x80 | (c & 0x3F); len = 2; }
  else if (c < 0x10000) { b[0] = 0xE0 | c >> 12; b[1] = 0x80 | ((c >> 6) & 0x3F); b[2] = 0x80 | (c & 0x3F); len = 3; }
  else { b[0] = 0xF0 | c >> 18; b[1] = 0x80 | ((c >> 12) & 0x3F); b[2] = 0x80 | ((c >> 6) & 0x3F); b[3] = 0x80 | (c & 0x3F); len = 4; }
  if (end - o <= len) return false;
  memcpy(o, b, len);
  o += len;
  return true;
}

// ---- the sequence table ----
uint32_t seq_len(uint32_t i) { return emoji_seq[i + 1] - emoji_seq[i]; }
const uint32_t *seq(uint32_t i) { return emoji_cps + emoji_seq[i]; }

// First entry whose first codepoint is `c` (entries are sorted).
uint32_t first_with(uint32_t c) {
  uint32_t lo = 0, hi = emoji_count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (seq(mid)[0] < c) lo = mid + 1; else hi = mid;
  }
  return lo;
}

// Longest emoji starting at `p`. Variation selectors in the text are skipped, as the
// table has none. Returns the emoji's index (or -1) and moves `p` past it.
int match(const char *&p) {
  const char *start = p;
  const uint32_t c0 = next_cp(start);
  uint32_t i = first_with(c0);
  if (i >= emoji_count || seq(i)[0] != c0) return -1;

  int best = -1;
  const char *best_end = nullptr;
  for (; i < emoji_count && seq(i)[0] == c0; i++) {
    const char *q = start;
    uint32_t k = 1;
    const uint32_t len = seq_len(i);
    while (k < len) {
      const char *before = q;
      uint32_t c = next_cp(q);
      if (c == VS16 || c == VS15) continue;
      if (c != seq(i)[k]) { q = before; break; }
      k++;
    }
    if (k < len) continue;
    // Digits, # and *, © and ® are emoji only when asked for (with U+FE0F); alone
    // they are ordinary text.
    if (len == 1 && c0 < 0x2000) {
      const char *v = q;
      if (!*v || next_cp(v) != VS16) continue;
    }
    if (!best_end || q > best_end) { best = i; best_end = q; }
  }
  if (best < 0) return -1;
  // Swallow a trailing presentation selector.
  const char *v = best_end;
  if (*v) {
    const uint32_t c = next_cp(v);
    if (c == VS16 || c == VS15) best_end = v;
  }
  p = best_end;
  return best;
}

// The text each mapped emoji came from, so text fields give back what they were given.
struct Original { uint32_t index; char text[48]; };
Original originals[32];
uint32_t originals_next = 0;

void remember(uint32_t index, const char *from, size_t len) {
  if (len >= sizeof originals[0].text) return;
  for (auto &o : originals)
    if (o.text[0] && o.index == index) {
      memcpy(o.text, from, len);
      o.text[len] = 0;
      return;
    }
  Original &o = originals[originals_next++ % (sizeof originals / sizeof originals[0])];
  o.index = index;
  memcpy(o.text, from, len);
  o.text[len] = 0;
}

// ---- images ----
// Each image: colour count, palette (RGB565 + alpha), LZ4 block of colour indexes.
bool lz4_decode(const uint8_t *in, const uint8_t *in_end, uint8_t *out, size_t out_len) {
  uint8_t *o = out, *const o_end = out + out_len;
  while (in < in_end) {
    const uint8_t token = *in++;
    size_t lit = token >> 4;
    if (lit == 15) { uint8_t b; do { if (in >= in_end) return false; b = *in++; lit += b; } while (b == 255); }
    if (lit > (size_t)(in_end - in) || lit > (size_t)(o_end - o)) return false;
    memcpy(o, in, lit);
    o += lit;
    in += lit;
    if (in >= in_end) break;  // the last sequence has literals only
    if (in_end - in < 2) return false;
    const size_t off = in[0] | in[1] << 8;
    in += 2;
    size_t mlen = token & 15;
    if (mlen == 15) { uint8_t b; do { if (in >= in_end) return false; b = *in++; mlen += b; } while (b == 255); }
    mlen += 4;
    if (off == 0 || off > (size_t)(o - out) || mlen > (size_t)(o_end - o)) return false;
    for (const uint8_t *m = o - off; mlen--;) *o++ = *m++;  // may overlap: byte by byte
  }
  return o == o_end;
}

void *big_alloc(size_t n) {
#ifdef ESP_PLATFORM
  if (void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
#endif
  return malloc(n);
}

struct Image {
  lv_image_dsc_t dsc;
  uint32_t index;
  uint8_t data[];  // RGB565 pixels, then the alpha of each pixel
};

// Decoded images are kept: a screen shows few different emoji. Past the limit the
// oldest is reused.
constexpr int KEEP = 160;
Image *kept[KEEP];
int kept_count = 0, kept_next = 0;

const lv_image_dsc_t *image(uint32_t index) {
  for (int i = 0; i < kept_count; i++)
    if (kept[i]->index == index) return &kept[i]->dsc;

  const int px = emoji_size * emoji_size;
  Image *img;
  if (kept_count < KEEP) {
    img = (Image *)big_alloc(sizeof(Image) + px * 3);
    if (!img) return nullptr;
    kept[kept_count++] = img;
  } else {
    img = kept[kept_next];
    kept_next = (kept_next + 1) % KEEP;
    lv_image_cache_drop(&img->dsc);
  }

  const uint8_t *p = emoji_lz4 + emoji_at[index], *end = emoji_lz4 + emoji_at[index + 1];
  const int colours = *p++;
  const uint8_t *palette = p;
  p += colours * 3;
  static uint8_t idx[64 * 64];
  if (px > (int)sizeof idx || !lz4_decode(p, end, idx, px)) return nullptr;

  uint8_t *rgb = img->data, *alpha = img->data + px * 2;
  for (int i = 0; i < px; i++) {
    const uint8_t *c = palette + 3 * (idx[i] < colours ? idx[i] : 0);
    rgb[2 * i] = c[0];
    rgb[2 * i + 1] = c[1];
    alpha[i] = c[2];
  }
  memset(&img->dsc, 0, sizeof img->dsc);
  img->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  img->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
  img->dsc.header.w = emoji_size;
  img->dsc.header.h = emoji_size;
  img->dsc.header.stride = emoji_size * 2;
  img->dsc.data_size = px * 3;
  img->dsc.data = img->data;
  img->index = index;
  return &img->dsc;
}

// ---- the font ----
// Characters that only shape an emoji: drawn as nothing if text reaches the font unmapped.
bool invisible(uint32_t c) {
  return c == VS15 || c == VS16 || c == ZWJ || (c >= 0xE0020 && c <= 0xE007F);
}

// An emoji written as one character, for text that was not mapped.
int single(uint32_t c) {
  if (c < 0x2000) return -1;
  const uint32_t i = first_with(c);
  return i < emoji_count && seq(i)[0] == c && seq_len(i) == 1 ? (int)i : -1;
}

bool glyph_dsc(const lv_font_t *, lv_font_glyph_dsc_t *g, uint32_t c, uint32_t) {
  int index = c >= PUA && c < PUA + emoji_count ? (int)(c - PUA) : single(c);
  if (index < 0) {
    if (!invisible(c)) return false;
    g->adv_w = g->box_w = g->box_h = 0;
    g->ofs_x = g->ofs_y = 0;
    g->format = LV_FONT_GLYPH_FORMAT_NONE;
    g->is_placeholder = 0;
    return true;
  }
  g->adv_w = emoji_size + 2;
  g->box_w = emoji_size;
  g->box_h = emoji_size;
  g->ofs_x = 1;
  g->ofs_y = -(emoji_size / 5);  // sits on the baseline like a capital, a little below
  g->format = LV_FONT_GLYPH_FORMAT_IMAGE;
  g->is_placeholder = 0;
  g->glyph_index = index;
  return true;
}

const void *glyph_bitmap(lv_font_glyph_dsc_t *g, uint32_t, lv_draw_buf_t *) {
  return g->format == LV_FONT_GLYPH_FORMAT_IMAGE ? image(g->glyph_index) : nullptr;
}

}  // namespace

extern "C" const lv_font_t emoji_font;
const lv_font_t emoji_font = {
    .get_glyph_dsc = glyph_dsc,
    .get_glyph_bitmap = glyph_bitmap,
    .release_glyph = nullptr,
    .line_height = 24,
    .base_line = 4,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = nullptr,
    .fallback = nullptr,
    .user_data = nullptr,
};

const char *map(const char *in, char *buf, size_t n) {
  if (!in || n == 0) return in;
  bool any = false;
  for (const uint8_t *s = (const uint8_t *)in; *s; s++)
    if (*s >= 0x80) { any = true; break; }
  if (!any) return in;  // plain ASCII: no emoji (keycaps need U+20E3)

  char *o = buf, *const end = buf + n;
  bool changed = false;
  for (const char *p = in; *p;) {
    const char *at = p;
    const int i = match(p);
    if (i >= 0) {
      remember(i, at, p - at);
      if (!put_cp(o, end, PUA + i)) break;
      changed = true;
      continue;
    }
    p = at;
    const uint32_t c = next_cp(p);
    if (c == ZWJ || c == VS16 || c == VS15) { changed = true; continue; }  // left over from an unknown sequence
    if ((size_t)(end - o) <= (size_t)(p - at)) break;
    memcpy(o, at, p - at);
    o += p - at;
  }
  *o = 0;
  return changed ? buf : in;
}

void unmap(const char *in, char *buf, size_t n) {
  if (n == 0) return;
  char *o = buf, *const end = buf + n;
  for (const char *p = in ? in : ""; *p;) {
    const char *at = p;
    const uint32_t c = next_cp(p);
    if (c >= PUA && c < PUA + emoji_count) {
      const uint32_t i = c - PUA;
      const char *orig = nullptr;
      for (auto &r : originals)
        if (r.text[0] && r.index == i) orig = r.text;
      if (orig) {
        const size_t len = strlen(orig);
        if ((size_t)(end - o) <= len) break;
        memcpy(o, orig, len);
        o += len;
      } else {
        bool fits = true;
        for (uint32_t k = 0; k < seq_len(i) && fits; k++) fits = put_cp(o, end, seq(i)[k]);
        if (seq_len(i) == 1 && seq(i)[0] < 0x1F000 && fits) fits = put_cp(o, end, VS16);  // the colour form
        if (!fits) break;
      }
      continue;
    }
    if ((size_t)(end - o) <= (size_t)(p - at)) break;
    memcpy(o, at, p - at);
    o += p - at;
  }
  *o = 0;
}

}  // namespace emoji

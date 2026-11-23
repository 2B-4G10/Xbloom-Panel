// Emoji check: every emoji maps to one drawable character and back, every image
// decodes, and ordinary text is left alone.
//
//   test_native/run.sh   (builds this against LVGL's headers; LVGL=folder of LVGL)
#include <stdio.h>
#include <string.h>

#include <string>

#include "emoji.h"

extern "C" {
extern const uint32_t emoji_count;
extern const uint16_t emoji_size;
extern const uint32_t emoji_cps[];
extern const uint16_t emoji_seq[];
void lv_image_cache_drop(const void *) {}
}

static int failures = 0;
#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      if (failures++ < 20) {            \
        printf("FAIL: " __VA_ARGS__);   \
        printf("\n");                   \
      }                                 \
    }                                   \
  } while (0)

static void utf8(std::string &s, uint32_t c) {
  if (c < 0x80) s += (char)c;
  else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 0x3F)); }
  else if (c < 0x10000) { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
  else { s += (char)(0xF0 | c >> 18); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
}

static std::string mapped(const char *in) {
  static char buf[512];
  return emoji::map(in, buf, sizeof buf);
}

static std::string round_trip(const char *in) {
  static char back[512];
  std::string m = mapped(in);
  emoji::unmap(m.c_str(), back, sizeof back);
  return back;
}

int main() {
  // Every emoji: alone, with a presentation selector after its first character, and
  // inside text.
  for (uint32_t i = 0; i < emoji_count; i++) {
    std::string plain, selected;
    for (uint32_t k = emoji_seq[i]; k < emoji_seq[i + 1]; k++) {
      utf8(plain, emoji_cps[k]);
      utf8(selected, emoji_cps[k]);
      if (k == emoji_seq[i]) utf8(selected, 0xFE0F);
    }
    std::string one;
    utf8(one, 0xF0000 + i);
    const bool needs_selector = emoji_seq[i + 1] - emoji_seq[i] == 1 && emoji_cps[emoji_seq[i]] < 0x2000;
    if (!needs_selector) CHECK(mapped(plain.c_str()) == one, "emoji %u alone", i);
    CHECK(mapped(selected.c_str()) == one, "emoji %u with U+FE0F", i);
    const std::string in_text = "Hi " + selected + " there";
    CHECK(mapped(in_text.c_str()) == "Hi " + one + " there", "emoji %u in text", i);
    CHECK(round_trip(in_text.c_str()) == in_text, "emoji %u round trip", i);
  }

  // Ordinary text stays as it is.
  const char *plain[] = {"Daily Pour Over", "# 1 2 3 * © ®", "قهوة ٨٠ مل", "1:16 | 240ml", "95°C", "●", ""};
  for (const char *t : plain) CHECK(mapped(t) == t, "plain text changed: %s", t);

  // Unknown sequences: the joiner between two known emoji is dropped, both still draw.
  CHECK(mapped("\xF0\x9F\x8D\x93\xE2\x80\x8D\xF0\x9F\x8D\xAB").size() == 8, "unknown sequence");

  // A cut-off character never crashes and never reads past the end.
  CHECK(!mapped("abc\xF0\x9F").empty(), "broken text");

  // Every image decodes and has something visible.
  int empty = 0;
  for (uint32_t i = 0; i < emoji_count; i++) {
    lv_font_glyph_dsc_t g;
    memset(&g, 0, sizeof g);
    const bool found = emoji_font.get_glyph_dsc(&emoji_font, &g, 0xF0000 + i, 0);
    CHECK(found && g.box_w == emoji_size, "glyph %u", i);
    g.resolved_font = &emoji_font;
    const lv_image_dsc_t *img = (const lv_image_dsc_t *)emoji_font.get_glyph_bitmap(&g, 0xF0000 + i, nullptr);
    CHECK(img && img->header.w == emoji_size && img->header.cf == LV_COLOR_FORMAT_RGB565A8, "image %u", i);
    if (!img) continue;
    int opaque = 0;
    const uint8_t *alpha = img->data + emoji_size * emoji_size * 2;
    for (int p = 0; p < emoji_size * emoji_size; p++) opaque += alpha[p] > 128;
    if (opaque < 8) empty++;
  }
  CHECK(empty == 0, "%d images are (nearly) empty", empty);

  printf("%u emoji checked, %d failures\n", emoji_count, failures);
  return failures ? 1 : 0;
}

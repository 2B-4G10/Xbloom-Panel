#pragma once
// Colour emoji in every label: all of Unicode's emoji, skin tones, families,
// professions, keycaps and flags (Noto Color Emoji images, src/emoji_data.c).
//
// LVGL draws one glyph per character, but many emoji are several characters
// (👩🏽‍💻 is five). So text is mapped first: each emoji becomes one Private Use
// character, which emoji_font draws as a small colour image. The text fonts list
// emoji_font as their last fallback.
#include <lvgl.h>
#include <stddef.h>

// The emoji image font (the last fallback of the text fonts in src/fonts/).
extern "C" const lv_font_t emoji_font;

namespace emoji {

// Text with every emoji as one drawable character. Returns `in` itself when it has
// no emoji, otherwise `buf`.
const char *map(const char *in, char *buf, size_t n);

// The original text back from mapped text (text fields hand this to the rest of
// the firmware, so names are saved exactly as written).
void unmap(const char *in, char *buf, size_t n);

}  // namespace emoji

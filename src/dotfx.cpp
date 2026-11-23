// Dot-matrix scenes: the idle clock, the brew-start ripple, the live brewing
// scene and the finished cup. Each scene is a full-screen black layer holding
// one or more dot fields (see dotfx.h) plus ordinary labels.
#include "dotfx.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "emoji.h"
#include "i18n.h"
#include "ui_theme.h"

namespace dotfx {

const uint32_t BLUE = 0x3F9BFF;
const uint32_t WHITE = 0xF2F2F2;
const uint32_t COFFEE = 0x9A6236;

namespace {

constexpr int SCREEN_W = 800, SCREEN_H = 480;

// ---- small maths -------------------------------------------------------------------
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// 0 before a, 1 after b, an S-curve between.
float ease(float a, float b, float x) {
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

// A fixed pseudo-random number per dot, so each dot has its own little delay.
uint32_t dot_noise(int a, int b) {
  uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// Colour between a (k = 0) and b (k = 1).
uint32_t blend(uint32_t a, uint32_t b, float k) {
  const int ar = a >> 16, ag = (a >> 8) & 255, ab = a & 255, br = b >> 16, bg = (b >> 8) & 255, bb = b & 255;
  return ((uint32_t)(ar + (br - ar) * k) << 16) | ((uint32_t)(ag + (bg - ag) * k) << 8) | (uint32_t)(ab + (bb - ab) * k);
}

// Labels show their text in the panel's language, emoji and Arabic digits and units included.
void set_label(lv_obj_t *l, const char *text) {
  static char with_emoji[128], buf[128];
  lv_label_set_text(l, i18n::localize(emoji::map(i18n::tr(text), with_emoji, sizeof with_emoji), buf, sizeof buf));
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  if (i18n::rtl()) lv_obj_set_style_base_dir(l, LV_BASE_DIR_AUTO, 0);  // each text in its own direction
  set_label(l, "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

// A hidden full-screen black layer above everything. Tappable, so taps never reach
// the screen underneath. In Arabic it is left as drawn (flag USER_2, see ui.cpp).
lv_obj_t *scene_layer() {
  lv_obj_t *l = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(l);
  lv_obj_set_size(l, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(l, LV_OBJ_FLAG_USER_2);
  lv_obj_set_style_base_dir(l, LV_BASE_DIR_LTR, 0);
  return l;
}

void show(lv_obj_t *layer) {
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(layer);
}
void hide(lv_obj_t *layer) {
  if (layer) lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
}

// Puts a number and its unit side by side on one text baseline, starting at x.
// With Arabic units the unit goes first (on the left): Arabic reads the number first.
void number_and_unit(lv_obj_t *number, lv_obj_t *unit, int x, int baseline) {
  lv_obj_update_layout(number);
  int xn = x, xu = x + lv_obj_get_width(number) + 6;
  if (i18n::arabic_units()) {
    lv_obj_update_layout(unit);
    xu = x;
    xn = x + lv_obj_get_width(unit) + 6;
  }
  const lv_font_t *fn = lv_obj_get_style_text_font(number, 0), *fu = lv_obj_get_style_text_font(unit, 0);
  lv_obj_set_pos(number, xn, baseline - (lv_font_get_line_height(fn) - fn->base_line));
  lv_obj_set_pos(unit, xu, baseline - (lv_font_get_line_height(fu) - fu->base_line));
}

// ---- dot fields --------------------------------------------------------------------
void draw_field(lv_event_t *e) {
  lv_obj_t *o = (lv_obj_t *)lv_event_get_target(e);
  const Field *f = (const Field *)lv_obj_get_user_data(o);
  if (!f || !f->fn) return;
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t area;
  lv_obj_get_coords(o, &area);
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  const uint32_t t = millis() - f->t0;
  for (int row = 0; row < f->rows; ++row)
    for (int col = 0; col < f->cols; ++col) {
      uint32_t color = f->color;
      const float v = f->fn(const_cast<Field *>(f), col, row, t, &color);
      if (v < 0.03f) continue;  // too faint to see: skip the work
      dot.bg_color = lv_color_hex(color);
      dot.bg_opa = (lv_opa_t)(clamp01(v * f->fade) * 255);
      const int cx = area.x1 + col * f->pitch + f->pitch / 2, cy = area.y1 + row * f->pitch + f->pitch / 2;
      const lv_area_t d = {(int32_t)(cx - f->radius), (int32_t)(cy - f->radius), (int32_t)(cx + f->radius - 1),
                           (int32_t)(cy + f->radius - 1)};
      lv_draw_rect(layer, &dot, &d);
    }
}

void free_field(lv_event_t *e) { delete (Field *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e)); }

// One 30 fps timer redraws every visible animated field and steps the scenes.
constexpr int MAX_ANIMATED = 8;
lv_timer_t *frame_timer;
lv_obj_t *animated[MAX_ANIMATED];
int n_animated = 0;
void on_frame(lv_timer_t *);

}  // namespace

lv_obj_t *field(lv_obj_t *parent, int x, int y, int cols, int rows, int pitch, int radius, DotFn fn, void *ctx,
                bool animate) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, cols * pitch, rows * pitch);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(o, new Field{cols, rows, pitch, radius, fn, ctx, millis(), BLUE, animate, 1.0f});
  lv_obj_add_event_cb(o, draw_field, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(o, free_field, LV_EVENT_DELETE, nullptr);
  if (animate && n_animated < MAX_ANIMATED) animated[n_animated++] = o;
  if (!frame_timer) frame_timer = lv_timer_create(on_frame, 33, nullptr);
  return o;
}

Field *field_of(lv_obj_t *o) { return (Field *)lv_obj_get_user_data(o); }

void restart(lv_obj_t *o) {
  field_of(o)->t0 = millis();
  lv_obj_invalidate(o);
}

// ---- 5x7 dot font -------------------------------------------------------------------
namespace {
// Rows top to bottom; bit 4 is the leftmost column.
struct Glyph {
  char c;
  uint8_t rows[7];
};
const Glyph GLYPHS[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
};
}  // namespace

int glyph_w(char ch) {
  switch (ch) {
    case ':': return 1;
    case ' ': return 3;
    case ',': return 4;
    default: return 5;
  }
}

bool glyph_dot(char ch, int x, int y) {
  if (y < 0 || y > 6 || x < 0 || x >= glyph_w(ch)) return false;
  if (ch == ':') return y == 2 || y == 4;
  if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
  for (const Glyph &g : GLYPHS)
    if (g.c == ch) return g.rows[y] & (0x10 >> x);
  return false;
}

// ---- text as dots ------------------------------------------------------------------
namespace {
struct DotText {
  static const int MAXC = 80, ROWS = 9;  // 7 glyph rows plus a blank row above and below
  uint8_t lit[ROWS][MAXC];
  int cols;
  int colon_col = -1;  // the colon's column, for blinking
  bool blink = false;
  float grid = 0.10f;  // brightness of unlit dots: the LED-panel look
};

// Lays `s` out into the bitmap; returns its width in dots (with one blank column
// at each side).
int set_dot_text(DotText &dt, const char *s) {
  memset(dt.lit, 0, sizeof dt.lit);
  dt.colon_col = -1;
  int x = 1;
  for (const char *p = s; *p && x < DotText::MAXC - 6; ++p) {
    const int w = glyph_w(*p);
    if (*p == ':') dt.colon_col = x;
    for (int gy = 0; gy < 7; ++gy)
      for (int gx = 0; gx < w; ++gx)
        if (glyph_dot(*p, gx, gy)) dt.lit[gy + 1][x + gx] = 1;
    x += w + 1;
  }
  return dt.cols = x;
}

float dot_text(Field *f, int col, int row, uint32_t, uint32_t *) {
  const DotText *dt = (const DotText *)f->ctx;
  if (col >= dt->cols || row >= DotText::ROWS) return 0;
  if (!dt->lit[row][col]) return dt->grid;
  if (dt->blink && col == dt->colon_col) return (millis() / 1000) % 2 ? 0.35f : 1.0f;
  return 1.0f;
}

// ---- clock ---------------------------------------------------------------------------
struct ClockScene {
  static const int TIME_PITCH = 22, TIME_DOT = 8, DATE_PITCH = 9, DATE_DOT = 3, GAP = 18;
  DotText time, date;
  lv_obj_t *time_field, *date_field;
} clock_scene;
}  // namespace

lv_obj_t *clock_create(lv_obj_t *parent) {
  auto &c = clock_scene;
  lv_obj_t *bg = lv_obj_create(parent);
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_base_dir(bg, LV_BASE_DIR_LTR, 0);
  c.time.blink = false;  // a steady colon: the clock redraws only when the minute changes
  c.time.grid = 0.09f;
  c.date.grid = 0.07f;
  c.time_field = field(bg, 0, 0, DotText::MAXC, DotText::ROWS, c.TIME_PITCH, c.TIME_DOT, dot_text, &c.time, false);
  c.date_field = field(bg, 0, 0, DotText::MAXC, DotText::ROWS, c.DATE_PITCH, c.DATE_DOT, dot_text, &c.date, false);
  return bg;
}

void clock_set(lv_obj_t *, const char *hm, const char *date, uint32_t color) {
  auto &c = clock_scene;
  // Called several times a second while showing: only redraw on a change.
  static char shown_hm[16] = "", shown_date[32] = "";
  static uint32_t shown_color = 0;
  if (!strcmp(hm, shown_hm) && !strcmp(date, shown_date) && color == shown_color) return;
  strlcpy(shown_hm, hm, sizeof shown_hm);
  strlcpy(shown_date, date, sizeof shown_date);
  shown_color = color;

  const int time_cols = set_dot_text(c.time, hm), date_cols = set_dot_text(c.date, date);
  Field *tf = field_of(c.time_field), *df = field_of(c.date_field);
  tf->cols = time_cols;
  df->cols = date_cols;
  tf->color = df->color = color;
  const int time_w = time_cols * c.TIME_PITCH, date_w = date_cols * c.DATE_PITCH;
  const int time_h = DotText::ROWS * c.TIME_PITCH, date_h = DotText::ROWS * c.DATE_PITCH;
  lv_obj_set_size(c.time_field, time_w, time_h);
  lv_obj_set_size(c.date_field, date_w, date_h);
  const int top = (SCREEN_H - (time_h + c.GAP + date_h)) / 2;  // the pair centred on screen
  lv_obj_set_pos(c.time_field, (SCREEN_W - time_w) / 2, top);
  lv_obj_set_pos(c.date_field, (SCREEN_W - date_w) / 2, top + time_h + c.GAP);
  lv_obj_invalidate(c.time_field);
  lv_obj_invalidate(c.date_field);
}

// ---- brew start: ripples, then a droplet builds up, then the recipe name ---------------
namespace {
struct IntroScene {
  static const int PITCH = 20, COLS = 40, ROWS = 24;
  static constexpr uint32_t FADE_FROM = 1900, END = 2600;  // ms
  lv_obj_t *layer, *dots, *name, *sub;
  bool on = false;
} intro;

// Inside the droplet? (px from its centre: a round bottom under a pointed top)
bool in_droplet(float x, float y) {
  x /= 1.35f;
  y /= 1.35f;
  if (y < -100 || y > 92) return false;
  if (y >= 30) return x * x + (y - 30) * (y - 30) <= 62 * 62;
  const float half_width = 62 * powf((y + 100) / 130.0f, 1.4f);
  return fabsf(x) <= half_width;
}

float intro_dots(Field *, int col, int row, uint32_t t, uint32_t *color) {
  const float px = col * intro.PITCH + intro.PITCH / 2, py = row * intro.PITCH + intro.PITCH / 2;
  // Three ripples spreading from where the water lands.
  const float dx = px - 400, dy = (py - 250) * 1.15f, dist = sqrtf(dx * dx + dy * dy);
  float v = 0;
  for (int k = 0; k < 3; ++k) {
    const float tk = (float)t - k * 260.0f;
    if (tk <= 0) continue;
    const float radius = tk * 0.55f, strength = (1 - clamp01(tk / 1300.0f)) * (k == 0 ? 1.0f : 0.6f);
    const float z = (dist - radius) / 26.0f;
    v += strength * expf(-z * z);
  }
  // Then the droplet appears, dot by dot, with a highlight on its upper left.
  const float hx = px - 400, hy = py - 172;
  if (in_droplet(hx, hy)) {
    const float jitter = (dot_noise(col, row) % 400);
    const float lit = ease(750 + jitter, 1150 + jitter, (float)t);
    const bool highlight = hx < -14 && hx > -62 && hy > 0 && hy < 70;
    if (lit > v) {
      v = lit;
      *color = highlight ? 0xD6ECFF : BLUE;
    }
  }
  return v;
}

// A tap skips straight to the fade-out.
void skip_intro(lv_event_t *) {
  Field *f = field_of(intro.dots);
  if (millis() - f->t0 < intro.FADE_FROM) f->t0 = millis() - intro.FADE_FROM;
}

void step_intro() {
  const uint32_t t = millis() - field_of(intro.dots)->t0;
  const float remaining = 1 - ease(intro.FADE_FROM, intro.END, (float)t);
  lv_obj_set_style_opa(intro.name, (lv_opa_t)(ease(1100, 1500, (float)t) * remaining * 255), 0);
  lv_obj_set_style_opa(intro.sub, (lv_opa_t)(ease(1250, 1650, (float)t) * remaining * 255), 0);
  field_of(intro.dots)->fade = remaining;
  lv_obj_set_style_bg_opa(intro.layer, (lv_opa_t)(remaining * 255), 0);
  if (t >= intro.END) {
    hide(intro.layer);
    intro.on = false;
  }
}

// ---- brew finished: a cup fills, steam rises, "Enjoy" and the numbers ------------------
struct FinishScene {
  static const int PITCH = 14, COLS = 21, ROWS = 17;
  lv_obj_t *layer, *dots, *title, *name, *stats[3][2];  // [weight, water, time][number, unit]
  bool on = false;
} finish;

float finish_dots(Field *, int c, int r, uint32_t t, uint32_t *color) {
  // The cup: body walls at cols 4 and 14 (rows 6-14), bottom row 15, handle, saucer row 16.
  const bool wall = (r >= 6 && r <= 14 && (c == 4 || c == 14)) || (r == 15 && c >= 5 && c <= 13);
  const bool handle =
      (c == 15 && (r == 8 || r == 11)) || (c == 16 && (r == 8 || r == 11)) || (c == 17 && r >= 9 && r <= 10);
  const bool saucer = r == 16 && c >= 2 && c <= 18;
  const float drawn = ease(0, 450, (float)t - (dot_noise(c, r) % 250));
  if (wall || handle || saucer) {
    *color = WHITE;
    return drawn * 0.95f;
  }
  // Coffee rising inside: darker toward the bottom, crema on the top row.
  if (c >= 5 && c <= 13 && r >= 7 && r <= 14) {
    const float level = clamp01(((float)t - 350) / 900.0f) * 8;  // rows filled
    const int from_bottom = 14 - r;
    if (from_bottom >= level) return 0.05f;
    const bool top_row = from_bottom + 1 > level;
    *color = top_row ? 0xD9A066 : blend(0xC58850, 0x7A4B29, clamp01(1.0f - from_bottom / 7.0f));
    return top_row ? 0.45f + 0.5f * (level - from_bottom) : 0.85f;
  }
  // Steam: three wavy columns drifting up.
  if (r <= 5 && t > 1100) {
    const float appear = ease(1100, 1700, (float)t);
    for (int k = 0; k < 3; ++k) {
      const float phase = k * 2.1f;
      const int x = 6 + k * 3 + (int)lroundf(0.9f * sinf(r * 0.95f - t * 0.0045f + phase));
      if (c == x) {
        const float pulse = 0.5f + 0.5f * sinf(t * 0.004f - r * 0.8f + phase);
        *color = WHITE;
        return appear * pulse * (0.25f + 0.13f * r);
      }
    }
  }
  return 0;
}

void build_intro_and_finish() {
  if (intro.layer) return;
  intro.layer = scene_layer();
  intro.dots = field(intro.layer, 0, 0, intro.COLS, intro.ROWS, intro.PITCH, 6, intro_dots, nullptr, true);
  intro.name = label(intro.layer, &inter_34b, 0xFFFFFF);
  intro.sub = label(intro.layer, &inter_20, 0x9A9AA0);
  lv_obj_add_event_cb(intro.layer, skip_intro, LV_EVENT_CLICKED, nullptr);

  finish.layer = scene_layer();
  finish.dots = field(finish.layer, (SCREEN_W - finish.COLS * finish.PITCH) / 2, 34, finish.COLS, finish.ROWS,
                      finish.PITCH, 5, finish_dots, nullptr, true);
  finish.title = label(finish.layer, &inter_34b, 0xFFFFFF);
  finish.name = label(finish.layer, &inter_20, 0x9A9AA0);
  for (auto &s : finish.stats) {
    s[0] = label(finish.layer, &inter_34b, 0xFFFFFF);
    s[1] = label(finish.layer, &inter_20, 0x9A9AA0);
  }
  lv_obj_add_event_cb(finish.layer, [](lv_event_t *) { finish_hide(); }, LV_EVENT_CLICKED, nullptr);
}

// ---- brewing: a dot dripper drips into a dot carafe that fills with the water; ---------
// the numbers and the Pause / Stop buttons are on the right.
struct BrewScene {
  static const int PITCH = 15, COLS = 26, ROWS = 29, TEXT_X = 440;
  lv_obj_t *layer, *dots, *name, *title, *sub, *water, *water_of, *stat[2][2], *caption[2];
  lv_obj_t *pause, *pause_label, *pause_icon, *stop;
  bool on = false;
  BrewView v = {};
  float level = 0;  // shown fill 0..1, easing toward the real one
  void (*on_pause)() = nullptr;
  void (*on_stop)() = nullptr;
} brew;

float brew_dots(Field *, int c, int r, uint32_t t, uint32_t *color) {
  const float appear = ease(0, 500, (float)t - (dot_noise(c, r) % 300));
  // Dripper: a cone over rows 1-8, centred between cols 12 and 13.
  if (r >= 1 && r <= 8) {
    const float half = 6.5f - (r - 1) * 0.72f, dx = fabsf(c - 12.5f);
    if ((r == 1 && dx <= half) || fabsf(dx - half) < 0.5f) {  // rim and sides
      *color = WHITE;
      return appear * 0.9f;
    }
    if (dx < half - 0.5f && r >= 3) {  // the wet coffee bed
      *color = blend(0x5A3A20, COFFEE, 0.4f + 0.3f * sinf(c * 1.7f + r));
      return appear * (brew.v.pouring ? 0.85f : 0.6f);
    }
    return 0;
  }
  // The drip from the tip down to the coffee surface. Carafe inside: rows 14-26.
  const int top_row = 13, bottom_row = 27, depth_rows = bottom_row - top_row;
  const int surface = bottom_row - (int)lroundf(brew.level * (depth_rows - 1));
  if ((c == 12 || c == 13) && r >= 9 && r < surface) {
    const float rows_per_ms = 0.012f;
    if (brew.v.pouring && !brew.v.paused) {  // a running stream
      const float phase = fmodf(r - t * rows_per_ms, 3.0f);
      *color = blend(COFFEE, 0xD9A066, 0.3f);
      return c == 12 ? 0.55f + 0.4f * (phase < 1.2f) : 0.0f;
    }
    const float drop_row = fmodf(t * rows_per_ms * 0.5f, 30.0f);  // between pours: a drop now and then
    if (c == 12 && fabsf(r - (9 + drop_row)) < 0.6f && !brew.v.paused) {
      *color = COFFEE;
      return 0.8f;
    }
    return 0;
  }
  // Carafe: walls at cols 4 and 21 (rows 12-26), rounded bottom corners, a lip.
  const bool wall = ((c == 4 || c == 21) && r >= 12 && r <= 26) || (r == 28 && c >= 6 && c <= 19) ||
                    (r == 27 && (c == 5 || c == 20));
  const bool lip = r == 12 && (c == 3 || c == 22);
  if (wall || lip) {
    *color = WHITE;
    return appear * 0.85f;
  }
  const bool inside = c >= 5 && c <= 20 && r >= 14 && r <= 27 && !(r == 27 && (c == 5 || c == 20));
  if (!inside) return 0;
  if (r > surface) {  // coffee, darker toward the bottom, with a slight shimmer
    const float depth = (float)(r - surface) / depth_rows;
    *color = blend(0xB97A45, 0x6E4426, clamp01(depth * 1.6f));
    return appear * (0.8f + 0.12f * sinf(t * 0.003f + c * 0.9f + r * 1.3f));
  }
  if (r == surface && brew.level > 0.01f) {  // the surface, rippling where the drip lands
    const float wave = 0.5f + 0.5f * sinf(fabsf(c - 12.5f) * 1.3f - t * 0.008f);
    *color = blend(COFFEE, 0xE0AE74, 0.6f);
    return appear * (brew.v.pouring ? 0.55f + 0.4f * wave : 0.75f);
  }
  return 0.05f;  // empty glass, just visible
}

// A pill button: icon (Montserrat symbol) and optional text, centred in a row.
lv_obj_t *pill_button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char *symbol,
                      const char *text, lv_obj_t **icon_out, lv_obj_t **label_out, lv_event_cb_t on_tap) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0xF2F2F2), 0);
  lv_obj_set_style_border_width(b, bg == 0x000000 ? 3 : 0, 0);  // black buttons get a ring
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 10, 0);
  lv_obj_t *icon = lv_label_create(b);
  set_label(icon, symbol);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(fg), 0);
  if (icon_out) *icon_out = icon;
  if (text) {
    lv_obj_t *l = lv_label_create(b);
    set_label(l, text);
    lv_obj_set_style_text_font(l, &inter_20b, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    if (label_out) *label_out = l;
  }
  lv_obj_add_event_cb(b, on_tap, LV_EVENT_CLICKED, nullptr);
  return b;
}

void build_brew() {
  if (brew.layer) return;
  const int X = brew.TEXT_X;
  brew.layer = scene_layer();
  brew.dots = field(brew.layer, 24, 26, brew.COLS, brew.ROWS, brew.PITCH, 5, brew_dots, nullptr, true);
  brew.name = label(brew.layer, &inter_20, 0x9A9AA0);
  lv_obj_set_pos(brew.name, X, 34);
  brew.title = label(brew.layer, &inter_34b, 0xFFFFFF);
  lv_obj_set_pos(brew.title, X, 64);
  brew.sub = label(brew.layer, &inter_20, 0xC8925A);
  lv_obj_set_pos(brew.sub, X, 110);
  brew.water = label(brew.layer, &inter_64b, 0xFFFFFF);
  brew.water_of = label(brew.layer, &inter_20, 0x9A9AA0);
  static const char *const CAPTIONS[2] = {"Scale", "Time"};
  for (int i = 0; i < 2; ++i) {
    brew.caption[i] = label(brew.layer, &inter_16, 0x8E8E93);
    set_label(brew.caption[i], CAPTIONS[i]);
    lv_obj_set_pos(brew.caption[i], X + i * 170, 262);
    brew.stat[i][0] = label(brew.layer, &inter_34b, 0xFFFFFF);
    brew.stat[i][1] = label(brew.layer, &inter_16, 0x9A9AA0);
  }
  brew.pause = pill_button(brew.layer, X, 370, 230, 70, 0xF2F2F2, 0x000000, LV_SYMBOL_PAUSE, "PAUSE",
                           &brew.pause_icon, &brew.pause_label, [](lv_event_t *) {
                             if (brew.on_pause) brew.on_pause();
                           });
  brew.stop = pill_button(brew.layer, X + 246, 370, 70, 70, 0x000000, 0xF2F2F2, LV_SYMBOL_STOP, nullptr, nullptr,
                          nullptr, [](lv_event_t *) {
                            if (brew.on_stop) brew.on_stop();
                          });
}

// ---- every frame ------------------------------------------------------------------------
void on_frame(lv_timer_t *) {
  for (int i = 0; i < n_animated; ++i) {
    lv_obj_t *o = animated[i], *layer = lv_obj_get_parent(o);
    if (!lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) && !(layer && lv_obj_has_flag(layer, LV_OBJ_FLAG_HIDDEN)))
      lv_obj_invalidate(o);
  }
  if (intro.on) step_intro();
  if (brew.on) {
    const float target = brew.v.total_ml > 0 ? clamp01(brew.v.water_ml / brew.v.total_ml) : 0;
    brew.level += (target - brew.level) * 0.08f;
  }
  if (finish.on) {  // the words and numbers fade in after the cup has filled
    const float t = (float)(millis() - field_of(finish.dots)->t0);
    lv_obj_set_style_opa(finish.title, (lv_opa_t)(ease(900, 1300, t) * 255), 0);
    lv_obj_set_style_opa(finish.name, (lv_opa_t)(ease(1000, 1400, t) * 255), 0);
    for (int i = 0; i < 3; ++i)
      for (lv_obj_t *o : finish.stats[i]) lv_obj_set_style_opa(o, (lv_opa_t)(ease(1150 + i * 120, 1550 + i * 120, t) * 255), 0);
  }
}

}  // namespace

// ---- public -----------------------------------------------------------------------------
void intro_play(const char *name, const char *sub) {
  build_intro_and_finish();
  set_label(intro.name, name);
  set_label(intro.sub, sub);
  lv_obj_align(intro.name, LV_ALIGN_TOP_MID, 0, 318);
  lv_obj_align(intro.sub, LV_ALIGN_TOP_MID, 0, 368);
  lv_obj_set_style_bg_opa(intro.layer, LV_OPA_COVER, 0);
  field_of(intro.dots)->fade = 1;
  lv_obj_set_style_opa(intro.name, LV_OPA_TRANSP, 0);
  lv_obj_set_style_opa(intro.sub, LV_OPA_TRANSP, 0);
  show(intro.layer);
  restart(intro.dots);
  intro.on = true;
}

void finish_play(const char *name, float grams, float ml, uint32_t ms) {
  build_intro_and_finish();
  intro.on = false;
  hide(intro.layer);
  brew_hide();
  set_label(finish.title, "Enjoy");
  set_label(finish.name, name);
  lv_obj_align(finish.title, LV_ALIGN_TOP_MID, 0, 290);
  lv_obj_align(finish.name, LV_ALIGN_TOP_MID, 0, 336);
  char value[3][16];
  snprintf(value[0], 16, "%.1f", grams);
  snprintf(value[1], 16, "%.0f", ml);
  snprintf(value[2], 16, "%lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
  static const char *const UNIT[3] = {"g", "ml", "min"};
  const bool ar = i18n::arabic_units();  // Arabic units: weight on the right, each unit left of its number
  const lv_font_t *big = &inter_34b, *small = &inter_20;
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *number = finish.stats[i][0], *unit = finish.stats[i][1];
    set_label(number, value[i]);
    set_label(unit, UNIT[i]);
    lv_obj_update_layout(number);
    lv_obj_update_layout(unit);
    // Each pair centred in its third of the screen, on one baseline.
    const int wn = lv_obj_get_width(number), wu = lv_obj_get_width(unit);
    const int centre = 160 + (ar ? 2 - i : i) * 240, x0 = centre - (wn + 6 + wu) / 2, baseline = 418;
    lv_obj_set_pos(number, ar ? x0 + wu + 6 : x0, baseline - (lv_font_get_line_height(big) - big->base_line));
    lv_obj_set_pos(unit, ar ? x0 : x0 + wn + 6, baseline - (lv_font_get_line_height(small) - small->base_line));
  }
  show(finish.layer);
  restart(finish.dots);
  finish.on = true;
}

void brew_show(const char *name, void (*on_pause)(), void (*on_stop)()) {
  build_intro_and_finish();
  build_brew();
  brew.on_pause = on_pause;
  brew.on_stop = on_stop;
  brew.level = 0;
  brew.v = {};
  set_label(brew.name, name);
  show(brew.layer);
  restart(brew.dots);
  brew.on = true;
}

void brew_update(const BrewView &v) {
  if (!brew.on) return;
  brew.v = v;
  set_label(brew.title, v.paused ? "Paused" : v.pour_title);
  set_label(brew.sub, v.pour_sub);
  char b[24];
  snprintf(b, sizeof b, "%.0f", v.water_ml);
  set_label(brew.water, b);
  snprintf(b, sizeof b, i18n::arabic_units() ? i18n::tr("/ %.0f ml") : "/ %.0f ml", v.total_ml);
  set_label(brew.water_of, b);
  number_and_unit(brew.water, brew.water_of, brew.TEXT_X, 236);
  snprintf(b, sizeof b, "%.1f", v.grams);
  set_label(brew.stat[0][0], b);
  set_label(brew.stat[0][1], "g");
  snprintf(b, sizeof b, "%lu:%02lu", (unsigned long)(v.ms / 60000), (unsigned long)(v.ms / 1000 % 60));
  set_label(brew.stat[1][0], b);
  set_label(brew.stat[1][1], "min");
  for (int i = 0; i < 2; ++i) number_and_unit(brew.stat[i][0], brew.stat[i][1], brew.TEXT_X + i * 170, 326);
  set_label(brew.pause_label, v.paused ? "RESUME" : "PAUSE");
  set_label(brew.pause_icon, v.paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

void brew_hide() {
  hide(brew.layer);
  brew.on = false;
}
bool brew_showing() { return brew.on; }

void finish_hide() {
  hide(finish.layer);
  finish.on = false;
}
bool finish_showing() { return finish.on; }
bool playing() { return intro.on; }

}  // namespace dotfx

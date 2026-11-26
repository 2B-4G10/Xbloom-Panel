// xBloom Panel screens (LVGL 9.1, 800x480), styled after the xBloom app.
//
//   Home tabs:  xBloom (Grinder / Brewer / Scale + favourites) · Recipes · Settings · Double Shot
//   Screens:    recipe detail and brewing, recipe editor, Grinder, Brewer, Scale,
//               Wi-Fi, text entry, software update, account, idle clock
//
// Everything here runs on the Arduino loop task; the Bluetooth, Wi-Fi and account
// tasks talk to it only through queues (see tick()).
//
// Layout follows one spacing system (ui_theme.h): a 36 px status bar, 24 px margins,
// 16 px gaps, 20 px corners, and numbers lined up on their text baselines.
#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ble_link.h"
#include "cloud.h"
#include "dotfx.h"
#include "emoji.h"
#include "i18n.h"
#include "panel_net.h"
#include "panel_storage.h"
#include "recipes.h"
#include "ui_theme.h"

using th::hex;
using namespace th;
using i18n::tr;
LV_FONT_DECLARE(ar_20);

namespace {

// ============================================================================
// State
// ============================================================================
// Brewing a recipe: Idle -> Loading (frames sent) -> Loaded (cup prompt) ->
// Starting -> Brewing <-> Paused -> Idle.
enum class Flow { Idle, Loading, Loaded, Starting, Brewing, Paused };

std::vector<PanelRecipe> lib;  // every recipe the panel lists (library.h); screens refer to them by uid
uint32_t next_uid = 1;
uint32_t cur = 0;              // uid on the recipe screen
uint32_t active = 0;           // uid loaded on / brewing in the machine; 0 = none
xb::Recipe active_copy;        // exactly what was sent to the machine
Flow flow = Flow::Idle;
LinkState link = LinkState::Idle;
char machine_name[32] = "XBLOOM";
float weight_g = 0, water_ml = 0;
float wake_ref_weight = 0;  // scale reading when the screen last woke for it
bool wake_req = false;      // ask main.cpp to wake the screen
int pour_idx = -1;          // pour in progress, -1 before the first
uint32_t brew_start_ms = 0;
uint32_t intro_for = 0;     // recipe the brew-start animation last played for

// Grinder, Brewer and Scale screens
int g_size = 36, g_rpm = 90;
bool grinding = false;
int b_volume = 250, b_temp = 85;  // temperature as shown: 39 means room temperature, then 40 to 99
float b_flow = 3.5f;
uint8_t b_pattern = xb::PAT_CIRCULAR;
bool pouring = false;
uint32_t pour_start_ms = 0;
int unit = 0;  // scale unit: 0 g, 1 oz, 2 ml
bool sw_running = false;
uint32_t sw_start = 0, sw_elapsed = 0;  // scale stopwatch

// Screens and the widgets that change after they are built
lv_obj_t *scr_home, *scr_detail, *scr_grinder, *scr_brewer, *scr_scale;
lv_obj_t *page_home, *page_recipes, *page_settings, *tab_lbl[4];
lv_obj_t *scr_store, *sb_bar;
lv_obj_t *home_name, *home_chev, *home_status, *home_spin, *home_dot, *fav_row;
lv_obj_t *seg_pill, *seg_lbl[3], *rec_list;
int seg = 0;  // Recipes tab: 0 Created, 1 Shared, 2 Starter
lv_obj_t *sb_bt, *sb_wifi, *sb_clock;
lv_obj_t *d_stats[3][2], *d_stat_unit[3], *d_start, *d_start_icon, *d_start_lbl, *d_stop, *modal;
struct Col {  // one pour's bar on the recipe screen
  lv_obj_t *bar, *fill;
  double vol, cum;  // its ml, and the ml poured before it
} d_cols[xb::MAX_POURS];
int d_ncols = 0;
lv_obj_t *gr_weight, *gr_play_icon, *gr_use, *br_weight, *br_timer, *br_play_icon;
lv_obj_t *sc_value, *sc_unit, *sc_timer, *sc_play_icon;
lv_obj_t *pat_btn[3], *unit_btn[3];
lv_obj_t *toast, *toast_lbl;
lv_timer_t *toast_timer;
lv_obj_t *st_acc_val, *st_lang_dd;

// Defined further down
lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg,
               lv_event_cb_t cb);
void open_editor(uint32_t uid);
void open_store(lv_event_t *);
void open_account(lv_event_t * = nullptr);
void toggle_favorite(uint32_t uid);
void build_detail();
void refresh_detail();
void refit(lv_obj_t *l);

// ============================================================================
// Text
// ============================================================================
// Every label goes through here: the text is translated, its emoji made drawable
// (include/emoji.h), given Arabic digits and
// units when chosen, and only handed to LVGL when it actually changed (each change
// redraws, and redraws share PSRAM bandwidth with the display; redrawing the clock
// and status bar several times a second caused line glitches).
void set_text(lv_obj_t *l, const char *t) {
  static char with_emoji[256], localized[256];
  t = i18n::localize(emoji::map(tr(t), with_emoji, sizeof with_emoji), localized, sizeof localized);
  const char *now = lv_label_get_text(l);
  if (now && t && strcmp(now, t) == 0) return;
  lv_label_set_text(l, t);
  if (lv_obj_get_user_data(l) && lv_obj_check_type(l, &lv_label_class)) refit(l);  // see fit_text()
}

// ============================================================================
// Right to left (Arabic)
// ============================================================================
// Screens are laid out in left-to-right coordinates. In Arabic each object is
// mirrored once, just before it is first drawn: its anchor moves to the other side
// and its x offset changes sign, so a label 24 px from the left edge ends up 24 px
// from the right and still grows the right way when its text changes. Labels, flex
// rows, switches and drop-downs follow the screens' RTL base direction themselves.
constexpr lv_obj_flag_t RTL_DONE = LV_OBJ_FLAG_USER_1;  // mirrored already
constexpr lv_obj_flag_t RTL_KEEP = LV_OBJ_FLAG_USER_2;  // leave as built (number + unit pairs, keyboard, scenes)
bool rtl = false;

lv_align_t mirror_align(lv_align_t a) {
  switch (a) {
    case LV_ALIGN_DEFAULT:
    case LV_ALIGN_TOP_LEFT: return LV_ALIGN_TOP_RIGHT;
    case LV_ALIGN_TOP_RIGHT: return LV_ALIGN_TOP_LEFT;
    case LV_ALIGN_LEFT_MID: return LV_ALIGN_RIGHT_MID;
    case LV_ALIGN_RIGHT_MID: return LV_ALIGN_LEFT_MID;
    case LV_ALIGN_BOTTOM_LEFT: return LV_ALIGN_BOTTOM_RIGHT;
    case LV_ALIGN_BOTTOM_RIGHT: return LV_ALIGN_BOTTOM_LEFT;
    default: return a;  // centred anchors stay put; only x flips
  }
}

bool plain_obj(const lv_obj_t *o) { return lv_obj_get_class(o) == &lv_obj_class; }

void swap_chevron(lv_obj_t *label) {
  const char *t = lv_label_get_text(label);
  if (!strcmp(t, LV_SYMBOL_LEFT)) lv_label_set_text(label, LV_SYMBOL_RIGHT);        // back
  else if (!strcmp(t, LV_SYMBOL_RIGHT)) lv_label_set_text(label, LV_SYMBOL_LEFT);   // "more"
}

void rtl_settle(lv_obj_t *o) {
  if (!rtl || lv_obj_has_flag(o, RTL_DONE) || lv_obj_has_flag(o, RTL_KEEP)) return;
  lv_obj_add_flag(o, RTL_DONE);
  lv_obj_t *parent = lv_obj_get_parent(o);
  if (parent && lv_obj_get_style_layout(parent, 0) == LV_LAYOUT_NONE) {  // flex rows reverse by themselves
    const lv_align_t a = (lv_align_t)lv_obj_get_style_align(o, 0), m = mirror_align(a);
    const int32_t x = lv_obj_get_style_x(o, 0);
    if (m != a) lv_obj_set_style_align(o, m, 0);
    if (x != 0 && !LV_COORD_IS_PCT(x)) lv_obj_set_style_x(o, -x, 0);
  }
  if (!lv_obj_check_type(o, &lv_label_class)) return;
  // Each label reads in the direction of its own text: Arabic right to left, while
  // "9:25" or "80ml" stay left to right (in an RTL paragraph they'd read "25:9", "ml80").
  lv_obj_set_style_base_dir(o, LV_BASE_DIR_AUTO, 0);
  const lv_text_align_t ta = (lv_text_align_t)lv_obj_get_style_text_align(o, 0);
  if (ta == LV_TEXT_ALIGN_LEFT) lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_RIGHT, 0);
  else if (ta == LV_TEXT_ALIGN_RIGHT) lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_LEFT, 0);
  swap_chevron(o);
}

// An object and everything in it. Only plain containers are entered: a widget's own
// parts (a text field's label, ...) are placed by the widget.
void rtl_tree(lv_obj_t *o) {
  if (lv_obj_has_flag(o, RTL_KEEP)) return;
  rtl_settle(o);
  if (!plain_obj(o)) return;
  for (uint32_t i = 0, n = lv_obj_get_child_count(o); i < n; ++i) rtl_tree(lv_obj_get_child(o, i));
}

// Runs before every redraw, so new objects are mirrored before anyone sees them.
void rtl_sweep(lv_event_t *) {
  rtl_tree(lv_screen_active());
  lv_obj_t *top = lv_layer_top();
  for (uint32_t i = 0, n = lv_obj_get_child_count(top); i < n; ++i) {
    lv_obj_t *child = lv_obj_get_child(top, i);
    if (plain_obj(child)) rtl_tree(child);  // drop-down lists place themselves
  }
}

// lv_obj_set_x for an object built with a left anchor; in Arabic, measured from the right.
void set_x_dir(lv_obj_t *o, int32_t x) {
  if (rtl) {
    rtl_settle(o);
    x = -x;
  }
  lv_obj_set_x(o, x);
}

// lv_obj_align_to beside `base`, with OUT_LEFT and OUT_RIGHT swapped in Arabic.
void align_beside(lv_obj_t *o, lv_obj_t *base, lv_align_t a, int32_t x, int32_t y) {
  if (rtl) {
    rtl_settle(base);
    rtl_settle(o);
    if (a == LV_ALIGN_OUT_RIGHT_MID) a = LV_ALIGN_OUT_LEFT_MID;
    else if (a == LV_ALIGN_OUT_LEFT_MID) a = LV_ALIGN_OUT_RIGHT_MID;
    x = -x;
  }
  lv_obj_align_to(o, base, a, x, y);
}

// ============================================================================
// Building blocks
// ============================================================================
// Style setters that skip the redraw when nothing changes.
void set_text_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel) {
  if (sel == 0 && lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) return;
  lv_obj_set_style_text_color(o, c, sel);
}
void set_opa(lv_obj_t *o, lv_opa_t v) {
  if (lv_obj_get_style_opa(o, LV_PART_MAIN) != v) lv_obj_set_style_opa(o, v, 0);
}
void show(lv_obj_t *o, bool on) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == !on) return;
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_color_t darker(uint32_t c, lv_opa_t amount) { return lv_color_darken(hex(c), amount); }

PanelRecipe *find(uint32_t uid) {
  for (PanelRecipe &p : lib)
    if (p.uid == uid) return &p;
  return nullptr;
}

// Which Recipes-tab segment a recipe belongs to.
int seg_of(const PanelRecipe &p) { return p.kind == KIND_SHARED ? 1 : p.kind == KIND_STARTER ? 2 : 0; }

// A plain object with no style, no scrolling and no taps.
lv_obj_t *bare(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

// A filled rectangle. Not tappable unless on_click() is called on it, so decoration
// never swallows a tap meant for the card underneath.
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius) {
  lv_obj_t *o = bare(parent, x, y, w, h);
  lv_obj_set_style_bg_color(o, hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_clip_corner(o, true, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// An invisible container.
lv_obj_t *group(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = bare(parent, x, y, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *text(lv_obj_t *parent, const char *t, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  set_text(l, t);
  lv_obj_set_style_text_font(l, font, 0);
  set_text_color(l, color, 0);
  return l;
}

// Puts a label so its text baseline sits `from_bottom` px above the parent's bottom edge.
void baseline_align(lv_obj_t *l, lv_align_t align, int x, int from_bottom) {
  lv_obj_align(l, align, x, lv_obj_get_style_text_font(l, 0)->base_line - from_bottom);
}

// Puts `l` left (or right) of `ref`, with the two text baselines lined up.
void baseline_next_to(lv_obj_t *l, lv_obj_t *ref, bool left, int gap) {
  if (rtl && !lv_obj_has_flag(l, RTL_KEEP)) {  // Arabic: the other side, of where `ref` really is
    rtl_settle(ref);
    rtl_settle(l);
    left = !left;
  }
  const lv_font_t *fl = lv_obj_get_style_text_font(l, 0), *fr = lv_obj_get_style_text_font(ref, 0);
  lv_obj_update_layout(ref);
  lv_obj_align_to(l, ref, left ? LV_ALIGN_OUT_LEFT_BOTTOM : LV_ALIGN_OUT_RIGHT_BOTTOM, left ? -gap : gap,
                  fl->base_line - fr->base_line);
}

// ---- labels that always show their whole text ------------------------------------
// The font steps down (26 -> 20 -> 16 -> 14 px) until the wrapped text fits the
// label's width and `max_h`. The label's user data keeps max_h and the starting
// size, and set_text() refits whenever the text changes.
const lv_font_t *const FIT_BOLD[] = {&inter_26b, &inter_20b, &inter_16, &inter_14};
const lv_font_t *const FIT_REGULAR[] = {&inter_20, &inter_16, &inter_14};

void refit(lv_obj_t *l) {
  const uintptr_t d = (uintptr_t)lv_obj_get_user_data(l);
  const int max_h = (int)(d >> 8), first = (int)(d & 0x7F) - 1;
  const bool regular = d & 0x80;
  const lv_font_t *const *steps = regular ? FIT_REGULAR : FIT_BOLD;
  const int n = regular ? 3 : 4, w = lv_obj_get_style_width(l, 0);
  const char *t = lv_label_get_text(l);
  for (int i = first; i < n; ++i) {
    lv_point_t size;
    lv_text_get_size(&size, t, steps[i], 0, 0, w, LV_TEXT_FLAG_NONE);
    if (size.y <= max_h || i == n - 1) {
      if (lv_obj_get_style_text_font(l, 0) != steps[i]) lv_obj_set_style_text_font(l, steps[i], 0);
      return;
    }
  }
}

void fit_text(lv_obj_t *l, int w, int max_h) {
  const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
  const bool regular = f == &inter_20;
  int first = 0;
  if (!regular)
    while (first < 3 && FIT_BOLD[first] != f) ++first;
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w);
  lv_obj_set_user_data(l, (void *)(((uintptr_t)max_h << 8) | (regular ? 0x80 : 0) | (uintptr_t)(first + 1)));
  refit(l);
}

// ---- taps -----------------------------------------------------------------------
void on_click(lv_obj_t *o, lv_event_cb_t cb, void *user = nullptr) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
}

// A light dim while pressed.
void pressable(lv_obj_t *o) { lv_obj_set_style_opa(o, LV_OPA_70, LV_STATE_PRESSED); }

// ---- odds and ends -----------------------------------------------------------------
void post(BleCmdType type, int a = 0, int b = 0, double x = 0, double y = 0, double z = 0) {
  BleCmd c;
  c.type = type;
  c.a = a;
  c.b = b;
  c.x = x;
  c.y = y;
  c.z = z;
  ble_link::post(c);
}

// 00'00'' (minutes and seconds, as the xBloom shows a timer)
void fmt_time(char *buf, size_t n, uint32_t ms) {
  const unsigned long s = ms / 1000;
  snprintf(buf, n, "%02lu'%02lu''", s / 60, s % 60);
}

void show_toast(const char *msg) {
  set_text(toast_lbl, msg);
  show(toast, true);
  lv_obj_move_foreground(toast);
  lv_timer_reset(toast_timer);
  lv_timer_resume(toast_timer);
  wake_req = true;
}

lv_obj_t *new_screen(uint32_t bg) {
  lv_obj_t *s = lv_obj_create(nullptr);
  if (rtl) lv_obj_set_style_base_dir(s, LV_BASE_DIR_RTL, 0);
  lv_obj_set_style_bg_color(s, hex(bg), 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// How a screen is entered. The panel's software renderer can't slide 800x480 pixels
// smoothly, so every screen switches instantly; the kind is kept for the call sites.
enum class Anim { None, PushLeft, PopRight, RiseUp, DropDown };
void load(lv_obj_t *scr, Anim) { lv_screen_load(scr); }

// ============================================================================
// Icons
// ============================================================================
// The machine's 5x5 dot icons: grinder (staggered), brewer (drop), scale (square).
enum class Dots { Grinder, Brewer, Scale };
lv_obj_t *dot_icon(lv_obj_t *parent, Dots kind, lv_color_t color, int dot, int gap) {
  static const char *const GRINDER[] = {"1.1.1", ".1.1.", "1.1.1", ".1.1.", "1.1.1"};
  static const char *const BREWER[] = {"..1..", ".111.", "11111", "11111", ".111."};
  static const char *const SCALE[] = {"11111", "11111", "11111", "11111", "11111"};
  const char *const *rows = kind == Dots::Grinder ? GRINDER : kind == Dots::Brewer ? BREWER : SCALE;
  const int step = dot + gap, size = 5 * step - gap;
  lv_obj_t *icon = group(parent, 0, 0, size, size);
  for (int r = 0; r < 5; ++r)
    for (int c = 0; c < 5; ++c)
      if (rows[r][c] == '1') lv_obj_set_style_bg_color(box(icon, c * step, r * step, dot, dot, 0, LV_RADIUS_CIRCLE), color, 0);
  return icon;
}

// An arc from a0 to a1 degrees.
lv_obj_t *ring(lv_obj_t *parent, int size, int width, lv_color_t color, int a0 = 0, int a1 = 360) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_angles(a, a0, a1);
  return a;
}

lv_obj_t *dot(lv_obj_t *parent, int d, lv_color_t color) {
  lv_obj_t *o = box(parent, 0, 0, d, d, 0, LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_center(o);
  return o;
}

// Pour patterns: centred = a dot in a faint ring, circular = a ring in four
// pieces, spiral = two open rings around a dot.
lv_obj_t *pattern_icon(lv_obj_t *parent, uint8_t pattern, int size, lv_color_t color) {
  lv_obj_t *icon = group(parent, 0, 0, size, size);
  const int w = size > 24 ? 4 : 2;
  if (pattern == xb::PAT_CENTERED) {
    lv_obj_center(ring(icon, size, w, lv_color_darken(color, 120)));
    dot(icon, size / 2, color);
  } else if (pattern == xb::PAT_CIRCULAR) {
    for (int q = 0; q < 4; ++q) lv_obj_center(ring(icon, size, w, color, q * 90 + 12, q * 90 + 78));
  } else {
    lv_obj_center(ring(icon, size, w, color, 0, 300));
    lv_obj_center(ring(icon, size * 6 / 10, w, color, 60, 360));
    dot(icon, w + 2, color);
  }
  return icon;
}

// Cup: the Omni dripper is a squat cup, everything else a dripper cone.
lv_obj_t *cup_icon(lv_obj_t *parent, int cup, lv_color_t color) {
  lv_obj_t *icon = group(parent, 0, 0, 22, 20);
  if (cup == recipes::CUP_OMNI) {
    lv_obj_set_style_bg_color(box(icon, 0, 4, 22, 4, 0, 2), color, 0);
    lv_obj_set_style_bg_color(box(icon, 3, 0, 16, 18, 0, 5), color, 0);
    return icon;
  }
  static lv_point_precise_t cone[] = {{2, 2}, {20, 2}, {11, 18}, {2, 2}};
  lv_obj_t *l = lv_line_create(icon);
  lv_line_set_points(l, cone, 4);
  lv_obj_set_style_line_width(l, 5, 0);
  lv_obj_set_style_line_color(l, color, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return icon;
}

// Heart: a square turned 45 degrees with a circle on each upper edge.
lv_obj_t *heart_icon(lv_obj_t *parent, uint32_t color) {
  lv_obj_t *icon = group(parent, 0, 0, 24, 22);
  lv_obj_t *point = box(icon, 6, 6, 12, 12, color, 1);
  lv_obj_set_style_transform_rotation(point, 450, 0);
  lv_obj_set_style_transform_pivot_x(point, 6, 0);
  lv_obj_set_style_transform_pivot_y(point, 6, 0);
  lv_obj_set_style_clip_corner(point, false, 0);
  box(icon, 1, 2, 12, 12, color, LV_RADIUS_CIRCLE);
  box(icon, 11, 2, 12, 12, color, LV_RADIUS_CIRCLE);
  return icon;
}
void heart_color(lv_obj_t *heart, uint32_t color) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(heart); ++i)
    lv_obj_set_style_bg_color(lv_obj_get_child(heart, i), hex(color), 0);
}

// ============================================================================
// Recipe pieces
// ============================================================================
// A pour's name as shown: its own, or "Bloom" / "Pour N" in the panel's language.
// (library::pour_name() is the English default that gets saved with the recipe.)
void shown_pour_name(const PanelRecipe &p, int i, char *out, size_t n) {
  if (p.pour_names[i][0]) strlcpy(out, p.pour_names[i], n);
  else if (i == 0) strlcpy(out, tr("Bloom"), n);
  else snprintf(out, n, tr("Pour %d"), i + 1);
}

double ratio_of(const xb::Recipe &r) { return round(r.total_water() / r.dose_g * 10) / 10; }

void recipe_line(const xb::Recipe &r, char *buf, size_t n) {
  snprintf(buf, n, tr("1:%g-%.0fml   |   %gg   |   %d Pours"), ratio_of(r), r.total_water(), r.dose_g, r.n_pours);
}

// A pastel recipe tile, as in the app: the name (or cup) top left, the cup bottom
// left, and the pour count large in the bottom-right corner, running off the edge
// on small tiles.
lv_obj_t *recipe_tile(lv_obj_t *parent, int x, int y, int w, int h, const PanelRecipe &p, bool show_name) {
  const uint32_t bg = p.color;
  const lv_color_t ink = darker(bg, 70);
  lv_obj_t *tile = box(parent, x, y, w, h, bg, R_SM);
  char count[4];
  snprintf(count, sizeof count, "%d", p.r.n_pours);
  lv_obj_t *big = text(tile, count, &inter_64b, lv_color_darken(hex(bg), 36));
  baseline_align(big, LV_ALIGN_BOTTOM_RIGHT, -6, h < 90 ? -4 : 2);
  lv_obj_t *top = text(tile, show_name ? p.r.name : recipes::cup_name(p.r.cup_type), show_name ? &inter_20b : &inter_16,
                       ink);
  if (show_name) {
    fit_text(top, w - 24, 2 * lv_font_get_line_height(&inter_20b));
  } else {
    lv_label_set_long_mode(top, LV_LABEL_LONG_DOT);
    lv_obj_set_width(top, w - 24);
  }
  lv_obj_set_pos(top, 12, 10);
  lv_obj_align(cup_icon(tile, p.r.cup_type, ink), LV_ALIGN_BOTTOM_LEFT, 12, -12);
  lv_obj_set_user_data(tile, (void *)(uintptr_t)p.uid);
  return tile;
}

// ============================================================================
// Value cards (Grinder and Brewer): drag across the card to set the value
// ============================================================================
// Name top left; value and unit bottom right on one baseline; a fill behind shows
// where the value sits in its range.
struct ValueCard {
  lv_obj_t *card, *fill, *value, *unit;
  double min, max, step, v;
  const char *fmt;
  void (*changed)(double v, bool released);
};
ValueCard cards[5];
int n_cards = 0;
const int CARD_BASELINE = 18;  // px from the card's bottom edge to the value's baseline

void card_render(ValueCard &c) {
  const int inner = lv_obj_get_width(c.card) - 16;
  const double share = (c.v - c.min) / (c.max - c.min);
  lv_obj_set_width(c.fill, (int)(inner * fmax(0.04, fmin(1.0, share))));
  char buf[16];
  snprintf(buf, sizeof buf, c.fmt, c.v);
  set_text(c.value, buf);
}

void card_event(lv_event_t *e) {
  ValueCard &c = *(ValueCard *)lv_event_get_user_data(e);
  lv_point_t touch;
  lv_indev_get_point(lv_indev_active(), &touch);
  lv_area_t area;
  lv_obj_get_coords(c.card, &area);
  double share = fmax(0, fmin(1, (double)(touch.x - area.x1 - 8) / (lv_area_get_width(&area) - 16)));
  if (rtl) share = 1 - share;  // the fill grows from the right
  const double v = c.min + round(share * (c.max - c.min) / c.step) * c.step;
  const bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
  if (v == c.v && !released) return;
  c.v = v;
  card_render(c);
  if (c.changed) c.changed(v, released);
}

ValueCard *value_card(lv_obj_t *parent, int x, int y, int w, int h, const char *name, const char *unit,
                      uint32_t accent, uint32_t fill, const lv_font_t *value_font, double min, double max,
                      double step, double v, const char *fmt, void (*changed)(double, bool)) {
  ValueCard &c = cards[n_cards++];
  c.card = box(parent, x, y, w, h, BG_MOD, R);
  lv_obj_set_style_border_color(c.card, hex(accent), 0);
  lv_obj_set_style_border_width(c.card, 2, 0);
  c.fill = box(c.card, 6, 6, 10, h - 12, fill, R - 6);
  lv_obj_set_pos(text(c.card, name, &inter_20, hex(SUB)), 20, 14);
  c.unit = text(c.card, unit, &inter_20, hex(SUB));
  baseline_align(c.unit, LV_ALIGN_BOTTOM_RIGHT, -20, CARD_BASELINE);
  lv_obj_update_layout(c.unit);
  c.value = text(c.card, "", value_font, hex(accent));
  baseline_align(c.value, LV_ALIGN_BOTTOM_RIGHT, -(20 + lv_obj_get_width(c.unit) + 8), CARD_BASELINE);
  c.min = min;
  c.max = max;
  c.step = step;
  c.v = v;
  c.fmt = fmt;
  c.changed = changed;
  lv_obj_add_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c.card, card_event, LV_EVENT_PRESSING, &c);
  lv_obj_add_event_cb(c.card, card_event, LV_EVENT_RELEASED, &c);
  lv_obj_update_layout(c.card);
  card_render(c);
  return &c;
}

// ============================================================================
// Buttons and headers
// ============================================================================
lv_obj_t *round_button(lv_obj_t *parent, int x, int y, int d, const char *symbol, const lv_font_t *font,
                       lv_obj_t **icon_out, lv_event_cb_t cb) {
  lv_obj_t *b = box(parent, x, y, d, d, 0x262626, LV_RADIUS_CIRCLE);
  lv_obj_t *icon = text(b, symbol, font, hex(0x9A9A9A));
  lv_obj_center(icon);
  if (icon_out) *icon_out = icon;
  on_click(b, cb);
  pressable(b);
  return b;
}

// Pour-pattern and scale-unit choices; the chosen one is tinted by its screen.
lv_obj_t *option_button(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *b = box(parent, x, y, w, h, SURFACE, R_SM);
  pressable(b);
  return b;
}

// Back control at the top left: a 64x52 tap area around the chevron.
lv_obj_t *back_button(lv_obj_t *scr, const char *symbol, lv_event_cb_t cb) {
  lv_obj_t *b = group(scr, M - 12, SB, 64, 52);
  lv_obj_align(text(b, symbol, &lv_font_montserrat_28, hex(TEXT)), LV_ALIGN_LEFT_MID, 12, 0);
  on_click(b, cb);
  pressable(b);
  return b;
}

// Grinder / Brewer / Scale header: a down chevron to go back, the module's icon centred.
void module_header(lv_obj_t *scr, Dots kind, lv_event_cb_t back) {
  back_button(scr, LV_SYMBOL_DOWN, back);
  lv_obj_align(dot_icon(scr, kind, hex(TEXT), 6, 4), LV_ALIGN_TOP_MID, 0, SB + 4);
}

// The big reading under a module header (weight, timer); optionally right-aligned at x.
lv_obj_t *readout(lv_obj_t *scr, const char *t, int x, bool right_aligned_to_x = false) {
  lv_obj_t *l = text(scr, t, &inter_34b, hex(TEXT));
  if (right_aligned_to_x) {
    lv_obj_set_width(l, 200);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    x -= 200;
  }
  lv_obj_set_pos(l, x, 96);
  return l;
}

// ============================================================================
// Navigation
// ============================================================================
void go_home_from_detail(lv_event_t * = nullptr) { load(scr_home, Anim::PopRight); }
void go_home_from_module() { load(scr_home, Anim::DropDown); }

void set_tab(int t) {
  show(page_home, t == 0);
  show(page_recipes, t == 1);
  show(page_settings, t == 2);
  for (int i = 0; i < 3; ++i) set_text_color(tab_lbl[i], hex(i == t ? TEXT : MUTED), 0);
}

void open_recipe(uint32_t uid, Anim how = Anim::PushLeft) {
  if (!find(uid)) return;
  cur = uid;
  build_detail();
  load(scr_detail, how);
}

void on_recipe_tile(lv_event_t *e) {
  open_recipe((uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_current_target(e)));
}

// ============================================================================
// Status bar: clock in the middle, Wi-Fi and Bluetooth on the right. It lives on
// the top layer, so every screen shares it.
// ============================================================================
void build_status_bar() {
  sb_bar = group(lv_layer_top(), 0, 0, W, SB);
  sb_clock = text(sb_bar, "", &inter_16, hex(TEXT));
  lv_obj_align(sb_clock, LV_ALIGN_CENTER, 0, 2);  // centred: the machine name takes the left on Home
  sb_bt = text(sb_bar, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_16, hex(MUTED));
  lv_obj_align(sb_bt, LV_ALIGN_RIGHT_MID, -M, 2);
  sb_wifi = text(sb_bar, LV_SYMBOL_WIFI, &lv_font_montserrat_16, hex(MUTED));
  lv_obj_align_to(sb_wifi, sb_bt, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  show(sb_wifi, false);  // appears once a network is set up
}

// ============================================================================
// Home tab: the machine, the three module tiles and the favourites
// ============================================================================
// Opening a module also moves the machine to its matching screen.
void open_grinder(lv_event_t *) {
  load(scr_grinder, Anim::RiseUp);
  post(BleCmdType::GrinderEnter, g_size, g_rpm);
}
void open_brewer(lv_event_t *) {
  load(scr_brewer, Anim::RiseUp);
  post(BleCmdType::BrewerEnter, xb::pattern_to_byte(b_pattern), 0, xb::temp_display_to_wire(b_temp));
}
void open_scale(lv_event_t *) {
  load(scr_scale, Anim::RiseUp);
  post(BleCmdType::ScaleEnter);
}

const int FAV_W = 160, FAV_H = 100;

void build_favorites() {
  lv_obj_clean(fav_row);
  int x = 0;
  for (const PanelRecipe &p : lib) {
    if (!p.favorite) continue;
    lv_obj_t *tile = recipe_tile(fav_row, x, 0, FAV_W, FAV_H, p, true);
    on_click(tile, on_recipe_tile);
    pressable(tile);
    x += FAV_W + G;
  }
  if (x == 0) lv_obj_align(text(fav_row, "No favorites yet", &inter_20, hex(0xBDB4AC)), LV_ALIGN_LEFT_MID, 0, 0);
}

void build_home_page(lv_obj_t *page) {
  // Machine name and link state share the status-bar row; set_link() lays them out.
  home_name = text(page, "XBLOOM", &inter_20b, hex(TEXT));
  lv_obj_set_pos(home_name, M, 6);
  home_chev = text(page, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, hex(TEXT));
  home_spin = lv_spinner_create(page);
  lv_obj_set_size(home_spin, 16, 16);
  lv_obj_remove_flag(home_spin, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(home_spin, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(home_spin, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(home_spin, hex(HAIRLINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(home_spin, hex(GREEN), LV_PART_INDICATOR);
  home_dot = box(page, 0, 0, 10, 10, GREEN, LV_RADIUS_CIRCLE);
  home_status = text(page, "Connecting", &inter_16, hex(MUTED));

  // Module tiles: as tall as the favourites panel below leaves room for.
  const int fav_h = 168, fav_y = H - TABBAR_H - G - fav_h;
  const int tile_y = SB + 8, tile_h = fav_y - G - tile_y, tile_w = (W - 2 * M - 2 * G) / 3;
  struct Module {
    const char *name;
    Dots icon;
    uint32_t bg, fg;
    lv_event_cb_t open;
  };
  const Module modules[3] = {
      {"Grinder", Dots::Grinder, GRINDER_BG, GRINDER_FG, open_grinder},
      {"Brewer", Dots::Brewer, BREWER_BG, BREWER_FG, open_brewer},
      {"Scale", Dots::Scale, SCALE_BG, SCALE_FG, open_scale},
  };
  for (int i = 0; i < 3; ++i) {
    const Module &m = modules[i];
    lv_obj_t *tile = box(page, M + i * (tile_w + G), tile_y, tile_w, tile_h, m.bg, R);
    lv_obj_align(dot_icon(tile, m.icon, hex(m.fg), 7, 5), LV_ALIGN_TOP_MID, 0, 32);
    baseline_align(text(tile, m.name, &inter_26b, hex(m.fg)), LV_ALIGN_BOTTOM_MID, 0, 32);
    on_click(tile, m.open);
    pressable(tile);
  }

  // Favourites: a row of tiles that scrolls sideways.
  lv_obj_t *panel = box(page, M, fav_y, W - 2 * M, fav_h, FAV_PANEL, R);
  lv_obj_set_pos(text(panel, "Favorite Recipes", &inter_20b, hex(TEXT)), 20, 16);
  fav_row = bare(panel, 20, fav_h - 20 - FAV_H, W - 2 * M - 20, FAV_H);
  lv_obj_set_scroll_dir(fav_row, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(fav_row, LV_SCROLLBAR_MODE_OFF);
  build_favorites();
}

// ============================================================================
// Recipes tab: Created / Shared / Starter, then the list
// ============================================================================
const int ROW_H = 88, TILE = 72;

void build_recipe_list() {
  lv_obj_clean(rec_list);
  const int name_w = W - 2 * M - TILE - 80, line_h16 = lv_font_get_line_height(&inter_16);
  int y = 0;
  for (const PanelRecipe &p : lib) {
    if (seg_of(p) != seg) continue;
    lv_obj_t *row = group(rec_list, 0, y, W - 2 * M, ROW_H);
    lv_obj_set_user_data(row, (void *)(uintptr_t)p.uid);
    on_click(row, on_recipe_tile);
    pressable(row);
    recipe_tile(row, 0, (ROW_H - TILE) / 2, TILE, TILE, p, false);
    lv_obj_t *name = text(row, p.r.name, &inter_20b, hex(TEXT));
    fit_text(name, name_w, 2 * line_h16);
    lv_obj_set_pos(name, TILE + 20, 18);
    char summary[64];
    recipe_line(p.r, summary, sizeof summary);
    lv_obj_t *line = text(row, summary, &inter_16, hex(MUTED));
    lv_obj_set_pos(line, TILE + 20, 48);
    lv_obj_update_layout(name);
    const int name_h = lv_obj_get_height(name);
    if (name_h > lv_font_get_line_height(&inter_20b)) {  // a long name: centre name and summary together
      const int top = (ROW_H - (name_h + 4 + line_h16)) / 2;
      lv_obj_set_y(name, top);
      lv_obj_set_y(line, top + name_h + 4);
    }
    lv_obj_align(text(row, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, hex(0x48484A)), LV_ALIGN_RIGHT_MID, -4, 0);
    if (p.dirty && p.kind != KIND_SEED && cloud::state() == cloud::Acct::SignedIn)
      lv_obj_align(text(row, "Not saved to account", &inter_14, hex(0xE0A458)), LV_ALIGN_RIGHT_MID, -28, 0);
    y += ROW_H + 8;
  }
  if (y == 0) {
    const char *hint = seg == 1   ? "Recipes saved from share links appear here."
                       : seg == 0 ? "Sign in under Settings to bring in your recipes."
                                  : "";
    lv_obj_set_pos(text(rec_list, hint, &inter_20, hex(MUTED)), 4, 16);
  }
}

void set_seg(int s) {
  seg = s;
  set_x_dir(seg_pill, 4 + s * ((W - 2 * M - 8) / 3));
  for (int i = 0; i < 3; ++i) set_text_color(seg_lbl[i], hex(i == s ? 0x000000 : MUTED), 0);
  build_recipe_list();
}

void build_recipes_page(lv_obj_t *page) {
  lv_obj_set_pos(text(page, "My Recipes", &inter_34b, hex(TEXT)), M, SB + 2);
  pill(page, W - M - 120, SB + 4, 120, 44, "+  New", 0x2C2C2E, TEXT, [](lv_event_t *) { open_editor(0); });

  // Segmented control: a white pill slides under the chosen segment.
  const int seg_y = 94, seg_h = 48, third = (W - 2 * M - 8) / 3;
  lv_obj_t *segments = box(page, M, seg_y, W - 2 * M, seg_h, SEG_BG, seg_h / 2);
  seg_pill = box(segments, 4, 4, third, seg_h - 8, 0xFFFFFF, (seg_h - 8) / 2);
  static const char *const NAMES[3] = {"Created", "Shared", "Starter"};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *hit = group(segments, 4 + i * third, 0, third, seg_h);
    seg_lbl[i] = text(hit, NAMES[i], &inter_20b, hex(MUTED));
    lv_obj_center(seg_lbl[i]);
    on_click(hit, [](lv_event_t *e) { set_seg((int)(intptr_t)lv_event_get_user_data(e)); }, (void *)(intptr_t)i);
  }

  const int list_y = seg_y + seg_h + G;
  rec_list = bare(page, M, list_y, W - 2 * M, H - TABBAR_H - list_y);
  lv_obj_set_scroll_dir(rec_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(rec_list, LV_SCROLLBAR_MODE_OFF);
  set_seg(0);
}

// ============================================================================
// Settings tab, and the Wi-Fi, text-entry and software-update screens
// ============================================================================
lv_obj_t *scr_wifi, *scr_pass, *scr_update;
lv_obj_t *st_wifi_val, *st_ver_val, *st_machine_val, *sw_clock, *sw_night, *st_sync_val, *st_sync_btn, *st_sleep_dd,
    *st_ccolor_dd;
lv_obj_t *wf_list, *wf_status_title, *wf_status_sub, *wf_forget, *wf_scan_lbl;
lv_obj_t *pw_title, *pw_ta, *pw_show_lbl;
lv_obj_t *up_steps, *up_url, *up_ip, *up_bar, *up_status, *up_wifi_btn, *up_online, *up_action, *up_action_lbl;
extern lv_obj_t *up_mode_dd;
char pw_ssid[33];
bool wf_scanning_shown = false;
net::Wifi last_wifi = (net::Wifi)255;  // nothing shown yet

void refresh_screen_prefs();
void refresh_sync_row();
void open_wifi(lv_event_t * = nullptr);
void open_update(lv_event_t * = nullptr);
void format_clock(const struct tm &t, char *hm, size_t n);

// Sleep-after choices, in drop-down order (minutes; 0 = never).
const int SLEEP_CHOICES[] = {1, 3, 5, 10, 0};

// ---- settings building blocks ----------------------------------------------------
// A titled card of rows, iOS style.
lv_obj_t *section(lv_obj_t *parent, const char *title, int x, int y, int w, int rows, int row_h) {
  lv_obj_set_pos(text(parent, title, &inter_14, hex(MUTED)), x + 4, y);
  return box(parent, x, y + 24, w, rows * row_h, 0x1C1C1E, R_SM);
}

// One row: its name on the left; a value (and a chevron when tappable) on the right.
lv_obj_t *row(lv_obj_t *card, int i, int h, const char *label, lv_obj_t **value_out, lv_event_cb_t on_tap) {
  const int w = lv_obj_get_style_width(card, 0);
  lv_obj_t *r = group(card, 0, i * h, w, h);
  if (i > 0) box(r, 16, 0, w - 16, 1, HAIRLINE, 0);  // divider
  lv_obj_t *name = text(r, label, &inter_20, hex(TEXT));
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);
  int value_right = -16;
  if (on_tap) {
    lv_obj_align(text(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, hex(0x48484A)), LV_ALIGN_RIGHT_MID, -16, 0);
    value_right = -40;
    on_click(r, on_tap);
    lv_obj_set_style_bg_color(r, hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  }
  if (value_out) {  // the value gets all the width the name leaves
    *value_out = text(r, "", &inter_20, hex(MUTED));
    lv_obj_update_layout(name);
    fit_text(*value_out, w - 16 - lv_obj_get_width(name) - 24 + value_right, h - 8);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(*value_out, LV_ALIGN_RIGHT_MID, value_right, 0);
  }
  return r;
}

lv_obj_t *toggle(lv_obj_t *in_row, bool on, lv_event_cb_t changed) {
  lv_obj_t *s = lv_switch_create(in_row);
  lv_obj_set_size(s, 56, 32);
  lv_obj_align(s, LV_ALIGN_RIGHT_MID, -16, 0);
  lv_obj_set_style_bg_color(s, hex(0x39393D), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, hex(GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(s, hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, -3, LV_PART_KNOB);
  if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s, changed, LV_EVENT_VALUE_CHANGED, nullptr);
  return s;
}

bool switched_on(lv_event_t *e) { return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); }
int chosen(lv_event_t *e) { return (int)lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e)); }

// A compact dark drop-down at the right of a row.
lv_obj_t *dropdown(lv_obj_t *in_row, const char *options, lv_event_cb_t changed) {
  lv_obj_t *d = lv_dropdown_create(in_row);
  lv_dropdown_set_options(d, tr(options));  // copies and joins Arabic letters (the static version does not)
  lv_obj_set_size(d, 140, 38);
  lv_obj_align(d, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_set_style_bg_color(d, hex(0x2C2C2E), 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_set_style_radius(d, 10, 0);
  lv_obj_set_style_text_color(d, hex(TEXT), 0);
  lv_obj_set_style_text_font(d, &inter_16, 0);
  lv_obj_set_style_pad_hor(d, 12, 0);
  lv_obj_set_style_pad_ver(d, 9, 0);
  lv_obj_set_style_text_font(d, &lv_font_montserrat_14, LV_PART_INDICATOR);  // the arrow is an icon
  lv_obj_set_style_text_color(d, hex(MUTED), LV_PART_INDICATOR);
  lv_obj_t *list = lv_dropdown_get_list(d);
  lv_obj_set_style_bg_color(list, hex(0x2C2C2E), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 12, 0);
  lv_obj_set_style_text_color(list, hex(TEXT), 0);
  lv_obj_set_style_text_font(list, &inter_20, 0);
  lv_obj_set_style_text_line_space(list, 18, 0);
  lv_obj_set_style_pad_all(list, 10, 0);
  lv_obj_set_style_max_height(list, 300, 0);
  lv_obj_set_style_bg_color(list, hex(0x48484A), LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(list, hex(0x3A3A3C), LV_PART_SELECTED | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(list, hex(TEXT), LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_add_event_cb(d, changed, LV_EVENT_VALUE_CHANGED, nullptr);
  return d;
}

// A rounded button with a centred label.
lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg,
               lv_event_cb_t on_tap) {
  lv_obj_t *b = box(parent, x, y, w, h, bg, h / 2);
  lv_obj_center(text(b, label, &inter_20b, hex(fg)));
  on_click(b, on_tap);
  pressable(b);
  return b;
}

// Sub-screen header: back chevron and title in one row.
void sub_header(lv_obj_t *scr, const char *title, lv_event_cb_t back, lv_obj_t **title_out = nullptr) {
  back_button(scr, LV_SYMBOL_LEFT, back);
  lv_obj_t *t = text(scr, title, &inter_26b, hex(TEXT));
  fit_text(t, W - 2 * M - 120, 44);
  lv_obj_set_pos(t, M + 44, SB + 10);
  if (title_out) *title_out = t;
}

void restart_panel() {
#ifndef UI_SIM
  ESP.restart();
#endif
}
void back_to_settings(lv_event_t *) { load(scr_home, Anim::PopRight); }

// ---- the Settings tab ------------------------------------------------------------
// Two columns: Connection | Screen, then About | Language.
void build_settings_page(lv_obj_t *page) {
  lv_obj_set_pos(text(page, "Settings", &inter_34b, hex(TEXT)), M, SB + 2);
  const int rh = 52, col_w = (W - 2 * M - G) / 2, y0 = 84, x2 = M + col_w + G, y1 = y0 + 24 + 4 * rh + G;

  lv_obj_t *connection = section(page, "CONNECTION", M, y0, col_w, 4, rh);
  row(connection, 0, rh, "Wi-Fi", &st_wifi_val, open_wifi);
  row(connection, 1, rh, "Software update", &st_ver_val, open_update);
  row(connection, 2, rh, "xBloom account", &st_acc_val, open_account);
  lv_obj_t *recipes_row = row(connection, 3, rh, "Recipes", nullptr, nullptr);  // Sync now + when it last synced
  st_sync_btn = pill(recipes_row, col_w - 16 - 120, (rh - 36) / 2, 120, 36, "Sync now", 0xFFFFFF, 0x000000,
                     [](lv_event_t *) {
                       if (cloud::state() != cloud::Acct::SignedIn) return open_account();
                       cloud::sync_now();
                       refresh_sync_row();
                     });
  st_sync_val = text(recipes_row, "", &inter_16, hex(MUTED));
  lv_obj_align(st_sync_val, LV_ALIGN_RIGHT_MID, -(16 + 120 + 12), 0);
  set_text(st_ver_val, FW_VERSION);

  lv_obj_t *screen = section(page, "SCREEN", x2, y0, col_w, 4, rh);
  sw_clock = toggle(row(screen, 0, rh, "Clock when idle", nullptr, nullptr), net::pref_clock(),
                    [](lv_event_t *e) { net::set_pref_clock(switched_on(e)); });
  st_sleep_dd = dropdown(row(screen, 1, rh, "Sleep after", nullptr, nullptr), "1 min\n3 min\n5 min\n10 min\nNever",
                         [](lv_event_t *e) { net::set_pref_sleep_min(SLEEP_CHOICES[chosen(e) % 5]); });
  st_ccolor_dd = dropdown(row(screen, 2, rh, "Clock colour", nullptr, nullptr), "White\nBlue\nBrown",
                          [](lv_event_t *e) { net::set_pref_clock_color(chosen(e) % 3); });
  sw_night = toggle(row(screen, 3, rh, "Off at night (11 PM-6 AM)", nullptr, nullptr), net::pref_night_off(),
                    [](lv_event_t *e) { net::set_pref_night_off(switched_on(e)); });
  refresh_screen_prefs();

  lv_obj_t *about = section(page, "ABOUT", M, y1, col_w, 1, rh);
  row(about, 0, rh, "Machine", &st_machine_val, nullptr);
  set_text(st_machine_val, "Not connected");

  // 0 English · 1 Arabic with Arabic numbers and units ("٨٠ مل") · 2 Arabic with English units ("80 ml").
  // A change is saved and the panel restarts, to build every screen in the new language.
  lv_obj_t *language = section(page, "LANGUAGE", x2, y1, col_w, 1, rh);
  st_lang_dd = dropdown(row(language, 0, rh, "Language", nullptr, nullptr),
                        "English\n"
                        "العربية · وحدات عربية\n"
                        "العربية · وحدات إنجليزية",
                        [](lv_event_t *e) {
                          const int lang = chosen(e);
                          if (lang == net::pref_lang()) return;
                          net::set_pref_lang(lang);
                          show_toast(lang >= 1 ? "جارٍ إعادة التشغيل..." : "Restarting...");
                          lv_timer_set_repeat_count(lv_timer_create([](lv_timer_t *) { restart_panel(); }, 1200, nullptr), 1);
                        });
  lv_obj_set_width(st_lang_dd, 240);
  lv_dropdown_set_selected(st_lang_dd, (uint32_t)LV_CLAMP(0, net::pref_lang(), 2));
}

void refresh_screen_prefs() {
  const int m = net::pref_sleep_min();
  for (int i = 0; i < 5; ++i)
    if (SLEEP_CHOICES[i] == m) lv_dropdown_set_selected(st_sleep_dd, i);
  lv_dropdown_set_selected(st_ccolor_dd, net::pref_clock_color() % 3);
}
void refresh_sync_row() {
  if (!st_sync_val) return;
  if (cloud::state() != cloud::Acct::SignedIn) return set_text(st_sync_val, "Not signed in");
  if (cloud::busy()) return set_text(st_sync_val, "Syncing...");
  const time_t synced = cloud::last_sync();
  if (!synced) return set_text(st_sync_val, "");
  struct tm t;
  localtime_r(&synced, &t);
  char hm[12], line[24];
  format_clock(t, hm, sizeof hm);
  snprintf(line, sizeof line, tr("Synced %s"), hm);
  set_text(st_sync_val, line);
}

// ---- Wi-Fi screen: networks on the left, the current one on the right -------------
void open_password(const char *ssid);

net::Ap wf_aps[24];
int wf_n = 0;

void on_network(lv_event_t *e) {
  const net::Ap *ap = (const net::Ap *)lv_event_get_user_data(e);
  if (ap->secure) open_password(ap->ssid);
  else net::join(ap->ssid, "");
}

// Signal strength as the icon's brightness.
uint32_t signal_color(int8_t rssi) { return rssi > -60 ? TEXT : rssi > -75 ? SUB : MUTED; }

void build_network_list() {
  lv_obj_clean(wf_list);
  wf_n = net::scan_results(wf_aps, 24);
  const int rh = 56, w = lv_obj_get_style_width(wf_list, 0);
  if (wf_n == 0) {
    lv_obj_set_pos(text(wf_list, net::scan_running() ? "Looking for networks..." : "No networks found", &inter_20,
                        hex(MUTED)),
                   16, 16);
    lv_obj_set_height(wf_list, 56);
    return;
  }
  const bool joined = net::state() == net::Wifi::Connected;
  for (int i = 0; i < wf_n; ++i) {
    const net::Ap &ap = wf_aps[i];
    lv_obj_t *r = group(wf_list, 0, i * rh, w, rh);
    if (i > 0) box(r, 52, 0, w - 52, 1, HAIRLINE, 0);
    lv_obj_align(text(r, LV_SYMBOL_WIFI, &lv_font_montserrat_20, hex(signal_color(ap.rssi))), LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *name = text(r, ap.ssid, &inter_20, hex(TEXT));
    fit_text(name, w - 150, 52);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 52, 0);
    const bool current = joined && strcmp(ap.ssid, net::ssid()) == 0;  // a tick, else Secured / Open
    lv_obj_t *tag = text(r, current ? LV_SYMBOL_OK : ap.secure ? "Secured" : "Open",
                         current ? &lv_font_montserrat_20 : &inter_16, hex(current ? GREEN : MUTED));
    lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -16, 0);
    on_click(r, on_network, &wf_aps[i]);
    lv_obj_set_style_bg_color(r, hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  }
  lv_obj_set_height(wf_list, LV_MIN(wf_n * rh, H - M - 96));
}

void refresh_wifi_status() {
  const char *title = net::ssid(), *detail = nullptr;
  char connected[96];
  switch (net::state()) {
    case net::Wifi::Connected:
      snprintf(connected, sizeof connected, tr("Connected\n%s"), net::ip());
      detail = connected;
      break;
    case net::Wifi::Connecting: detail = "Connecting..."; break;
    case net::Wifi::WrongPassword: detail = "Wrong password. Tap the network to try again."; break;
    case net::Wifi::NotFound: detail = "Network not found. Is it in range?"; break;
    default:
      title = "Not connected";
      detail = "Choose your network from the list.";
  }
  set_text(wf_status_title, title);
  set_text(wf_status_sub, detail);
  show(wf_forget, net::has_saved_network());
}

void build_wifi() {
  scr_wifi = new_screen(BG);
  sub_header(scr_wifi, "Wi-Fi", back_to_settings);
  const int list_w = 480, rx = M + list_w + 20, rw = W - M - rx;
  wf_list = box(scr_wifi, M, 96, list_w, 56, 0x1C1C1E, R_SM);  // grows with the networks found
  lv_obj_add_flag(wf_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scroll_dir(wf_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(wf_list, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *card = box(scr_wifi, rx, 96, rw, 200, 0x1C1C1E, R_SM);
  wf_status_title = text(card, "", &inter_20b, hex(TEXT));
  fit_text(wf_status_title, rw - 32, 48);
  lv_obj_set_pos(wf_status_title, 16, 16);
  wf_status_sub = text(card, "", &inter_16, hex(SUB));
  lv_label_set_long_mode(wf_status_sub, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(wf_status_sub, rw - 32);
  lv_obj_set_style_text_line_space(wf_status_sub, 4, 0);
  lv_obj_set_pos(wf_status_sub, 16, 48);
  wf_forget = pill(card, 16, 200 - 16 - 48, rw - 32, 48, "Forget network", 0x2C2C2E, 0xFF6B6B,
                   [](lv_event_t *) { net::forget(); });
  lv_obj_t *scan = pill(scr_wifi, rx, 96 + 200 + G, rw, 52, "Scan again", 0x2C2C2E, TEXT, [](lv_event_t *) {
    net::scan_start();
    wf_scanning_shown = true;
    build_network_list();
  });
  wf_scan_lbl = lv_obj_get_child(scan, 0);
}

void open_wifi(lv_event_t *) {
  net::scan_start();
  wf_scanning_shown = true;
  build_network_list();
  refresh_wifi_status();
  load(scr_wifi, Anim::PushLeft);
}

// ---- text entry: one keyboard screen for the Wi-Fi password, recipe names and sign-in ----
typedef void (*TextDone)(const char *text);
TextDone te_done = nullptr;
lv_obj_t *te_back_to = nullptr, *te_show_btn, *te_action_lbl;

void te_submit(lv_event_t *) {
  char entered[192];
  emoji::unmap(lv_textarea_get_text(pw_ta), entered, sizeof entered);  // emoji as they were written
  if (te_done) te_done(entered);
}

// Latin keyboard layouts: LVGL's own, without the Arabic-page key it adds once Arabic
// letter shaping is compiled in. Used in English; Arabic keeps LVGL's (with that page).
#define KEY(w) (LV_BUTTONMATRIX_CTRL_POPOVER | (w))
#define FN_KEY (LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_CHECKED)
#define DARK LV_BUTTONMATRIX_CTRL_CHECKED
const char *KB_LOWER[] = {"1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                          "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
                          "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
                          LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
const char *KB_UPPER[] = {"1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
                          "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
                          "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
                          LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
const char *KB_SYMBOLS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
                            "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
                            "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
                            LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
const lv_buttonmatrix_ctrl_t KB_LETTER_KEYS[] = {
    FN_KEY | 5, KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), KEY(4), DARK | 7,
    FN_KEY | 6, KEY(3), KEY(3), KEY(3), KEY(3), KEY(3), KEY(3), KEY(3), KEY(3), KEY(3), DARK | 7,
    DARK | KEY(1), DARK | KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), DARK | KEY(1), DARK | KEY(1),
    DARK | KEY(1),
    FN_KEY | 2, DARK | 2, 6, DARK | 2, FN_KEY | 2};
const lv_buttonmatrix_ctrl_t KB_SYMBOL_KEYS[] = {
    KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), DARK | 2,
    FN_KEY | 2, KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1),
    KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1), KEY(1),
    FN_KEY | 2, DARK | 2, 6, DARK | 2, FN_KEY | 2};
#undef KEY
#undef FN_KEY
#undef DARK

void style_text_field(lv_obj_t *ta) {
  lv_obj_set_style_base_dir(ta, LV_BASE_DIR_LTR, 0);  // emails, passwords, network names
  lv_textarea_set_one_line(ta, true);
  lv_obj_set_pos(ta, M, 100);
  lv_obj_set_style_text_font(ta, &inter_20, 0);
  set_text_color(ta, hex(TEXT), 0);
  lv_obj_set_style_bg_color(ta, hex(0x1C1C1E), 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_set_style_radius(ta, R_SM, 0);
  lv_obj_set_style_pad_hor(ta, 16, 0);
  lv_obj_set_style_pad_ver(ta, 14, 0);
  set_text_color(ta, hex(MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta, hex(TEXT), LV_PART_CURSOR);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
}

void style_keyboard(lv_obj_t *kb) {
  lv_obj_set_style_base_dir(kb, LV_BASE_DIR_LTR, 0);  // keys stay where they are in Arabic too
  lv_obj_add_flag(kb, RTL_KEEP);
  if (!rtl) {
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_LETTER_KEYS);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_LETTER_KEYS);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SYMBOLS, KB_SYMBOL_KEYS);
  }
  lv_obj_set_size(kb, W, H - 176);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, hex(0x1C1C1E), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, 8, 0);
  lv_obj_set_style_pad_gap(kb, 8, 0);
  lv_obj_set_style_bg_color(kb, hex(0x3A3A3C), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, hex(0x5A5A5E), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(kb, hex(0x2A2A2C), LV_PART_ITEMS | LV_STATE_CHECKED);
  set_text_color(kb, hex(TEXT), LV_PART_ITEMS);
  set_text_color(kb, hex(TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
  // Key labels: the Montserrat symbols, with the Arabic letters behind them in Arabic.
  static lv_font_t key_font = lv_font_montserrat_20;
  if (rtl) key_font.fallback = &ar_20;
  lv_obj_set_style_text_font(kb, &key_font, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
}

void build_text_entry() {
  scr_pass = new_screen(BG);
  sub_header(scr_pass, "", [](lv_event_t *) { load(te_back_to, Anim::PopRight); }, &pw_title);
  pw_ta = lv_textarea_create(scr_pass);
  style_text_field(pw_ta);

  te_show_btn = pill(scr_pass, W - M - 2 * 120 - 12, 100, 120, 56, "Show", 0x2C2C2E, TEXT, [](lv_event_t *) {
    const bool hidden = lv_textarea_get_password_mode(pw_ta);
    lv_textarea_set_password_mode(pw_ta, !hidden);
    set_text(pw_show_lbl, hidden ? "Hide" : "Show");
  });
  pw_show_lbl = lv_obj_get_child(te_show_btn, 0);
  te_action_lbl = lv_obj_get_child(pill(scr_pass, W - M - 120, 100, 120, 56, "Join", 0xFFFFFF, 0x000000, te_submit), 0);

  lv_obj_t *kb = lv_keyboard_create(scr_pass);
  style_keyboard(kb);
  lv_keyboard_set_textarea(kb, pw_ta);
  lv_obj_add_event_cb(kb, te_submit, LV_EVENT_READY, nullptr);  // the keyboard's tick key
  lv_obj_add_event_cb(kb, [](lv_event_t *) { load(te_back_to, Anim::PopRight); }, LV_EVENT_CANCEL, nullptr);
}

// Drops a character cut in half at the end of `s` (after copying into a short buffer).
void utf8_trim(char *s) {
  size_t n = strlen(s), lead = n;
  while (lead && ((uint8_t)s[lead - 1] & 0xC0) == 0x80) lead--;
  if (!lead) return;
  const uint8_t c = s[lead - 1];
  const size_t need = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  if (n - (lead - 1) < need) s[lead - 1] = 0;
}

// Opens the keyboard screen. `done` gets the text; Back returns to `back_to`.
// Secret text (passwords) is hidden, with a Show / Hide button.
void open_text_entry(const char *title, const char *initial, const char *placeholder, bool secret,
                     const char *action, lv_obj_t *back_to, TextDone done, int max_len = 63) {
  set_text(pw_title, title);
  lv_textarea_set_max_length(pw_ta, max_len);
  static char shown[256];
  lv_textarea_set_text(pw_ta, emoji::map(initial, shown, sizeof shown));
  lv_textarea_set_placeholder_text(pw_ta, tr(placeholder));
  lv_textarea_set_password_mode(pw_ta, secret);
  set_text(pw_show_lbl, "Show");
  set_text(te_action_lbl, action);
  show(te_show_btn, secret);
  const int buttons = secret ? 2 : 1;  // the field gives way to the buttons beside it
  lv_obj_set_size(pw_ta, W - 2 * M - buttons * (120 + 12), 56);
  te_back_to = back_to;
  te_done = done;
  load(scr_pass, Anim::PushLeft);
}

// The Wi-Fi password for a secured network.
void open_password(const char *ssid) {
  strlcpy(pw_ssid, ssid, sizeof pw_ssid);
  char title[64];
  snprintf(title, sizeof title, tr("Join \"%s\""), ssid);
  open_text_entry(title, "", "Password", true, "Join", scr_wifi, [](const char *password) {
    net::join(pw_ssid, password);
    refresh_wifi_status();
    load(scr_wifi, Anim::PopRight);
  });
}

// ---- software update ----------------------------------------------------------------
// The panel checks GitHub for a newer release by itself: one tap on Install downloads,
// checks and installs it. A file from a phone or computer on the same Wi-Fi still works.
void refresh_update() {
  const bool online = net::state() == net::Wifi::Connected;
  const net::Upd state = net::update_state();
  const net::Online found = net::online_state();
  const bool installing = state == net::Upd::Receiving || state == net::Upd::Done;
  char line[96];

  show(up_wifi_btn, !online);
  const char *action = nullptr;
  if (!online) {
    set_text(up_online, "Connect the panel to Wi-Fi first.");
  } else {
    switch (found) {
      case net::Online::Unknown:
      case net::Online::Checking: set_text(up_online, "Checking for updates..."); break;
      case net::Online::UpToDate:
        set_text(up_online, "You're up to date");
        action = "Check again";
        break;
      case net::Online::Available:
        snprintf(line, sizeof line, tr("Version %s is ready"), net::online_version());
        set_text(up_online, line);
        action = "Install";
        break;
      case net::Online::Failed:
        set_text(up_online, "Couldn't check for updates");
        action = "Try again";
        break;
    }
  }
  set_text_color(up_online, hex(found == net::Online::Available && online ? GREEN : TEXT), 0);
  show(up_action, online && action && !installing);
  if (action) set_text(up_action_lbl, action);

  show(up_steps, online);
  show(up_url, online);
  show(up_ip, online);
  if (online) {
    set_text(up_steps, "Or from your phone or computer (same Wi-Fi), open:");
    snprintf(line, sizeof line, "http://%s", net::hostname());
    set_text(up_url, line);
    snprintf(line, sizeof line, tr("or  http://%s     then choose the update file."), net::ip());
    set_text(up_ip, line);
  }

  lv_bar_set_value(up_bar, state == net::Upd::Idle ? 0 : net::update_progress(), LV_ANIM_OFF);
  show(up_bar, state != net::Upd::Idle);
  uint32_t color = SUB;
  switch (state) {
    case net::Upd::Idle: set_text(up_status, ""); break;
    case net::Upd::Receiving:
      snprintf(line, sizeof line, tr("Installing... %d%%   Keep the panel powered."), net::update_progress());
      set_text(up_status, line);
      break;
    case net::Upd::Done:
      set_text(up_status, "Installed. Restarting...");
      color = GREEN;
      break;
    case net::Upd::Failed:
      snprintf(line, sizeof line, tr("Failed: %s"), tr(net::update_error()));
      set_text(up_status, line);
      color = 0xFF6B6B;
      break;
  }
  set_text_color(up_status, hex(color), 0);
}

void build_update() {
  scr_update = new_screen(BG);
  sub_header(scr_update, "Software update", [](lv_event_t *) {
    if (net::update_state() == net::Upd::Receiving) return;  // not while a file is installing
    net::update_page(false);
    load(scr_home, Anim::PopRight);
  });
  const int cw = W - 2 * M;
  lv_obj_t *card = box(scr_update, M, 96, cw, H - M - 96, 0x1C1C1E, R_SM);
  char version[48];
  snprintf(version, sizeof version, tr("Installed version %s"), FW_VERSION);
  lv_obj_set_pos(text(card, version, &inter_20, hex(SUB)), 24, 20);
  up_online = text(card, "", &inter_26b, hex(TEXT));
  lv_obj_set_pos(up_online, 24, 58);
  up_action = pill(card, cw - 24 - 200, 50, 200, 52, "Install", 0xFFFFFF, 0x000000, [](lv_event_t *) {
    if (net::online_state() == net::Online::Available) net::install_online();
    else net::check_online();
    refresh_update();
  });
  up_action_lbl = lv_obj_get_child(up_action, 0);
  up_wifi_btn = pill(card, cw - 24 - 240, 50, 240, 52, "Wi-Fi settings", 0xFFFFFF, 0x000000, [](lv_event_t *) {
    net::update_page(false);
    open_wifi();
  });
  up_bar = lv_bar_create(card);
  lv_obj_set_size(up_bar, cw - 48, 10);
  lv_obj_set_pos(up_bar, 24, 124);
  lv_obj_set_style_bg_color(up_bar, hex(0x2C2C2E), 0);
  lv_obj_set_style_bg_color(up_bar, hex(GREEN), LV_PART_INDICATOR);
  lv_bar_set_range(up_bar, 0, 100);
  up_status = text(card, "", &inter_20, hex(SUB));
  lv_obj_set_pos(up_status, 24, 144);
  // When an update is out: ask (pop-up), install automatically, or don't remind.
  lv_obj_t *mode_row = bare(card, 0, 168, cw, 48);
  lv_obj_align(text(mode_row, "When an update is out", &inter_20, hex(TEXT)), LV_ALIGN_LEFT_MID, 24, 0);
  up_mode_dd = dropdown(mode_row, "Ask me\nInstall automatically\nDon't remind me",
                        [](lv_event_t *e) { net::set_update_mode(chosen(e) % 3); });
  lv_obj_set_width(up_mode_dd, 260);
  lv_obj_align(up_mode_dd, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_dropdown_set_selected(up_mode_dd, (uint32_t)LV_CLAMP(0, net::update_mode(), 2));
  up_steps = text(card, "", &inter_16, hex(MUTED));
  lv_obj_set_pos(up_steps, 24, 226);
  up_url = text(card, "", &inter_20b, hex(TEXT));
  lv_obj_set_pos(up_url, 24, 248);
  up_ip = text(card, "", &inter_16, hex(SUB));
  lv_obj_set_pos(up_ip, 24, 276);
  lv_obj_align(text(card, "If an update ever fails to start, the panel goes back to this version by itself.", &inter_16,
                    hex(MUTED)),
               LV_ALIGN_BOTTOM_LEFT, 24, -20);
}

void open_update(lv_event_t *) {
  net::update_page(true);  // the web page only accepts a file while this screen is open
  refresh_update();
  load(scr_update, Anim::PushLeft);
}

// ---- idle clock (main.cpp shows it instead of a dark screen) -------------------
lv_obj_t *clock_layer;
uint32_t clock_color = dotfx::WHITE;

uint32_t clock_color_pref() {
  switch (net::pref_clock_color()) {
    case 1: return dotfx::BLUE;
    case 2: return 0xC8925A;  // brown
    default: return dotfx::WHITE;
  }
}

void build_clock() {
  clock_layer = dotfx::clock_create(lv_layer_top());
  lv_obj_add_flag(clock_layer, RTL_KEEP);
  show(clock_layer, false);
}

// 12-hour time without leading zero: 9:25
void format_clock(const struct tm &t, char *hm, size_t n) {
  const int h = t.tm_hour % 12;
  snprintf(hm, n, "%d:%02d", h ? h : 12, t.tm_min);
}

void refresh_clock() {
  struct tm t;
  if (!net::local_time(&t)) return;
  char hm[16], day[8], month[8], date[32];
  format_clock(t, hm, sizeof hm);
  strftime(day, sizeof day, "%a", &t);
  strftime(month, sizeof month, "%b", &t);
  snprintf(date, sizeof date, "%s %d %s", day, t.tm_mday, month);  // MON 2 NOV
  dotfx::clock_set(clock_layer, hm, date, clock_color_pref());
}

// ---- everything that follows Wi-Fi: status bar, Settings, Wi-Fi and update screens ----
const char *wifi_summary(net::Wifi s) {
  switch (s) {
    case net::Wifi::Connected: return net::ssid();
    case net::Wifi::Off: return "Off";
    case net::Wifi::Connecting: return "Connecting...";
    default: return "Not connected";
  }
}

// ---- "Update available" pop-up -------------------------------------------------------
// Shown on the home screen when the panel finds a newer release, unless the person
// chose otherwise: Ok installs now, Cancel asks again next start, Remind me Tomorrow
// waits a day, Never Show This Again! leaves only the "Update ready" in Settings, and
// Always Update Automatically installs this one and every later one by itself.
extern lv_obj_t *cf_layer;
lv_obj_t *up_prompt, *up_prompt_text, *up_mode_dd;
int up_prompt_dismissed = 0;  // the release build Cancel was pressed for (until restart)

void close_update_prompt() {
  show(up_prompt, false);
  up_prompt_dismissed = net::online_build();
}

void install_now() {
  show(up_prompt, false);
  open_update();
  net::install_online();
  refresh_update();
}

lv_obj_t *prompt_button(lv_obj_t *dialog, int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg,
                        const lv_font_t *font, lv_event_cb_t on_tap) {
  lv_obj_t *b = box(dialog, x, y, w, h, bg, h / 2);
  lv_obj_center(text(b, label, font, hex(fg)));
  pressable(b);
  on_click(b, on_tap);
  return b;
}

void build_update_prompt() {
  up_prompt = bare(lv_layer_top(), 0, 0, W, H);  // a dark veil over everything
  lv_obj_set_style_bg_color(up_prompt, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(up_prompt, LV_OPA_70, 0);
  lv_obj_add_flag(up_prompt, LV_OBJ_FLAG_CLICKABLE);
  const int dw = 560, pad = 28, iw = dw - 2 * pad, half = (iw - G) / 2;
  const int y_small = 132, y_auto = y_small + 44 + 12, y_main = y_auto + 50 + 20, dh = y_main + 56 + pad;
  lv_obj_t *dialog = box(up_prompt, 0, 0, dw, dh, 0x1C1C1E, 24);
  lv_obj_center(dialog);
  lv_obj_set_pos(text(dialog, "Update available \xe2\x9c\xa8", &inter_26b, hex(TEXT)), pad, pad);
  up_prompt_text = text(dialog, "", &inter_16, hex(SUB));
  lv_obj_set_width(up_prompt_text, iw);
  lv_label_set_long_mode(up_prompt_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(up_prompt_text, 4, 0);
  lv_obj_set_pos(up_prompt_text, pad, pad + 44);

  prompt_button(dialog, pad, y_small, half, 44, "Remind me Tomorrow", 0x2C2C2E, SUB, &inter_16, [](lv_event_t *) {
    net::snooze_update_day();
    show(up_prompt, false);
  });
  prompt_button(dialog, pad + half + G, y_small, half, 44, "Never Show This Again!", 0x2C2C2E, SUB, &inter_16,
                [](lv_event_t *) {
                  net::set_update_mode(2);
                  if (up_mode_dd) lv_dropdown_set_selected(up_mode_dd, 2);
                  show(up_prompt, false);
                });
  prompt_button(dialog, pad, y_auto, iw, 50, "Always Update Automatically", 0x16351F, GREEN, &inter_20b,
                [](lv_event_t *) {
                  net::set_update_mode(1);
                  if (up_mode_dd) lv_dropdown_set_selected(up_mode_dd, 1);
                  install_now();
                });
  prompt_button(dialog, pad, y_main, half, 56, "Cancel", 0x2C2C2E, TEXT, &inter_20b,
                [](lv_event_t *) { close_update_prompt(); });
  prompt_button(dialog, pad + half + G, y_main, half, 56, "Ok", 0xFFFFFF, 0x000000, &inter_20b,
                [](lv_event_t *) { install_now(); });
  show(up_prompt, false);
}

void open_update_prompt() {
  char line[160];
  snprintf(line, sizeof line,
           tr("Version %s is ready. Ok installs it now: about a minute, and your recipes and settings stay."),
           net::online_version());
  set_text(up_prompt_text, line);
  lv_obj_move_foreground(up_prompt);
  show(up_prompt, true);
  lv_display_trigger_activity(nullptr);  // light the screen so it's seen
}

// Called with the other periodic refreshes.
void update_prompt_tick() {
  if (net::online_state() != net::Online::Available || net::update_state() == net::Upd::Receiving ||
      net::update_state() == net::Upd::Done)
    return;
  const bool calm = lv_screen_active() == scr_home && !::ui::busy() && lv_obj_has_flag(modal, LV_OBJ_FLAG_HIDDEN) &&
                    lv_obj_has_flag(cf_layer, LV_OBJ_FLAG_HIDDEN);
  switch (net::update_mode()) {
    case 1:  // automatic: after two quiet minutes, never mid-brew
      if (calm && net::update_state() != net::Upd::Failed && lv_display_get_inactive_time(nullptr) > 120000) {
        lv_display_trigger_activity(nullptr);
        install_now();
      }
      break;
    case 0:
      if (calm && lv_obj_has_flag(up_prompt, LV_OBJ_FLAG_HIDDEN) && up_prompt_dismissed != net::online_build() &&
          !net::update_snoozed()) {
        open_update_prompt();
      }
      break;
    default: break;  // don't remind: Settings still says "Update ready"
  }
}

void refresh_net() {
  const net::Wifi s = net::state();
  show(sb_wifi, s != net::Wifi::Off);
  set_text_color(sb_wifi, hex(s == net::Wifi::Connected ? TEXT : MUTED), 0);
  struct tm t;
  if (net::local_time(&t)) {
    char hm[12];
    format_clock(t, hm, sizeof hm);
    set_text(sb_clock, hm);
  }
  const bool on_wifi_screen = lv_screen_active() == scr_wifi;
  if (s != last_wifi) {
    last_wifi = s;
    set_text(st_wifi_val, wifi_summary(s));
    if (s == net::Wifi::WrongPassword) show_toast("Wrong Wi-Fi password");
    if (on_wifi_screen) build_network_list();
  }
  if (on_wifi_screen) {
    refresh_wifi_status();
    if (wf_scanning_shown && !net::scan_running()) {  // a scan just finished
      wf_scanning_shown = false;
      build_network_list();
    }
    set_text(wf_scan_lbl, net::scan_running() ? "Scanning..." : "Scan again");
  }
  if (lv_screen_active() == scr_update) refresh_update();
  update_prompt_tick();
  static bool ready_shown = false;  // Settings › Software update says when one is ready
  const bool ready = net::online_state() == net::Online::Available;
  if (ready != ready_shown) {
    ready_shown = ready;
    set_text(st_ver_val, ready ? "Update ready" : FW_VERSION);
    set_text_color(st_ver_val, hex(ready ? GREEN : MUTED), 0);
  }
  if (!lv_obj_has_flag(clock_layer, LV_OBJ_FLAG_HIDDEN)) refresh_clock();
}

// ---- the home screen: three tab pages and the tab bar ------------------------------
void build_home_screen() {
  scr_home = new_screen(BG);
  page_home = box(scr_home, 0, 0, W, H - TABBAR_H, BG, 0);
  page_recipes = box(scr_home, 0, 0, W, H - TABBAR_H, BG, 0);
  page_settings = box(scr_home, 0, 0, W, H - TABBAR_H, BG, 0);
  build_home_page(page_home);
  build_recipes_page(page_recipes);
  build_settings_page(page_settings);

  lv_obj_t *bar = box(scr_home, 0, H - TABBAR_H, W, TABBAR_H, TABBAR, 0);
  box(bar, 0, 0, W, 1, HAIRLINE, 0);
  // Three tabs switch pages; the fourth opens the Double Shot page over everything.
  static const char *const TABS[4] = {"xBloom", "Recipes", "Settings", "Double Shot"};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *hit = group(bar, i * W / 4, 0, W / 4, TABBAR_H);
    tab_lbl[i] = text(hit, TABS[i], &inter_20b, hex(MUTED));
    lv_obj_center(tab_lbl[i]);
    if (i == 3) {
      on_click(hit, open_store);
      pressable(hit);
    } else {
      on_click(hit, [](lv_event_t *e) { set_tab((int)(intptr_t)lv_event_get_user_data(e)); }, (void *)(intptr_t)i);
    }
  }
  set_tab(0);
}

// ============================================================================
// Double Shot page (last tab)
// ============================================================================
// doubleshot-sa.com needs a web browser, which an ESP32 doesn't have, so this is a
// page about the store with QR codes that open the shop and WhatsApp on a phone.
// It covers the whole screen, status bar included, with a big X to close.
const char *const STORE_URL = "https://doubleshot-sa.com";
const char *const STORE_WHATSAPP_URL = "https://wa.me/966502065920";

void close_store(lv_event_t *) { load(scr_home, Anim::DropDown); }

void open_store(lv_event_t *) {
  show(sb_bar, false);  // back when the page closes (LV_EVENT_SCREEN_UNLOADED)
  load(scr_store, Anim::RiseUp);
}

// A white card (dark on white scans best) with a QR code and two caption lines.
lv_obj_t *qr_card(lv_obj_t *parent, int x, int y, const char *url, const char *title, const char *caption) {
  const int CARD_W = 168, CARD_H = 236, QR = 140;
  lv_obj_t *card = box(parent, x, y, CARD_W, CARD_H, 0xFFFFFF, R_SM);
  lv_obj_t *qr = lv_qrcode_create(card);
  lv_qrcode_set_size(qr, QR);
  lv_qrcode_set_dark_color(qr, hex(0x000000));
  lv_qrcode_set_light_color(qr, hex(0xFFFFFF));
  lv_qrcode_update(qr, url, strlen(url));
  lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 14);
  lv_obj_t *t = text(card, title, &inter_20b, hex(0x111111));
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14 + QR + 12);
  lv_obj_align_to(text(card, caption, &inter_14, hex(0x5C5C60)), t, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  return card;
}

void build_store() {
  scr_store = new_screen(BG);
  lv_obj_add_event_cb(scr_store, [](lv_event_t *) { show(sb_bar, true); }, LV_EVENT_SCREEN_UNLOADED, nullptr);

  // The Double Shot logo, white on black as on the boot screen.
  lv_obj_t *logo = lv_image_create(scr_store);
  lv_image_set_src(logo, &store_logo);
  lv_obj_set_pos(logo, M, 26);
  lv_obj_set_style_image_recolor(logo, hex(TEXT), 0);
  lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);

  // Close: a 64 px circle in a 112x112 tap area at the top right.
  lv_obj_t *close_hit = group(scr_store, W - 112, 0, 112, 112);
  lv_obj_t *close_circle = box(close_hit, 112 - M - 64, M, 64, 64, SEG_BG, 32);
  lv_obj_center(text(close_circle, LV_SYMBOL_CLOSE, &lv_font_montserrat_28, hex(TEXT)));
  on_click(close_hit, close_store);
  pressable(close_hit);

  lv_obj_set_pos(text(scr_store, "Scan a code with your phone to shop online or chat on WhatsApp.", &inter_20, hex(SUB)),
                 M, 152);

  // Left: what the store sells.
  const int COL_W = 380, BODY_Y = 204, CHIP_W = (COL_W - 2 * 10) / 3, CHIP_H = 48;
  lv_obj_set_pos(text(scr_store, "SHOP BY CATEGORY", &inter_14, hex(MUTED)), M, BODY_Y);
  static const char *const CATEGORIES[6] = {"Roasters",       "Grinders",       "Drip & brew",
                                            "Espresso tools", "Kettles & cups", "Cleaning"};
  for (int i = 0; i < 6; ++i) {
    lv_obj_t *chip = box(scr_store, M + (i % 3) * (CHIP_W + 10), BODY_Y + 26 + (i / 3) * (CHIP_H + 10), CHIP_W, CHIP_H,
                         SURFACE, R_SM);
    lv_obj_center(text(chip, CATEGORIES[i], &inter_16, hex(TEXT)));
  }
  lv_obj_t *brands_title = text(scr_store, "FEATURED BRANDS", &inter_14, hex(MUTED));
  lv_obj_set_pos(brands_title, M, BODY_Y + 26 + 2 * CHIP_H + 10 + 22);
  lv_obj_t *brands = text(scr_store,
                          "Mahlk\xc3\xb6nig \xc2\xb7 xBloom \xc2\xb7 La Marzocco \xc2\xb7 Rocket \xc2\xb7 TIMEMORE"
                          " \xc2\xb7 Brewista \xc2\xb7 AeroPress \xc2\xb7 illy",
                          &inter_16, hex(SUB));
  lv_obj_set_width(brands, COL_W);
  lv_label_set_long_mode(brands, LV_LABEL_LONG_WRAP);
  lv_obj_align_to(brands, brands_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

  // Right: the two codes.
  const int CARD_GAP = 16, CARDS_X = W - M - 2 * 168 - CARD_GAP;
  qr_card(scr_store, CARDS_X, BODY_Y, STORE_URL, "Shop online", "doubleshot-sa.com");
  qr_card(scr_store, CARDS_X + 168 + CARD_GAP, BODY_Y, STORE_WHATSAPP_URL, "WhatsApp", "+966 50 206 5920");
}

// ============================================================================
// Recipe screen: the recipe card, Dose / Grind / RPM (live readings while
// brewing), Start / Pause and Stop on the left; one bar per pour on the right.
// ============================================================================
void on_start(lv_event_t *);
void on_stop(lv_event_t *);

// ---- "Place your cup": after loading, the brew starts only from this dialog ---------
void close_modal() { show(modal, false); }

void build_modal() {
  modal = bare(lv_layer_top(), 0, 0, W, H);  // a dark veil over everything
  lv_obj_set_style_bg_color(modal, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
  const int dw = 456, dh = 240, pad = 28, bw = (dw - 2 * pad - G) / 2, bh = 56, by = dh - pad - bh;
  lv_obj_t *dialog = box(modal, 0, 0, dw, dh, 0x1C1C1E, 24);
  lv_obj_center(dialog);
  lv_obj_set_pos(text(dialog, "Place your cup", &inter_26b, hex(TEXT)), pad, pad);
  lv_obj_t *body = text(dialog, "The recipe is loaded on the machine.\nPut your cup on the scale, then start.",
                        &inter_16, hex(SUB));
  lv_obj_set_style_text_line_space(body, 4, 0);
  lv_obj_set_pos(body, pad, pad + 46);

  lv_obj_t *cancel = box(dialog, pad, by, bw, bh, 0x2C2C2E, bh / 2);
  lv_obj_center(text(cancel, "Cancel", &inter_20b, hex(TEXT)));
  pressable(cancel);
  on_click(cancel, [](lv_event_t *) {  // unload it from the machine
    close_modal();
    post(BleCmdType::QuitRecipe);
    flow = Flow::Idle;
    active = 0;
    refresh_detail();
  });
  lv_obj_t *brew = box(dialog, pad + bw + G, by, bw, bh, 0xFFFFFF, bh / 2);
  lv_obj_center(text(brew, "Brew", &inter_20b, hex(0x000000)));
  pressable(brew);
  on_click(brew, [](lv_event_t *) {
    close_modal();
    post(BleCmdType::Execute);  // the one place a recipe brew is ever started
    flow = Flow::Starting;
    refresh_detail();
  });
  show(modal, false);
}

// Two columns under the back row.
const int D_TOP = 92, D_LEFT_W = 360, D_RIGHT_X = M + D_LEFT_W + 24, D_RIGHT_W = W - M - D_RIGHT_X;
const int D_HDR_H = 160, D_STATS_Y = D_TOP + D_HDR_H + 12, D_STATS_H = 84;
const int D_BTN_H = 96, D_BTN_Y = H - M - D_BTN_H;
const uint32_t HEART_ON = 0xFF453A, HEART_OFF = 0x5A5A5E;

// The card: cup and name, the ratio and water, and the pour count large behind.
void build_recipe_card(const PanelRecipe &p) {
  const xb::Recipe &r = p.r;
  const uint32_t bg = p.color;
  const lv_color_t ink = darker(bg, 70), dark = hex(0x2A2A2A), ghost = lv_color_darken(hex(bg), 30);
  lv_obj_t *card = box(scr_detail, M, D_TOP, D_LEFT_W, D_HDR_H, bg, R_SM);
  char s[48];
  snprintf(s, sizeof s, "%d", r.n_pours);
  baseline_align(text(card, s, &inter_120_bold, ghost), LV_ALIGN_BOTTOM_RIGHT, -16, -6);
  lv_obj_align(text(card, "POURS", &inter_26b, ghost), LV_ALIGN_TOP_RIGHT, -16, 12);
  lv_obj_set_pos(cup_icon(card, r.cup_type, ink), 20, 20);
  lv_obj_set_pos(text(card, recipes::cup_name(r.cup_type), &inter_20, ink), 52, 17);
  lv_obj_t *name = text(card, r.name, &inter_20, dark);
  lv_obj_set_width(name, D_LEFT_W - 140);
  lv_obj_set_pos(name, 20, 66);

  // 1:16 | 240ml, on one baseline, with a thin divider between.
  snprintf(s, sizeof s, "1:%g", ratio_of(r));
  lv_obj_t *ratio = text(card, s, &inter_34b, dark);
  baseline_align(ratio, LV_ALIGN_BOTTOM_LEFT, 20, 22);
  lv_obj_t *divider = box(card, 0, 0, 2, 30, 0, 0);
  lv_obj_set_style_bg_color(divider, dark, 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_40, 0);
  lv_obj_update_layout(ratio);
  lv_obj_align_to(divider, ratio, LV_ALIGN_OUT_RIGHT_MID, 12, 2);
  snprintf(s, sizeof s, "%.0fml", r.total_water());
  lv_obj_t *water = text(card, s, &inter_34b, dark);
  lv_obj_update_layout(divider);
  baseline_align(water, LV_ALIGN_BOTTOM_LEFT, lv_obj_get_x(divider) + 14, 22);

  // The whole name, smaller if need be, in the room between the cup row and the ratio.
  lv_obj_update_layout(card);
  const int name_top = 46, room = lv_obj_get_y(ratio) + 6 - name_top;
  fit_text(name, D_LEFT_W - 140, room);
  lv_obj_update_layout(name);
  const int name_h = lv_obj_get_height(name);
  if (name_h > lv_font_get_line_height(&inter_20)) lv_obj_set_y(name, name_top + (room - name_h) / 2);
}

// Three columns; set_stat() fills them.
void build_stats_box(uint32_t bg) {
  lv_obj_t *stats = box(scr_detail, M, D_STATS_Y, D_LEFT_W, D_STATS_H, 0x2C2C2E, R_SM);
  lv_obj_set_style_border_color(stats, hex(0x3A3A3C), 0);
  lv_obj_set_style_border_width(stats, 1, 0);
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *col = group(stats, i * D_LEFT_W / 3, 0, D_LEFT_W / 3, D_STATS_H);
    d_stats[i][0] = text(col, "", &inter_16, hex(MUTED));
    lv_obj_align(d_stats[i][0], LV_ALIGN_TOP_MID, 0, 12);
    d_stats[i][1] = text(col, "", &inter_26b, lv_color_lighten(hex(bg), 40));
    d_stat_unit[i] = text(col, "", &inter_16, hex(SUB));
    for (lv_obj_t *o : {d_stats[i][1], d_stat_unit[i]}) {  // set_stat() places these itself
      lv_obj_add_flag(o, RTL_KEEP);
      lv_obj_set_style_base_dir(o, LV_BASE_DIR_LTR, 0);
    }
  }
}

// A wide Start pill; while brewing it narrows to make room for a round Stop.
void build_brew_buttons(uint32_t bg) {
  d_start = box(scr_detail, M, D_BTN_Y, D_LEFT_W, D_BTN_H, bg, D_BTN_H / 2);
  d_start_icon = text(d_start, LV_SYMBOL_PLAY, &lv_font_montserrat_28, hex(BG_MOD));
  lv_obj_align(d_start_icon, LV_ALIGN_TOP_MID, 0, 16);
  d_start_lbl = text(d_start, "START", &inter_20b, hex(BG_MOD));
  baseline_align(d_start_lbl, LV_ALIGN_BOTTOM_MID, 0, 18);
  on_click(d_start, on_start);
  pressable(d_start);
  d_stop = box(scr_detail, M + D_LEFT_W - D_BTN_H, D_BTN_Y, D_BTN_H, D_BTN_H, BG_MOD, LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_color(d_stop, hex(bg), 0);
  lv_obj_set_style_border_width(d_stop, 4, 0);
  lv_obj_center(text(d_stop, LV_SYMBOL_STOP, &lv_font_montserrat_28, hex(bg)));
  on_click(d_stop, on_stop);
  pressable(d_stop);
}

// One column per pour: ml over a bar (scaled to the largest pour, filling with water
// while brewing), then pattern and temperature, name, and the wait after it.
void build_pours_chart(const PanelRecipe &p) {
  const xb::Recipe &r = p.r;
  lv_obj_set_pos(text(scr_detail, "Pours", &inter_26b, hex(TEXT)), D_RIGHT_X, D_TOP - 4);
  const int chart_y = D_TOP + 44, chart_h = H - M - chart_y;
  lv_obj_t *chart = group(scr_detail, D_RIGHT_X, chart_y, D_RIGHT_W, chart_h);
  double most = 1;
  for (int i = 0; i < r.n_pours; ++i) most = fmax(most, r.pours[i].volume_ml);
  d_ncols = r.n_pours;
  const int gap = 6, col_w = (D_RIGHT_W - gap * (r.n_pours - 1)) / r.n_pours;
  const int base = chart_h - 104, tallest = base - 40;  // room above for the ml label
  const bool narrow = col_w < 70;                        // many pours: smaller text, short names
  double before = 0;
  char s[48];
  for (int i = 0; i < r.n_pours; ++i) {
    const xb::Pour &pour = r.pours[i];
    const int x = i * (col_w + gap);
    if (i < r.n_pours - 1) box(chart, x + col_w + gap / 2 - 1, 0, 1, chart_h, 0x262626, 0);  // column divider
    const int bar_h = (int)fmax(4, tallest * pour.volume_ml / most);
    lv_obj_t *bar = box(chart, x, base - bar_h, col_w, bar_h, 0x3A3A3A, 4);
    lv_obj_t *fill = box(bar, 0, bar_h, col_w, 0, p.color, 0);
    d_cols[i] = {bar, fill, pour.volume_ml, before};
    before += pour.volume_ml;

    auto centred = [&](const char *t, const lv_font_t *font, lv_color_t color, int y) {
      lv_obj_t *l = text(chart, t, font, color);
      lv_obj_set_width(l, col_w);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(l, x, y);
    };
    snprintf(s, sizeof s, "%.0fml", pour.volume_ml);
    centred(s, narrow ? &inter_16 : &inter_20b, hex(TEXT), base - bar_h - 30);

    lv_obj_t *pattern_row = group(chart, x, base + 14, col_w, 22);
    lv_obj_set_flex_flow(pattern_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pattern_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pattern_row, 6, 0);
    lv_obj_set_style_base_dir(pattern_row, LV_BASE_DIR_LTR, 0);  // icon then temperature, centred, in any language
    pattern_icon(pattern_row, pour.pattern, 18, hex(0xDDDDDD));
    snprintf(s, sizeof s, "%.0f°C", pour.temp_c);
    text(pattern_row, s, &inter_16, hex(SUB));

    char name[24];
    if (!narrow) shown_pour_name(p, i, name, sizeof name);
    else if (i == 0) strcpy(name, "Bloom");
    else snprintf(name, sizeof name, "P%d", i + 1);
    centred(name, narrow ? &inter_16 : &inter_20, hex(TEXT), base + 46);
    snprintf(s, sizeof s, pour.agitate_after || pour.agitate_before ? "%ds ~" : "%ds", pour.pause_s);  // ~ stirs
    centred(s, &inter_16, hex(MUTED), base + 78);
  }
}

void build_detail() {
  const PanelRecipe *p = find(cur);
  if (!p) return;
  // The screen object is reused: deleting one that may still be on its way out is unsafe.
  if (!scr_detail) scr_detail = new_screen(BG_MOD);
  else lv_obj_clean(scr_detail);
  // A wash of the recipe's colour at the top, fading to the background.
  lv_obj_set_style_bg_color(scr_detail, lv_color_mix(hex(p->color), hex(BG_MOD), 40), 0);
  lv_obj_set_style_bg_grad_color(scr_detail, hex(BG_MOD), 0);
  lv_obj_set_style_bg_grad_dir(scr_detail, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(scr_detail, 0, 0);
  lv_obj_set_style_bg_grad_stop(scr_detail, 150, 0);

  // Back on the left; heart and edit on the right.
  back_button(scr_detail, LV_SYMBOL_LEFT, go_home_from_detail);
  lv_obj_t *edit = group(scr_detail, W - M - 52, SB, 52, 52);
  lv_obj_center(text(edit, LV_SYMBOL_EDIT, &lv_font_montserrat_20, hex(TEXT)));
  on_click(edit, [](lv_event_t *) { open_editor(cur); });
  pressable(edit);
  lv_obj_t *heart_hit = group(scr_detail, W - M - 52 - 8 - 52, SB, 52, 52);
  lv_obj_center(heart_icon(heart_hit, p->favorite ? HEART_ON : HEART_OFF));
  on_click(heart_hit, [](lv_event_t *e) {
    toggle_favorite(cur);
    const PanelRecipe *now = find(cur);
    heart_color(lv_obj_get_child((lv_obj_t *)lv_event_get_current_target(e), 0),
                now && now->favorite ? HEART_ON : HEART_OFF);
  });
  pressable(heart_hit);

  build_recipe_card(*p);
  build_stats_box(p->color);
  build_brew_buttons(p->color);
  build_pours_chart(*p);
  refresh_detail();
}

// One box of the stats row: number and smaller unit centred together on one
// baseline; the number drops to a smaller size if the pair wouldn't fit.
void set_stat(int i, const char *num, const char *unit) {
  lv_obj_t *n = d_stats[i][1], *u = d_stat_unit[i];
  set_text(n, num);
  set_text(u, unit);
  const int col_w = D_LEFT_W / 3, gap = unit[0] ? 4 : 0;
  lv_obj_update_layout(n);
  lv_obj_update_layout(u);
  int wn = lv_obj_get_width(n);
  const int wu = unit[0] ? lv_obj_get_width(u) : 0;
  const lv_font_t *big = wn + gap + wu > col_w - 16 ? &inter_20b : &inter_26b;
  if (lv_obj_get_style_text_font(n, 0) != big) {
    lv_obj_set_style_text_font(n, big, 0);
    lv_obj_update_layout(n);
    wn = lv_obj_get_width(n);
  }
  const int x0 = (col_w - (wn + gap + wu)) / 2, baseline = D_STATS_H - 18;
  const lv_font_t *small = &inter_16;
  const bool ar = i18n::arabic_units();  // Arabic reads the number first: unit on its left
  lv_obj_set_style_align(n, LV_ALIGN_TOP_LEFT, 0);  // explicit, since a default anchor follows RTL
  lv_obj_set_style_align(u, LV_ALIGN_TOP_LEFT, 0);
  lv_obj_set_pos(n, ar ? x0 + wu + gap : x0, baseline - (lv_font_get_line_height(big) - big->base_line));
  lv_obj_set_pos(u, ar ? x0 : x0 + wn + gap, baseline - (lv_font_get_line_height(small) - small->base_line));
}

void fmt_min_sec(char *buf, size_t n, uint32_t ms) {
  const unsigned long s = ms / 1000;
  snprintf(buf, n, "%lu:%02lu", s / 60, s % 60);
}

const char *start_label(bool mine, bool other_busy, const char **icon) {
  *icon = LV_SYMBOL_PLAY;
  if (other_busy) return "BUSY";
  if (!mine) return "START";
  switch (flow) {
    case Flow::Loading: *icon = LV_SYMBOL_REFRESH; return "LOADING";
    case Flow::Loaded: return "READY";
    case Flow::Starting: *icon = LV_SYMBOL_REFRESH; return "STARTING";
    case Flow::Brewing: *icon = LV_SYMBOL_PAUSE; return "PAUSE";
    case Flow::Paused: return "RESUME";
    default: return "START";
  }
}

void refresh_detail() {
  const PanelRecipe *p = find(cur);
  if (!scr_detail || !p) return;
  const bool mine = active == cur;
  const bool brewing = mine && (flow == Flow::Brewing || flow == Flow::Paused);
  static const char *const RECIPE_STATS[3] = {"Dose", "Grind Size", "RPM"};
  static const char *const LIVE_STATS[3] = {"Scale", "Water", "Time"};
  for (int i = 0; i < 3; ++i) set_text(d_stats[i][0], (brewing ? LIVE_STATS : RECIPE_STATS)[i]);
  char s[32];
  if (brewing) {
    snprintf(s, sizeof s, "%.1f", weight_g);
    set_stat(0, s, "g");
    snprintf(s, sizeof s, "%.0f", water_ml);
    set_stat(1, s, "ml");
    fmt_min_sec(s, sizeof s, brew_start_ms ? millis() - brew_start_ms : 0);
    set_stat(2, s, "");
  } else {
    snprintf(s, sizeof s, "%g", p->r.dose_g);
    set_stat(0, s, "g");
    snprintf(s, sizeof s, "%d", p->r.grind_size);
    set_stat(1, s, "");
    snprintf(s, sizeof s, "%d", p->r.rpm);
    set_stat(2, s, "");
  }

  // Each pour's bar fills from the bottom as its share of the water goes in.
  for (int i = 0; i < d_ncols; ++i) {
    const Col &c = d_cols[i];
    const double share = brewing ? fmax(0, fmin(1, (water_ml - c.cum) / c.vol)) : 0;
    const int bar_h = lv_obj_get_height(c.bar), fill_h = (int)(bar_h * share);
    lv_obj_set_y(c.fill, bar_h - fill_h);
    lv_obj_set_height(c.fill, fill_h);
  }

  const bool other_busy = active && !mine && flow != Flow::Idle;
  const char *icon;
  set_text(d_start_lbl, start_label(mine, other_busy, &icon));
  set_text(d_start_icon, icon);
  lv_obj_set_width(d_start, brewing ? D_LEFT_W - D_BTN_H - G : D_LEFT_W);
  show(d_stop, brewing);
  set_opa(d_start, link == LinkState::Connected && !other_busy ? LV_OPA_COVER : LV_OPA_50);
}

// Start: load the recipe (then "Place your cup" starts it); while brewing it pauses
// and resumes.
void on_start(lv_event_t *) {
  if (link != LinkState::Connected) return show_toast("Not connected to the xBloom");
  if (active == cur) {
    if (flow == Flow::Brewing) post(BleCmdType::Pause);
    else if (flow == Flow::Paused) post(BleCmdType::Resume);
    else if (flow == Flow::Loaded) show(modal, true);
    return;
  }
  if (flow != Flow::Idle) return show_toast("Another recipe is on the machine");
  const PanelRecipe *p = find(cur);
  if (!p) return;
  if (const char *problem = xb::validate(p->r)) return show_toast(problem);
  BleCmd load_cmd;
  load_cmd.type = BleCmdType::Load;
  load_cmd.recipe = p->r;
  active_copy = p->r;
  if (!ble_link::post(load_cmd)) return;
  active = cur;
  flow = Flow::Loading;
  refresh_detail();
}

void on_stop(lv_event_t *) { post(BleCmdType::Stop); }

// ============================================================================
// Grinder: grind size and speed cards, and a big start / stop button
// ============================================================================
// What a grind size suits, shown beside the number.
const char *grind_use(int size) {
  struct Use {
    int up_to;
    const char *name;
  };
  static const Use USES[] = {{12, "Espresso"},  {24, "Moka Pot"},     {32, "AeroPress"},
                             {55, "Pourover\nCoffee Maker"}, {70, "French Press"}};
  for (const Use &u : USES)
    if (size <= u.up_to) return u.name;
  return "Cold Brew";
}
ValueCard *gr_size_card;

void place_grind_use() {
  set_text(gr_use, grind_use(g_size));
  baseline_next_to(gr_use, gr_size_card->value, true, 20);
}

bool need_machine() {
  if (link == LinkState::Connected) return true;
  show_toast("Not connected to the xBloom");
  return false;
}

void grinder_back(lv_event_t *) {
  if (grinding) post(BleCmdType::GrinderStop);
  grinding = false;
  set_text(gr_play_icon, LV_SYMBOL_PLAY);
  post(BleCmdType::QuitGrinder);
  go_home_from_module();
}

void build_grinder() {
  scr_grinder = new_screen(BG_MOD);
  module_header(scr_grinder, Dots::Grinder, grinder_back);
  gr_weight = readout(scr_grinder, "0.0g", M);
  const int card_h = (MOD_BOTTOM - MOD_TOP - G) / 2;
  // Settings go to the machine when the finger lifts.
  gr_size_card = value_card(scr_grinder, M, MOD_TOP, MOD_LEFT_W, card_h, "Grind size", "Level", GRINDER_FG,
                            GRINDER_FILL, &inter_96_num, 1, 80, 1, g_size, "%.0f", [](double v, bool released) {
                              g_size = (int)v;
                              place_grind_use();
                              if (released) post(BleCmdType::GrinderEnter, g_size, g_rpm);
                            });
  gr_use = text(gr_size_card->card, "", &inter_20, hex(GRINDER_FG));
  lv_obj_set_style_text_align(gr_use, LV_TEXT_ALIGN_RIGHT, 0);
  place_grind_use();
  value_card(scr_grinder, M, MOD_TOP + card_h + G, MOD_LEFT_W, card_h, "Grinder Speed", "RPM", GRINDER_FG,
             GRINDER_FILL, &inter_96_num, 60, 120, 10, g_rpm, "%.0f", [](double v, bool released) {
               g_rpm = (int)v;
               if (released) post(BleCmdType::GrinderEnter, g_size, g_rpm);
             });
  const int d = 136;
  round_button(scr_grinder, MOD_RIGHT_X + (MOD_RIGHT_W - d) / 2, MOD_TOP + (MOD_BOTTOM - MOD_TOP - d) / 2, d,
               LV_SYMBOL_PLAY, &lv_font_montserrat_28, &gr_play_icon, [](lv_event_t *) {
                 if (!need_machine()) return;
                 grinding = !grinding;
                 post(grinding ? BleCmdType::GrinderStart : BleCmdType::GrinderStop, g_size, g_rpm);
                 set_text(gr_play_icon, grinding ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
               });
}

// ============================================================================
// Brewer: hot water by volume, temperature and flow, in a chosen pattern
// ============================================================================
const uint8_t PATTERN_ORDER[3] = {xb::PAT_CENTERED, xb::PAT_CIRCULAR, xb::PAT_SPIRAL};

void set_pattern(uint8_t pattern, bool send) {
  b_pattern = pattern;
  for (int i = 0; i < 3; ++i) {
    const bool on = PATTERN_ORDER[i] == pattern;
    lv_obj_set_style_bg_color(pat_btn[i], hex(on ? BREWER_SEL : SURFACE), 0);
    set_text_color(lv_obj_get_child(pat_btn[i], 1), hex(on ? BREWER_FG : 0xBBBBBB), 0);
  }
  if (send) post(BleCmdType::BrewerPattern, xb::pattern_to_byte(pattern));
}

void brewer_back(lv_event_t *) {
  if (pouring) post(BleCmdType::BrewerStop);
  pouring = false;
  set_text(br_play_icon, LV_SYMBOL_PLAY);
  post(BleCmdType::QuitBrewer);
  go_home_from_module();
}

void build_brewer() {
  scr_brewer = new_screen(BG_MOD);
  module_header(scr_brewer, Dots::Brewer, brewer_back);
  br_weight = readout(scr_brewer, "0.0g", M);
  br_timer = readout(scr_brewer, "00'00''", M + MOD_LEFT_W, true);
  const int gap = 14, card_h = (MOD_BOTTOM - MOD_TOP - 2 * gap) / 3;
  auto card_y = [&](int i) { return MOD_TOP + i * (card_h + gap); };
  value_card(scr_brewer, M, card_y(0), MOD_LEFT_W, card_h, "Volume", "ml", BREWER_FG, BREWER_FILL, &inter_64b, 30,
             500, 5, b_volume, "%.0f", [](double v, bool) { b_volume = (int)v; });
  value_card(scr_brewer, M, card_y(1), MOD_LEFT_W, card_h, "Temperature", "°C", BREWER_FG, BREWER_FILL, &inter_64b,
             39, 99, 1, b_temp, "%.0f", [](double v, bool released) {
               b_temp = (int)v;
               if (released) post(BleCmdType::BrewerTemp, 0, 0, xb::temp_display_to_wire(b_temp));
             });
  value_card(scr_brewer, M, card_y(2), MOD_LEFT_W, card_h, "Flow rate", "ml/s", BREWER_FG, BREWER_FILL, &inter_64b,
             3.0, 3.5, 0.1, b_flow, "%.1f", [](double v, bool) { b_flow = (float)v; });

  static const char *const NAMES[3] = {"Centered", "Circular", "Spiral"};
  const int bh = 56, bgap = 12;
  for (int i = 0; i < 3; ++i) {
    pat_btn[i] = option_button(scr_brewer, MOD_RIGHT_X, MOD_TOP + i * (bh + bgap), MOD_RIGHT_W, bh);
    lv_obj_align(pattern_icon(pat_btn[i], PATTERN_ORDER[i], 28, hex(0xDDDDDD)), LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_align(text(pat_btn[i], NAMES[i], &inter_20b, hex(0xBBBBBB)), LV_ALIGN_LEFT_MID, 58, 0);
    on_click(pat_btn[i], [](lv_event_t *e) { set_pattern((uint8_t)(intptr_t)lv_event_get_user_data(e), true); },
             (void *)(intptr_t)PATTERN_ORDER[i]);
  }
  set_pattern(b_pattern, false);
  const int d = 100;
  round_button(scr_brewer, MOD_RIGHT_X + (MOD_RIGHT_W - d) / 2, MOD_BOTTOM - d, d, LV_SYMBOL_PLAY,
               &lv_font_montserrat_28, &br_play_icon, [](lv_event_t *) {
                 if (!need_machine()) return;
                 pouring = !pouring;
                 if (pouring) {
                   pour_start_ms = millis();
                   post(BleCmdType::BrewerStart, xb::pattern_to_byte(b_pattern), 0, b_flow, b_volume,
                        xb::temp_display_to_wire(b_temp));
                 } else {
                   post(BleCmdType::BrewerStop);
                 }
                 set_text(br_play_icon, pouring ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
               });
}

// ============================================================================
// Scale: weight in g / oz / ml, tare, and a stopwatch
// ============================================================================
const char *const UNIT_NAMES[3] = {"g", "oz", "ml"};

void render_scale() {
  char reading[16];
  if (unit == 1) snprintf(reading, sizeof reading, "%.2f", weight_g / 28.3495f);
  else snprintf(reading, sizeof reading, "%.1f", weight_g);
  set_text(sc_value, reading);
  set_text(sc_unit, UNIT_NAMES[unit]);
  // Number and unit centred together; with Arabic units the unit is on the left.
  lv_obj_update_layout(sc_unit);
  const int shift = (lv_obj_get_width(sc_unit) + 6) / 2;
  const bool ar = i18n::arabic_units();
  lv_obj_align(sc_value, LV_ALIGN_CENTER, ar ? shift : -shift, 0);
  baseline_next_to(sc_unit, sc_value, ar, 6);
}

void set_unit(int u, bool send) {
  unit = u;
  for (int i = 0; i < 3; ++i) {
    lv_obj_set_style_bg_color(unit_btn[i], hex(i == u ? SCALE_SEL : SURFACE), 0);
    set_text_color(lv_obj_get_child(unit_btn[i], 0), hex(i == u ? SCALE_FG : 0xDDDDDD), 0);
  }
  render_scale();
  static const xb::WeightUnit CODES[3] = {xb::UNIT_G, xb::UNIT_OZ, xb::UNIT_ML};
  if (send) post(BleCmdType::WeightUnit, CODES[u]);
}

// Stopwatch: start from zero, pause; the next start begins again from zero.
void toggle_stopwatch(lv_event_t *) {
  if (sw_running) {
    sw_elapsed += millis() - sw_start;
    sw_running = false;
  } else {
    sw_elapsed = 0;
    sw_start = millis();
    sw_running = true;
  }
  set_text(sc_play_icon, sw_running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

void build_scale() {
  scr_scale = new_screen(BG_MOD);
  module_header(scr_scale, Dots::Scale, [](lv_event_t *) {
    post(BleCmdType::QuitScale);
    go_home_from_module();
  });
  sc_timer = readout(scr_scale, "00'00''", M);
  const int stopwatch = 48;
  round_button(scr_scale, M + MOD_LEFT_W - stopwatch, 96, stopwatch, LV_SYMBOL_PLAY, &lv_font_montserrat_16,
               &sc_play_icon, toggle_stopwatch);

  const int card_h = MOD_BOTTOM - MOD_TOP;
  lv_obj_t *card = box(scr_scale, M, MOD_TOP, MOD_LEFT_W, card_h, BG_MOD, R);
  lv_obj_set_style_border_color(card, hex(SCALE_FG), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_t *inner = box(card, 6, 6, MOD_LEFT_W - 12, card_h - 12, SCALE_FILL, R - 6);
  sc_value = text(inner, "0.0", &inter_96_num, hex(0x5FB5A4));
  sc_unit = text(inner, "g", &inter_34b, hex(0x5FB5A4));
  for (lv_obj_t *o : {sc_value, sc_unit}) {  // render_scale() places these itself
    lv_obj_add_flag(o, RTL_KEEP);
    lv_obj_set_style_base_dir(o, LV_BASE_DIR_LTR, 0);
  }

  const int bh = 60, bgap = 12;
  for (int i = 0; i < 3; ++i) {
    unit_btn[i] = option_button(scr_scale, MOD_RIGHT_X, MOD_TOP + i * (bh + bgap), MOD_RIGHT_W, bh);
    lv_obj_center(text(unit_btn[i], UNIT_NAMES[i], &inter_26b, hex(0xDDDDDD)));
    on_click(unit_btn[i], [](lv_event_t *e) { set_unit((int)(intptr_t)lv_event_get_user_data(e), true); },
             (void *)(intptr_t)i);
  }
  const int tare = 88;
  round_button(scr_scale, MOD_RIGHT_X + (MOD_RIGHT_W - tare) / 2, MOD_BOTTOM - tare, tare, "T", &inter_34b, nullptr,
               [](lv_event_t *) { post(BleCmdType::Tare); });
  set_unit(0, false);
}

// ============================================================================
// The recipe list: saved on the panel, kept in step with the account
// ============================================================================
const char *const LIB_FILE = "/recipes.json";

void assign_uid(PanelRecipe &p) {
  if (!p.uid) p.uid = next_uid++;
  else if (p.uid >= next_uid) next_uid = p.uid + 1;
}

void save_lib() {
  if (!storage::write(LIB_FILE, library::to_cache_json(lib))) Serial.println("[lib] save failed");
}

// The saved list, or (on first start) the recipes built in for before sign-in,
// then the starters with their saved favourite flags.
void load_lib() {
  std::vector<PanelRecipe> saved;
  std::string file;
  const bool have_file = storage::read(LIB_FILE, file) && library::from_cache_json(file.data(), file.size(), saved);
  lib.clear();
  for (const PanelRecipe &p : saved)
    if (p.kind != KIND_STARTER) lib.push_back(p);
  if (!have_file) lib = recipes::seeds();
  for (PanelRecipe starter : recipes::starters()) {
    for (const PanelRecipe &s : saved)
      if (s.kind == KIND_STARTER && strcmp(s.r.name, starter.r.name) == 0) starter.favorite = s.favorite;
    lib.push_back(starter);
  }
  for (PanelRecipe &p : lib) assign_uid(p);
}

void rebuild_lists() {
  build_favorites();
  build_recipe_list();
}

// After the list changed: leave the recipe screen if its recipe is gone, else
// redraw it (unless that recipe is on the machine right now).
void detail_follow_library() {
  if (lv_screen_active() != scr_detail) return;
  if (!find(cur)) return go_home_from_detail();
  if (!(active == cur && flow != Flow::Idle)) build_detail();
}

PanelRecipe *find_by_id(std::vector<PanelRecipe> &list, const char *id) {
  if (!*id) return nullptr;
  for (PanelRecipe &p : list)
    if (strcmp(p.id, id) == 0) return &p;
  return nullptr;
}

// A fresh copy of the account's recipes arrived. The account wins, except for panel
// edits it hasn't received yet (dirty), which are kept and sent again.
void merge_account(std::vector<PanelRecipe> &account) {
  std::vector<PanelRecipe> before = lib, after;
  for (PanelRecipe &a : account) {
    PanelRecipe *mine = find_by_id(before, a.id);
    if (!mine)  // a built-in or panel-made recipe of the same name becomes this one
      for (PanelRecipe &b : before)
        if (!b.id[0] && b.kind != KIND_STARTER && strcmp(b.r.name, a.r.name) == 0) {
          strlcpy(b.id, a.id, sizeof b.id);
          mine = &b;
          break;
        }
    if (mine && mine->dirty && mine->kind != KIND_SEED) {
      PanelRecipe keep = *mine;
      keep.kind = a.kind;
      strlcpy(keep.id, a.id, sizeof keep.id);
      after.push_back(keep);
      continue;
    }
    a.uid = mine ? mine->uid : 0;
    a.favorite = mine && mine->favorite;
    after.push_back(a);
  }
  // From the old list keep only recipes made on the panel and not yet sent. Gone:
  // built-in seeds (the account has them now) and recipes deleted in the app.
  for (const PanelRecipe &b : before)
    if (b.kind != KIND_STARTER && b.kind != KIND_SEED && !b.id[0] && b.dirty) after.push_back(b);
  for (const PanelRecipe &b : before)
    if (b.kind == KIND_STARTER) after.push_back(b);
  for (PanelRecipe &p : after) assign_uid(p);
  lib.swap(after);
  for (const PanelRecipe &p : lib)
    if (p.dirty && p.kind != KIND_STARTER && p.kind != KIND_SEED) cloud::push(p);
  save_lib();
  rebuild_lists();
  detail_follow_library();
}

// A recipe saved in the editor (new or changed). It becomes one of "mine".
void store_recipe(PanelRecipe p) {
  p.dirty = true;
  if (p.kind == KIND_SEED || p.kind == KIND_STARTER) p.kind = KIND_MINE;
  if (PanelRecipe *slot = find(p.uid)) {
    *slot = p;
  } else {
    assign_uid(p);
    lib.insert(lib.begin(), p);
  }
  save_lib();
  if (cloud::state() == cloud::Acct::SignedIn) cloud::push(p);
  rebuild_lists();
}

void delete_recipe(uint32_t uid) {
  for (auto it = lib.begin(); it != lib.end(); ++it)
    if (it->uid == uid) {
      if (it->id[0] && cloud::state() == cloud::Acct::SignedIn) cloud::remove(it->id);
      lib.erase(it);
      break;
    }
  save_lib();
  rebuild_lists();
}

void toggle_favorite(uint32_t uid) {
  PanelRecipe *p = find(uid);
  if (!p) return;
  p->favorite = !p->favorite;
  save_lib();
  build_favorites();
}

// ============================================================================
// Confirmation dialog (delete a recipe, sign out)
// ============================================================================
lv_obj_t *cf_layer, *cf_title, *cf_text, *cf_ok_lbl;
void (*cf_action)() = nullptr;

void build_confirm() {
  cf_layer = bare(lv_layer_top(), 0, 0, W, H);
  lv_obj_set_style_bg_color(cf_layer, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(cf_layer, LV_OPA_70, 0);
  lv_obj_add_flag(cf_layer, LV_OBJ_FLAG_CLICKABLE);
  const int dw = 456, dh = 228, pad = 28, bw = (dw - 2 * pad - G) / 2, bh = 56, by = dh - pad - bh;
  lv_obj_t *dialog = box(cf_layer, 0, 0, dw, dh, 0x1C1C1E, 24);
  lv_obj_center(dialog);
  cf_title = text(dialog, "", &inter_26b, hex(TEXT));
  lv_obj_set_pos(cf_title, pad, pad);
  cf_text = text(dialog, "", &inter_16, hex(SUB));
  lv_label_set_long_mode(cf_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(cf_text, dw - 2 * pad);
  lv_obj_set_pos(cf_text, pad, pad + 44);
  pill(dialog, pad, by, bw, bh, "Cancel", 0x2C2C2E, TEXT, [](lv_event_t *) { show(cf_layer, false); });
  lv_obj_t *ok = pill(dialog, pad + bw + G, by, bw, bh, "Delete", 0xFF453A, TEXT, [](lv_event_t *) {
    show(cf_layer, false);
    if (cf_action) cf_action();
  });
  cf_ok_lbl = lv_obj_get_child(ok, 0);
  show(cf_layer, false);
}

void confirm(const char *title, const char *body, const char *ok, void (*action)()) {
  set_text(cf_title, title);
  set_text(cf_text, body);
  set_text(cf_ok_lbl, ok);
  cf_action = action;
  lv_obj_move_foreground(cf_layer);
  show(cf_layer, true);
}

// ============================================================================
// Recipe editor: name, cup, dose / grind / speed on the left; the pours on the
// right (pick one with the numbered chips, + adds one like the last)
// ============================================================================
lv_obj_t *scr_edit, *ed_title, *ed_name, *ed_summary, *ed_delete, *ed_chips, *ed_cup_btn[4], *ed_pat_btn[3];
lv_obj_t *ed_agit_btn[2], *ed_remove_pour;
PanelRecipe ed;       // the copy being edited
uint32_t ed_uid = 0;  // the recipe it came from; 0 = a new one
int ed_pour = 0;      // pour being edited

enum Field { F_DOSE, F_GRIND, F_RPM, F_VOL, F_TEMP, F_WAIT, F_FLOW, F_COUNT };
lv_obj_t *ed_val[F_COUNT];
int ed_repeat = 0;  // repeats of the current hold on - or +

const uint32_t CHIP_OFF = 0x1C1C1E, CHIP_ON = 0x3A3A3C;

void refresh_editor();

// One press of - or + (dir -1 / +1). Holding repeats, and after a while takes bigger steps.
void adjust(Field f, int dir) {
  const int boost = ed_repeat > 20 ? 10 : ed_repeat > 6 ? 5 : 1;
  const bool held = boost > 1;
  xb::Recipe &r = ed.r;
  xb::Pour &q = r.pours[ed_pour];
  switch (f) {
    case F_DOSE: {
      double lo, hi;
      recipes::dose_range(r.cup_type, lo, hi);
      r.dose_g = fmin(hi, fmax(lo, r.dose_g + dir * 0.5 * (held ? 2 : 1)));
      break;
    }
    case F_GRIND: r.grind_size = (int)fmin(80, fmax(1, r.grind_size + dir * boost)); break;
    case F_RPM: r.rpm = (int)fmin(120, fmax(60, r.rpm + dir * 10)); break;
    case F_VOL: q.volume_ml = fmin(240, fmax(0, round(q.volume_ml) + dir * boost)); break;
    case F_TEMP: q.temp_c = fmin(99, fmax(40, round(q.temp_c) + dir * (held ? 2 : 1))); break;
    case F_WAIT: q.pause_s = (uint8_t)fmin(59, fmax(0, (int)q.pause_s + dir * (held ? 5 : 1))); break;
    case F_FLOW: q.flow = fmin(3.5, fmax(3.0, round((q.flow + dir * 0.1) * 10) / 10)); break;
    default: break;
  }
  refresh_editor();
}

void stepper_event(lv_event_t *e) {
  const intptr_t which = (intptr_t)lv_event_get_user_data(e);  // field << 1 | (1 for +)
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) ed_repeat = 0;
  else if (code == LV_EVENT_LONG_PRESSED_REPEAT) ++ed_repeat;
  else return;
  adjust((Field)(which >> 1), (which & 1) ? 1 : -1);
}

// [ -   name / value   + ]
void stepper(lv_obj_t *parent, int x, int y, int w, int h, const char *label, Field f) {
  lv_obj_t *card = box(parent, x, y, w, h, 0x1C1C1E, R_SM);
  lv_obj_align(text(card, label, &inter_14, hex(MUTED)), LV_ALIGN_TOP_MID, 0, 8);
  ed_val[f] = text(card, "", &inter_26b, hex(TEXT));
  lv_obj_align(ed_val[f], LV_ALIGN_BOTTOM_MID, 0, -8);
  for (int plus = 0; plus < 2; ++plus) {
    lv_obj_t *key = group(card, plus ? w - 52 : 0, 0, 52, h);
    lv_obj_center(text(key, plus ? LV_SYMBOL_PLUS : LV_SYMBOL_MINUS, &lv_font_montserrat_20, hex(SUB)));
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(key, hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, LV_STATE_PRESSED);
    void *which = (void *)(intptr_t)((f << 1) | plus);
    lv_obj_add_event_cb(key, stepper_event, LV_EVENT_PRESSED, which);
    lv_obj_add_event_cb(key, stepper_event, LV_EVENT_LONG_PRESSED_REPEAT, which);
  }
}

// Highlights choice `chosen` of a row of option boxes (their label is the last child).
void select_option(lv_obj_t **options, int n, int chosen, uint32_t on_bg = CHIP_ON) {
  for (int i = 0; i < n; ++i) {
    lv_obj_set_style_bg_color(options[i], hex(i == chosen ? on_bg : CHIP_OFF), 0);
    set_text_color(lv_obj_get_child(options[i], lv_obj_get_child_count(options[i]) - 1), hex(i == chosen ? TEXT : MUTED),
                   0);
  }
}

void on_pour_chip(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == ed.r.n_pours) {  // "+": a new pour, the same as the last
    ed.r.pours[i] = ed.r.pours[i - 1];
    ed.pour_names[i][0] = 0;
    ++ed.r.n_pours;
  }
  ed_pour = i;
  refresh_editor();
}

// 1 2 3 ... and a + while there is room for another pour.
void build_pour_chips() {
  lv_obj_clean(ed_chips);
  const int chip_w = 36, gap = 4, chips = ed.r.n_pours + (ed.r.n_pours < xb::MAX_POURS ? 1 : 0);
  for (int i = 0; i < chips; ++i) {
    const bool add = i == ed.r.n_pours, current = !add && i == ed_pour;
    lv_obj_t *chip = box(ed_chips, i * (chip_w + gap), 0, chip_w, 40, add ? 0x2C2C2E : current ? 0xFFFFFF : CHIP_OFF, 10);
    char number[4];
    snprintf(number, sizeof number, "%d", i + 1);
    lv_obj_center(text(chip, add ? LV_SYMBOL_PLUS : number, add ? &lv_font_montserrat_16 : &inter_16,
                       hex(current ? 0x000000 : TEXT)));
    pressable(chip);
    on_click(chip, on_pour_chip, (void *)(intptr_t)i);
  }
}

void refresh_editor() {
  const xb::Recipe &r = ed.r;
  const xb::Pour &q = r.pours[ed_pour];
  set_text(ed_name, r.name[0] ? r.name : "Untitled");
  char s[48];
  auto show_value = [&](Field f, const char *fmt, auto v) {
    snprintf(s, sizeof s, fmt, v);
    set_text(ed_val[f], s);
  };
  show_value(F_DOSE, "%gg", r.dose_g);
  show_value(F_GRIND, "%d", r.grind_size);
  show_value(F_RPM, "%d", r.rpm);
  show_value(F_VOL, "%.0f ml", q.volume_ml);
  show_value(F_TEMP, "%.0f°C", q.temp_c);
  show_value(F_WAIT, "%ds", q.pause_s);
  show_value(F_FLOW, "%.1f", q.flow);
  snprintf(s, sizeof s, "1:%g  |  %.0f ml", library::ratio_denominator(r), r.total_water());
  set_text(ed_summary, s);
  select_option(ed_cup_btn, 4, r.cup_type - 1);
  for (int i = 0; i < 3; ++i)
    if (PATTERN_ORDER[i] == q.pattern) select_option(ed_pat_btn, 3, i);
  const bool agitate[2] = {q.agitate_before, q.agitate_after};
  for (int i = 0; i < 2; ++i) lv_obj_set_style_bg_color(ed_agit_btn[i], hex(agitate[i] ? CHIP_ON : CHIP_OFF), 0);
  for (int i = 0; i < 2; ++i) set_text_color(lv_obj_get_child(ed_agit_btn[i], 0), hex(agitate[i] ? TEXT : MUTED), 0);
  show(ed_remove_pour, r.n_pours > 1);
  build_pour_chips();
}

void editor_save(lv_event_t *) {
  if (const char *problem = xb::validate(ed.r)) return show_toast(problem);
  store_recipe(ed);
  cur = ed.uid ? ed.uid : lib.front().uid;  // a new recipe goes to the top of the list
  if (cloud::state() != cloud::Acct::SignedIn) show_toast("Saved on the panel. Sign in to save it to your account too.");
  open_recipe(cur, Anim::PopRight);
}

void remove_current_pour(lv_event_t *) {
  xb::Recipe &r = ed.r;
  if (r.n_pours <= 1) return;
  for (int i = ed_pour; i < r.n_pours - 1; ++i) {
    r.pours[i] = r.pours[i + 1];
    memcpy(ed.pour_names[i], ed.pour_names[i + 1], sizeof ed.pour_names[0]);
  }
  --r.n_pours;
  ed.pour_names[r.n_pours][0] = 0;
  if (ed_pour >= r.n_pours) ed_pour = r.n_pours - 1;
  refresh_editor();
}

void build_editor() {
  scr_edit = new_screen(BG);
  back_button(scr_edit, LV_SYMBOL_LEFT, [](lv_event_t *) {  // leaves without saving
    if (ed_uid && find(ed_uid)) open_recipe(ed_uid, Anim::PopRight);
    else load(scr_home, Anim::PopRight);
  });
  ed_title = text(scr_edit, "Edit recipe", &inter_26b, hex(TEXT));
  lv_obj_set_pos(ed_title, M + 44, SB + 10);
  pill(scr_edit, W - M - 110, SB + 4, 110, 44, "Save", 0xFFFFFF, 0x000000, editor_save);
  ed_delete = pill(scr_edit, W - M - 110 - 12 - 110, SB + 4, 110, 44, "Delete", 0x2C2C2E, 0xFF6B6B, [](lv_event_t *) {
    const bool in_account = cloud::state() == cloud::Acct::SignedIn && ed.id[0];
    confirm("Delete recipe?",
            in_account ? "It will also be deleted from your xBloom account." : "It will be removed from the panel.",
            "Delete", [] {
              delete_recipe(ed_uid);
              load(scr_home, Anim::PopRight);
            });
  });

  // Left column.
  const int lx = M, lw = 352, top = 96;
  lv_obj_t *name_box = box(scr_edit, lx, top, lw, 52, 0x1C1C1E, R_SM);
  ed_name = text(name_box, "", &inter_20b, hex(TEXT));
  fit_text(ed_name, lw - 60, 48);
  lv_obj_align(ed_name, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_align(text(name_box, LV_SYMBOL_EDIT, &lv_font_montserrat_16, hex(MUTED)), LV_ALIGN_RIGHT_MID, -16, 0);
  pressable(name_box);
  on_click(name_box, [](lv_event_t *) {
    open_text_entry("Recipe name", ed.r.name, "Name", false, "Done", scr_edit, [](const char *t) {
      strlcpy(ed.r.name, t, sizeof ed.r.name);
      utf8_trim(ed.r.name);
      refresh_editor();
      load(scr_edit, Anim::PopRight);
    });
  });
  static const char *const CUPS[4] = {"xPod", "Omni", "Other", "Tea"};  // cup types 1-4
  const int cup_w = (lw - 3 * 6) / 4;
  for (int i = 0; i < 4; ++i) {
    ed_cup_btn[i] = box(scr_edit, lx + i * (cup_w + 6), top + 60, cup_w, 44, CHIP_OFF, 12);
    lv_obj_center(text(ed_cup_btn[i], CUPS[i], &inter_16, hex(MUTED)));
    pressable(ed_cup_btn[i]);
    on_click(ed_cup_btn[i], [](lv_event_t *e) {
      ed.r.cup_type = (int)(intptr_t)lv_event_get_user_data(e);
      double lo, hi;  // each cup has its own dose range
      recipes::dose_range(ed.r.cup_type, lo, hi);
      ed.r.dose_g = fmin(hi, fmax(lo, ed.r.dose_g));
      refresh_editor();
    }, (void *)(intptr_t)(i + 1));
  }
  const int step_h = 72, sy = top + 112;
  stepper(scr_edit, lx, sy, lw, step_h, "Dose", F_DOSE);
  stepper(scr_edit, lx, sy + step_h + 8, lw, step_h, "Grind size", F_GRIND);
  stepper(scr_edit, lx, sy + 2 * (step_h + 8), lw, step_h, "RPM", F_RPM);

  // Right column: the selected pour.
  const int rx = lx + lw + 24, rw = W - M - rx, half = (rw - 8) / 2, py = top + 48;
  ed_chips = group(scr_edit, rx, top, rw, 40);
  stepper(scr_edit, rx, py, half, step_h, "Volume", F_VOL);
  stepper(scr_edit, rx + half + 8, py, half, step_h, "Temperature", F_TEMP);
  stepper(scr_edit, rx, py + step_h + 8, half, step_h, "Wait after", F_WAIT);
  stepper(scr_edit, rx + half + 8, py + step_h + 8, half, step_h, "Flow rate", F_FLOW);
  static const char *const PATTERNS[3] = {"Centered", "Circular", "Spiral"};
  const int pat_w = (rw - 2 * 6) / 3, pat_y = py + 2 * (step_h + 8);
  for (int i = 0; i < 3; ++i) {
    ed_pat_btn[i] = box(scr_edit, rx + i * (pat_w + 6), pat_y, pat_w, 44, CHIP_OFF, 12);
    lv_obj_align(pattern_icon(ed_pat_btn[i], PATTERN_ORDER[i], 20, hex(0xDDDDDD)), LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_align(text(ed_pat_btn[i], PATTERNS[i], &inter_14, hex(MUTED)), LV_ALIGN_LEFT_MID, 36, 0);
    pressable(ed_pat_btn[i]);
    on_click(ed_pat_btn[i], [](lv_event_t *e) {
      ed.r.pours[ed_pour].pattern = (uint8_t)(intptr_t)lv_event_get_user_data(e);
      refresh_editor();
    }, (void *)(intptr_t)PATTERN_ORDER[i]);
  }
  static const char *const AGITATE[2] = {"Agitate before", "Agitate after"};
  for (int i = 0; i < 2; ++i) {
    ed_agit_btn[i] = box(scr_edit, rx + i * (half + 8), pat_y + 52, half, 40, CHIP_OFF, 12);
    lv_obj_center(text(ed_agit_btn[i], AGITATE[i], &inter_14, hex(MUTED)));
    pressable(ed_agit_btn[i]);
    on_click(ed_agit_btn[i], [](lv_event_t *e) {
      xb::Pour &q = ed.r.pours[ed_pour];
      bool &flag = (intptr_t)lv_event_get_user_data(e) ? q.agitate_after : q.agitate_before;
      flag = !flag;
      refresh_editor();
    }, (void *)(intptr_t)i);
  }
  ed_summary = text(scr_edit, "", &inter_16, hex(SUB));
  lv_obj_set_pos(ed_summary, rx + 4, H - M - 26);
  ed_remove_pour = text(scr_edit, "Remove this pour", &inter_16, hex(0xFF6B6B));
  lv_obj_align(ed_remove_pour, LV_ALIGN_TOP_RIGHT, -M - 4, H - M - 26);
  lv_obj_set_ext_click_area(ed_remove_pour, 12);
  on_click(ed_remove_pour, remove_current_pour);
}

// uid 0 starts a new recipe from the first starter.
void open_editor(uint32_t uid) {
  const PanelRecipe *p = uid ? find(uid) : nullptr;
  if (p) {
    ed = *p;
    if (p->kind == KIND_STARTER) {  // editing a starter makes a recipe of your own
      ed.uid = 0;
      ed.id[0] = 0;
      ed.kind = KIND_MINE;
      ed.favorite = false;
    }
  } else {
    ed = recipes::starters().front();
    strlcpy(ed.r.name, "New recipe", sizeof ed.r.name);
    ed.kind = KIND_MINE;
    ed.uid = 0;
    ed.color = CARDS[next_uid % N_CARDS];
  }
  ed_uid = ed.uid;
  ed_pour = 0;
  set_text(ed_title, ed_uid ? "Edit recipe" : "New recipe");
  show(ed_delete, ed_uid != 0);
  refresh_editor();
  load(scr_edit, Anim::PushLeft);
}

// ============================================================================
// xBloom account screen
// ============================================================================
lv_obj_t *scr_account, *ac_title, *ac_sub, *ac_msg, *ac_primary, *ac_primary_lbl, *ac_signout;
char ac_email[96];  // between the email and password steps of signing in
cloud::Acct last_acct = (cloud::Acct)255;

// "at 9:25 AM", or "not yet".
void sync_time(char *out, size_t n) {
  strlcpy(out, "not yet", n);
  const time_t synced = cloud::last_sync();
  struct tm t;
  if (!synced || !localtime_r(&synced, &t)) return;
  const int h = t.tm_hour % 12;
  snprintf(out, n, tr("at %d:%02d %s"), h ? h : 12, t.tm_min, tr(t.tm_hour < 12 ? "AM" : "PM"));
}

void refresh_account() {
  const cloud::Acct state = cloud::state();
  if (state == cloud::Acct::SignedIn) {
    set_text(ac_title, cloud::email());
    int created = 0, shared = 0;
    for (const PanelRecipe &p : lib) {
      if (p.id[0] && p.kind == KIND_MINE) ++created;
      if (p.kind == KIND_SHARED) ++shared;
    }
    char when[32], summary[128];
    sync_time(when, sizeof when);
    snprintf(summary, sizeof summary, tr("%d created  |  %d shared\nLast synced %s%s"), created, shared, when,
             cloud::busy() ? "  (syncing...)" : "");
    set_text(ac_sub, summary);
    set_text(ac_primary_lbl, cloud::busy() ? "Syncing..." : "Sync now");
  } else {
    set_text(ac_title, state == cloud::Acct::SigningIn ? "Signing in..." : "Not signed in");
    set_text(ac_sub, "Sign in with your xBloom app email and password to bring your recipes to the panel. "
                     "Edits sync both ways.");
    set_text(ac_primary_lbl, "Sign in");
  }
  set_text(ac_msg, cloud::message());
  show(ac_signout, state == cloud::Acct::SignedIn);
}

// Signing in takes two keyboard screens: email, then password.
void start_sign_in() {
  if (net::state() != net::Wifi::Connected) return show_toast("Connect to Wi-Fi first (Settings > Wi-Fi)");
  open_text_entry("Email", cloud::email(), "you@example.com", false, "Next", scr_account, [](const char *email) {
    strlcpy(ac_email, email, sizeof ac_email);
    open_text_entry("Password", "", "xBloom password", true, "Sign in", scr_account, [](const char *password) {
      cloud::sign_in(ac_email, password);
      refresh_account();
      load(scr_account, Anim::PopRight);
    });
  });
}

void build_account() {
  scr_account = new_screen(BG);
  sub_header(scr_account, "xBloom account", back_to_settings);
  const int inner_w = W - 2 * M - 48;
  lv_obj_t *card = box(scr_account, M, 96, W - 2 * M, 224, 0x1C1C1E, R_SM);
  ac_title = text(card, "", &inter_26b, hex(TEXT));
  lv_obj_set_pos(ac_title, 24, 22);
  ac_sub = text(card, "", &inter_20, hex(SUB));
  lv_label_set_long_mode(ac_sub, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ac_sub, inner_w);
  lv_obj_set_style_text_line_space(ac_sub, 6, 0);
  lv_obj_set_pos(ac_sub, 24, 68);
  ac_msg = text(card, "", &inter_16, hex(0xFF6B6B));  // the account's last problem, if any
  lv_label_set_long_mode(ac_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ac_msg, inner_w);
  lv_obj_set_pos(ac_msg, 24, 170);
  // One button: Sign in, or Sync now once signed in.
  ac_primary = pill(scr_account, M, 336, 260, 56, "Sign in", 0xFFFFFF, 0x000000, [](lv_event_t *) {
    if (cloud::state() == cloud::Acct::SignedIn) cloud::sync_now();
    else start_sign_in();
  });
  ac_primary_lbl = lv_obj_get_child(ac_primary, 0);
  ac_signout = pill(scr_account, M + 260 + G, 336, 200, 56, "Sign out", 0x2C2C2E, 0xFF6B6B, [](lv_event_t *) {
    confirm("Sign out?", "Your account's recipes will be removed from the panel. Recipes made on the panel stay.",
            "Sign out", [] { cloud::sign_out(); });
  });
  lv_obj_set_pos(text(scr_account, "The panel saves your password so it can sign in again by itself.", &inter_16,
                      hex(MUTED)),
                 M, 412);
}

void open_account(lv_event_t *) {
  refresh_account();
  load(scr_account, Anim::PushLeft);
}

// After signing out, only the starters and recipes made on the panel remain.
void drop_account_recipes() {
  std::vector<PanelRecipe> keep;
  for (const PanelRecipe &p : lib)
    if (p.kind == KIND_STARTER || (!p.id[0] && p.kind != KIND_SHARED)) keep.push_back(p);
  lib.swap(keep);
  save_lib();
  rebuild_lists();
  detail_follow_library();
}

void handle_cloud(const cloud::Event &e) {
  char line[160];
  switch (e.type) {
    case cloud::Ev::Synced:
      merge_account(*e.list);
      delete e.list;
      break;
    case cloud::Ev::SyncFailed:
      if (lv_screen_active() == scr_account) refresh_account();
      refresh_sync_row();
      break;
    case cloud::Ev::Pushed:
      if (PanelRecipe *p = find(e.uid)) {
        strlcpy(p->id, e.id, sizeof p->id);
        p->dirty = false;
        save_lib();
        build_recipe_list();
      }
      break;
    case cloud::Ev::PushFailed:
      snprintf(line, sizeof line, tr("Saved on the panel, but not to your account: %s"), e.msg);
      show_toast(line);
      break;
    case cloud::Ev::RemoveFailed:
      snprintf(line, sizeof line, tr("Couldn't delete it from your account: %s"), e.msg);
      show_toast(line);
      break;
    case cloud::Ev::Removed: break;
    case cloud::Ev::Auth:
      if (cloud::state() == cloud::Acct::SignedOut) drop_account_recipes();
      if (cloud::state() == cloud::Acct::BadLogin && e.msg[0]) show_toast(e.msg);
      break;
  }
}

// ============================================================================
// Machine readings and connection
// ============================================================================
void render_weight() {
  char reading[16];
  snprintf(reading, sizeof reading, "%.1fg", weight_g);
  set_text(gr_weight, reading);
  set_text(br_weight, reading);
  render_scale();
}

const char *link_word(LinkState s) {
  return s == LinkState::Connected ? "Connected" : s == LinkState::Connecting ? "Connecting" : "Searching";
}

void set_link(LinkState s, const char *name) {
  link = s;
  if (name && *name) strncpy(machine_name, name, sizeof machine_name - 1);
  const bool up = s == LinkState::Connected;
  set_text(home_name, machine_name);
  if (st_machine_val) set_text(st_machine_val, up ? machine_name : "Not connected");
  show(home_spin, !up);
  show(home_dot, up);
  set_text(home_status, link_word(s));
  // One row on Home:  XBLOOM 381 >   ● Connected   (a spinner instead of the dot while searching)
  baseline_next_to(home_chev, home_name, false, 8);
  lv_obj_update_layout(home_chev);
  align_beside(home_dot, home_chev, LV_ALIGN_OUT_RIGHT_MID, 22, 0);
  align_beside(home_spin, home_chev, LV_ALIGN_OUT_RIGHT_MID, 19, 0);
  baseline_next_to(home_status, home_name, false, 0);
  lv_obj_update_layout(home_dot);
  if (rtl) {
    lv_obj_update_layout(home_status);
    lv_obj_set_x(home_status, lv_obj_get_x(home_dot) - 8 - lv_obj_get_width(home_status));
  } else {
    lv_obj_set_x(home_status, lv_obj_get_x(home_dot) + 10 + 8);
  }
  set_text_color(sb_bt, hex(up ? TEXT : MUTED), 0);
  if (!up) {  // whatever the machine was doing is over
    if (flow != Flow::Idle) {
      flow = Flow::Idle;
      active = 0;
      dotfx::brew_hide();
      intro_for = 0;
    }
    grinding = pouring = false;
  }
  refresh_detail();
}

// ============================================================================
// Brewing: the dot scene for the whole brew, and the machine's events
// ============================================================================
uint32_t water_seen_ms = 0;  // when the poured volume last went up
float last_water = 0;

const char *pattern_name(uint8_t p) {
  return p == xb::PAT_CENTERED ? "Centered" : p == xb::PAT_CIRCULAR ? "Circular" : "Spiral";
}

void update_brew_scene() {
  if (!dotfx::brew_showing()) return;
  const PanelRecipe *p = find(active);
  if (!p) return;
  const uint32_t now = millis();
  if (water_ml > last_water + 0.4f) {
    last_water = water_ml;
    water_seen_ms = now;
  }
  dotfx::BrewView v;
  v.water_ml = water_ml;
  v.total_ml = p->r.total_water();
  v.grams = weight_g;
  v.ms = brew_start_ms ? now - brew_start_ms : 0;
  v.paused = flow == Flow::Paused;
  v.pouring = water_seen_ms && now - water_seen_ms < 1500;  // water went in within the last 1.5 s
  // Title and line: "Starting" and the recipe, then each pour's name and settings.
  char title[32], line[48];
  const int i = pour_idx >= 0 && pour_idx < p->r.n_pours ? pour_idx : -1;
  if (i < 0) {
    strlcpy(title, tr("Starting"), sizeof title);
    snprintf(line, sizeof line, tr("%d pours  |  %.0f ml"), p->r.n_pours, p->r.total_water());
  } else {
    const xb::Pour &pour = p->r.pours[i];
    shown_pour_name(*p, i, title, sizeof title);
    snprintf(line, sizeof line, tr("%.0f\xC2\xB0" "C  |  %s  |  %d of %d"), pour.temp_c, tr(pattern_name(pour.pattern)),
             i + 1, p->r.n_pours);
  }
  v.pour_title = title;
  v.pour_sub = line;
  dotfx::brew_update(v);
}

// The brewing scene comes up once per brew; its buttons pause / resume and stop.
void brew_started() {
  if (!active || intro_for == active) return;
  intro_for = active;
  const PanelRecipe *p = find(active);
  if (!p) return;
  dotfx::brew_show(
      p->r.name, [] { post(flow == Flow::Paused ? BleCmdType::Resume : BleCmdType::Pause); },
      [] { post(BleCmdType::Stop); });
  water_seen_ms = 0;
  last_water = 0;
  update_brew_scene();
  wake_req = true;
}

void brew_ended() {
  intro_for = 0;
  dotfx::brew_hide();
  flow = Flow::Idle;
  active = 0;
  brew_start_ms = 0;
  pour_idx = -1;
  refresh_detail();
}

// Machine activity codes that mean a brew is under way (grinding included).
bool brewing_activity(int a) { return a == 34 || a == 30 || a == 35 || a == 16; }

void on_command_failed(BleCmdType t, const char *why) {
  show_toast(why[0] ? why : "Command failed");
  switch (t) {
    case BleCmdType::Load: brew_ended(); break;
    case BleCmdType::Execute: flow = Flow::Loaded; break;
    case BleCmdType::GrinderStart:
      grinding = false;
      set_text(gr_play_icon, LV_SYMBOL_PLAY);
      break;
    case BleCmdType::BrewerStart:
      pouring = false;
      set_text(br_play_icon, LV_SYMBOL_PLAY);
      break;
    default: break;
  }
}

void on_command_done(BleCmdType t) {
  if (t == BleCmdType::Load) {  // loaded: ask for the cup
    flow = Flow::Loaded;
    if (lv_screen_active() == scr_detail && cur == active) show(modal, true);
  } else if (t == BleCmdType::Execute) {
    flow = Flow::Brewing;
    water_ml = 0;
    brew_start_ms = millis();
    brew_started();
  } else if (t == BleCmdType::Stop) {
    brew_ended();
  }
}

void handle(const UiEvent &e) {
  switch (e.type) {
    case UiEvType::Link: set_link((LinkState)e.i, e.msg); break;
    case UiEvType::Weight:
      weight_g = e.f;
      if (fabsf(weight_g - wake_ref_weight) >= 2.0f) {  // a cup put down or lifted: wake the screen
        wake_ref_weight = weight_g;
        wake_req = true;
      }
      render_weight();
      break;
    case UiEvType::Water: water_ml = e.f > 5000 ? e.f / 1000.0f : e.f; break;  // the final total comes in thousandths
    case UiEvType::PourIndex: pour_idx = e.i; break;
    case UiEvType::Activity:
      wake_req = true;  // the knob turned, a brew phase changed...
      if (brewing_activity(e.i) && active && (flow == Flow::Loaded || flow == Flow::Starting)) {
        flow = Flow::Brewing;  // started on the machine itself
        if (!brew_start_ms) brew_start_ms = millis();
        brew_started();
      }
      if (e.i == 1 && active && (flow == Flow::Brewing || flow == Flow::Paused)) brew_ended();  // back home
      refresh_detail();
      break;
    case UiEvType::BrewDone: {
      const PanelRecipe *p = find(active);
      const float ml = water_ml > 0 ? water_ml : p ? p->r.total_water() : 0;
      dotfx::finish_play(p ? p->r.name : "Your coffee", weight_g, ml, brew_start_ms ? millis() - brew_start_ms : 0);
      wake_req = true;
      brew_ended();
      break;
    }
    case UiEvType::BrewAbandoned: brew_ended(); break;
    case UiEvType::BrewPaused:
      if (flow == Flow::Brewing) flow = Flow::Paused;
      refresh_detail();
      break;
    case UiEvType::BrewResumed:
      if (flow == Flow::Paused) flow = Flow::Brewing;
      refresh_detail();
      break;
    case UiEvType::Fault: show_toast(e.msg); break;
    case UiEvType::Sleep: break;
    case UiEvType::PodScanned: wake_req = true; break;
    case UiEvType::CmdResult:
      if (e.ok) on_command_done((BleCmdType)e.i);
      else on_command_failed((BleCmdType)e.i, e.msg);
      refresh_detail();
      break;
  }
}

// ---- back to Home when left alone ----------------------------------------------
// After 30 s untouched with the machine idle, close whatever is open and show Home.
const uint32_t HOME_AFTER_MS = 30 * 1000;

void go_home_idle() {
  dotfx::finish_hide();
  show(modal, false);
  show(cf_layer, false);
  show(toast, false);
  lv_obj_t *s = lv_screen_active();
  if (s == scr_grinder) {
    grinder_back(nullptr);
  } else if (s == scr_brewer) {
    brewer_back(nullptr);
  } else if (s == scr_scale) {
    post(BleCmdType::QuitScale);
    load(scr_home, Anim::None);
  } else if (s == scr_update) {
    net::update_page(false);
    load(scr_home, Anim::None);
  } else if (s != scr_home) {
    load(scr_home, Anim::None);
  }
  set_tab(0);
}

void check_idle_home() {
  if (ui::busy() || dotfx::playing()) return;
  if (lv_display_get_inactive_time(nullptr) < HOME_AFTER_MS) return;
  const bool on_home_tab = lv_screen_active() == scr_home && !lv_obj_has_flag(page_home, LV_OBJ_FLAG_HIDDEN);
  if (on_home_tab && !dotfx::finish_showing()) return;
  go_home_idle();
}

void build_toast() {
  toast = box(lv_layer_top(), 0, 0, 600, 0, 0x2C2C2E, R_SM);
  lv_obj_set_height(toast, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(toast, 20, 0);
  lv_obj_set_style_pad_ver(toast, 14, 0);
  lv_obj_set_style_shadow_width(toast, 24, 0);
  lv_obj_set_style_shadow_opa(toast, LV_OPA_50, 0);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, SB + 8);
  toast_lbl = text(toast, "", &inter_20, hex(TEXT));
  lv_label_set_long_mode(toast_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(toast_lbl, 560);
  on_click(toast, [](lv_event_t *) { show(toast, false); });  // tap to dismiss, or it goes after 4 s
  show(toast, false);
  toast_timer = lv_timer_create(
      [](lv_timer_t *t) {
        show(toast, false);
        lv_timer_pause(t);
      },
      4000, nullptr);
  lv_timer_pause(toast_timer);
}

// Five times a second: timers, the brewing scene, network and account state.
void refresh_periodic() {
  char t[16];
  fmt_time(t, sizeof t, pouring ? millis() - pour_start_ms : 0);
  set_text(br_timer, t);
  fmt_time(t, sizeof t, sw_elapsed + (sw_running ? millis() - sw_start : 0));
  set_text(sc_timer, t);
  if (lv_screen_active() == scr_detail && active == cur) refresh_detail();
  update_brew_scene();
  check_idle_home();
  refresh_net();
  const cloud::Acct acct = cloud::state();
  set_text(st_acc_val, acct == cloud::Acct::SignedIn    ? cloud::email()
                       : acct == cloud::Acct::SigningIn ? "Signing in..."
                                                        : "Not signed in");
  if (lv_screen_active() == scr_account) refresh_account();
  // A manual pour ends by itself at its volume: reset the button once it should be over.
  if (pouring && millis() - pour_start_ms > (uint32_t)((b_volume / b_flow + 8) * 1000)) {
    pouring = false;
    set_text(br_play_icon, LV_SYMBOL_PLAY);
  }
}

}  // namespace

// ============================================================================
// Public (ui.h)
// ============================================================================
namespace ui {

static lv_obj_t *scr_splash = nullptr;

// The Double Shot boot logo (src/boot_logo.c), from power-on until begin() is done.
void splash() {
  scr_splash = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr_splash, hex(BG), 0);
  lv_obj_set_style_bg_opa(scr_splash, LV_OPA_COVER, 0);
  lv_obj_t *logo = lv_image_create(scr_splash);
  lv_image_set_src(logo, &boot_logo);
  lv_obj_set_style_image_recolor(logo, hex(TEXT), 0);  // the logo is an alpha mask: this is its colour
  lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
  lv_obj_center(logo);
  lv_screen_load(scr_splash);
  lv_refr_now(nullptr);  // draw it now: start-up keeps the loop busy for a while
}

void begin() {
  // Language setting: 0 English, 1 Arabic with Arabic numbers and units, 2 Arabic with English units.
  i18n::set_lang(net::pref_lang() >= 1 ? i18n::Lang::AR : i18n::Lang::EN);
  i18n::set_arabic_units(net::pref_lang() == 1);
  rtl = i18n::rtl();
  if (rtl) {
    lv_obj_set_style_base_dir(lv_layer_top(), LV_BASE_DIR_RTL, 0);
    lv_display_add_event_cb(lv_display_get_default(), rtl_sweep, LV_EVENT_REFR_START, nullptr);
  }
  storage::begin();
  load_lib();
  build_home_screen();
  build_grinder();
  build_brewer();
  build_scale();
  build_wifi();
  build_text_entry();
  build_editor();
  build_account();
  build_update();
  build_store();
  build_status_bar();
  build_modal();
  build_confirm();
  build_update_prompt();
  build_clock();
  build_toast();
  lv_screen_load(scr_home);
  if (scr_splash) {
    lv_obj_delete(scr_splash);
    scr_splash = nullptr;
  }
  set_link(LinkState::Scanning, nullptr);
}

void tick() {
  UiEvent e;
  for (int n = 0; n < 24 && ble_link::poll(e); ++n) handle(e);  // at most 24 per loop: keep drawing smooth
  cloud::Event ce;
  while (cloud::poll(ce)) handle_cloud(ce);
  static uint32_t last = 0;
  if (millis() - last > 200) {
    last = millis();
    refresh_periodic();
  }
}

bool busy() {
  return flow != Flow::Idle || grinding || pouring || sw_running || net::update_state() == net::Upd::Receiving;
}

void show_clock(bool on) {
  if (on) {
    refresh_clock();
    lv_obj_move_foreground(clock_layer);
  }
  show(clock_layer, on);
}

// Standby dimming, done on the picture: a black veil over everything, so the
// backlight stays fully on. The board dims the backlight by pulsing it, and its
// coil whines audibly at the in-between levels.
void set_dim(uint8_t opa) {
  static lv_obj_t *veil = nullptr;
  if (!veil) {
    veil = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(veil);
    lv_obj_set_size(veil, W, H);
    lv_obj_set_style_bg_color(veil, lv_color_black(), 0);
    lv_obj_remove_flag(veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(veil, LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_obj_set_style_bg_opa(veil, opa, 0);
  show(veil, opa > 0);
}

bool take_wake() {
  const bool wake = wake_req;
  wake_req = false;
  return wake;
}

#ifdef UI_SIM
int sim_find(const char *name) {
  for (size_t i = 0; i < lib.size(); i++)
    if (!strcmp(lib[i].r.name, name)) return (int)i;
  return 0;
}

// Desktop preview: jump straight to a screen or state (see sim/sim_main.cpp).
void sim_show(int which, int recipe) {
  const uint32_t uid = lib[recipe].uid;
  auto brewing = [&](float water, float grams, uint32_t elapsed) {
    open_recipe(uid, Anim::None);
    active = uid;
    flow = Flow::Brewing;
    water_ml = water;
    weight_g = grams;
    brew_start_ms = millis() - elapsed;
  };
  switch (which) {
    case 0: lv_screen_load(scr_home); set_tab(0); break;
    case 1: lv_screen_load(scr_home); set_tab(1); break;
    case 2: open_recipe(uid, Anim::None); break;
    case 3: brewing(95, 88.4f, 74000); refresh_detail(); break;
    case 4: lv_screen_load(scr_grinder); break;
    case 5: lv_screen_load(scr_brewer); break;
    case 6: lv_screen_load(scr_scale); break;
    case 7: show(modal, true); break;
    case 8: show(modal, false); break;
    case 9: lv_screen_load(scr_home); set_tab(2); break;
    case 10: open_wifi(); break;
    case 11: open_password("Home Wi-Fi"); break;
    case 12: open_update(); break;
    case 13: show_clock(true); break;
    case 14: show_clock(false); break;
    case 15: open_editor(uid); break;
    case 16: lv_screen_load(scr_home); set_tab(2); open_account(); break;
    case 17: lv_screen_load(scr_home); set_tab(1); set_seg(1); break;
    case 18: lv_screen_load(scr_home); set_tab(1); set_seg(0); break;
    case 19: confirm("Delete recipe?", "It will also be deleted from your xBloom account.", "Delete", nullptr); break;
    case 20: show(cf_layer, false); break;
    case 21: brewing(0, 0, 0); intro_for = 0; refresh_detail(); brew_started(); break;  // brew-start animation
    case 22: dotfx::finish_play(lib[recipe].r.name, 141.3f, 220, 181000); break;
    case 23: clock_color = dotfx::WHITE; show_clock(true); break;
    case 24: clock_color = dotfx::BLUE; show_clock(true); break;
    case 25: brewing(220000 / 1000.0f, 141.3f, 181000); refresh_detail(); break;
    case 26: brew_ended(); break;
    case 27: lv_screen_load(scr_home); set_tab(2); lv_dropdown_open(st_sleep_dd); break;
    case 28: open_store(nullptr); break;
    case 29: close_store(nullptr); break;
    case 30: lv_screen_load(scr_home); set_tab(2); lv_dropdown_open(st_lang_dd); break;
    case 31: lv_screen_load(scr_home); set_tab(1); set_seg(2); break;  // the starters
    case 32: {  // emoji test card: every kind, in each text size
      lv_obj_t *scr = new_screen(BG);
      static const char *const lines[] = {
          "Faces 😀😋🥹🫠🤌 hearts ❤️🧡💛 ☕️🍓🍫🍯🧊",
          "Tones 👍🏻👍🏽👍🏿 👩🏽‍💻 🧑🏾‍🍳 👨‍👩‍👧‍👦 🏳️‍🌈 🏴‍☠️",
          "Keycaps 1️⃣#️⃣ flags 🇸🇦🇯🇵🇺🇸 🏴󠁧󠁢󠁳󠁣󠁴󠁿 ©️ new 🫨🪿🪼",
          "Plain text stays: # 1 © ® 2026",
          "قهوة ☕ مع 🍓 وتمر 🌴"};
      const lv_font_t *fonts[] = {&inter_14, &inter_16, &inter_20, &inter_26b, &inter_34b};
      int y = 16;
      for (int i = 0; i < 5; i++) {
        lv_obj_t *l = text(scr, lines[i], fonts[i], hex(TEXT));
        if (i == 4) lv_obj_set_style_base_dir(l, LV_BASE_DIR_AUTO, 0);
        lv_obj_set_pos(l, 16, y);
        y += i < 2 ? 40 : 56;
      }
      lv_screen_load(scr);
      break;
    }
    case 33: open_editor(uid); open_text_entry("Recipe name", lib[recipe].r.name, "Name", false, "Done", scr_edit,
                                                [](const char *t) { printf("entered: %s\n", t); });
             break;
    case 34: te_submit(nullptr); break;
    case 35: lv_screen_load(scr_home); open_update_prompt(); break;  // the "Update available" pop-up
  }
}
#endif

}  // namespace ui

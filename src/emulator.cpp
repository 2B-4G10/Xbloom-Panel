// Emulator build: in-memory display, scripted taps, a simulated xBloom Studio and
// screen pictures in the serial log. See include/emulator.h.
#ifdef XB_EMULATOR
#include "emulator.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <deque>

#include "ble_link.h"
#include "emoji.h"
#include "i18n.h"
#include "panel_net.h"
#include "pins_config.h"
#include "ui.h"

LV_FONT_DECLARE(inter_20);
LV_FONT_DECLARE(inter_26b);

namespace {

// ---- display: two frames in PSRAM; the last finished one is the picture ----
uint16_t *finished = nullptr;

void flush(lv_display_t *d, const lv_area_t *, uint8_t *px) {
  if (lv_display_flush_is_last(d)) finished = (uint16_t *)px;
  lv_display_flush_ready(d);
}

uint32_t uptime_ms() { return millis(); }

// ---- pictures: run-length coded RGB565, base64, between @@SHOT and @@END ----
void put_base64(const uint8_t *data, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char line[80];
  int len = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = data[i] << 16 | (i + 1 < n ? data[i + 1] << 8 : 0) | (i + 2 < n ? data[i + 2] : 0);
    line[len++] = A[v >> 18];
    line[len++] = A[(v >> 12) & 63];
    line[len++] = i + 1 < n ? A[(v >> 6) & 63] : '=';
    line[len++] = i + 2 < n ? A[v & 63] : '=';
    if (len >= 76) {
      line[len] = 0;
      Serial.printf("@@%s\n", line);
      len = 0;
    }
  }
  if (len) {
    line[len] = 0;
    Serial.printf("@@%s\n", line);
  }
}

// ---- the simulated machine ----
std::deque<UiEvent> to_ui;
struct Later { uint32_t at; UiEvent e; };
std::deque<Later> later;
LinkState g_link = LinkState::Idle;

void send(UiEvType t, int32_t i = 0, float f = 0, const char *msg = nullptr, bool ok = true, uint32_t after = 0) {
  UiEvent e;
  e.type = t;
  e.i = i;
  e.f = f;
  e.ok = ok;
  if (msg) strlcpy(e.msg, msg, sizeof e.msg);
  later.push_back({millis() + after, e});
}

struct {
  bool on = false, paused = false;
  xb::Recipe r;
  int pour = 0;
  float in_pour = 0;   // ml of the current pour so far
  float water = 0;
  uint32_t last = 0;
} brew;

xb::Recipe loaded;
float scale_g = 0;

void command(const BleCmd &c) {
  Serial.printf("[machine] command %d\n", (int)c.type);
  switch (c.type) {
    case BleCmdType::Load: {
      // The real frames, as the panel would send them.
      const auto frames = xb::build_load_frames(c.recipe);
      size_t bytes = 0;
      for (const auto &f : frames) bytes += f.size();
      const char *problem = xb::validate(c.recipe);
      Serial.printf("[machine] load \"%s\": %u frames, %u bytes, %s\n", c.recipe.name, (unsigned)frames.size(),
                    (unsigned)bytes, problem ? problem : "valid");
      loaded = c.recipe;
      send(UiEvType::CmdResult, (int)c.type, 0, problem, !problem, 400);
      return;
    }
    case BleCmdType::Execute:
      send(UiEvType::CmdResult, (int)c.type, 0, nullptr, true, 300);
      brew = {};
      brew.on = true;
      brew.r = loaded;
      brew.last = millis() + 300;
      send(UiEvType::Activity, 34, 0, nullptr, true, 350);
      send(UiEvType::PourIndex, 0, 0, nullptr, true, 350);
      return;
    case BleCmdType::Pause:
      brew.paused = true;
      send(UiEvType::CmdResult, (int)c.type);
      send(UiEvType::BrewPaused);
      return;
    case BleCmdType::Resume:
      brew.paused = false;
      brew.last = millis();
      send(UiEvType::CmdResult, (int)c.type);
      send(UiEvType::BrewResumed);
      return;
    case BleCmdType::Stop:
      brew.on = false;
      send(UiEvType::CmdResult, (int)c.type, 0, nullptr, true, 200);
      send(UiEvType::Activity, 1, 0, nullptr, true, 300);
      return;
    case BleCmdType::Tare:
      scale_g = 0;
      send(UiEvType::Weight, 0, 0);
      send(UiEvType::CmdResult, (int)c.type);
      return;
    default:
      send(UiEvType::CmdResult, (int)c.type, 0, nullptr, true, 150);
      return;
  }
}

// Pours at 20 ml/s, waits at a tenth: a brew takes seconds, not minutes.
void run_brew() {
  if (!brew.on || brew.paused || millis() < brew.last) return;
  const float dt = (millis() - brew.last) / 1000.0f;
  brew.last = millis();
  if (brew.pour >= brew.r.n_pours) return;
  const xb::Pour &p = brew.r.pours[brew.pour];
  const float add = fminf(20.0f * dt, (float)p.volume_ml - brew.in_pour);
  brew.in_pour += add;
  brew.water += add;
  scale_g = brew.water * 0.9f;
  send(UiEvType::Water, 0, brew.water);
  send(UiEvType::Weight, 0, scale_g);
  if (brew.in_pour >= p.volume_ml - 0.01f) {
    brew.pour++;
    brew.in_pour = 0;
    brew.last = millis() + p.pause_s * 100;
    if (brew.pour < brew.r.n_pours) {
      send(UiEvType::PourIndex, brew.pour, 0, nullptr, true, p.pause_s * 100);
    } else {
      brew.on = false;
      send(UiEvType::Water, 0, brew.water * 1000, nullptr, true, 500);  // the final total, in thousandths
      send(UiEvType::BrewDone, 0, 0, nullptr, true, 600);
      send(UiEvType::Activity, 1, 0, nullptr, true, 700);
    }
  }
}

// ---- scripted touch ----
lv_point_t touch_at = {0, 0};
bool touch_down = false;

void read_touch(lv_indev_t *, lv_indev_data_t *d) {
  d->point = touch_at;
  d->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// The text a label shows for an English UI string.
const char *shown(const char *s) {
  static char a[160], b[160];
  return i18n::localize(emoji::map(i18n::tr(s), a, sizeof a), b, sizeof b);
}

lv_obj_t *find_in(lv_obj_t *o, const char *text, bool want_textarea) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return nullptr;
  if (want_textarea ? lv_obj_check_type(o, &lv_textarea_class)
                    : lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), text))
    return o;
  if (lv_obj_check_type(o, &lv_textarea_class) || lv_obj_check_type(o, &lv_keyboard_class)) return nullptr;
  for (int32_t i = (int32_t)lv_obj_get_child_count(o) - 1; i >= 0; --i)  // topmost first
    if (lv_obj_t *f = find_in(lv_obj_get_child(o, i), text, want_textarea)) return f;
  return nullptr;
}

lv_obj_t *find(const char *text, bool want_textarea = false) {
  lv_obj_t *roots[] = {lv_layer_sys(), lv_layer_top(), lv_screen_active()};
  for (lv_obj_t *r : roots)
    if (lv_obj_t *f = find_in(r, text, want_textarea)) return f;
  return nullptr;
}

// ---- the script ----
enum class Op { Wait, Tap, TapAny, Again, Expect, Shot, Type, Call, Mark };
struct Step {
  Op op;
  const char *s;
  int ms;
};

#define BACK_SYMBOLS LV_SYMBOL_LEFT "|" LV_SYMBOL_RIGHT "|" LV_SYMBOL_DOWN

const Step PHASE_EN[] = {
    {Op::Expect, "Grinder", 20000},
    {Op::Wait, nullptr, 3000},
    {Op::Shot, "en_01_home", 0},

    {Op::Mark, "grinder", 0},
    {Op::Tap, "Grinder", 1200},
    {Op::Shot, "en_02_grinder", 0},
    {Op::TapAny, LV_SYMBOL_PLAY, 1500},
    {Op::Shot, "en_03_grinding", 0},
    {Op::Again, nullptr, 800},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "brewer", 0},
    {Op::Tap, "Brewer", 1200},
    {Op::Tap, "Spiral", 600},
    {Op::TapAny, LV_SYMBOL_PLAY, 1500},
    {Op::Shot, "en_04_brewer_pouring", 0},
    {Op::Again, nullptr, 800},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "scale", 0},
    {Op::Tap, "Scale", 1200},
    {Op::Tap, "oz", 800},
    {Op::Shot, "en_05_scale_oz", 0},
    {Op::Tap, "g", 400},
    {Op::Tap, "T", 600},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "recipes", 0},
    {Op::Tap, "Recipes", 1200},
    {Op::Tap, "Starter", 1000},
    {Op::Shot, "en_06_starters", 0},
    {Op::Tap, "🍓 Berry Bomb", 1500},
    {Op::Shot, "en_07_detail_emoji", 0},

    {Op::Mark, "brew", 0},
    {Op::Tap, "START", 300},
    {Op::Expect, "Place your cup", 8000},
    {Op::Shot, "en_08_place_cup", 0},
    {Op::Tap, "Brew", 2500},
    {Op::Shot, "en_09_brew_start", 0},
    {Op::Wait, nullptr, 3000},
    {Op::Shot, "en_10_brewing", 0},
    {Op::Tap, "PAUSE", 1200},
    {Op::Shot, "en_11_paused", 0},
    {Op::Again, nullptr, 500},
    {Op::Expect, "Enjoy", 60000},
    {Op::Wait, nullptr, 2500},
    {Op::Shot, "en_12_enjoy", 0},
    {Op::Wait, nullptr, 8000},

    {Op::Mark, "editor", 0},
    {Op::TapAny, LV_SYMBOL_EDIT, 1500},
    {Op::Shot, "en_13_editor", 0},
    {Op::Tap, "🍓 Berry Bomb", 1200},
    {Op::Shot, "en_14_name_entry", 0},
    {Op::Type, "🍓 Berry Bomb 2 🇸🇦", 300},
    {Op::Tap, "Done", 1200},
    {Op::Shot, "en_15_editor_renamed", 0},
    {Op::Tap, "Save", 1500},
    {Op::Expect, "🍓 Berry Bomb 2 🇸🇦", 3000},
    {Op::Shot, "en_16_saved", 0},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "settings", 0},
    {Op::Tap, "Settings", 1200},
    {Op::Shot, "en_17_settings", 0},
    {Op::Tap, "Wi-Fi", 2500},
    {Op::Shot, "en_18_wifi_networks", 0},
    {Op::Tap, "Home Wi-Fi", 1200},
    {Op::Type, "wrong-pass", 200},
    {Op::Tap, "Join", 200},
    {Op::Expect, "Wrong password. Tap the network to try again.", 20000},
    {Op::Shot, "en_19_wifi_wrong_password", 0},
    {Op::Tap, "Home Wi-Fi", 1200},
    {Op::Type, "coffee123", 200},
    {Op::Tap, "Join", 200},
    {Op::Expect, "Connected\n192.168.1.50", 20000},
    {Op::Wait, nullptr, 800},
    {Op::Shot, "en_20_wifi_connected", 0},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "account", 0},
    {Op::Tap, "xBloom account", 1200},
    {Op::Shot, "en_21_account", 0},
    {Op::Tap, "Sign in", 1200},
    {Op::Type, "panel-test@example.com", 200},
    {Op::Tap, "Next", 1200},
    {Op::Type, "not-a-real-password", 200},
    {Op::Tap, "Sign in", 15000},
    {Op::Shot, "en_22_sign_in_offline", 0},
    {Op::TapAny, BACK_SYMBOLS, 1200},

    {Op::Mark, "update and store", 0},
    {Op::Tap, "Software update", 1500},
    {Op::Shot, "en_23_update", 0},
    {Op::TapAny, BACK_SYMBOLS, 1200},
    {Op::Tap, "Double Shot", 1500},
    {Op::Shot, "en_24_store", 0},
    {Op::Tap, "xBloom", 1200},

    {Op::Mark, "clock", 0},
    {Op::Call, "clock_on", 2000},
    {Op::Shot, "en_25_clock", 0},
    {Op::Call, "clock_off", 1000},

    {Op::Mark, "emoji", 0},
    {Op::Call, "emoji_card", 1500},
    {Op::Shot, "en_26_emoji", 0},

    {Op::Mark, "restart in Arabic", 0},
    {Op::Call, "arabic", 0},
};

const Step PHASE_AR[] = {
    {Op::Expect, "Grinder", 20000},
    {Op::Wait, nullptr, 3000},
    {Op::Shot, "ar_01_home", 0},
    {Op::Tap, "Recipes", 1200},
    {Op::Tap, "Starter", 1000},
    {Op::Shot, "ar_02_starters", 0},
    {Op::Tap, "🍯 Honey Morning", 1500},
    {Op::Shot, "ar_03_detail", 0},
    {Op::Tap, "START", 300},
    {Op::Expect, "Place your cup", 8000},
    {Op::Tap, "Brew", 5000},
    {Op::Shot, "ar_04_brewing", 0},
    {Op::Expect, "Enjoy", 60000},
    {Op::Wait, nullptr, 2500},
    {Op::Shot, "ar_05_enjoy", 0},
    {Op::Wait, nullptr, 8000},
    {Op::TapAny, BACK_SYMBOLS, 1200},
    {Op::Tap, "Settings", 1200},
    {Op::Shot, "ar_06_settings", 0},
    {Op::Tap, "xBloom", 1200},
    {Op::Tap, "Scale", 1200},
    {Op::Shot, "ar_07_scale", 0},
    {Op::TapAny, BACK_SYMBOLS, 1200},
    {Op::Call, "emoji_card", 1500},
    {Op::Shot, "ar_08_emoji", 0},
    {Op::Mark, "restart in English", 0},
    {Op::Call, "english", 0},
};

const Step PHASE_DONE[] = {
    {Op::Expect, "Grinder", 20000},
    {Op::Wait, nullptr, 2000},
    {Op::Shot, "en_27_after_restarts", 0},
    {Op::Call, "finish", 0},
};

Preferences emu_prefs;
int phase = 0;
const Step *script = nullptr;
size_t script_len = 0, at = 0;
uint32_t step_started = 0, busy_until = 0;
bool step_begun = false;
int passed = 0, failed = 0;
lv_point_t last_tap = {0, 0};
uint32_t release_at = 0;

void tap(lv_point_t p) {
  last_tap = p;
  touch_at = p;
  touch_down = true;
  release_at = millis() + 120;
}

lv_point_t centre(lv_obj_t *o) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}

void fail(const char *what, const char *s) {
  failed++;
  Serial.printf("[emu] FAIL %s \"%s\"\n", what, s ? s : "");
}
void pass(const char *what, const char *s) {
  passed++;
  Serial.printf("[emu] ok   %s \"%s\"\n", what, s ? s : "");
}

// Finds `text` (English; shown translated), scrolling it into view if needed.
lv_obj_t *visible(const char *english) {
  lv_obj_t *o = find(shown(english));
  if (o && !lv_obj_is_visible(o)) {
    lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
    lv_refr_now(nullptr);
  }
  return o && lv_obj_is_visible(o) ? o : nullptr;
}

void emoji_card() {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  static const char *const lines[] = {
      "Faces 😀😋🥹🫠🤌 hearts ❤️🧡💛 ☕️🍓🍫🍯🧊",
      "Tones 👍🏻👍🏽👍🏿 👩🏽‍💻 🧑🏾‍🍳 👨‍👩‍👧‍👦 🏳️‍🌈 🏴‍☠️",
      "Keycaps 1️⃣#️⃣ flags 🇸🇦🇯🇵🇺🇸 🏴󠁧󠁢󠁳󠁣󠁴󠁿 ©️ new 🫨🪿🪼",
      "قهوة ☕ مع 🍓 وتمر 🌴",
  };
  int y = 40;
  for (int i = 0; i < 4; i++) {
    char buf[200];
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, i == 3 ? &inter_26b : &inter_20, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_base_dir(l, LV_BASE_DIR_AUTO, 0);
    lv_label_set_text(l, emoji::map(lines[i], buf, sizeof buf));
    lv_obj_set_pos(l, 24, y);
    y += 70;
  }
  lv_screen_load(scr);
}

void call(const char *what) {
  if (!strcmp(what, "clock_on")) ui::show_clock(true);
  else if (!strcmp(what, "clock_off")) ui::show_clock(false);
  else if (!strcmp(what, "emoji_card")) emoji_card();
  else if (!strcmp(what, "arabic") || !strcmp(what, "english")) {
    const bool ar = !strcmp(what, "arabic");
    emu_prefs.putInt("phase", ar ? 1 : 2);
    emu_prefs.putInt("passed", passed);
    emu_prefs.putInt("failed", failed);
    net::set_pref_lang(ar ? 1 : 0);
    Serial.printf("[emu] restarting (%d passed, %d failed so far)\n", passed, failed);
    Serial.flush();
    delay(100);
    ESP.restart();
  } else if (!strcmp(what, "finish")) {
    emu_prefs.putInt("phase", 0);
    Serial.printf("[mem] internal free %u, PSRAM free %u, loop stack unused %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    Serial.printf("[emu] RESULT %d passed, %d failed\n", passed, failed);
    Serial.println(failed ? "[emu] FAILED" : "[emu] PASSED");
    Serial.flush();
  }
}

void run_script() {
  if (touch_down && millis() >= release_at) touch_down = false;
  if (!script || at >= script_len || millis() < busy_until) return;
  const Step &s = script[at];
  if (!step_begun) {
    step_begun = true;
    step_started = millis();
  }
  auto next = [&](int wait) {
    busy_until = millis() + wait;
    at++;
    step_begun = false;
  };
  switch (s.op) {
    case Op::Wait: next(s.ms); break;
    case Op::Mark: Serial.printf("[emu] --- %s\n", s.s); next(0); break;
    case Op::Shot: emulator::shot(s.s); next(0); break;
    case Op::Call: call(s.s); next(s.ms); break;
    case Op::Again: tap(last_tap); pass("tap again", ""); next(s.ms); break;
    case Op::Expect:
      if (visible(s.s)) {
        pass("see", s.s);
        next(0);
      } else if (millis() - step_started > (uint32_t)s.ms) {
        fail("see", s.s);
        emulator::shot("fail");
        next(0);
      }
      break;
    case Op::Tap:
    case Op::TapAny: {
      lv_obj_t *o = nullptr;
      if (s.op == Op::Tap) o = visible(s.s);
      else {  // any of several symbols, '|' between them
        char list[64];
        strlcpy(list, s.s, sizeof list);
        for (char *t = strtok(list, "|"); t && !o; t = strtok(nullptr, "|")) {
          lv_obj_t *f = find(t);
          if (f && lv_obj_is_visible(f)) o = f;
        }
      }
      if (o) {
        tap(centre(o));
        Serial.printf("[emu] tap (%d,%d)\n", (int)last_tap.x, (int)last_tap.y);
        pass("tap", s.op == Op::Tap ? s.s : "symbol");
        next(s.ms);
      } else if (millis() - step_started > 3000) {
        fail("tap", s.op == Op::Tap ? s.s : "symbol");
        emulator::shot("fail");
        next(0);
      }
      break;
    }
    case Op::Type: {
      lv_obj_t *ta = find(nullptr, true);
      if (ta) {
        lv_textarea_set_text(ta, "");
        char buf[160];
        lv_textarea_add_text(ta, emoji::map(s.s, buf, sizeof buf));  // as the keyboard would put it
        pass("type", s.s);
      } else {
        fail("type", s.s);
      }
      next(s.ms);
      break;
    }
  }
}

}  // namespace

// ---- the Bluetooth link, simulated ----
namespace ble_link {
void begin() {
  g_link = LinkState::Scanning;
  send(UiEvType::Link, (int)LinkState::Scanning, 0, nullptr, true, 200);
  send(UiEvType::Link, (int)LinkState::Connecting, 0, "XBLOOM EMU", true, 700);
  send(UiEvType::Link, (int)LinkState::Connected, 0, "XBLOOM EMU", true, 1200);
  send(UiEvType::Weight, 0, 0, nullptr, true, 1300);
}
bool post(const BleCmd &c) {
  command(c);
  return true;
}
bool poll(UiEvent &e) {
  while (!later.empty() && (int32_t)(millis() - later.front().at) >= 0) {
    if (later.front().e.type == UiEvType::Link) g_link = (LinkState)later.front().e.i;
    to_ui.push_back(later.front().e);
    later.pop_front();
  }
  if (to_ui.empty()) return false;
  e = to_ui.front();
  to_ui.pop_front();
  return true;
}
LinkState state() { return g_link; }
}  // namespace ble_link

namespace emulator {

void start_display() {
  lv_init();
  lv_tick_set_cb(uptime_ms);
  const size_t bytes = LCD_H_RES * LCD_V_RES * 2;
  void *a = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM), *b = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
  lv_display_t *d = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, a, b, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  finished = (uint16_t *)a;
  // Fixed time, as on the simulator's screens: 23 Nov 2026, 9:25 in Riyadh.
  setenv("TZ", "<+03>-3", 1);
  tzset();
  timeval tv = {1795415100, 0};
  settimeofday(&tv, nullptr);
  Serial.println("[emu] emulator build: in-memory display, simulated machine and Wi-Fi");
}

void begin() {
  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, read_touch);
  emu_prefs.begin("emu", false);
  phase = emu_prefs.getInt("phase", 0);
  if (phase > 0) {
    passed = emu_prefs.getInt("passed", 0);
    failed = emu_prefs.getInt("failed", 0);
  }
  switch (phase) {
    case 1: script = PHASE_AR; script_len = sizeof PHASE_AR / sizeof PHASE_AR[0]; break;
    case 2: script = PHASE_DONE; script_len = sizeof PHASE_DONE / sizeof PHASE_DONE[0]; break;
    default: script = PHASE_EN; script_len = sizeof PHASE_EN / sizeof PHASE_EN[0]; break;
  }
  Serial.printf("[emu] script phase %d, %u steps\n", phase, (unsigned)script_len);
}

void tick() {
  run_brew();
  run_script();
}

void shot(const char *name) {
  Serial.printf("[emu] picture %s\n", name);
  lv_refr_now(nullptr);
  const uint16_t *px = finished;
  const int n = LCD_H_RES * LCD_V_RES;
  // Runs of one colour: (count, colour) pairs of 16-bit little-endian values.
  uint8_t *out = (uint8_t *)heap_caps_malloc(n * 4, MALLOC_CAP_SPIRAM);
  if (!out) return;
  size_t len = 0;
  for (int i = 0; i < n;) {
    const uint16_t c = px[i];
    int run = 1;
    while (i + run < n && px[i + run] == c && run < 65535) run++;
    out[len++] = run & 0xFF;
    out[len++] = run >> 8;
    out[len++] = c & 0xFF;
    out[len++] = c >> 8;
    i += run;
  }
  Serial.printf("@@SHOT %s %d %d %u\n", name, LCD_H_RES, LCD_V_RES, (unsigned)len);
  put_base64(out, len);
  Serial.println("@@END");
  heap_caps_free(out);
}

}  // namespace emulator

#endif  // XB_EMULATOR

// xBloom Panel by Double Shot: firmware for the Elecrow CrowPanel Advance 4.3"
// (ESP32-S3, 800x480 RGB panel, GT911 touch; board versions 1.1-1.3).
//
// Start-up order: board power and touch, display, boot logo, then the Wi-Fi /
// settings, account and UI modules, and last the Bluetooth link.
#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>
#include <mbedtls/platform.h>

#include "LovyanGFX_Driver.h"
#include "ble_link.h"
#include "cloud.h"
#include "emulator.h"
#include "panel_net.h"
#include "pins_config.h"
#include "ui.h"

namespace {

constexpr uint8_t BOARD_MCU = 0x30, TOUCH_GT911 = 0x5D;  // I2C addresses
constexpr uint32_t BOOT_LOGO_MS = 2500;                  // the logo stays up at least this long
constexpr uint32_t KEEP_UPDATE_AFTER_MS = 30000;         // see confirm_update()

LGFX lgfx_touch;  // LovyanGFX drives only the GT911 touch controller here
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t vsync = nullptr;

// ---- display -------------------------------------------------------------------
bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(vsync, &woke);
  return woke == pdTRUE;
}

// LVGL draws straight into one of the panel's two frame buffers; on the last
// area of a frame, switch to it and wait for the next vertical sync.
void flush(lv_display_t *disp, const lv_area_t *, uint8_t *pixels) {
  if (lv_display_flush_is_last(disp)) {
    xSemaphoreTake(vsync, 0);
    if (esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, pixels) == ESP_OK)
      xSemaphoreTake(vsync, pdMS_TO_TICKS(100));
  }
  lv_display_flush_ready(disp);
}

uint32_t uptime_ms() { return millis(); }

void start_display() {
  vsync = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  // 15 MHz pixel clock (~37 frames/s; Elecrow use 18): leaves PSRAM bandwidth for Wi-Fi and drawing.
  cfg.timings.pclk_hz = 15000000;
  cfg.timings.h_res = LCD_H_RES;
  cfg.timings.v_res = LCD_V_RES;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 8;
  cfg.timings.vsync_front_porch = 8;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.timings.flags.pclk_idle_high = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  // Bounce buffers of 10 lines: they sit in internal RAM, which Wi-Fi and TLS also need.
  cfg.bounce_buffer_size_px = LCD_H_RES * 10;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = GPIO_NUM_40;
  cfg.vsync_gpio_num = GPIO_NUM_41;
  cfg.de_gpio_num = GPIO_NUM_42;
  cfg.pclk_gpio_num = GPIO_NUM_39;
  cfg.disp_gpio_num = -1;
  static const int DATA_PINS[16] = {21, 47, 48, 45, 38, 9, 10, 11, 12, 13, 14, 7, 17, 18, 3, 46};  // D0..D15
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = DATA_PINS[i];
  cfg.flags.fb_in_psram = 1;
  cfg.flags.bb_invalidate_cache = 1;

  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = on_vsync;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  lv_init();
  lv_tick_set_cb(uptime_ms);

  const size_t frame_bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
  void *frame[2] = {};
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &frame[0], &frame[1]));
  for (void *f : frame) memset(f, 0, frame_bytes);

  lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, flush);
  lv_display_set_buffers(display, frame[0], frame[1], frame_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
}

// ---- board power, backlight and touch ---------------------------------------------
bool on_bus(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void board_command(uint8_t c) {  // the board's helper MCU: backlight and touch power
  Wire.beginTransmission(BOARD_MCU);
  Wire.write(c);
  Wire.endTransmission();
}

void start_board() {
  Wire.begin(15, 16);
  delay(50);
  // The touch chip comes up only after the helper MCU wakes it and its reset line
  // (GPIO 1) is pulsed; try until both answer.
  for (int attempt = 0; attempt < 40 && !(on_bus(BOARD_MCU) && on_bus(TOUCH_GT911)); ++attempt) {
    board_command(250);  // wake the touch chip
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
    delay(120);
    pinMode(1, INPUT);
    delay(100);
  }
  board_command(0);  // backlight full on
  if (!lgfx_touch._touch_instance.init()) Serial.println("GT911 touch init failed");
}

// ---- screen sleep -----------------------------------------------------------------
// Idle: dim after a minute (30 s when "Sleep after" is 1 min), then after the chosen
// time a dim clock, or dark (clock off, time unknown, or night with "Off at night").
// Never while the machine is working. A touch, a cup on the scale or machine
// activity wakes it; the touch that wakes it does nothing else.
enum class Screen { On, Dim, Clock, Off };
Screen screen = Screen::On;
bool ignore_touch = false;

// Backlight levels sent to the helper MCU: 0 brightest ... 245 off. Only full on
// and off are used: the helper MCU dims by pulsing the backlight, and at the levels
// in between its coil buzzes. Dim and the clock darken the picture instead.
uint8_t backlight_level(Screen s) { return s == Screen::Off ? 245 : 0; }
uint8_t picture_level(Screen s) {  // % of the Brightness setting
  switch (s) {
    case Screen::Dim: return 50;
    case Screen::Clock: return 30;
    default: return 100;
  }
}

uint32_t dim_after_ms() { return net::pref_sleep_min() == 1 ? 30000 : 60000; }
uint32_t sleep_after_ms() {
  const int m = net::pref_sleep_min();
  return m <= 0 ? UINT32_MAX : (uint32_t)m * 60000;
}

void set_screen(Screen s) {
  if (s == screen) return;
  screen = s;
  ui::show_clock(s == Screen::Clock);
  ui::set_dim(picture_level(s));
  uint8_t level = backlight_level(s);
  // Through LovyanGFX's I2C driver, which the touch polling also uses, so they never collide.
#ifndef XB_EMULATOR
  lgfx::i2c::transactionWrite(I2C_NUM_0, BOARD_MCU, &level, 1, 400000);
#endif
  static const char *const NAMES[] = {"on", "dim", "clock", "off"};
  Serial.printf("[screen] %s\n", NAMES[(int)s]);
}

void wake() {
  lv_display_trigger_activity(nullptr);
  set_screen(Screen::On);
}

void read_touch(lv_indev_t *, lv_indev_data_t *data) {
  lgfx::touch_point_t point;
  const bool down = lgfx_touch._touch_instance.getTouchRaw(&point, 1);
  if (down && screen != Screen::On) {
    ignore_touch = true;
    wake();
  }
  if (!down) ignore_touch = false;
  const bool use = down && !ignore_touch;
  data->state = use ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (use) {
    data->point.x = point.x;
    data->point.y = point.y;
  }
}

void start_touch_input() {
  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, read_touch);
}

void manage_screen() {
  if (ui::take_wake() || ui::busy()) {
    if (screen != Screen::On) wake();
    else lv_display_trigger_activity(nullptr);
    return;
  }
  const uint32_t idle = lv_display_get_inactive_time(nullptr);
  if (idle < dim_after_ms()) return set_screen(Screen::On);
  if (idle < sleep_after_ms()) return set_screen(Screen::Dim);
  struct tm now;
  const bool know_time = net::local_time(&now);
  const bool night = know_time && (now.tm_hour >= 23 || now.tm_hour < 6);
  const bool show_clock = net::pref_clock() && know_time && !(night && net::pref_night_off());
  set_screen(show_clock ? Screen::Clock : Screen::Off);
}

// ---- safe updates -------------------------------------------------------------------
// A newly installed version is kept only after running 30 s; if it crashes or
// restarts before then, the bootloader returns to the previous one by itself.
void confirm_update() {
  static bool checked = false;
  if (checked || millis() < KEEP_UPDATE_AFTER_MS) return;
  checked = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[update] new version confirmed");
  }
}

// TLS to xBloom needs ~40 KB of buffers. Internal RAM is mostly taken by the display,
// Bluetooth and Wi-Fi, so mbedTLS allocates from the 8 MB PSRAM first.
void *tls_calloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void tls_free(void *p) { heap_caps_free(p); }

// Why the panel last restarted, and where it crashed if it did (saved to the
// coredump partition by the crash handler, read back here, then cleared).
// Lower-case "backtrace" so the installer's start-up check doesn't take it for a new crash.
char crash_fields[600] = "";  // a crash to report, once the saved settings are loaded

void report_last_restart() {
  static const char *const WHY[] = {"unknown", "power on", "reset button", "restart", "crash", "interrupt watchdog",
                                    "task watchdog", "watchdog", "deep sleep", "power dip (brownout)", "SDIO", "USB",
                                    "JTAG", "eFuse", "power glitch", "CPU lockup"};
  const int r = (int)esp_reset_reason();
  Serial.printf("[boot] last restart: %s\n", r >= 0 && r < (int)(sizeof WHY / sizeof *WHY) ? WHY[r] : "?");
  esp_core_dump_summary_t *s = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if (s && esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(s) == ESP_OK) {
    Serial.printf("[crash] last crash: task %s, pc 0x%08x, firmware %.9s\n[crash] backtrace", s->exc_task,
                  (unsigned)s->exc_pc, s->app_elf_sha256);
    char bt[400] = "";
    for (uint32_t i = 0; i < s->exc_bt_info.depth; i++) {
      Serial.printf(" 0x%08x:0x0", (unsigned)s->exc_bt_info.bt[i]);
      const size_t n = strlen(bt);
      snprintf(bt + n, sizeof bt - n, "%s0x%08x:0x0", n ? " " : "", (unsigned)s->exc_bt_info.bt[i]);
    }
    Serial.println();
    snprintf(crash_fields, sizeof crash_fields,
             "\"reason\":\"%s\",\"task\":\"%.15s\",\"pc\":\"0x%08x\",\"bt\":\"%s\"",
             r >= 0 && r < (int)(sizeof WHY / sizeof *WHY) ? WHY[r] : "?", s->exc_task, (unsigned)s->exc_pc, bt);
    esp_core_dump_image_erase();
  } else if (r == 5 || r == 6 || r == 7 || r == 15) {  // watchdogs and lockups leave no crash record
    snprintf(crash_fields, sizeof crash_fields, "\"reason\":\"%s\",\"task\":\"\",\"pc\":\"\",\"bt\":\"\"", WHY[r]);
  }
  free(s);
}

}  // namespace

// The UI runs on Arduino's loop task. Its default 8 KB stack is too small for
// LVGL's first full-screen draw (about 9 KB measured), which overflowed it at
// start-up. 24 KB leaves ample room; [mem] in the start-up log shows what is left.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

extern "C" bool verifyRollbackLater() { return true; }  // Arduino: we confirm updates ourselves

void setup() {
  mbedtls_platform_set_calloc_free(tls_calloc, tls_free);  // before any TLS connection
  Serial.begin(115200);
  Serial.printf("\nxBloom Panel %s starting\n", FW_VERSION);
  report_last_restart();
#ifdef XB_EMULATOR
  emulator::start_display();
  emulator::start_network();
#else
  start_board();
  start_display();
  start_touch_input();
#endif
  ui::splash();
#ifdef XB_EMULATOR
  emulator::shot("boot_logo");
#endif
  const uint32_t logo_since = millis();
  net::begin();  // first: it loads the saved settings the UI shows
  if (crash_fields[0]) net::set_crash_report(crash_fields);
  cloud::begin();
  ui::begin();
  ble_link::begin();
#ifdef XB_EMULATOR
  emulator::begin();
#endif
  Serial.printf("[mem] internal free %u, largest block %u, PSRAM free %u, loop stack unused %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  while (millis() - logo_since < BOOT_LOGO_MS) delay(10);
  Serial.println("ready");
}

void loop() {
#ifdef XB_EMULATOR
  emulator::tick();
#endif
  ui::tick();
  manage_screen();
  confirm_update();
  lv_timer_handler();
  delay(2);
}

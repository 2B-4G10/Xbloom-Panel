// xBloom Studio Bluetooth protocol: building command frames, encoding recipes and
// reading the machine's notifications. Plain C++ (no Arduino), so it is tested on a PC
// (test_native/protocol_golden.cpp).
//
// The protocol is not published by xBloom; what is known of it comes from the
// xbloom-py project (MIT, see THIRD_PARTY_NOTICES.md). Not affiliated with xBloom.
// A machine firmware update may change any of it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <vector>

namespace xb {

using Bytes = std::vector<uint8_t>;

// ---- GATT -----------------------------------------------------------------
// Commands go to FFE1; replies and live events arrive as FFE2 notifications.
static const char *const UUID_TX = "0000ffe1-0000-1000-8000-00805f9b34fb";
static const char *const UUID_RX = "0000ffe2-0000-1000-8000-00805f9b34fb";
static const char *const NAME_PREFIX = "XBLOOM";  // advertised name, e.g. "XBLOOM 1234"

// ---- Command codes (host -> machine) --------------------------------------
enum Cmd : uint16_t {
  CMD_HANDSHAKE = 8100,
  CMD_BYPASS_DOSE = 8102,
  CMD_SET_CUP = 8104,
  CMD_RECIPE_GRIND = 8001,
  CMD_RECIPE_NO_GRIND = 8004,
  CMD_EXECUTE = 8002,      // starts brewing: sent only when the user taps Start
  CMD_QUIT_RECIPE = 8017,  // unload a recipe that has not started; machine goes home
  CMD_TARE = 8500,
  CMD_BREW_PAUSE = 40518,
  CMD_BREW_RESUME = 40524,
  CMD_BREW_STOP = 40519,
};

// ---- Notification codes (machine -> host) ---------------------------------
enum Notify : uint16_t {
  N_ACTIVITY = 8023,     // uint32 state, see activity_name()
  N_WEIGHT = 20501,      // float grams
  N_WEIGHT_ALT = 10507,  // float grams
  N_WATER = 40523,       // float ml poured
  N_BLOOM = 40510,       // uint32 pour index
  N_ENJOY = 40512,       // brew complete
  N_ABANDONED = 40513,   // brew left from pause
  N_MACHINE_INFO = 40521,
  N_SLEEPING = 8009,
  N_AWAKE = 8011,
  N_PODS = 40501,        // recipe card scanned, ASCII pod id
  N_FAULT_NO_WATER = 40522,
  N_FAULT_NO_BEANS = 40517,
  N_FAULT_DOSE_WATER = 8204,
  N_FAULT_GEAR = 8203,
};

// ---- Recipes ----------------------------------------------------------------
// Pour pattern, as the xBloom app/API numbers it.
enum Pattern : uint8_t { PAT_CENTERED = 1, PAT_SPIRAL = 2, PAT_CIRCULAR = 3 };

// Cup types: 1 = xPod, 2 = Omni dripper, 3 = Other, 4 = Tea.
static const int MAX_POURS = 9;

struct Pour {
  double volume_ml = 60;   // 0..240
  double temp_c = 92;      // 20 (room temp), 40 .. 99
  uint8_t pattern = PAT_SPIRAL;
  double flow = 3.0;       // 3.0..3.5 ml/s
  uint8_t pause_s = 0;     // wait after this pour, 0..59
  bool agitate_before = false;
  bool agitate_after = false;
};

struct Recipe {
  char name[64] = "Recipe";
  double dose_g = 15;      // grams
  int grind_size = 40;     // 1..80; 0 = grinder off
  int rpm = 90;            // 60..120
  int cup_type = 3;
  bool grinder_on = true;
  uint8_t n_pours = 0;
  Pour pours[MAX_POURS];

  double total_water() const;
};

// nullptr if the machine will take the recipe, else why not. Check before sending.
const char *validate(const Recipe &r);

// ---- Frames ------------------------------------------------------------------
uint16_t crc16(const uint8_t *data, size_t len);  // CRC16-Kermit
Bytes build_frame(uint16_t code, const uint32_t *ints = nullptr, size_t n_ints = 0,
                  const uint8_t *raw = nullptr, size_t raw_len = 0, uint8_t frame_type = 1);
Bytes encode_recipe_blob(const Recipe &r);

// Frames that load a recipe, in sending order: handshake, dose, cup range, recipe.
// Loading never starts brewing; frame_execute() does.
std::vector<Bytes> build_load_frames(const Recipe &r);

Bytes frame_execute();      // start the loaded brew
Bytes frame_quit_recipe();
Bytes frame_tare();
Bytes frame_pause();
Bytes frame_resume();
Bytes frame_stop();

// ---- Manual modules (the machine's Grinder / Brewer / Scale screens) -------
// Pattern bytes on the wire: 0 = centered, 1 = circular, 2 = spiral.
uint8_t pattern_to_byte(uint8_t api_pattern);
double temp_display_to_wire(int display_c);  // 39 -> 20 (room temp), 40..99 as they are

Bytes frame_grinder_enter(int size, int rpm);  // go to grinder screen, set size/rpm (no motor)
Bytes frame_grinder_start(int size, int rpm);
Bytes frame_grinder_stop();
Bytes frame_brewer_enter(uint8_t pattern_byte, double temp_c);
Bytes frame_brewer_start(double flow, double volume_ml, double temp_c, uint8_t pattern_byte);  // pours hot water
Bytes frame_brewer_pause();    // 8019 (named in the official app; not yet live-tested by xbloom-py)
Bytes frame_brewer_resume();   // 8021 (same)
Bytes frame_brewer_stop();     // 4507 (same)
Bytes frame_brewer_temp(double temp_c);
Bytes frame_brewer_pattern(uint8_t pattern_byte);
Bytes frame_scale_enter();
Bytes frame_quit_grinder();
Bytes frame_quit_brewer();
Bytes frame_quit_scale();
enum WeightUnit : uint8_t { UNIT_G = 1, UNIT_ML = 0, UNIT_OZ = 2 };  // xbloom-py spec codes
Bytes frame_weight_unit(WeightUnit u);

// Command code carried by a frame (ours or the machine's), or 0.
uint16_t frame_code(const Bytes &f);

// ---- Notifications ----------------------------------------------------------
struct Event {
  uint16_t code = 0;
  const uint8_t *payload = nullptr;
  size_t len = 0;
  bool has_u32() const { return len >= 4; }
  uint32_t u32() const;
  float f32() const;
};

// Error field in a command reply; 0 = accepted.
uint32_t refusal_code(const Bytes &frame);
const char *refusal_reason(uint32_t code);  // nullptr if no known bit
const char *activity_name(uint32_t activity);
const char *fault_name(uint16_t code);      // nullptr if not a fault

// Turns FFE2 notifications into whole frames: one notification can hold several
// frames, and a long frame can arrive in two notifications. Feed them in order.
class Assembler {
 public:
  void reset() { pending_.clear(); }
  void feed(const uint8_t *data, size_t len, const std::function<void(const Bytes &)> &on_frame);

 private:
  Bytes pending_;
};

bool parse_event(const Bytes &frame, Event &out);

}  // namespace xb

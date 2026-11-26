// xBloom Studio wire format. See xbloom_protocol.h for the overview.
#include "xbloom_protocol.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace xb {
namespace {

// ---- little-endian helpers ---------------------------------------------------
void append_le(Bytes &out, uint32_t v, int width) {
  for (int i = 0; i < width; ++i) out.push_back((uint8_t)(v >> (8 * i)));
}
uint32_t read_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t bits_of(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}
// Round half to even, as the machine's own app does.
long round_even(double x) { return (long)nearbyint(x); }

constexpr uint8_t SYNC = 0x58, DIR_HOST = 0x01, DIR_MACHINE = 0x02;
constexpr size_t HEADER = 10, CRC = 2, OVERHEAD = HEADER + CRC;  // 58 dir type code(2) len(4) 01 ... crc(2)
constexpr size_t LONGEST_FRAME = 1024;

Bytes frame_of_ints(uint16_t code, std::initializer_list<uint32_t> v) {
  return build_frame(code, v.begin(), v.size());
}

// Pattern numbers on the wire differ from the app's: centred 0, circular 1, spiral 2.
uint8_t wire_pattern(uint8_t app) {
  return app == PAT_CIRCULAR ? 1 : app == PAT_SPIRAL ? 2 : 0;
}

bool grinds(const Recipe &r) { return r.grinder_on && r.grind_size > 0; }

// The machine accepts a recipe (grind size included) only when, computed in float,
//   int((ratio_byte / 10) * dose) == the sum of the pour volumes it is sent.
// So pick a ratio byte near the real ratio for which that product lands on a whole
// number (or close to one), then share that many ml across the pours in proportion.
struct Volumes {
  std::vector<int> ml;
  int ratio_byte = 0;
};
Volumes match_volumes(const std::vector<double> &want, int dose) {
  Volumes out;
  double sum = 0;
  for (double v : want) sum += v;
  if (dose <= 0 || sum <= 0 || want.empty()) {
    for (double v : want) out.ml.push_back((int)round_even(v));
    return out;
  }
  const long guess = round_even(sum / dose * 10);
  long ratio = -1, total = -1;
  for (long r : {guess, guess + 1, guess - 1, guess + 2, guess - 2}) {
    if (r < 1 || r > 255) continue;
    const double product = (float)r / 10.0f * (float)dose;
    if (product - floor(product) < 0.5) {  // truncates to the same whole number it rounds to
      ratio = r;
      total = (long)product;
      break;
    }
  }
  if (total < 0) {
    ratio = std::min(255L, std::max(1L, guess));
    total = (long)(double)((float)ratio / 10.0f * (float)dose);
  }
  const size_t n = want.size();
  std::vector<double> exact(n);
  out.ml.resize(n);
  long given = 0;
  for (size_t i = 0; i < n; ++i) {
    exact[i] = want[i] * (double)total / sum;
    out.ml[i] = (int)exact[i];
    given += out.ml[i];
  }
  // Hand the leftover ml, one each, to the pours that lost the most to rounding down.
  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return exact[a] - out.ml[a] > exact[b] - out.ml[b]; });
  for (long k = 0; k < total - given && k < (long)n; ++k) out.ml[order[k]] += 1;
  out.ratio_byte = (int)(ratio & 0xFF);
  return out;
}

// ---- incoming frames --------------------------------------------------------
// Length a machine frame at `d` says it has, or 0 if `d` doesn't start one.
size_t declared_length(const uint8_t *d, size_t n) {
  if (n < 9 || d[0] != SYNC || d[1] != DIR_MACHINE) return 0;
  const uint32_t len = read_le32(d + 5);
  return len >= OVERHEAD && len <= LONGEST_FRAME ? len : 0;
}
bool checksum_ok(const uint8_t *f, size_t n) {
  return n >= OVERHEAD && crc16(f, n - CRC) == (f[n - 2] | f[n - 1] << 8);
}
// True if `d` could be the beginning of a frame whose rest is still to come.
bool unfinished_frame(const uint8_t *d, size_t n) {
  if (n < 9 && d[0] == SYNC && (n < 2 || d[1] == DIR_MACHINE)) return true;
  const size_t len = declared_length(d, n);
  return len > n;
}

}  // namespace

// ---- recipes ----------------------------------------------------------------
double Recipe::total_water() const {
  double ml = 0;
  for (int i = 0; i < n_pours; ++i) ml += pours[i].volume_ml;
  return ml;
}

const char *validate(const Recipe &r) {
  if (r.n_pours < 1 || r.n_pours > MAX_POURS) return "1-9 pours required";
  if (r.cup_type < 1 || r.cup_type > 4) return "bad cup type";
  struct Range { double lo, hi; };
  static const Range dose[5] = {{0, 0}, {15, 15}, {5, 18}, {5, 25}, {1, 5}};  // by cup type
  if (r.dose_g < dose[r.cup_type].lo || r.dose_g > dose[r.cup_type].hi) return "dose out of range for cup";
  if (r.grinder_on && (r.grind_size < 1 || r.grind_size > 80)) return "grind size must be 1-80";
  if (r.grinder_on && (r.rpm < 60 || r.rpm > 120)) return "grinder rpm must be 60-120";
  for (int i = 0; i < r.n_pours; ++i) {
    const Pour &p = r.pours[i];
    if (p.volume_ml < 0 || p.volume_ml > 240) return "pour volume must be 0-240 ml";
    if (p.temp_c < 20 || p.temp_c > 99) return "temperature must be 20-99 C";
    if (p.flow < 3.0 - 1e-9 || p.flow > 3.5 + 1e-9) return "flow must be 3.0-3.5";
    if (p.pause_s > 59) return "pause must be 0-59 s";
    if (p.pattern < PAT_CENTERED || p.pattern > PAT_CIRCULAR) return "bad pattern";
  }
  const double water = r.total_water();
  if (water <= 0) return "no water";
  const double ratio = water / r.dose_g;
  if (ratio < 5 || ratio > 25) return "ratio must be 1:5 - 1:25";
  return nullptr;
}

// ---- outgoing frames --------------------------------------------------------
uint16_t crc16(const uint8_t *data, size_t len) {  // CRC-16/Kermit, bit-reversed 0x1021
  uint16_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (int bit = 0; bit < 8; ++bit) crc = crc & 1 ? (crc >> 1) ^ 0x8408 : crc >> 1;
  }
  return crc;
}

Bytes build_frame(uint16_t code, const uint32_t *ints, size_t n_ints, const uint8_t *raw, size_t raw_len,
                  uint8_t frame_type) {
  const size_t body = raw ? raw_len : 4 * n_ints;
  Bytes f;
  f.reserve(OVERHEAD + body);
  f.insert(f.end(), {SYNC, DIR_HOST, frame_type});
  append_le(f, code, 2);
  append_le(f, (uint32_t)(OVERHEAD + body), 4);
  f.push_back(0x01);
  if (raw) f.insert(f.end(), raw, raw + raw_len);
  else
    for (size_t i = 0; i < n_ints; ++i) append_le(f, ints[i], 4);
  append_le(f, crc16(f.data(), f.size()), 2);
  return f;
}

// Recipe blob: [step count] then per pour one or more 4-byte "water" steps (at most
// 127 ml each) and one "settings" step, then [grind size] [ratio byte].
Bytes encode_recipe_blob(const Recipe &r) {
  const bool grind = grinds(r);
  std::vector<double> want;
  for (int i = 0; i < r.n_pours; ++i) want.push_back(std::max(0.0, r.pours[i].volume_ml));
  const Volumes vol = match_volumes(want, r.dose_g ? (int)round_even(r.dose_g) : 0);

  Bytes steps;
  for (int i = 0; i < r.n_pours; ++i) {
    const Pour &p = r.pours[i];
    const uint8_t temp = (uint8_t)(int)p.temp_c, pattern = wire_pattern(p.pattern);
    const uint8_t agitate = (p.agitate_before ? 1 : 0) | (p.agitate_after ? 2 : 0);
    int left = vol.ml[i];
    do {  // a 0 ml pour still sends one water step
      const int chunk = std::min(left, 127);
      steps.insert(steps.end(), {(uint8_t)chunk, temp, pattern, agitate});
      left -= chunk;
    } while (left > 0);
    const uint8_t rpm = i == 0 && grind ? (uint8_t)r.rpm : 0;  // the grinder runs before the first pour
    steps.insert(steps.end(), {(uint8_t)-(int)p.pause_s, 0, rpm, (uint8_t)round_even(p.flow * 10)});
  }
  Bytes blob;
  blob.push_back((uint8_t)steps.size());
  blob.insert(blob.end(), steps.begin(), steps.end());
  blob.push_back((uint8_t)(grind ? std::min(r.grind_size, 80) : 0));
  blob.push_back((uint8_t)vol.ratio_byte);
  return blob;
}

std::vector<Bytes> build_load_frames(const Recipe &r) {
  const bool omni = r.cup_type == 2;  // the Omni dripper takes a narrower cup range
  const Bytes blob = encode_recipe_blob(r);
  return {
      frame_of_ints(CMD_HANDSHAKE, {185, 1}),
      frame_of_ints(CMD_BYPASS_DOSE, {0, 0, (uint32_t)round_even(r.dose_g)}),
      frame_of_ints(CMD_SET_CUP, {bits_of(omni ? 110.f : 200.f), bits_of(omni ? 90.f : 80.f)}),
      build_frame(grinds(r) ? CMD_RECIPE_GRIND : CMD_RECIPE_NO_GRIND, nullptr, 0, blob.data(), blob.size()),
  };
}

Bytes frame_execute() { return build_frame(CMD_EXECUTE); }
Bytes frame_quit_recipe() { return build_frame(CMD_QUIT_RECIPE); }
Bytes frame_tare() { return build_frame(CMD_TARE); }
Bytes frame_pause() { return build_frame(CMD_BREW_PAUSE); }
Bytes frame_resume() { return build_frame(CMD_BREW_RESUME); }
Bytes frame_stop() { return build_frame(CMD_BREW_STOP); }

uint8_t pattern_to_byte(uint8_t api) { return wire_pattern(api); }
double temp_display_to_wire(int c) { return c <= 39 ? 20.0 : c > 99 ? 99.0 : c; }  // 39 is the room-temperature mark

// Manual modules. Temperatures, flows and volumes travel as float bit patterns x10.
Bytes frame_grinder_enter(int size, int rpm) { return frame_of_ints(8006, {(uint32_t)size, (uint32_t)rpm}); }
Bytes frame_grinder_start(int size, int rpm) { return frame_of_ints(3500, {1000, (uint32_t)size, (uint32_t)rpm}); }
Bytes frame_grinder_stop() { return build_frame(3505); }
Bytes frame_brewer_enter(uint8_t pattern, double temp_c) {
  return frame_of_ints(8007, {pattern, bits_of((float)(temp_c * 10))});
}
Bytes frame_brewer_start(double flow, double volume_ml, double temp_c, uint8_t pattern) {
  return frame_of_ints(4506, {bits_of((float)(flow * 10)), bits_of((float)(volume_ml * 10)),
                              bits_of((float)(temp_c * 10)), 0 /* water tank */, pattern});
}
Bytes frame_brewer_pause() { return build_frame(8019); }
Bytes frame_brewer_resume() { return build_frame(8021); }
Bytes frame_brewer_stop() { return build_frame(4507); }
Bytes frame_brewer_temp(double temp_c) { return frame_of_ints(4510, {(uint32_t)round_even(temp_c * 10)}); }
Bytes frame_brewer_pattern(uint8_t pattern) { return frame_of_ints(8016, {pattern}); }
Bytes frame_scale_enter() { return build_frame(8003); }
Bytes frame_quit_grinder() { return build_frame(8012); }
Bytes frame_quit_brewer() { return build_frame(8013); }
Bytes frame_quit_scale() { return build_frame(8014); }
Bytes frame_weight_unit(WeightUnit u) { return frame_of_ints(8005, {u}); }

uint16_t frame_code(const Bytes &f) { return f.size() >= 5 && f[0] == SYNC ? f[3] | f[4] << 8 : 0; }

// ---- incoming frames --------------------------------------------------------
uint32_t Event::u32() const { return len >= 4 ? read_le32(payload) : 0; }
float Event::f32() const {
  float f = 0;
  if (len >= 4) memcpy(&f, payload, sizeof f);
  return f;
}

bool parse_event(const Bytes &d, Event &e) {
  if (d.size() < HEADER || d[0] != SYNC || d[1] != DIR_MACHINE) return false;
  e.code = d[3] | d[4] << 8;
  const bool has_payload = d.size() > OVERHEAD;
  e.payload = has_payload ? d.data() + HEADER : nullptr;
  e.len = has_payload ? d.size() - OVERHEAD : 0;
  return true;
}

// A reply's error field: 3 big-endian bytes at payload offset 12 (payload >= 16 bytes).
uint32_t refusal_code(const Bytes &f) {
  if (f.size() < OVERHEAD + 16) return 0;
  const uint8_t *p = f.data() + HEADER + 12;
  return (uint32_t)p[0] << 16 | p[1] << 8 | p[2];
}

const char *refusal_reason(uint32_t code) {
  struct Reason { uint32_t bits; const char *text; };
  static const Reason reasons[] = {  // first match wins
      {0x000040, "No water"},
      {0x001800, "Go to the home screen and retry"},
      {0x100000, "Machine not ready"},
      {0x200000, "Power-cycle restore incomplete"},
      {0x400000, "Recipe rejected"},
      {0x8F4700, "Machine busy"},
  };
  for (const Reason &r : reasons)
    if (code & r.bits) return r.text;
  return nullptr;
}

const char *activity_name(uint32_t a) {
  switch (a) {
    case 1: return "Home";
    case 3: return "Brewer";
    case 4: case 5: return "Scale";
    case 16: case 30: case 35: return "Brewing";
    case 29: return "Starting";
    case 31: return "Paused";
    case 34: return "Grinding";
    case 36: return "Done";
    case 65: return "Auto";
    default: return nullptr;
  }
}

const char *fault_name(uint16_t code) {
  switch (code) {
    case N_FAULT_NO_WATER: return "Out of water";
    case N_FAULT_NO_BEANS: return "No beans";
    case N_FAULT_DOSE_WATER: return "Dose/water error";
    case N_FAULT_GEAR: return "Grinder gear error";
    default: return nullptr;
  }
}

// The machine packs several frames into one notification and splits long ones
// across two. An unfinished frame at the end is kept and completed by the next
// notification; data that holds no frame at all is passed on whole.
void Assembler::feed(const uint8_t *data, size_t len, const std::function<void(const Bytes &)> &on_frame) {
  Bytes buf(data, data + len);
  bool got_one = false;
  if (!pending_.empty()) {
    Bytes joined = pending_;
    joined.insert(joined.end(), data, data + len);
    const size_t want = declared_length(joined.data(), joined.size());
    if (want > joined.size()) {  // still not complete
      pending_ = joined;
      return;
    }
    pending_.clear();
    if (want && checksum_ok(joined.data(), want)) {
      on_frame(Bytes(joined.begin(), joined.begin() + want));
      got_one = true;
      buf.assign(joined.begin() + want, joined.end());
    }  // else the kept bytes were noise: read this notification on its own
  }
  if (buf.empty()) return;

  size_t at = 0;
  int frames = 0;
  for (size_t want; at < buf.size() && (want = declared_length(&buf[at], buf.size() - at)) && want <= buf.size() - at;
       at += want, ++frames)
    on_frame(Bytes(buf.begin() + at, buf.begin() + at + want));

  if (frames == 0 && !got_one && !unfinished_frame(buf.data(), buf.size())) {
    on_frame(buf);  // not frame-shaped: hand it over as one piece
    return;
  }
  if (at < buf.size() && unfinished_frame(&buf[at], buf.size() - at)) pending_.assign(buf.begin() + at, buf.end());
}

}  // namespace xb

// Golden test for the protocol module: prints the result of every public function
// for thousands of seeded random inputs. Build it against two versions of
// xbloom_protocol.cpp and compare the output; any difference is a behaviour change.
#include <stdio.h>
#include <string.h>

#include <random>
#include <string>

#include "xbloom_protocol.h"

static std::mt19937 rng(20261123);
static int ri(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
static double pick(std::initializer_list<double> v) { return *(v.begin() + ri(0, (int)v.size() - 1)); }

static void hex(const xb::Bytes &b) {
  for (uint8_t c : b) printf("%02x", c);
  printf("\n");
}

static xb::Recipe random_recipe() {
  xb::Recipe r;
  r.dose_g = pick({0, 1, 4.5, 5, 10, 12, 15, 15.5, 16.5, 18, 20, 22, 25, 26, 33.3});
  r.grind_size = ri(-2, 90);
  r.rpm = ri(50, 130);
  r.cup_type = ri(0, 5);
  r.grinder_on = ri(0, 3) != 0;
  r.n_pours = (uint8_t)ri(0, xb::MAX_POURS);
  for (int i = 0; i < xb::MAX_POURS; ++i) {
    xb::Pour &p = r.pours[i];
    p.volume_ml = pick({-5, 0, 0.4, 0.5, 1.5, 30, 40, 45.5, 50, 60, 77.7, 100, 126.5, 127, 128, 200, 240, 254, 255, 300});
    p.temp_c = pick({15, 20, 39, 85, 90, 92.5, 93, 96, 98, 99});
    p.pattern = (uint8_t)ri(0, 4);
    p.flow = pick({2.9, 3.0, 3.05, 3.1, 3.25, 3.5, 3.6});
    p.pause_s = (uint8_t)ri(0, 70);
    p.agitate_before = ri(0, 1);
    p.agitate_after = ri(0, 1);
  }
  return r;
}

static xb::Bytes random_bytes(int n) {
  xb::Bytes b(n);
  for (auto &c : b) c = (uint8_t)ri(0, 255);
  return b;
}

int main() {
  // recipes: validation, blob, load frames
  for (int k = 0; k < 6000; ++k) {
    xb::Recipe r = random_recipe();
    const char *v = xb::validate(r);
    printf("v %s w %.6f\n", v ? v : "-", r.total_water());
    hex(xb::encode_recipe_blob(r));
    for (auto &f : xb::build_load_frames(r)) hex(f);
  }
  // fixed frames
  hex(xb::frame_execute()); hex(xb::frame_quit_recipe()); hex(xb::frame_tare());
  hex(xb::frame_pause()); hex(xb::frame_resume()); hex(xb::frame_stop());
  hex(xb::frame_grinder_stop()); hex(xb::frame_brewer_pause()); hex(xb::frame_brewer_resume());
  hex(xb::frame_brewer_stop()); hex(xb::frame_scale_enter()); hex(xb::frame_quit_grinder());
  hex(xb::frame_quit_brewer()); hex(xb::frame_quit_scale());
  for (int u : {0, 1, 2}) hex(xb::frame_weight_unit((xb::WeightUnit)u));
  for (int p = 0; p < 6; ++p) printf("pb %d %d\n", p, xb::pattern_to_byte((uint8_t)p));
  for (int d = 30; d < 100; ++d) printf("tw %d %.3f\n", d, xb::temp_display_to_wire(d));
  for (int k = 0; k < 500; ++k) {
    int s = ri(0, 90), rpm = ri(50, 130);
    double t = pick({20, 85, 92.5, 96.04, 98}), fl = pick({3.0, 3.25, 3.5}), vol = pick({0, 50, 250, 333.3});
    uint8_t pb = (uint8_t)ri(0, 3);
    hex(xb::frame_grinder_enter(s, rpm)); hex(xb::frame_grinder_start(s, rpm));
    hex(xb::frame_brewer_enter(pb, t)); hex(xb::frame_brewer_start(fl, vol, t, pb));
    hex(xb::frame_brewer_temp(t)); hex(xb::frame_brewer_pattern(pb));
  }
  // generic frames, crc, frame_code
  for (int k = 0; k < 500; ++k) {
    xb::Bytes raw = random_bytes(ri(0, 40));
    printf("crc %04x\n", xb::crc16(raw.data(), raw.size()));
    uint32_t ints[5];
    for (auto &x : ints) x = (uint32_t)rng();
    const uint16_t code = (uint16_t)rng();
    hex(xb::build_frame(code, ints, ri(0, 5), nullptr, 0, (uint8_t)ri(0, 3)));
    xb::Bytes f = xb::build_frame(code, nullptr, 0, raw.data(), raw.size());
    hex(f);
    printf("code %u %u\n", xb::frame_code(f), xb::frame_code(raw));
  }
  // events, refusals, names
  for (int k = 0; k < 2000; ++k) {
    xb::Bytes d = random_bytes(ri(0, 30));
    if (ri(0, 2) && d.size() >= 2) { d[0] = 0x58; d[1] = (uint8_t)ri(1, 2); }
    xb::Event e;
    bool ok = xb::parse_event(d, e);
    printf("ev %d %u %zu %d %u %.6g\n", ok, ok ? e.code : 0, ok ? e.len : 0, ok ? e.has_u32() : 0,
           ok ? e.u32() : 0, ok ? (double)e.f32() : 0.0);
    uint32_t rc = xb::refusal_code(d);
    const char *rr = xb::refusal_reason(rc);
    printf("rf %06x %s\n", rc, rr ? rr : "-");
    const uint32_t bit = 1u << ri(0, 24);
    rr = xb::refusal_reason(bit);
    printf("rb %06x %s\n", bit, rr ? rr : "-");
  }
  for (uint32_t a = 0; a < 70; ++a) { const char *n = xb::activity_name(a); printf("an %u %s\n", a, n ? n : "-"); }
  for (uint16_t c : {0, 40522, 40517, 8204, 8203, 8023}) { const char *n = xb::fault_name(c); printf("fn %u %s\n", c, n ? n : "-"); }

  // assembler: real machine frames (and junk) cut into random notifications
  for (int s = 0; s < 3000; ++s) {
    xb::Bytes stream;
    for (int f = ri(1, 6); f > 0; --f) {
      const int kind = ri(0, 9);
      if (kind == 0) {
        xb::Bytes j = random_bytes(ri(1, 12));
        stream.insert(stream.end(), j.begin(), j.end());
        continue;
      }
      xb::Bytes body = random_bytes(ri(0, 24));
      xb::Bytes fr = xb::build_frame((uint16_t)rng(), nullptr, 0, body.data(), body.size());
      fr[1] = 0x02;  // machine -> host
      if (kind != 1) {  // kind 1: leave the checksum wrong
        const uint16_t c = xb::crc16(fr.data(), fr.size() - 2);
        fr[fr.size() - 2] = c & 0xFF;
        fr[fr.size() - 1] = c >> 8;
      }
      if (kind == 2) fr[5] = (uint8_t)ri(0, 255);  // bad length
      stream.insert(stream.end(), fr.begin(), fr.end());
    }
    xb::Assembler a;
    size_t i = 0;
    while (i < stream.size()) {
      size_t n = std::min(stream.size() - i, (size_t)ri(1, 40));
      a.feed(stream.data() + i, n, [](const xb::Bytes &f) { printf("A "); hex(f); });
      i += n;
      printf("|\n");
    }
    if (ri(0, 4) == 0) a.reset();
    printf("--\n");
  }
  return 0;
}

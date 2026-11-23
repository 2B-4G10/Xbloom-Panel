// Golden test for the recipe library: feeds seeded random xBloom cloud replies (numbers
// as numbers or strings, missing fields, odd names) through every library function
// and prints the results. Build against two versions of library.cpp and compare.
#include <stdio.h>
#include <string.h>

#include <random>
#include <string>
#include <vector>

#include "library.h"

static std::mt19937 rng(77);
static int ri(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
template <class T> static T pick(std::initializer_list<T> v) { return *(v.begin() + ri(0, (int)v.size() - 1)); }

// A JSON value for a number: plain, quoted, junk, null or absent ("").
static std::string numv(double d) {
  char b[48];
  switch (ri(0, 9)) {
    case 0: snprintf(b, sizeof b, "\"%g\"", d); return b;
    case 1: return "\"x\"";
    case 2: return "null";
    case 3: return "";
    case 4: return ri(0, 1) ? "true" : "false";
    default: snprintf(b, sizeof b, "%.10g", d); return b;
  }
}
static void field(std::string &o, const char *k, const std::string &v) {
  if (v.empty()) return;
  if (o.back() != '{') o += ",";
  o += "\"";
  o += k;
  o += "\":";
  o += v;
}

static const char *names[] = {"MAD ice coffee", "  Spaced  ", "Yaye [Anaerobic] {Filter}", "Café Olé",
                              "قهوة مختصة إثيوبية من مزرعة صغيرة في جبال سيدامو عالية الارتفاع",
                              "A very long recipe name that goes on and on past sixty four bytes for sure", "", " "};

static std::string recipe() {
  std::string o = "{";
  if (ri(0, 15)) field(o, "tableId", ri(0, 3) ? "\"" + std::to_string(ri(1, 99999999)) + "\"" : std::to_string(ri(1, 9999)));
  if (ri(0, 15)) field(o, "theName", ri(0, 10) ? std::string("\"") + names[ri(0, 7)] + "\"" : "12");
  field(o, "dose", numv(pick({12.0, 15.0, 15.5, 18.0, 20.0, 22.0, 25.0})));
  field(o, "grinderSize", numv(pick({0.0, 12.0, 36.0, 42.4, 42.5, 80.0})));
  field(o, "isSetGrinderSize", numv(ri(1, 2)));
  field(o, "rpm", numv(pick({60.0, 90.0, 100.0, 120.0})));
  field(o, "cupType", numv(ri(1, 4)));
  field(o, "isEnableBypassWater", numv(ri(1, 2)));
  field(o, "bypassTemp", numv(pick({80.0, 85.0, 92.5})));
  field(o, "bypassVolume", numv(pick({5.0, 10.0, 33.3})));
  if (ri(0, 4)) field(o, "theColor", "\"" + std::string(pick({"#b0bde0", "#C9D5B8", "e0c0a0", "#FFF", "#zz", ""})) + "\"");
  field(o, "adaptedModel", numv(ri(0, 3)));
  field(o, "createTimeStamp", numv(pick({0.0, 1700000000123.0, 1731000000000.0})));
  field(o, "isShortcuts", numv(ri(0, 2)));
  field(o, "subSetType", numv(ri(0, 3)));
  field(o, "theSubsetId", numv(ri(0, 500)));
  if (ri(0, 8)) {
    if (o.back() != '{') o += ",";
    o += "\"pourList\":[";
    for (int n = ri(0, 11), i = 0; i < n; ++i) {
      std::string q = "{";
      field(q, "volume", numv(pick({0.0, 30.0, 45.5, 60.0, 128.0, 240.0})));
      field(q, "temperature", numv(pick({20.0, 85.0, 92.0, 96.0})));
      field(q, "pattern", numv(ri(1, 3)));
      field(q, "flowRate", numv(pick({3.0, 3.2, 3.5})));
      field(q, "pausing", numv(ri(0, 59)));
      field(q, "isEnableVibrationBefore", numv(ri(1, 2)));
      field(q, "isEnableVibrationAfter", numv(ri(1, 2)));
      if (ri(0, 2)) field(q, "theName", "\"" + std::string(pick({"Bloom", "Pour 2", "صبّة طويلة جداً جداً جداً", ""})) + "\"");
      o += (i ? "," : "") + q + "}";
    }
    o += "]";
  }
  return o + "}";
}

static void dump(const PanelRecipe &p) {
  const xb::Recipe &r = p.r;
  printf("[%s|%s|%.17g|%d|%d|%d|%d|%d|%.17g|%.17g|%06x|%s|%d|%lld|%d|%d|%d|%d|%u|%d|%d]", p.id, r.name, r.dose_g,
         r.grind_size, r.grinder_on, r.rpm, r.cup_type, p.bypass, p.bypass_temp, p.bypass_volume, p.color, p.color_src,
         p.adapted_model, (long long)p.created_ms, p.is_shortcut, p.subset_type, p.subset_id, p.kind, p.uid,
         p.favorite, p.dirty);
  for (int i = 0; i < r.n_pours; ++i) {
    const xb::Pour &q = r.pours[i];
    printf("(%.17g,%.17g,%d,%.17g,%d,%d,%d,%s)", q.volume_ml, q.temp_c, q.pattern, q.flow, q.pause_s, q.agitate_before,
           q.agitate_after, p.pour_names[i]);
  }
  printf("\n");
}

int main() {
  const std::string env = "{\"interfaceVersion\":20240918,\"memberId\":7,\"token\":\"t\"}";
  for (int k = 0; k < 400; ++k) {
    std::string reply;
    switch (ri(0, 12)) {
      case 0: reply = "{\"result\":\"fail\",\"code\":" + pick<std::string>({"10001", "\"20003\"", "5", "\"x\""}) + ",\"msg\":\"nope\"}"; break;
      case 1: reply = "{\"result\":\"error\",\"errorMsg\":\"\",\"reason\":\"why\"}"; break;
      case 2: reply = "not json"; break;
      case 3: reply = "{\"result\":\"success\"}"; break;
      default: {
        reply = "{\"result\":\"success\",\"list\":[";
        for (int n = ri(0, 8), i = 0; i < n; ++i) reply += (i ? "," : "") + recipe();
        reply += "]}";
      }
    }
    std::vector<PanelRecipe> lib;
    std::string err;
    bool expired = false;
    const bool ok = library::parse_cloud_list(reply.data(), reply.size(), ri(0, 1) ? KIND_MINE : KIND_SHARED, lib, err, expired);
    printf("list %d %d %s %zu\n", ok, expired, err.c_str(), lib.size());
    for (size_t i = 0; i < lib.size(); ++i) {
      PanelRecipe &p = lib[i];
      if (ri(0, 3) == 0) p.kind = (uint8_t)ri(0, 3);
      p.uid = (uint32_t)ri(1, 100000);
      p.favorite = ri(0, 1);
      p.dirty = ri(0, 1);
      if (ri(0, 4) == 0) p.color = (uint32_t)ri(0, 0xFFFFFF);
      dump(p);
      printf("B %s\n", library::cloud_body(p, env, ri(0, 3) ? p.id : nullptr).c_str());
      char nb[24];
      for (int j = 0; j < p.r.n_pours; ++j) printf("pn %s\n", library::pour_name(p, j, nb));
      printf("rd %.17g\n", library::ratio_denominator(p.r));
      char hx[8];
      library::color_hex(p.color, hx);
      printf("c %s %06x\n", hx, library::parse_color(p.color_src));
    }
    const std::string cache = library::to_cache_json(lib);
    printf("C %s\n", cache.c_str());
    std::vector<PanelRecipe> back;
    printf("L %d\n", library::from_cache_json(cache.data(), cache.size(), back));
    for (auto &p : back) dump(p);
  }
  // hand-made cache files
  for (const char *c : {"{\"v\":2,\"recipes\":[]}", "{\"v\":1}", "{\"v\":1,\"recipes\":[{\"k\":2,\"n\":\"Starter\",\"fav\":true},{\"n\":\"bare\"}]}", "[]", ""}) {
    std::vector<PanelRecipe> back;
    printf("L %d\n", library::from_cache_json(c, strlen(c), back));
    for (auto &p : back) dump(p);
  }
  return 0;
}

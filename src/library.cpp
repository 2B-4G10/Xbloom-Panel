// Recipe library: the xBloom cloud's recipe JSON and the panel's own cache file.
#include "library.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

using namespace ArduinoJson;

namespace {

#ifdef ESP_PLATFORM
// A whole recipe list is too big for internal RAM: JSON documents go to PSRAM.
struct Psram : Allocator {
  static constexpr uint32_t CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  void *allocate(size_t n) override { return heap_caps_malloc(n, CAPS); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, CAPS); }
};
Psram psram;
JsonDocument new_doc() { return JsonDocument(&psram); }
#else
JsonDocument new_doc() { return JsonDocument(); }
#endif

constexpr uint32_t DEFAULT_COLOR = 0xC9D5B8;

// ---- reading loosely typed values ---------------------------------------------
// The cloud sends some numbers as strings ("15") and flags as numbers or booleans.

// Text starting with a number, as that number; otherwise nothing.
bool leading_number(const char *s, double &d) {
  char *end;
  d = strtod(s, &end);
  return end != s;
}

double as_number(JsonVariantConst v, double fallback = 0) {
  if (v.isNull()) return fallback;
  if (v.is<const char *>()) {
    double d;
    return leading_number(v.as<const char *>(), d) ? d : fallback;
  }
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  return v.as<double>();
}

long long as_integer(JsonVariantConst v, long long fallback = 0) {
  if (v.isNull()) return fallback;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end;
    const long long n = strtoll(s, &end, 10);
    return end == s ? fallback : n;
  }
  if (v.is<long long>()) return v.as<long long>();
  return llround(as_number(v, (double)fallback));
}

// The cloud's 1 = yes / 2 = no flags.
bool as_flag(JsonVariantConst v, long long fallback) { return as_integer(v, fallback) == 1; }

// Text, or a number written as text ("1234", "2.5").
void as_text(JsonVariantConst v, char *out, size_t n) {
  out[0] = 0;
  if (v.isNull()) return;
  if (v.is<const char *>()) {
    snprintf(out, n, "%s", v.as<const char *>());
    return;
  }
  const double d = as_number(v);
  if (d == floor(d)) snprintf(out, n, "%lld", (long long)d);
  else snprintf(out, n, "%g", d);
}

// ---- strings ----------------------------------------------------------------
// Copy into a fixed buffer; if it must be cut, cut before a whole UTF-8 character.
void copy_utf8(char *dst, size_t n, const char *src) {
  size_t len = strlen(src);
  if (len >= n)
    for (len = n - 1; len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80;) --len;
  memcpy(dst, src, len);
  dst[len] = 0;
}

// Names lose spaces at both ends, as they do when saved in the xBloom app.
void copy_name(char *dst, size_t n, const char *src) {
  while (*src == ' ') ++src;
  copy_utf8(dst, n, src);
  for (size_t len = strlen(dst); len > 0 && dst[len - 1] == ' ';) dst[--len] = 0;
}

// ---- error replies ------------------------------------------------------------
// The cloud names its error code and message differently from call to call.
int error_code(JsonObjectConst reply) {
  for (const char *key : {"code", "errorCode", "resultCode", "result", "status"}) {
    JsonVariantConst v = reply[key];
    if (v.isNull()) continue;
    if (v.is<const char *>()) {  // only text that is wholly a number
      const char *s = v.as<const char *>();
      char *end;
      const long c = strtol(s, &end, 10);
      if (end != s && *end == 0) return (int)c;
    } else if (v.is<long long>() || v.is<double>()) {
      return (int)as_number(v);
    }
  }
  return -1;
}

const char *error_text(JsonObjectConst reply) {
  for (const char *key : {"msg", "message", "errorMsg", "errorMessage", "error", "reason", "tips", "info"}) {
    const char *s = reply[key].as<const char *>();
    if (reply[key].is<const char *>() && *s) return s;
  }
  return "request failed";
}

bool sign_in_expired(int code) { return code == 10001 || code == 20003; }

// ---- one cloud recipe ---------------------------------------------------------
bool read_cloud_recipe(JsonObjectConst c, RecipeKind kind, PanelRecipe &p) {
  if (c["tableId"].isNull() || c["theName"].isNull()) return false;
  p = PanelRecipe();
  xb::Recipe &r = p.r;
  p.kind = kind;
  as_text(c["tableId"], p.id, sizeof p.id);
  const char *name = c["theName"].as<const char *>();
  copy_name(r.name, sizeof r.name, name ? name : "");

  r.dose_g = as_number(c["dose"]);
  r.grind_size = (int)llround(as_number(c["grinderSize"]));
  r.grinder_on = as_flag(c["isSetGrinderSize"], 1);
  r.rpm = (int)as_integer(c["rpm"], 90);
  r.cup_type = (int)as_integer(c["cupType"], 3);

  p.bypass = as_flag(c["isEnableBypassWater"], 2);
  p.bypass_temp = as_number(c["bypassTemp"], 85);
  p.bypass_volume = as_number(c["bypassVolume"], 5);
  const char *color = c["theColor"] | "";
  p.color = *color ? library::parse_color(color) : DEFAULT_COLOR;
  copy_utf8(p.color_src, sizeof p.color_src, color);
  p.adapted_model = (int)as_integer(c["adaptedModel"], 1);
  p.created_ms = as_integer(c["createTimeStamp"], 0);
  p.is_shortcut = (int)as_integer(c["isShortcuts"], 0);
  p.subset_type = (int)as_integer(c["subSetType"], 0);
  p.subset_id = (int)as_integer(c["theSubsetId"], 0);

  for (JsonObjectConst q : c["pourList"].as<JsonArrayConst>()) {
    if (r.n_pours >= xb::MAX_POURS) break;
    xb::Pour &pour = r.pours[r.n_pours];
    pour.volume_ml = as_number(q["volume"]);
    pour.temp_c = as_number(q["temperature"], 92);
    pour.pattern = (uint8_t)as_integer(q["pattern"], 2);
    pour.flow = as_number(q["flowRate"], 3.0);
    pour.pause_s = (uint8_t)as_integer(q["pausing"], 0);
    pour.agitate_before = as_flag(q["isEnableVibrationBefore"], 2);
    pour.agitate_after = as_flag(q["isEnableVibrationAfter"], 2);
    copy_utf8(p.pour_names[r.n_pours], sizeof p.pour_names[0], q["theName"] | "");
    ++r.n_pours;
  }
  return true;
}

// The pours, as the cloud wants them: a JSON array sent as a string.
std::string pours_json(const PanelRecipe &p) {
  JsonDocument d;
  JsonArray list = d.to<JsonArray>();
  for (int i = 0; i < p.r.n_pours; ++i) {
    const xb::Pour &q = p.r.pours[i];
    char buf[24];
    JsonObject o = list.add<JsonObject>();
    o["theName"] = library::pour_name(p, i, buf);
    o["volume"] = q.volume_ml;
    o["temperature"] = q.temp_c;
    o["flowRate"] = q.flow;
    o["pattern"] = q.pattern;
    o["pausing"] = q.pause_s;
    o["isEnableVibrationBefore"] = q.agitate_before ? 1 : 2;
    o["isEnableVibrationAfter"] = q.agitate_after ? 1 : 2;
  }
  std::string s;
  serializeJson(d, s);
  return s;
}

}  // namespace

namespace library {

uint32_t parse_color(const char *hex) {
  if (*hex == '#') ++hex;
  return (uint32_t)strtoul(hex, nullptr, 16) & 0xFFFFFF;
}

void color_hex(uint32_t c, char out[8]) { snprintf(out, 8, "#%06X", (unsigned)(c & 0xFFFFFF)); }

double ratio_denominator(const xb::Recipe &r) {
  return r.dose_g > 0 ? round(r.total_water() / r.dose_g * 10) / 10 : 16.0;
}

const char *pour_name(const PanelRecipe &p, int i, char buf[24]) {
  if (p.pour_names[i][0]) return p.pour_names[i];
  if (i == 0) return "Bloom";
  snprintf(buf, 24, "Pour %d", i + 1);
  return buf;
}

bool parse_cloud_list(const char *json, size_t len, RecipeKind kind, std::vector<PanelRecipe> &out, std::string &err,
                      bool &token_expired) {
  token_expired = false;
  JsonDocument doc = new_doc();
  if (DeserializationError e = deserializeJson(doc, json, len)) {
    err = std::string("bad reply from xBloom (") + e.c_str() + ")";
    return false;
  }
  JsonObjectConst reply = doc.as<JsonObjectConst>();
  if (strcmp(reply["result"] | "", "success") != 0) {
    token_expired = sign_in_expired(error_code(reply));
    err = error_text(reply);
    return false;
  }
  for (JsonObjectConst c : reply["list"].as<JsonArrayConst>()) {
    PanelRecipe p;
    if (read_cloud_recipe(c, kind, p)) out.push_back(p);
  }
  return true;
}

std::string cloud_body(const PanelRecipe &p, const std::string &envelope_json, const char *table_id) {
  const xb::Recipe &r = p.r;
  JsonDocument doc = new_doc();
  deserializeJson(doc, envelope_json);  // the sign-in fields come first
  char name[64];
  copy_name(name, sizeof name, r.name);
  char color[8];
  color_hex(p.color, color);
  // Send the account's own spelling of the colour back unless it was changed here.
  const bool color_kept = p.color_src[0] && parse_color(p.color_src) == p.color;

  if (table_id) doc["tableId"] = table_id;
  doc["theName"] = name;
  doc["dose"] = r.dose_g;
  doc["grandWater"] = ratio_denominator(r);
  doc["grinderSize"] = (double)r.grind_size;
  doc["rpm"] = r.rpm;
  doc["cupType"] = r.cup_type;
  doc["adaptedModel"] = p.adapted_model;
  doc["isEnableBypassWater"] = p.bypass ? 1 : 2;
  doc["isSetGrinderSize"] = r.grinder_on ? 1 : 2;
  doc["theColor"] = color_kept ? p.color_src : color;
  doc["theSubsetId"] = p.subset_id;
  doc["subSetType"] = p.subset_type ? p.subset_type : 2;
  doc["appPlace"].to<JsonArray>().add(4);
  doc["createTimeStamp"] = (long long)p.created_ms;
  doc["isShortcuts"] = p.is_shortcut ? p.is_shortcut : 2;
  if (p.bypass) {
    doc["bypassTemp"] = p.bypass_temp;
    doc["bypassVolume"] = p.bypass_volume;
  }
  doc["pourDataJSONStr"] = pours_json(p);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// ---- the panel's cache file (/recipes.json), version 1 ------------------------
// Short keys keep the file small. Built-in starters store only their favourite flag.
std::string to_cache_json(const std::vector<PanelRecipe> &lib) {
  JsonDocument doc = new_doc();
  doc["v"] = 1;
  JsonArray list = doc["recipes"].to<JsonArray>();
  for (const PanelRecipe &p : lib) {
    const xb::Recipe &r = p.r;
    JsonObject o = list.add<JsonObject>();
    o["k"] = p.kind;
    if (p.kind == KIND_STARTER) {
      o["n"] = r.name;
      o["fav"] = p.favorite;
      continue;
    }
    o["id"] = p.id;
    o["uid"] = p.uid;
    o["n"] = r.name;
    o["col"] = p.color;
    o["cs"] = p.color_src;
    o["fav"] = p.favorite;
    o["dirty"] = p.dirty;
    o["dose"] = r.dose_g;
    o["gs"] = r.grind_size;
    o["gon"] = r.grinder_on;
    o["rpm"] = r.rpm;
    o["cup"] = r.cup_type;
    o["ct"] = (long long)p.created_ms;
    o["sid"] = p.subset_id;
    o["st"] = p.subset_type;
    o["sc"] = p.is_shortcut;
    o["am"] = p.adapted_model;
    o["bp"] = p.bypass;
    o["bpt"] = p.bypass_temp;
    o["bpv"] = p.bypass_volume;
    JsonArray pours = o["p"].to<JsonArray>();
    for (int i = 0; i < r.n_pours; ++i) {  // [volume, temp, pattern, flow, pause, agitate before, after, name]
      const xb::Pour &q = r.pours[i];
      JsonArray e = pours.add<JsonArray>();
      e.add(q.volume_ml);
      e.add(q.temp_c);
      e.add(q.pattern);
      e.add(q.flow);
      e.add(q.pause_s);
      e.add(q.agitate_before);
      e.add(q.agitate_after);
      e.add(p.pour_names[i]);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool from_cache_json(const char *json, size_t len, std::vector<PanelRecipe> &out) {
  JsonDocument doc = new_doc();
  if (deserializeJson(doc, json, len) || (doc["v"] | 0) != 1) return false;
  for (JsonObjectConst o : doc["recipes"].as<JsonArrayConst>()) {
    PanelRecipe p;
    xb::Recipe &r = p.r;
    p.kind = o["k"] | 0;
    copy_utf8(r.name, sizeof r.name, o["n"] | "");
    p.favorite = o["fav"] | false;
    if (p.kind != KIND_STARTER) {  // a starter's recipe comes from the firmware
      copy_utf8(p.id, sizeof p.id, o["id"] | "");
      p.uid = o["uid"] | 0u;
      p.color = o["col"] | DEFAULT_COLOR;
      copy_utf8(p.color_src, sizeof p.color_src, o["cs"] | "");
      p.dirty = o["dirty"] | false;
      r.dose_g = o["dose"] | 15.0;
      r.grind_size = o["gs"] | 40;
      r.grinder_on = o["gon"] | true;
      r.rpm = o["rpm"] | 90;
      r.cup_type = o["cup"] | 3;
      p.created_ms = o["ct"] | 0ll;
      p.subset_id = o["sid"] | 0;
      p.subset_type = o["st"] | 2;
      p.is_shortcut = o["sc"] | 2;
      p.adapted_model = o["am"] | 1;
      p.bypass = o["bp"] | false;
      p.bypass_temp = o["bpt"] | 85.0;
      p.bypass_volume = o["bpv"] | 5.0;
      for (JsonArrayConst e : o["p"].as<JsonArrayConst>()) {
        if (r.n_pours >= xb::MAX_POURS) break;
        xb::Pour &q = r.pours[r.n_pours];
        q.volume_ml = e[0] | 0.0;
        q.temp_c = e[1] | 92.0;
        q.pattern = e[2] | 2;
        q.flow = e[3] | 3.0;
        q.pause_s = e[4] | 0;
        q.agitate_before = e[5] | false;
        q.agitate_after = e[6] | false;
        copy_utf8(p.pour_names[r.n_pours], sizeof p.pour_names[0], e[7] | "");
        ++r.n_pours;
      }
    }
    out.push_back(p);
  }
  return true;
}

}  // namespace library

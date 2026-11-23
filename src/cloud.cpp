// xBloom account: sign-in, recipe sync and saving, on a background task.
// The UI posts jobs (sign_in, sync_now, push, remove) and polls for events.
#include "cloud.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include <string>

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#include <ArduinoJson.h>

#include "cloud_crypto.h"
#include "panel_net.h"

namespace {

constexpr const char *API_ROOT = "https://client-api.xbloom.com/";
constexpr uint32_t RESYNC_EVERY_MS = 15 * 60 * 1000;
constexpr const char *RECIPE_PAGE = "{\"pageNumber\":1,\"countPerPage\":100,\"adaptedModel\":1}";

// ---- session ------------------------------------------------------------------
// Saved in NVS namespace "acct": email, pass, tok, mid.
struct Session {
  char email[96] = "", password[96] = "";
  char token[2048] = "";  // xBloom's signed session string runs to hundreds of characters
  long long member = 0;
} me;
Preferences saved;

volatile cloud::Acct g_state = cloud::Acct::SignedOut;
volatile bool g_busy = false;
volatile time_t g_synced_at = 0;     // wall clock, for "Last synced"
uint32_t g_synced_ms = 0;            // uptime of the last attempt, for the 15-minute resync
bool g_resync_when_online = true;
char g_message[256] = "";

void set_message(const char *m) { strlcpy(g_message, m, sizeof g_message); }

// ---- jobs in, events out --------------------------------------------------------
enum class JobType : uint8_t { SignIn, SignOut, Sync, Push, Remove };
struct Job {
  JobType type;
  PanelRecipe *recipe = nullptr;  // Push: a heap copy the task deletes
  char a[96] = "", b[96] = "";    // SignIn: email, password. Remove: recipe id
};
QueueHandle_t jobs, events;

void post_job(const Job &j) {
  if (xQueueSend(jobs, &j, pdMS_TO_TICKS(50)) != pdTRUE) delete j.recipe;
}
void send_event(cloud::Event &e) {
  if (xQueueSend(events, &e, pdMS_TO_TICKS(100)) != pdTRUE) delete e.list;
}
void notify(cloud::Ev type, const char *msg = "", uint32_t uid = 0, const char *id = "") {
  cloud::Event e;
  e.type = type;
  e.uid = uid;
  strlcpy(e.id, id, sizeof e.id);
  strlcpy(e.msg, msg, sizeof e.msg);
  send_event(e);
}

// ---- HTTPS ------------------------------------------------------------------------
// Errors carry enough detail (TLS code, errno, free RAM, the server's words) that a
// photo of the screen is enough to tell what went wrong.
bool post_once(const char *endpoint, const std::string &body, std::string &reply, std::string &err) {
  const std::string url = std::string(API_ROOT) + endpoint;
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 30000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // check xBloom's certificate
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;
  cfg.keep_alive_enable = false;
  esp_http_client_handle_t http = esp_http_client_init(&cfg);
  if (!http) {
    err = "out of memory";
    return false;
  }
  esp_http_client_set_header(http, "Content-Type", "application/json");
  esp_http_client_set_header(http, "Referer", "https://share-h5.xbloom.com/");
  esp_http_client_set_header(http, "User-Agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X)");

  const uint32_t started = millis();
  const unsigned free_k = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  char why[128];
  bool ok = false;
  if (esp_http_client_open(http, body.size()) != ESP_OK) {
    int tls_code = 0, sock_errno = 0;  // the flags slot holds errno when TCP itself failed
    esp_http_client_get_and_clear_last_tls_error(http, &tls_code, &sock_errno);
    snprintf(why, sizeof why, "can't reach xBloom (tls -0x%04X, errno %d, ram %uk)", (unsigned)-tls_code, sock_errno,
             free_k);
    err = why;
  } else if (esp_http_client_write(http, body.data(), body.size()) != (int)body.size()) {
    snprintf(why, sizeof why, "send failed (errno %d, ram %uk)", esp_http_client_get_errno(http), free_k);
    err = why;
  } else {
    esp_http_client_fetch_headers(http);
    const int status = esp_http_client_get_status_code(http);
    reply.clear();
    char chunk[1024];
    for (int n; (n = esp_http_client_read(http, chunk, sizeof chunk)) > 0;) reply.append(chunk, n);
    ok = status == 200 && !reply.empty();
    if (!ok) {
      snprintf(why, sizeof why, "xBloom said %d (%u B, %us, ram %uk): ", status, (unsigned)reply.size(),
               (unsigned)((millis() - started) / 1000), free_k);
      std::string said = reply.substr(0, 160);
      for (char &c : said)
        if (c == '\n' || c == '\r') c = ' ';
      err = std::string(why) + (said.empty() ? "(empty)" : said);
    }
  }
  if (!ok) Serial.printf("[cloud] %s: %s\n", endpoint, err.c_str());
  esp_http_client_close(http);
  esp_http_client_cleanup(http);
  return ok;
}

// One retry after a pause, so a dropped connection doesn't fail a whole sync.
bool post(const char *endpoint, const std::string &body, std::string &reply, std::string &err) {
  if (post_once(endpoint, body, reply, err)) return true;
  vTaskDelay(pdMS_TO_TICKS(1500));
  return post_once(endpoint, body, reply, err);
}

int hardware_random(void *, unsigned char *out, size_t n) {
  esp_fill_random(out, n);
  return 0;
}

// "tu..." endpoints take the JSON encrypted, sent as one JSON string.
bool post_encrypted(const char *endpoint, const std::string &json, std::string &reply, std::string &err) {
  std::string cipher;
  if (!cloudcrypto::encrypt_body(cloudcrypto::XBLOOM_PUBLIC_KEY_PEM, json, cipher, hardware_random, nullptr)) {
    err = "encryption failed";
    return false;
  }
  return post(endpoint, "\"" + cipher + "\"", reply, err);
}

// ---- request and reply JSON ---------------------------------------------------------
// Fields every request carries (as the xBloom app sends them), the session when
// signed in, then any call-specific fields.
std::string request(bool signed_in, const char *extra_json = nullptr) {
  JsonDocument d;
  d["interfaceVersion"] = 20240918;
  d["skey"] = "testskey";
  d["clientType"] = 2;
  d["phoneType"] = "Android";
  d["languageType"] = 0;
  if (signed_in) {
    d["memberId"] = me.member;
    d["token"] = me.token;
  }
  if (extra_json) {
    JsonDocument extra;
    deserializeJson(extra, extra_json);
    for (JsonPairConst kv : extra.as<JsonObjectConst>()) d[kv.key()] = kv.value();
  }
  std::string s;
  serializeJson(d, s);
  return s;
}

long long as_id(JsonVariantConst v) { return v.is<const char *>() ? atoll(v.as<const char *>()) : v.as<long long>(); }

// Checks a reply to a save or delete. `expired` is set when xBloom wants a new sign-in.
bool accepted(const std::string &reply, std::string &err, bool &expired, std::string *new_id = nullptr) {
  expired = false;
  JsonDocument d;
  if (deserializeJson(d, reply)) {
    err = "bad reply from xBloom";
    return false;
  }
  if (strcmp(d["result"] | "", "success") == 0) {
    if (new_id) {
      JsonVariantConst id = d["tableId"];
      *new_id = id.is<const char *>() ? std::string(id.as<const char *>()) : std::to_string(id.as<long long>());
    }
    return true;
  }
  for (const char *key : {"code", "errorCode", "resultCode", "status"}) {
    const long long code = as_id(d[key]);
    if (code == 10001 || code == 20003) expired = true;
  }
  err = "request failed";
  for (const char *key : {"msg", "message", "errorMsg", "errorMessage", "error", "reason", "tips", "info"})
    if (d[key].is<const char *>() && *d[key].as<const char *>()) {
      err = d[key].as<const char *>();
      break;
    }
  return false;
}

// ---- account calls ----------------------------------------------------------------
bool log_in(const char *email, const char *password, std::string &err) {
  JsonDocument d;
  deserializeJson(d, request(false));
  d["email"] = email;
  d["password"] = password;
  std::string body, reply;
  serializeJson(d, body);
  if (!post("tMemberLogin.thtml", body, reply, err)) return false;

  JsonDocument r;
  if (deserializeJson(r, reply)) {
    err = "bad reply from xBloom";
    return false;
  }
  if (strcmp(r["result"] | "", "success") != 0) {
    err = "Wrong email or password";
    for (const char *key : {"msg", "message", "info", "error"})
      if (r[key].is<const char *>() && *r[key].as<const char *>()) {
        err = r[key].as<const char *>();
        break;
      }
    return false;
  }
  JsonVariantConst member = r["member"]["tableId"];
  const char *token = r["token"] | "";
  if (member.isNull() || !*token) {
    err = "xBloom didn't return a session";
    return false;
  }
  if (strlen(token) >= sizeof me.token) {
    err = "xBloom's session is too long for the panel";
    return false;
  }
  Serial.printf("[cloud] signed in, token %u chars\n", (unsigned)strlen(token));
  me.member = as_id(member);
  strlcpy(me.token, token, sizeof me.token);
  saved.putLong64("mid", me.member);
  saved.putString("tok", me.token);
  return true;
}

// Runs `call`; if the session has expired, signs in again with the saved password
// and runs it once more.
template <typename Call>
bool signed_call(Call call, std::string &err) {
  bool expired = false;
  if (call(expired, err)) return true;
  if (!expired || !me.password[0]) return false;
  if (!log_in(me.email, me.password, err)) {
    g_state = cloud::Acct::BadLogin;
    notify(cloud::Ev::Auth, err.c_str());
    return false;
  }
  return call(expired, err);
}

bool fetch(const char *endpoint, RecipeKind kind, std::vector<PanelRecipe> &out, bool &expired, std::string &err) {
  std::string reply;
  if (!post_encrypted(endpoint, request(true, RECIPE_PAGE), reply, err)) return false;
  return library::parse_cloud_list(reply.data(), reply.size(), kind, out, err, expired);
}

void sync() {
  std::string err;
  if (!me.token[0] && me.password[0] && !log_in(me.email, me.password, err)) {
    set_message(err.c_str());
    notify(cloud::Ev::SyncFailed, err.c_str());
    return;
  }
  auto *list = new std::vector<PanelRecipe>();
  const bool ok = signed_call(
      [&](bool &expired, std::string &e) {
        list->clear();
        if (!fetch("tuMyTeaRecipeCreated.tuhtml", KIND_MINE, *list, expired, e)) return false;
        // Shared recipes are a bonus: if that list fails, the sync still counts.
        std::vector<PanelRecipe> shared;
        bool ignored = false;
        std::string ignored_err;
        if (fetch("tuMyRecipeShared.tuhtml", KIND_SHARED, shared, ignored, ignored_err))
          for (const PanelRecipe &s : shared) {
            bool have = false;
            for (const PanelRecipe &m : *list) have |= strcmp(m.id, s.id) == 0;
            if (!have) list->push_back(s);
          }
        return true;
      },
      err);
  g_synced_ms = millis();
  if (!ok) {
    delete list;
    set_message(err.c_str());
    notify(cloud::Ev::SyncFailed, err.c_str());
    Serial.printf("[cloud] sync failed: %s\n", err.c_str());
    return;
  }
  g_synced_at = time(nullptr);
  set_message("");
  cloud::Event e;
  e.type = cloud::Ev::Synced;
  e.list = list;  // the UI takes it
  send_event(e);
  Serial.printf("[cloud] synced %u recipes\n", (unsigned)list->size());
}

void save(PanelRecipe *p) {
  const bool is_new = p->id[0] == 0;
  if (is_new && p->created_ms == 0) p->created_ms = (int64_t)time(nullptr) * 1000;
  std::string err, new_id;
  const bool ok = signed_call(
      [&](bool &expired, std::string &e) {
        const std::string body = library::cloud_body(*p, request(true), is_new ? nullptr : p->id);
        std::string reply;
        return post_encrypted(is_new ? "tuRecipeAdd.tuhtml" : "tuRecipeUpdate.tuhtml", body, reply, e) &&
               accepted(reply, e, expired, is_new ? &new_id : nullptr);
      },
      err);
  if (ok) notify(cloud::Ev::Pushed, "", p->uid, is_new ? new_id.c_str() : p->id);
  else notify(cloud::Ev::PushFailed, err.c_str(), p->uid, p->id);
  delete p;
}

void erase(const char *id) {
  std::string err;
  const bool ok = signed_call(
      [&](bool &expired, std::string &e) {
        JsonDocument which;
        which["tableId"] = id;
        std::string extra, reply;
        serializeJson(which, extra);
        return post_encrypted("tuRecipeDelete.tuhtml", request(true, extra.c_str()), reply, e) &&
               accepted(reply, e, expired);
      },
      err);
  notify(ok ? cloud::Ev::Removed : cloud::Ev::RemoveFailed, err.c_str(), 0, id);
}

void sign_in_now(const char *email, const char *password) {
  std::string err;
  if (!log_in(email, password, err)) {
    g_state = cloud::Acct::BadLogin;
    set_message(err.c_str());
    notify(cloud::Ev::Auth, err.c_str());
    return;
  }
  strlcpy(me.email, email, sizeof me.email);
  strlcpy(me.password, password, sizeof me.password);  // to sign in again when the session expires
  saved.putString("email", me.email);
  saved.putString("pass", me.password);
  g_state = cloud::Acct::SignedIn;
  set_message("");
  notify(cloud::Ev::Auth);
  sync();
}

void sign_out_now() {
  saved.clear();
  me = Session();
  g_synced_at = 0;
  g_state = cloud::Acct::SignedOut;
  set_message("");
  notify(cloud::Ev::Auth);
}

// Without Wi-Fi every job but sign-out fails at once, with a reason.
void refuse_offline(const Job &j) {
  switch (j.type) {
    case JobType::Push:
      notify(cloud::Ev::PushFailed, "No Wi-Fi", j.recipe->uid, j.recipe->id);
      delete j.recipe;
      break;
    case JobType::Remove: notify(cloud::Ev::RemoveFailed, "No Wi-Fi", 0, j.a); break;
    case JobType::SignIn:
      g_state = cloud::Acct::SignedOut;
      set_message("Connect to Wi-Fi first");
      notify(cloud::Ev::Auth, g_message);
      break;
    default: notify(cloud::Ev::SyncFailed, "No Wi-Fi"); break;
  }
}

void run(void *) {
  for (;;) {
    const bool online = net::state() == net::Wifi::Connected;
    if (!online) g_resync_when_online = true;
    // Sync on its own when Wi-Fi comes (back) and every 15 minutes.
    if (online && g_state == cloud::Acct::SignedIn &&
        (g_resync_when_online || millis() - g_synced_ms > RESYNC_EVERY_MS)) {
      g_resync_when_online = false;
      g_busy = true;
      sync();
      g_busy = false;
    }
    Job j;
    if (xQueueReceive(jobs, &j, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    if (!online && j.type != JobType::SignOut) {
      refuse_offline(j);
      continue;
    }
    g_busy = true;
    switch (j.type) {
      case JobType::SignIn: sign_in_now(j.a, j.b); break;
      case JobType::SignOut: sign_out_now(); break;
      case JobType::Sync: sync(); break;
      case JobType::Push: save(j.recipe); break;
      case JobType::Remove: erase(j.a); break;
    }
    g_busy = false;
  }
}

}  // namespace

namespace cloud {

void begin() {
  jobs = xQueueCreate(16, sizeof(Job));
  events = xQueueCreate(16, sizeof(Event));
  saved.begin("acct", false);
  strlcpy(me.email, saved.getString("email", "").c_str(), sizeof me.email);
  strlcpy(me.password, saved.getString("pass", "").c_str(), sizeof me.password);
  strlcpy(me.token, saved.getString("tok", "").c_str(), sizeof me.token);
  me.member = saved.getLong64("mid", 0);
  // Firmware before 3.1.4 saved the token cut to 95 characters, which xBloom
  // rejects. Drop it; the next sync signs in again with the password.
  if (strlen(me.token) == 95) me.token[0] = 0;
  if (me.email[0] && (me.token[0] || me.password[0])) g_state = Acct::SignedIn;
  // TLS and JSON need a deep stack; the big buffers themselves are in PSRAM.
  xTaskCreatePinnedToCore(run, "cloud", 12288, nullptr, 1, nullptr, 0);
}

Acct state() { return g_state; }
const char *email() { return me.email; }
const char *message() { return g_message; }
bool busy() { return g_busy; }
time_t last_sync() { return g_synced_at; }

void sign_in(const char *email, const char *password) {
  Job j;
  j.type = JobType::SignIn;
  strlcpy(j.a, email, sizeof j.a);
  strlcpy(j.b, password, sizeof j.b);
  g_state = Acct::SigningIn;
  set_message("");
  post_job(j);
}

void sign_out() {
  Job j;
  j.type = JobType::SignOut;
  post_job(j);
}

void sync_now() {
  Job j;
  j.type = JobType::Sync;
  post_job(j);
}

void push(const PanelRecipe &p) {
  Job j;
  j.type = JobType::Push;
  j.recipe = new PanelRecipe(p);
  post_job(j);
}

void remove(const char *id) {
  Job j;
  j.type = JobType::Remove;
  strlcpy(j.a, id, sizeof j.a);
  post_job(j);
}

bool poll(Event &e) { return xQueueReceive(events, &e, 0) == pdTRUE; }

}  // namespace cloud

// Wi-Fi, clock (NTP), the update web page and the saved screen settings.
// Wi-Fi work happens on its own task; the UI only reads state and sets requests.
#include "panel_net.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include <algorithm>
#include <string>

namespace {

constexpr const char *HOST = "xbloom-panel";        // xbloom-panel.local
constexpr const char *TZ_RIYADH = "<+03>-3";        // POSIX TZ: UTC+3 all year
constexpr const char *UPDATE_FILE = "xbloom-panel-doubleshot-UPDATE-WiFi-or-USB.bin";
constexpr int MAX_APS = 24;
constexpr uint32_t JOIN_VERDICT_MS = 12000;          // how long before "wrong password" / "not found"

Preferences settings;  // NVS namespace "panel"
SemaphoreHandle_t lock_handle;
WebServer server(80);

// Takes the lock for one block (strings shared with the UI task).
struct Locked {
  Locked() { xSemaphoreTake(lock_handle, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock_handle); }
};

// ---- Wi-Fi state ----------------------------------------------------------------
volatile net::Wifi g_wifi = net::Wifi::Off;
char g_ssid[33] = "", g_password[65] = "", g_ip[16] = "", g_host[40] = "";
volatile bool want_join = false, want_forget = false, want_scan = false, scanning = false;
volatile uint8_t last_disconnect = 0;  // esp_wifi reason code; 0 after getting an IP
uint32_t join_started = 0;
net::Ap g_aps[MAX_APS];
int g_ap_count = 0;
volatile bool web_up = false, mdns_up = false, ntp_up = false;

// ---- saved settings -------------------------------------------------------------
struct {
  bool clock = true, night_off = false;
  int sleep_min = 3, clock_color = 0, lang = 0;
} prefs;

// ---- update upload --------------------------------------------------------------
volatile bool page_open = false;  // only while Settings > Software update is on screen
volatile net::Upd g_upd = net::Upd::Idle;
volatile int g_upd_pct = 0;
char g_upd_error[64] = "";
size_t upload_size = 0, upload_done = 0;
uint8_t at_0x8000[2];  // where a USB (factory) image has its partition table

void set_upd_error(const char *e) {
  Locked l;
  strlcpy(g_upd_error, e, sizeof g_upd_error);
}

void fail_update(const char *why) {
  set_upd_error(why);
  g_upd = net::Upd::Failed;
}

// ---- updates from GitHub ------------------------------------------------------------
constexpr const char *RELEASES = "https://github.com/2B-4G10/Xbloom-Panel/releases";
constexpr uint32_t RECHECK_MS = 12UL * 3600 * 1000;
volatile net::Online g_online = net::Online::Unknown;
volatile bool want_check = false, want_install = false;
uint32_t last_check = 0;
char g_online_ver[24] = "", g_online_tag[40] = "";
int g_online_build = 0;

// The newest release is wherever /releases/latest redirects to (…/tag/v4.2.0-ds.25):
// no API call, so no rate limit.
esp_err_t grab_location(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Location"))
    strlcpy((char *)e->user_data, e->header_value, 200);
  return ESP_OK;
}

esp_http_client_handle_t open_url(const char *url, int &status, int64_t &length, http_event_handle_cb on_event = nullptr,
                                  void *user = nullptr, bool follow = true) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = 20000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1536;  // GitHub's download links are long
  cfg.max_redirection_count = 5;
  cfg.disable_auto_redirect = true;
  cfg.event_handler = on_event;
  cfg.user_data = user;
  esp_http_client_handle_t http = esp_http_client_init(&cfg);
  if (!http) return nullptr;
  esp_http_client_set_header(http, "User-Agent", "xbloom-panel/" FW_VERSION);
  for (int hop = 0; hop < 6; ++hop) {
    if (esp_http_client_open(http, 0) != ESP_OK) break;
    length = esp_http_client_fetch_headers(http);
    status = esp_http_client_get_status_code(http);
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (!redirect || !follow) return http;
    esp_http_client_flush_response(http, nullptr);
    esp_http_client_set_redirection(http);
    esp_http_client_close(http);
  }
  esp_http_client_cleanup(http);
  return nullptr;
}

void finish(esp_http_client_handle_t http) {
  esp_http_client_close(http);
  esp_http_client_cleanup(http);
}

void check_latest() {
  g_online = net::Online::Checking;
  last_check = millis();
  char location[200] = "";
  int status = 0;
  int64_t length = 0;
  const std::string url = std::string(RELEASES) + "/latest";
  esp_http_client_handle_t http = open_url(url.c_str(), status, length, grab_location, location, false);
  if (http) finish(http);
  // …/tag/v4.2.0-ds.25
  const char *tag = strstr(location, "/tag/");
  int build = 0;
  const char *ds = tag ? strstr(tag, "-ds.") : nullptr;
  if (!tag || !ds || (build = atoi(ds + 4)) <= 0) {
    Serial.printf("[update] check failed (status %d)\n", status);
    g_online = net::Online::Failed;
    return;
  }
  tag += 5;
  {
    Locked l;
    strlcpy(g_online_tag, tag, sizeof g_online_tag);
    const char *v = tag[0] == 'v' ? tag + 1 : tag;
    strlcpy(g_online_ver, v, std::min<size_t>(sizeof g_online_ver, ds - v + 1));
    g_online_build = build;
  }
#ifdef XB_EMULATOR
  const int own = 1;  // the emulator test always finds the real latest release newer
#else
  const int own = FW_BUILD;
#endif
  const bool newer = build > own;
  Serial.printf("[update] latest %s, this is build %d: %s\n", tag, own, newer ? "update available" : "up to date");
  g_online = newer ? net::Online::Available : net::Online::UpToDate;
}

// Fetches a small text file (the release's SHA256SUMS.txt).
bool fetch_text(const char *url, std::string &out) {
  int status = 0;
  int64_t length = 0;
  esp_http_client_handle_t http = open_url(url, status, length);
  if (!http) return false;
  char chunk[512];
  out.clear();
  for (int n; status == 200 && out.size() < 4096 && (n = esp_http_client_read(http, chunk, sizeof chunk)) > 0;)
    out.append(chunk, n);
  finish(http);
  return status == 200 && !out.empty();
}

void install_latest() {
  char tag[40];
  {
    Locked l;
    strlcpy(tag, g_online_tag, sizeof tag);
  }
  if (!tag[0]) return fail_update("Check for the update first");
  set_upd_error("");
  g_upd_pct = 0;
  g_upd = net::Upd::Receiving;
  const std::string base = std::string(RELEASES) + "/download/" + tag + "/";

  // The expected SHA-256 of the update file, from the release's SHA256SUMS.txt.
  std::string sums;
  if (!fetch_text((base + "SHA256SUMS.txt").c_str(), sums)) return fail_update("Can't reach GitHub. Try again");
  // Lines read "<64 hex digits>  <file name>".
  const size_t at = sums.find(UPDATE_FILE);
  const size_t line = at == std::string::npos ? at : sums.rfind('\n', at);
  const size_t start = line == std::string::npos ? 0 : line + 1;
  if (at == std::string::npos || at < start + 64) return fail_update("The release has no checksum for the update");
  const std::string want = sums.substr(start, 64);

  int status = 0;
  int64_t length = 0;
  esp_http_client_handle_t http = open_url((base + UPDATE_FILE).c_str(), status, length);
  if (!http || status != 200 || length <= 0) {
    if (http) finish(http);
    char why[64];
    snprintf(why, sizeof why, "Download failed (%d). Try again", status);
    return fail_update(why);
  }
  if (!Update.begin((size_t)length, U_FLASH)) {
    finish(http);
    return fail_update("Not enough space for the update");
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  static uint8_t buf[4096];
  int64_t done = 0;
  bool ok = true;
  while (done < length) {
    const int n = esp_http_client_read(http, (char *)buf, sizeof buf);
    if (n <= 0) {
      ok = false;
      fail_update("Connection lost. Try again");
      break;
    }
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) {
      ok = false;
      fail_update(Update.errorString());
      break;
    }
    done += n;
    g_upd_pct = (int)std::min<int64_t>(99, done * 100 / length);
  }
  finish(http);
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (!ok) return Update.abort();
  char got[65];
  for (int i = 0; i < 32; ++i) snprintf(got + 2 * i, 3, "%02x", digest[i]);
  if (want != got) {
    Update.abort();
    return fail_update("The download was damaged. Try again");
  }
  if (!Update.end(true)) return fail_update(Update.errorString());
  g_upd_pct = 100;
  g_upd = net::Upd::Done;
  Serial.printf("[update] installed %s, restarting\n", tag);
  vTaskDelay(pdMS_TO_TICKS(2000));  // time for the screen to say "restarting"
  ESP.restart();
}

void online_updates() {
  if (g_wifi != net::Wifi::Connected || g_upd == net::Upd::Receiving) return;
  if (want_install) {
    want_install = false;
    return install_latest();
  }
  const uint32_t wait = g_online == net::Online::Failed ? 10UL * 60 * 1000 : RECHECK_MS;  // a failed check: again in 10 min
  const bool due = g_online == net::Online::Unknown || millis() - last_check > wait;
  if (want_check || due) {
    want_check = false;
    check_latest();
  }
}

// ---- the web page -----------------------------------------------------------------
const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>xBloom Panel update</title>
<style>
:root{color-scheme:dark}body{margin:0;background:#000;color:#fff;font:16px -apple-system,system-ui,sans-serif}
main{max-width:440px;margin:0 auto;padding:32px 20px}h1{font-size:26px;margin:0 0 4px}.sub{color:#8e8e93;margin:0 0 28px}
.card{background:#1c1c1e;border-radius:16px;padding:20px}label{display:block;color:#aeaeb2;margin-bottom:10px}
input[type=file]{width:100%;color:#fff}button{margin-top:20px;width:100%;border:0;border-radius:28px;padding:16px;
font-size:17px;font-weight:600;background:#fff;color:#000}button:disabled{opacity:.4}
.bar{height:8px;background:#2c2c2e;border-radius:4px;margin-top:20px;overflow:hidden}.bar i{display:block;height:100%;width:0;background:#34c759}
#msg{margin-top:14px;min-height:22px;color:#aeaeb2}.err{color:#ff6b6b!important}.ok{color:#34c759!important}
</style></head><body><main><h1>xBloom Panel</h1><p class="sub">Installed version %VER%</p>
<div class="card"><label for="f">Choose the update file (<b>%FILE%</b>)</label>
<input id="f" type="file" accept=".bin"><button id="b" disabled>Install update</button>
<div class="bar"><i id="p"></i></div><div id="msg"></div></div></main><script>
const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),m=document.getElementById('msg');
f.onchange=()=>{b.disabled=!f.files.length;m.textContent='';m.className=''};
b.onclick=()=>{const x=new XMLHttpRequest(),d=new FormData();d.append('fw',f.files[0]);b.disabled=f.disabled=true;
m.className='';m.textContent='Uploading...';x.upload.onprogress=e=>{if(e.lengthComputable)p.style.width=(e.loaded/e.total*100)+'%'};
x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
if(x.status==200&&r.ok){p.style.width='100%';m.className='ok';m.textContent='Installed. The panel is restarting.'}
else{m.className='err';m.textContent=r.error||('Failed ('+x.status+')');b.disabled=f.disabled=false}};
x.onerror=()=>{m.className='err';m.textContent='Connection lost. Try again.';b.disabled=f.disabled=false};
x.open('POST','/update');x.send(d)};
</script></body></html>)HTML";

void reply_json(int status, bool ok, const char *err = "") {
  server.send(status, "application/json",
              ok ? String("{\"ok\":true}") : String("{\"ok\":false,\"error\":\"") + err + "\"}");
}

void serve_page() {
  if (!page_open) {
    server.send(403, "text/plain", "Open Settings > Software update on the panel first, then reload this page.");
    return;
  }
  String page = FPSTR(PAGE);
  page.replace("%VER%", FW_VERSION);
  page.replace("%FILE%", UPDATE_FILE);
  server.send(200, "text/html", page);
}

// Streams the uploaded file into the free app slot.
void receive_upload() {
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      upload_done = 0;
      at_0x8000[0] = at_0x8000[1] = 0;
      upload_size = server.header("Content-Length").toInt();
      set_upd_error("");
      if (!page_open) return fail_update("Open Settings > Software update on the panel first");
      g_upd_pct = 0;
      g_upd = net::Upd::Receiving;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) fail_update("Not enough space for the update");
      break;

    case UPLOAD_FILE_WRITE: {
      if (g_upd != net::Upd::Receiving) return;
      // The USB-only file starts with the bootloader, with the partition table
      // (magic AA 50) at 0x8000. It must never be written into an app slot.
      for (size_t i = 0; i < up.currentSize; ++i) {
        const size_t at = upload_done + i;
        if (at == 0x8000 || at == 0x8001) at_0x8000[at - 0x8000] = up.buf[i];
      }
      if (upload_done + up.currentSize > 0x8001 && at_0x8000[0] == 0xAA && at_0x8000[1] == 0x50) {
        Update.abort();
        return fail_update("That's the USB-only file. Use the UPDATE-WiFi-or-USB file");
      }
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        set_upd_error(Update.errorString());
        Update.abort();
        g_upd = net::Upd::Failed;
        return;
      }
      upload_done += up.currentSize;
      if (upload_size > 0) g_upd_pct = (int)std::min<size_t>(99, upload_done * 100 / upload_size);
      break;
    }

    case UPLOAD_FILE_END:
      if (g_upd != net::Upd::Receiving) return;
      if (Update.end(true)) {
        g_upd_pct = 100;
        g_upd = net::Upd::Done;
      } else {
        fail_update(Update.errorString());
      }
      break;

    case UPLOAD_FILE_ABORTED:
      Update.abort();
      fail_update("Upload interrupted");
      break;
  }
}

void finish_upload() {
  if (g_upd == net::Upd::Done) {
    reply_json(200, true);
    delay(1500);  // time for the reply to reach the phone and the panel to say "restarting"
    ESP.restart();
    return;
  }
  char err[64];
  {
    Locked l;
    strlcpy(err, g_upd_error[0] ? g_upd_error : "Update failed", sizeof err);
  }
  reply_json(400, false, err);
}

// Web server, mDNS name and NTP, started once the first time Wi-Fi connects.
void start_online_services() {
  if (!web_up) {
    const char *keep[] = {"Content-Length"};
    server.collectHeaders(keep, 1);
    server.on("/", HTTP_GET, serve_page);
    server.on("/update", HTTP_POST, finish_upload, receive_upload);
    server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
    server.begin();
    web_up = true;
  }
  if (!mdns_up && MDNS.begin(HOST)) {
    MDNS.addService("http", "tcp", 80);
    mdns_up = true;
  }
  if (!ntp_up) {
    configTzTime(TZ_RIYADH, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntp_up = true;
  }
}

// ---- Wi-Fi ------------------------------------------------------------------------
void on_wifi_event(WiFiEvent_t ev, WiFiEventInfo_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) last_disconnect = info.wifi_sta_disconnected.reason;
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) last_disconnect = 0;
}

#ifdef XB_EMULATOR
// Emulator build: no radio. Simulated networks; "Home Wi-Fi" joins with the
// password "coffee123", any other password is refused.
void join_saved() {
  last_disconnect = 0;
  join_started = millis();
  g_wifi = net::Wifi::Connecting;
}

void forget_network() {
  {
    Locked l;
    g_ssid[0] = g_password[0] = g_ip[0] = 0;
  }
  settings.remove("ssid");
  settings.remove("pass");
  g_wifi = net::Wifi::Off;
}

void scan() {
  scanning = true;
  vTaskDelay(pdMS_TO_TICKS(800));
  static const net::Ap FAKE[] = {{"Home Wi-Fi", -48, true}, {"Neighbour", -71, true}, {"Cafe Guest", -80, false}};
  Locked l;
  g_ap_count = sizeof FAKE / sizeof FAKE[0];
  memcpy(g_aps, FAKE, sizeof FAKE);
  scanning = false;
}

void track_connection() {
  if (g_wifi != net::Wifi::Connecting || millis() - join_started < 1500) return;
  Locked l;
  if (!strcmp(g_ssid, "Home Wi-Fi") && !strcmp(g_password, "coffee123")) {
    strlcpy(g_ip, "192.168.1.50", sizeof g_ip);
    g_wifi = net::Wifi::Connected;
    Serial.printf("[wifi] (simulated) connected, %s\n", g_ip);
  } else {
    g_wifi = net::Wifi::WrongPassword;
    Serial.println("[wifi] (simulated) wrong password");
  }
}
#else
void join_saved() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOST);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  char ssid[33], password[65];
  {
    Locked l;
    strlcpy(ssid, g_ssid, sizeof ssid);
    strlcpy(password, g_password, sizeof password);
  }
  last_disconnect = 0;
  join_started = millis();
  g_wifi = net::Wifi::Connecting;
  WiFi.begin(ssid, password[0] ? password : nullptr);
}

void forget_network() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  {
    Locked l;
    g_ssid[0] = g_password[0] = g_ip[0] = 0;
  }
  settings.remove("ssid");
  settings.remove("pass");
  g_wifi = net::Wifi::Off;
}

// Networks, strongest first, each name once.
void scan() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  scanning = true;
  const int found = WiFi.scanNetworks(false, false);
  Locked l;
  g_ap_count = 0;
  for (int i = 0; i < found && g_ap_count < MAX_APS; ++i) {
    const String name = WiFi.SSID(i);
    if (!name.length()) continue;
    bool seen = false;
    for (int k = 0; k < g_ap_count && !seen; ++k) seen = name == g_aps[k].ssid;
    if (seen) continue;
    net::Ap &ap = g_aps[g_ap_count++];
    strlcpy(ap.ssid, name.c_str(), sizeof ap.ssid);
    ap.rssi = (int8_t)WiFi.RSSI(i);
    ap.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  scanning = false;
}

bool password_problem(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_AUTH_EXPIRE: return true;
    default: return false;
  }
}

void track_connection() {
  if (g_wifi == net::Wifi::Off) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (g_wifi != net::Wifi::Connected) {
      Locked l;
      strlcpy(g_ip, WiFi.localIP().toString().c_str(), sizeof g_ip);
      g_wifi = net::Wifi::Connected;
      Serial.printf("[wifi] connected, %s\n", g_ip);
    }
    start_online_services();
    return;
  }
  if (g_wifi == net::Wifi::Connected) {  // lost: auto-reconnect is already at it
    Locked l;
    g_ip[0] = 0;
    g_wifi = net::Wifi::Connecting;
    join_started = millis();
  }
  // After a while of trying, name the problem if the radio told us one.
  const uint8_t reason = last_disconnect;
  if (g_wifi == net::Wifi::Connecting && millis() - join_started > JOIN_VERDICT_MS) {
    if (password_problem(reason)) g_wifi = net::Wifi::WrongPassword;
    else if (reason == WIFI_REASON_NO_AP_FOUND) g_wifi = net::Wifi::NotFound;
  }
}
#endif  // XB_EMULATOR

void task(void *) {
  for (;;) {
    if (want_forget) {
      want_forget = false;
      forget_network();
    }
    if (want_join) {
      want_join = false;
      join_saved();
    }
    if (want_scan) {
      want_scan = false;
      scan();
    }
    track_connection();
    online_updates();
    if (web_up) server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(g_upd == net::Upd::Receiving ? 1 : 20));  // keep up with an upload
  }
}

}  // namespace

namespace net {

void begin() {
  lock_handle = xSemaphoreCreateMutex();
  settings.begin("panel", false);
  prefs.clock = settings.getBool("clock", true);
  prefs.night_off = settings.getBool("night", false);
  prefs.sleep_min = settings.getInt("sleep", 3);
  prefs.clock_color = settings.getInt("ccolor", 0);
  prefs.lang = settings.getInt("lang", 0);
  strlcpy(g_ssid, settings.getString("ssid", "").c_str(), sizeof g_ssid);
  strlcpy(g_password, settings.getString("pass", "").c_str(), sizeof g_password);
  snprintf(g_host, sizeof g_host, "%s.local", HOST);
#ifndef XB_EMULATOR
  WiFi.onEvent(on_wifi_event);
  if (g_ssid[0]) want_join = true;
  else WiFi.mode(WIFI_OFF);
#else
  if (g_ssid[0]) want_join = true;
#endif
  xTaskCreatePinnedToCore(task, "net", 12288, nullptr, 2, nullptr, 0);
}

Wifi state() { return g_wifi; }
const char *ssid() { return g_ssid; }
const char *ip() { return g_ip; }
const char *hostname() { return g_host; }
bool has_saved_network() { return g_ssid[0] != 0; }

void join(const char *ssid, const char *password) {
  if (!password) password = "";
  {
    Locked l;
    strlcpy(g_ssid, ssid, sizeof g_ssid);
    strlcpy(g_password, password, sizeof g_password);
  }
  settings.putString("ssid", ssid);
  settings.putString("pass", password);
  g_wifi = Wifi::Connecting;
  want_join = true;
}

void forget() { want_forget = true; }

void scan_start() {
  scanning = true;
  want_scan = true;
}
bool scan_running() { return scanning; }
int scan_results(Ap *out, int max) {
  Locked l;
  const int n = std::min(max, g_ap_count);
  memcpy(out, g_aps, n * sizeof(Ap));
  return n;
}

bool time_valid() { return time(nullptr) > 1735689600; }  // after 2025-01-01: NTP has answered
bool local_time(struct tm *out) {
  if (!time_valid()) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, out);
  return true;
}

void update_page(bool enabled) {
  page_open = enabled;
  if (enabled && g_upd != Upd::Receiving) g_upd = Upd::Idle;
}
Upd update_state() { return g_upd; }
int update_progress() { return g_upd_pct; }
const char *update_error() { return g_upd_error; }

void check_online() { want_check = true; }
Online online_state() { return g_online; }
const char *online_version() { return g_online_ver; }
void install_online() {
  if (g_online == Online::Available && g_upd != Upd::Receiving) want_install = true;
}

bool pref_clock() { return prefs.clock; }
void set_pref_clock(bool on) { prefs.clock = on; settings.putBool("clock", on); }
bool pref_night_off() { return prefs.night_off; }
void set_pref_night_off(bool on) { prefs.night_off = on; settings.putBool("night", on); }
int pref_sleep_min() { return prefs.sleep_min; }
void set_pref_sleep_min(int m) { prefs.sleep_min = m; settings.putInt("sleep", m); }
int pref_clock_color() { return prefs.clock_color; }
void set_pref_clock_color(int c) { prefs.clock_color = c; settings.putInt("ccolor", c); }
int pref_lang() { return prefs.lang; }
void set_pref_lang(int l) { prefs.lang = l; settings.putInt("lang", l); }

}  // namespace net

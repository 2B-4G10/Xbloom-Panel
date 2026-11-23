// Bluetooth link to the xBloom Studio, on its own FreeRTOS task (core 0).
//
//   NimBLE host --notify--> raw queue --> this task --> UI event queue --> ui::tick()
//   ui --command queue--> this task --> FFE1 write, then wait for the machine's echo
#include "ble_link.h"

#ifndef XB_EMULATOR  // the emulator build simulates the machine (src/emulator.cpp)

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include <algorithm>

namespace {

using xb::Bytes;

struct Notification {
  uint16_t len;
  uint8_t data[244];  // one notification at MTU 247
};

QueueHandle_t from_radio;  // NimBLE -> this task
QueueHandle_t commands;    // UI -> this task
QueueHandle_t to_ui;       // this task -> UI

volatile LinkState g_link = LinkState::Idle;
NimBLEClient *client = nullptr;
NimBLERemoteCharacteristic *tx = nullptr;  // FFE1
xb::Assembler frames;
bool machine_asleep = false;

// The command waiting for its echo.
struct {
  uint16_t code = 0;  // 0 = not waiting
  bool echoed = false;
  uint32_t refusal = 0;
} pending;

// ---- to the UI ------------------------------------------------------------------
void to_screen(const UiEvent &e) { xQueueSend(to_ui, &e, 0); }

void to_screen(UiEvType type, const char *msg, int32_t i = 0, bool ok = true) {
  UiEvent e;
  e.type = type;
  e.i = i;
  e.ok = ok;
  if (msg) strlcpy(e.msg, msg, sizeof e.msg);
  to_screen(e);
}

void set_link(LinkState s, const char *machine = nullptr) {
  g_link = s;
  to_screen(UiEvType::Link, machine, (int32_t)s);
}

// ---- from the machine ---------------------------------------------------------------
// Runs on the NimBLE host task: copy and hand over, never block.
void on_notify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  Notification n;
  n.len = (uint16_t)std::min(len, sizeof n.data);
  memcpy(n.data, data, n.len);
  xQueueSend(from_radio, &n, 0);
}

void on_frame(const Bytes &frame) {
  if (pending.code && !pending.echoed && xb::frame_code(frame) == pending.code) {
    pending.echoed = true;
    pending.refusal = xb::refusal_code(frame);
  }
  xb::Event ev;
  if (!xb::parse_event(frame, ev)) return;
  UiEvent u;
  auto send_value = [&](UiEvType type, bool as_float) {
    if (!ev.has_u32()) return;
    u.type = type;
    if (as_float) u.f = ev.f32();
    else u.i = (int32_t)ev.u32();
    to_screen(u);
  };
  switch (ev.code) {
    case xb::N_WEIGHT:
    case xb::N_WEIGHT_ALT: send_value(UiEvType::Weight, true); break;
    case xb::N_WATER: send_value(UiEvType::Water, true); break;
    case xb::N_ACTIVITY: send_value(UiEvType::Activity, false); break;
    case xb::N_BLOOM: send_value(UiEvType::PourIndex, false); break;
    case xb::N_ENJOY: u.type = UiEvType::BrewDone; to_screen(u); break;
    case xb::N_ABANDONED: u.type = UiEvType::BrewAbandoned; to_screen(u); break;
    case 40515: case 9009: case 9010: u.type = UiEvType::BrewPaused; to_screen(u); break;
    case 40516: case 9011: u.type = UiEvType::BrewResumed; to_screen(u); break;
    case xb::N_SLEEPING:
    case xb::N_AWAKE:
      machine_asleep = ev.code == xb::N_SLEEPING;
      u.type = UiEvType::Sleep;
      u.i = machine_asleep;
      to_screen(u);
      break;
    case xb::N_PODS: {  // pod id as printable ASCII
      u.type = UiEvType::PodScanned;
      const size_t n = std::min(ev.len, sizeof u.msg - 1);
      for (size_t k = 0; k < n; ++k) u.msg[k] = isprint(ev.payload[k]) ? ev.payload[k] : 0;
      to_screen(u);
      break;
    }
    default:
      if (const char *fault = xb::fault_name(ev.code)) to_screen(UiEvType::Fault, fault);
  }
}

// Handle notifications for up to `ms`, returning early once the awaited echo is in.
void listen(uint32_t ms) {
  const uint32_t start = millis();
  Notification n;
  do {
    const uint32_t left = ms - std::min(ms, (uint32_t)(millis() - start));
    if (xQueueReceive(from_radio, &n, pdMS_TO_TICKS(std::min<uint32_t>(left, 20)))) {
      do frames.feed(n.data, n.len, on_frame);
      while (xQueueReceive(from_radio, &n, 0));
    }
    if (pending.code && pending.echoed) return;
  } while (millis() - start < ms);
}

bool linked() { return client && client->isConnected() && tx; }

// Write a frame and listen up to `ms` for its echo. False if the write itself failed.
bool write_and_listen(const Bytes &frame, uint32_t ms) {
  pending.code = xb::frame_code(frame);
  pending.echoed = false;
  pending.refusal = 0;
  if (!tx->writeValue(frame.data(), frame.size(), false)) {
    pending.code = 0;
    return false;
  }
  listen(ms);
  pending.code = 0;
  return true;
}

// Recipe steps: each must be echoed. 3 s per try, two tries (timing from xbloom-py).
// Returns nullptr when accepted, else why not.
const char *send_step(const Bytes &frame) {
  if (!linked()) return "Not connected";
  for (int attempt = 1; attempt <= 2; ++attempt) {
    if (!write_and_listen(frame, 3000)) return "Bluetooth write failed";
    if (pending.echoed) {
      const char *why = xb::refusal_reason(pending.refusal);
      // On the second try, "busy" / "restore incomplete" mean the first copy got through.
      if (why && attempt > 1 && (pending.refusal & (0x800000 | 0x200000))) return nullptr;
      return why;
    }
    Serial.printf("[ble] no echo for %u (attempt %d)\n", xb::frame_code(frame), attempt);
  }
  return "No reply from machine";
}

// One-off commands, sent the way xBloom's app does: a missing echo from an awake
// machine counts as done; only a sleeping machine (which hasn't acted) gets it again.
const char *send_once(const Bytes &frame) {
  if (!linked()) return "Not connected";
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (!write_and_listen(frame, 1500)) return "Bluetooth write failed";
    if (pending.echoed) return xb::refusal_reason(pending.refusal);
    if (!machine_asleep) return nullptr;
  }
  return "Machine is asleep";
}

const char *load_recipe(const xb::Recipe &r) {
  if (const char *bad = xb::validate(r)) return bad;
  for (const Bytes &f : xb::build_load_frames(r))
    if (const char *err = send_step(f)) return err;
  return nullptr;
}

void run(const BleCmd &c) {
  using T = BleCmdType;
  const char *err = nullptr;
  switch (c.type) {
    case T::Load: err = load_recipe(c.recipe); break;
    case T::Execute: err = send_step(xb::frame_execute()); break;
    case T::Pause: err = send_step(xb::frame_pause()); break;
    case T::Resume: err = send_step(xb::frame_resume()); break;
    case T::Stop: err = send_step(xb::frame_stop()); break;
    case T::QuitRecipe: err = send_step(xb::frame_quit_recipe()); break;
    case T::Tare: err = send_once(xb::frame_tare()); break;
    case T::GrinderEnter: err = send_once(xb::frame_grinder_enter(c.a, c.b)); break;
    case T::GrinderStart: err = send_once(xb::frame_grinder_start(c.a, c.b)); break;
    case T::GrinderStop: err = send_once(xb::frame_grinder_stop()); break;
    case T::BrewerEnter: err = send_once(xb::frame_brewer_enter(c.a, c.x)); break;
    case T::BrewerStart: err = send_once(xb::frame_brewer_start(c.x, c.y, c.z, c.a)); break;
    case T::BrewerStop: err = send_once(xb::frame_brewer_stop()); break;
    case T::BrewerTemp: err = send_once(xb::frame_brewer_temp(c.x)); break;
    case T::BrewerPattern: err = send_once(xb::frame_brewer_pattern(c.a)); break;
    case T::ScaleEnter: err = send_once(xb::frame_scale_enter()); break;
    case T::QuitGrinder: err = send_once(xb::frame_quit_grinder()); break;
    case T::QuitBrewer: err = send_once(xb::frame_quit_brewer()); break;
    case T::QuitScale: err = send_once(xb::frame_quit_scale()); break;
    case T::WeightUnit: err = send_once(xb::frame_weight_unit((xb::WeightUnit)c.a)); break;
  }
  to_screen(UiEvType::CmdResult, err, (int32_t)c.type, err == nullptr);
}

// ---- finding and connecting -------------------------------------------------------
// The first device advertising a name that starts with "XBLOOM" (any case).
const NimBLEAdvertisedDevice *find_machine() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(90);
  NimBLEScanResults found = scan->getResults(4000, false);
  const size_t prefix = strlen(xb::NAME_PREFIX);
  for (int i = 0; i < found.getCount(); ++i) {
    const NimBLEAdvertisedDevice *d = found.getDevice(i);
    const std::string name = d->getName();
    if (name.size() >= prefix && strncasecmp(name.c_str(), xb::NAME_PREFIX, prefix) == 0) return d;
  }
  return nullptr;
}

bool connect(const NimBLEAdvertisedDevice *machine) {
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setConnectionParams(12, 24, 0, 400);
    client->setConnectTimeout(8000);
  }
  if (!client->connect(machine)) return false;
  tx = nullptr;
  NimBLERemoteCharacteristic *rx = nullptr;
  for (NimBLERemoteService *s : client->getServices(true)) {
    if (!tx) tx = s->getCharacteristic(NimBLEUUID(xb::UUID_TX));
    if (!rx) rx = s->getCharacteristic(NimBLEUUID(xb::UUID_RX));
  }
  if (!tx || !rx || !rx->canNotify() || !rx->subscribe(true, on_notify)) {
    Serial.println("[ble] FFE1/FFE2 not found or subscribe failed");
    client->disconnect();
    return false;
  }
  frames.reset();
  xQueueReset(from_radio);
  vTaskDelay(pdMS_TO_TICKS(300));  // let the first notifications settle before writing
  return true;
}

void task(void *) {
  NimBLEDevice::init("xBloom Panel");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setPower(9);
  BleCmd cmd;
  for (;;) {
    if (!client || !client->isConnected()) {
      if (g_link == LinkState::Connected) Serial.println("[ble] disconnected");
      set_link(LinkState::Scanning);
      const NimBLEAdvertisedDevice *machine = find_machine();
      if (!machine) {
        while (xQueueReceive(commands, &cmd, 0))  // nothing to send them to: say so
          to_screen(UiEvType::CmdResult, "Not connected", (int32_t)cmd.type, false);
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      const std::string name = machine->getName();
      set_link(LinkState::Connecting, name.c_str());
      if (!connect(machine)) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        continue;
      }
      Serial.printf("[ble] connected to %s\n", name.c_str());
      set_link(LinkState::Connected, name.c_str());
    }
    if (xQueueReceive(commands, &cmd, 0)) run(cmd);
    listen(30);
  }
}

}  // namespace

namespace ble_link {

void begin() {
  from_radio = xQueueCreate(16, sizeof(Notification));  // internal RAM is scarce; 16 is plenty
  commands = xQueueCreate(8, sizeof(BleCmd));
  to_ui = xQueueCreate(64, sizeof(UiEvent));
  // Core 0; the UI and LVGL run on core 1.
  xTaskCreatePinnedToCore(task, "ble", 8192, nullptr, 3, nullptr, 0);
}

bool post(const BleCmd &c) { return xQueueSend(commands, &c, 0) == pdTRUE; }
bool poll(UiEvent &e) { return xQueueReceive(to_ui, &e, 0) == pdTRUE; }
LinkState state() { return g_link; }

}  // namespace ble_link

#endif  // XB_EMULATOR

// Emulator build (pio run -e emulator): the firmware as it runs in Espressif's QEMU.
// QEMU has the ESP32-S3, its flash and PSRAM, but no display, touch, Bluetooth or
// Wi-Fi radio. This build draws into memory, stands in a simulated xBloom Studio
// for the Bluetooth link and simulated networks for Wi-Fi, and drives the screens
// with scripted taps, writing a picture of the screen to the serial log at each
// step (tools/emulator/ turns the log into PNG files and a report).
#pragma once
#ifdef XB_EMULATOR

namespace emulator {
void start_display();  // lv_init and an in-memory display instead of the RGB panel
void begin();          // touch script, simulated machine (after ui::begin)
void tick();           // from loop()
void shot(const char *name);  // the screen as it is now, to the serial log
}  // namespace emulator

#endif

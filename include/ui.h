#pragma once
#include <stdint.h>

namespace ui {
void splash(); // show the boot logo (call once the display is up, before begin())
void begin();  // build the screens
void tick();   // call from loop(): applies Bluetooth and network events to the screen

// Screen sleep support
bool busy();               // true while a recipe is loaded/brewing, grinding, pouring, the stopwatch runs
                           // or an update is installing
bool take_wake();          // true once after the machine did something worth waking the screen for
void show_clock(bool on);  // full-screen clock used instead of a black screen
void set_dim(uint8_t opa); // darkens the whole picture (0 = normal) for standby
}  // namespace ui

#ifdef UI_SIM
namespace ui { void sim_show(int which, int recipe); int sim_find(const char *name); }
#endif

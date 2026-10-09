#pragma once
// Parked sleep, for a gauge on always-on power (OBD pin 16, a fused battery feed, or its own Li battery) so the clock
// keeps running while the car is off. With it switched on in SETTINGS the gauge turns its screen off and deep-sleeps
// once the engine is off (no rpm, no charging voltage) and nobody touched it for a few minutes. A touch wakes it at
// once; every 30 s it also wakes with the screen dark, connects to the OBD adapter and only turns the screen on when
// the engine runs, otherwise it sleeps again. Off by default: a gauge on switched (ACC) power never needs it.

#include <stdbool.h>

void park_init(void);          // early at boot: reads the setting and why the chip woke
bool park_silent(void);        // woke by the timer: keep the screen dark until the engine is seen running
bool park_enabled(void);
void park_set_enabled(bool on);
void park_tick(void);          // about once a second from the UI timer

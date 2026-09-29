#pragma once
// Racing My Car locked boot animation: compiled into the firmware (locked_boot_anim.c) and played on the
// Logo screen at every power-on, before the owner's own boot animation. It does not depend on any setting
// or on the bootmedia partition, so it cannot be replaced from the app; the owner's boot animation
// (bootmedia, one-click install) still plays afterwards as before.
#include <stdbool.h>
#include "lvgl.h"

// Start playing on `parent` (the Logo screen). Safe to call once; if the animation cannot be created,
// locked_boot_done() becomes true straight away and the boot continues.
void locked_boot_start(lv_obj_t *parent);
// True once the locked animation has finished (its last frame stays on screen).
bool locked_boot_done(void);

#pragma once
// 车型 as the phone and the 7" dash pick it: OBD2 标准, or brand → model → year (→ 读法) from vehicle_catalog.h
// (generated from the website's web/vehicle-catalog.js). Picking a car sets the vehicle profile that reads it.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "vehicle_catalog.h"

typedef struct { bool obd2; uint8_t brand, model, alt; uint16_t year; } vehicle_pick_t;

int vehicle_profile_find(const char *name);                      // the profile's index in vehicle_profiles.c, or -1
vehicle_pick_t vehicle_pick_get(void);                            // what is picked (from the profile when never picked)
void vehicle_pick_set(vehicle_pick_t p);                          // stores it and makes its profile the active one
void vehicle_pick_label(vehicle_pick_t p, bool zh, char *out, size_t n);   // "2014 Toyota Corolla Altis", "OBD2 STANDARD"
const char *vehicle_model_name(const vc_model_t *m, bool zh);

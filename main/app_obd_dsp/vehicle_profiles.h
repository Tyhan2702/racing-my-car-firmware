#pragma once

#include <stdint.h>
#include "obd_data_cache.h"
#include "vehicle_custom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define VEHICLE_MAX_GEARS 9   // accommodates index0 + up to 8 forward gears (e.g. ZF 8HP)

// Oil temp query mode priority
typedef enum {
    OIL_TEMP_MODE_NONE = 0xFF,
    OIL_TEMP_MODE_PID_5C = 0,       // Standard PID 01 5C
    OIL_TEMP_MODE_UDS_22_10_17 = 1, // UDS Mode 22 10 17
    OIL_TEMP_MODE_TOYOTA_21_01 = 2, // Toyota Mode 21 01
    OIL_TEMP_MODE_MAZDA_22_111F = 3, // Mazda Skyactiv Mode 22 PID 111F, single byte A-50
    OIL_TEMP_MODE_MAZDA_22_1310 = 4, // Mazda Skyactiv Mode 22 PID 1310, two bytes (A*256+B)/100-40
    OIL_TEMP_MODE_MINI_22_5822 = 5,    // MINI/BMW Mode 22 PID 5822, single byte °C = A-60 (°F=A*9/5-76)
    OIL_TEMP_MODE_BMW_22_4402 = 6,     // BMW F-series (F48 etc., B38/B48) Mode 22 PID 4402, second byte °C = B-64
    OIL_TEMP_MODE_BMW_22_03F3 = 7,     // BMW G-series Mode 22 PID 03F3, single byte °C = A-40
    OIL_TEMP_MODE_BMW_G_22_4402 = 8,   // BMW G-series Mode 22 PID 4402, two bytes °C = (A*256+B)*191.25/255-48
    OIL_TEMP_MODE_BMW_22_D002 = 9,    // BMW G-series Mode 22 PID D002, two bytes °C = (A*256+B)*191.25/255-48
    OIL_TEMP_MODE_BMW_22_111F = 10,    // BMW Mode 22 PID 111F (Header 7E0), single byte °C = A-50
} oil_temp_query_mode_t;

// Vehicle gear ratio ranges (used for gear detection)
typedef struct {
    float min_ratio;
    float max_ratio;
    enGear gear;
} gear_ratio_range_t;

// Oil temp query strategy
typedef struct {
    oil_temp_query_mode_t primary;     // preferred query mode
    oil_temp_query_mode_t secondary;   // backup 1
    oil_temp_query_mode_t tertiary;    // backup 2
    oil_temp_query_mode_t quaternary;  // backup 3 (final fallback)
    // Reserved coefficients for legacy special formulas. When can_den=0, the parser falls back to its built-in default.
    int16_t can_num;
    int16_t can_den;
    int16_t can_off;
} oil_temp_strategy_t;

// ---- Brand method lists ----
// A way to read a value the standard OBD-II PIDs don't carry, recorded on many models in OBDb (github.com/OBDb, CC BY-SA
// 4.0). A brand profile lists several per value; the gauge tries them in order, keeps the first one the car answers and
// moves on after a few unanswered requests (elm327_ble_client.c, mv_*), so one profile covers a brand's models and years.
typedef enum { MV_OIL_TEMP = 0, MV_GEAR, MV_OIL_PRESSURE, MV_TRANS_TEMP, MV_KNOCK, MV_FUEL_LEVEL, MV_FUEL_PRESSURE, MV_COUNT } mv_signal_t;
typedef enum {
    MV_LINEAR = 0,     // value = raw * mul / div + add  (°C for oil temp, kPa for oil pressure)
    MV_GEAR_PLAIN,     // raw = gear (0 = neutral), above 8 = not a forward gear
    MV_GEAR_HONDA,     // Honda 22 3086: 0 P, 1..9 D1..D9, 14 N, 15 R
} mv_kind_t;
typedef struct {
    const char *hdr;       // ATSH for the request ("7E0", "700", "18DA10F1"); NULL = the profile's own header
    const char *rx;        // ATCRA receive filter when the answer comes from a non-standard ID ("708"); NULL = default
    uint8_t  service;      // 0x01, 0x21 or 0x22
    uint16_t pid;          // 0x5C, 0x51, 0x107B ... (one byte for 0x01/0x21, two for 0x22)
    uint8_t  byte;         // data byte after the echoed service + PID
    uint8_t  len;          // 1 or 2 (big endian)
    bool     sign;         // two's complement raw value
    int32_t  mul, div, add;
    uint8_t  kind;         // mv_kind_t
    bool     need_29bit;   // the header only exists on 29-bit CAN (protocol 7/9)
} obd_method_t;
typedef struct {
    const obd_method_t *list;
    uint8_t count;
} obd_method_set_t;

// Vehicle parameter configuration
typedef struct {
    const char *name;                    // display name (e.g. "BRZ ZC6")
    float final_drive_ratio;             // final drive ratio
    float tire_rolling_radius_m;         // tire rolling radius (m)
    uint8_t gear_count;                  // number of forward gears (5 or 6)
    float gear_ratios[VEHICLE_MAX_GEARS]; // per-gear ratios, index 0 unused, 1~gear_count valid
    float gear_tolerance;                // gear detection tolerance (e.g. 0.15 = ±15%)
    oil_temp_strategy_t oil_temp_strategy; // oil temp query strategy
    bool has_boost;                      // whether turbocharged (decides whether to query/display boost pressure)
    uint16_t obd_oil_pressure_did;       // 0=off; else Mode 22 DID for OBD oil pressure (4436=B58 hPa, 586F=N55 hPa); supersedes the ADS1115 ADC
    uint16_t obd_gear_did;               // 0=off; else Mode 22 DID for direct gear read (D031=BMW ZF 8HP current gear); supersedes ratio-based gear calc
    uint8_t forced_protocol;             // forced ELM327 protocol number (ATSP), 0=auto-detect; lock to 6 for cars like BMW where auto-detect is unstable
    bool obd_functional_addr;            // true=standard PIDs use functional addressing (ATSH 7DF, same as phone apps); false=physical addressing (ATSH 7E0, Subaru etc.)
    bool obd_29bit_functional;           // true=29-bit CAN functional broadcast (ATSH 18DB33F1) for standard PIDs; only valid when obd_functional_addr=true and forced_protocol=7/9; Honda Integra/Civic 11th gen need this
    float speed_scale;                   // speed correction factor (read value × this factor), 0 or unset = 1.0 (no correction)
    uint8_t obd_timeout;                 // ATST timeout value (ELM327 units, 0=default 0x19); for BMW G OBD fast responses, set 0x0F to reduce NO DATA waits
    uint8_t poll_gap_ms;                 // poll slot interval (ms), 0=use the default OBD_POLL_SLOT_GAP_MS(30ms)
    bool can_broadcast_mode;             // true=read data by listening to CAN broadcast frames via ATMA (currently ZN/C6 CAN only), replacing standard OBD PID polling
    obd_method_set_t methods[MV_COUNT];  // brand method lists (oil temp, gear, oil pressure); they replace the profile's other ways for that value
    bool auto_29bit_functional;          // standard PIDs on 18DB33F1 when the car turned out to be 29-bit CAN (protocol 7/9), else 7DF
} vehicle_profile_t;

// Get all predefined vehicle profiles
const vehicle_profile_t *vehicle_profile_get_all(uint8_t *count);

// Get the vehicle profile at the given index
const vehicle_profile_t *vehicle_profile_get(uint8_t index);

// Get the currently active vehicle profile
const vehicle_profile_t *vehicle_profile_get_active(void);

// Set the active vehicle profile (also saved to NVS)
void vehicle_profile_set_active(uint8_t index);

// Calculate the speed constant: 1 / (final_drive * 0.377 * tire_radius)
float vehicle_profile_calc_constant(const vehicle_profile_t *p);

// Generate the gear range array from the currently active vehicle profile
// Returns the range array pointer; count outputs the number of valid elements
const gear_ratio_range_t *vehicle_profile_get_gear_ranges(uint8_t *count);

// Get the oil temp query strategy of the currently active vehicle
const oil_temp_strategy_t *vehicle_profile_get_oil_temp_strategy(void);

// Get the override configuration of the currently active vehicle (NULL = pure OBD2 standard)
const vehicle_override_t *vehicle_profile_get_override(void);

#ifdef __cplusplus
}
#endif

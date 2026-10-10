#include "vehicle_profiles.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "esp_log.h"
#include <string.h>

#define TAG "vehicle_profile"

// ---- Brand method lists (obd_method_t, vehicle_profiles.h) ----
// From OBDb (github.com/OBDb, CC BY-SA 4.0): the signal sets and the per-year replies recorded on real cars, 2000-2026,
// counted over 139 models of Toyota/Lexus/Scion, Honda/Acura, Mazda and Nissan (2026-10). Each list is in the order that
// covers the most model-years first. "my" = recorded model-years.
//   hdr   rx     svc  pid     byte len sign mul div add  kind            29bit
static const obd_method_t TOYOTA_OIL_T[] = {
    {"7E0", NULL,  0x21, 0x51,   9, 1, false, 1, 1, -40, MV_LINEAR, false},   // 231 my / 23 models (KWP-style ECUs, to ~2019)
    {"700", "708", 0x22, 0x107B, 24, 1, false, 1, 1, -40, MV_LINEAR, false},  // 175 my / 30 models (newer ECUs)
    {"700", "708", 0x22, 0x1F5C, 0, 1, false, 1, 1, -40, MV_LINEAR, false},   // 160 my / 30 models
    {NULL,  NULL,  0x01, 0x5C,   0, 1, false, 1, 1, -40, MV_LINEAR, false},   // standard, 83 my (2020 on)
};
static const obd_method_t TOYOTA_GEAR[] = {
    {"7E0", NULL,  0x21, 0x85,   0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false}, // 172 my / 19 models
    {"700", "708", 0x22, 0x1621, 0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false}, // 156 my / 30 models
    {"7E0", NULL,  0x21, 0xDA,   0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false}, // 150 my / 23 models
};
// The shift lever (P R N D, and S / manual mode): the gear number of 21 85 only means something in S / manual mode
// (in P and N the ECU reports 1); 2014 Corolla, 2008-2019 KWP-style ECUs (OBDb Toyota-Corolla, 7E0 21 25)
static const obd_method_t TOYOTA_SHIFT[] = {
    {"7E0", NULL,  0x21, 0x25,   3, 2, false, 1, 1, 0, MV_SHIFT_TOYOTA, false},
};
static const obd_method_t TOYOTA_OIL_P[] = {
    {"700", "708", 0x22, 0x1074, 0, 2, false, 10, 128, 0, MV_LINEAR, false},  // kPa, 168 my / 30 models
};
static const obd_method_t HONDA_OIL_T[] = {   // 29-bit engine ECU, its address differs by model; OBDb names two bytes
    // "engine oil temperature": on the recorded 2015 Civic byte 14 carried it (18..83 °C) and byte 11 read 0, so 14
    // first. A raw 0 (-40 °C) counts as no reading, so a model using the other byte moves on to it.
    {"18DA10F1", NULL, 0x22, 0x2666, 14, 1, false, 1, 1, -40, MV_LINEAR, true}, // 46 my / 7 models
    {"18DA10F1", NULL, 0x22, 0x2666, 11, 1, false, 1, 1, -40, MV_LINEAR, true},
    {"18DA11F1", NULL, 0x22, 0x2666, 14, 1, false, 1, 1, -40, MV_LINEAR, true}, // 36 my / 8 models
    {"18DA11F1", NULL, 0x22, 0x2666, 11, 1, false, 1, 1, -40, MV_LINEAR, true},
    {"18DA0EF1", NULL, 0x22, 0x2666, 14, 1, false, 1, 1, -40, MV_LINEAR, true}, // 11 my / 3 models
    {NULL,       NULL, 0x01, 0x5C,    0, 1, false, 1, 1, -40, MV_LINEAR, false},
};
static const obd_method_t HONDA_GEAR[] = {
    {"18DA1EF1", NULL, 0x22, 0x3086, 20, 1, false, 1, 1, 0, MV_GEAR_HONDA, true}, // transmission ECU, 70 my / 9 models
};
static const obd_method_t MAZDA_OIL_T[] = {
    {"7E0", NULL, 0x22, 0x1310, 0, 2, false, 1, 100, -40, MV_LINEAR, false},   // 74 my / 9 models
    {"7E0", NULL, 0x21, 0x51,   9, 1, false, 1, 1,   -40, MV_LINEAR, false},   // 23 my / 4 models
    {"7E0", NULL, 0x22, 0x111F, 0, 1, false, 1, 1,   -50, MV_LINEAR, false},   // 5 my / 2 models
    {NULL,  NULL, 0x01, 0x5C,   0, 1, false, 1, 1,   -40, MV_LINEAR, false},
};
static const obd_method_t MAZDA_GEAR[] = {
    {"7E0", NULL, 0x22, 0x1E12, 0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false},  // 20 my / 3 models
    {"7E1", NULL, 0x22, 0x1E12, 0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false},  // transmission ECU, 10 my / 2 models
    {"7E0", NULL, 0x22, 0x1E1F, 0, 1, false, 1, 1, 0, MV_GEAR_PLAIN, false},  // 18 my / 3 models; 4 of 18 recorded replies held no gear (70, 128): last
};
static const obd_method_t MAZDA_OIL_P[] = {
    {"7E0", NULL, 0x22, 0x0415, 0, 2, true, 1, 1, 0, MV_LINEAR, false},       // kPa, 70 my / 9 models
};
// Extra data for themes (OBDb recorded signals; model-years/models counted over 2000-2026). Transmission fluid in °C,
// fuel level in %, fuel pressure in kPa, knock as ignition retard in 0.1° (Toyota reports feedback, negative = retard).
static const obd_method_t TOYOTA_TRANS_T[] = {
    {"7E0", NULL,  0x21, 0x82,   0, 2, false, 1, 256, -40, MV_LINEAR, false},  // pan, 2008-2019 (Corolla 2014)
    {"700", "708", 0x22, 0x1638, 0, 1, false, 1, 1,   -40, MV_LINEAR, false},  // 177 my / 29 models
    {"700", "708", 0x22, 0x1627, 0, 2, false, 1, 256, -40, MV_LINEAR, false},  // pan, 156 my / 30 models
};
static const obd_method_t TOYOTA_KNOCK[] = {
    {"700", "708", 0x22, 0x105C, 2, 2, false, -5, 16, 10240, MV_LINEAR, false},          // feedback, 185 my / 30 models
    {"7E0", NULL,  0x21, 0xB2,   2, 2, false, -20480, 65535, 640, MV_LINEAR, false},     // feedback, 63 my / 19 models
    {"7E0", NULL,  0x21, 0x37,   6, 2, false, -20480, 65535, 10240, MV_LINEAR, false},   // feedback, 63 my / 19 models
};
static const obd_method_t TOYOTA_FUEL_L[] = {
    {"700", "708", 0x22, 0x1F2F, 0, 1, false, 100, 255, 0, MV_LINEAR, false},  // %, 194 my / 30 models
};
static const obd_method_t TOYOTA_FUEL_P[] = {
    {"700", "708", 0x22, 0x1F6D, 3, 2, false, 10, 1, 0, MV_LINEAR, false},    // direct injection rail, actual, 145 my
    {"700", "708", 0x22, 0x10CD, 0, 2, false, 1, 10, -3277, MV_LINEAR, false}, // low-pressure side, 137 my / 24 models
};
static const obd_method_t HONDA_TRANS_T[] = {
    {"18DA1EF1", NULL, 0x22, 0x3083, 14, 1, false, 1, 1, -40, MV_LINEAR, true}, // 73 my / 9 models
    {"18DA1DF1", NULL, 0x22, 0x2201, 26, 1, false, 1, 1, -40, MV_LINEAR, true}, // 65 my / 9 models
    {"18DA1EF1", NULL, 0x22, 0x2201, 26, 1, false, 1, 1, -40, MV_LINEAR, true}, // 41 my / 6 models
};
static const obd_method_t MAZDA_TRANS_T[] = {
    {"7E0", NULL, 0x22, 0x1E1C, 0, 2, true, 1, 16, 0, MV_LINEAR, false},      // 20 my / 3 models
    {"7E1", NULL, 0x22, 0x1E1C, 0, 2, true, 1, 16, 0, MV_LINEAR, false},      // transmission ECU, 5 my
};
static const obd_method_t MAZDA_KNOCK[] = {
    {"7E0", NULL, 0x22, 0x1746, 0, 1, false, 1000, 284, 0, MV_LINEAR, false}, // retard, 34 my / 5 models
};
static const obd_method_t NISSAN_TRANS_T[] = {
    {"7E1", NULL, 0x22, 0x110C, 0, 1, false, 1, 1, -40, MV_LINEAR, false},    // CVT/AT ECU
};
static const obd_method_t STD_OIL_T[] = {
    {NULL, NULL, 0x01, 0x5C, 0, 1, false, 1, 1, -40, MV_LINEAR, false},
};
#define MV_SET(a) { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])) }

// Predefined vehicle profiles
static const vehicle_profile_t s_profiles[] = {
    {
        // Generic OBD2 standard profile (SAE J1979 / ISO 15031-5)
        // Uses only standard PIDs, no manufacturer-specific protocols:
        //   RPM 010C, Speed 010D, Coolant 0105, Oil Temp 015C, Intake 010F, Load 0104, TPS 0111, Voltage 0142
        // Gear ratios are common 6MT placeholders (only affects gear detection accuracy, not data reading).
        .name = "OBD2 Generic",
        .final_drive_ratio = 3.500f,       // Generic placeholder
        .tire_rolling_radius_m = 0.315f,   // Common for 205/55R16
        .gear_count = 6,
        .gear_ratios = {0, 3.500f, 2.000f, 1.400f, 1.100f, 0.900f, 0.750f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // Standard OBD2 oil temp PID (°C = A - 40)
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // standard 010B intake pressure: turbo cars show boost, NA cars ~0 / vacuum
        // Standard OBD-II talks to the 7DF functional address: every car answers mode 01 there (it is what phone
        // scan apps use). The physical engine address 7E0 is not answered by many ECUs (BMW E/F/G, others), so a
        // generic profile on 7E0 read nothing on those cars until the owner picked a brand profile.
        .obd_functional_addr = true,
    },
    {
        // BRZ ZC6 Gen1 (2013-2020, FA20 NA, Gen1)
        // RPM stays on OBD; TPS/coolant/oil come from CAN broadcast frames.
        // For the explicit OBD-only fallback version, see "ZN/C6 PID" right below
        .name = "ZN/C6 CAN",
        .final_drive_ratio = 4.100f,
        .tire_rolling_radius_m = 0.314f,   // 215/45R17
        .gear_count = 6,
        .gear_ratios = {0, 3.626f, 2.188f, 1.541f, 1.213f, 1.000f, 0.767f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_TOYOTA_21_01,
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .forced_protocol = 6,
        .can_broadcast_mode = true,
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        // BRZ ZC6 / GT86 ZN6 standard PID fallback version (no ATMA CAN monitoring)
        // Some cheap clone ELM327 adapters do not support ATMA monitoring, or the car's bus does not emit the 0x140/0x360 frames,
        // so "ZN/C6 CAN" would get no RPM/oil temp/coolant temp. In this version RPM/coolant temp go through standard OBD PIDs (01 0C / 01 05),
        // oil temp goes through Toyota Mode 21 01 (same as the old config before the CAN version was introduced), trading the 100Hz RPM refresh rate for stability.
        // If ATMA monitoring works fine, prefer "ZN/C6 CAN" above.
        .name = "ZN/C6 PID",
        .final_drive_ratio = 4.100f,
        .tire_rolling_radius_m = 0.314f,   // 215/45R17
        .gear_count = 6,
        .gear_ratios = {0, 3.626f, 2.188f, 1.541f, 1.213f, 1.000f, 0.767f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_TOYOTA_21_01,  // FA20 does not support 01 5C; oil temp fixed to Mode 21 01
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .forced_protocol = 6,
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        // BRZ ZD8 OBD-only fallback (2022+, FA24 NA, Gen2, 6MT)
        // CAN monitoring is disabled here to keep the ELM327 loop single-threaded.
        // Remaining channels use standard OBD PID.
        .name = "ZD8 OBD",
        .final_drive_ratio = 4.100f,       // ZD8 6MT final drive ratio
        .tire_rolling_radius_m = 0.318f,   // 225/40R18
        .gear_count = 6,
        .gear_ratios = {0, 3.626f, 2.189f, 1.541f, 1.213f, 1.000f, 0.767f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // ZD8 uses standard PID 5C
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .forced_protocol = 6,
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        // BRZ ZD8 standard OBD fallback (6MT).
        // This variant routes RPM/coolant temp/oil temp all through standard OBD PIDs (01 0C / 01 05 / 01 5C), trading refresh rate for stability.
        .name = "ZD8",
        .final_drive_ratio = 4.100f,
        .tire_rolling_radius_m = 0.318f,   // 225/40R18
        .gear_count = 6,
        .gear_ratios = {0, 3.626f, 2.189f, 1.541f, 1.213f, 1.000f, 0.767f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // ZD8 supports standard PID 5C
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .forced_protocol = 6,
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        .name = "MX-5 ND",
        .final_drive_ratio = 2.866f,       // ND 6MT (all manuals identical; auto is 3.583)
        .tire_rolling_radius_m = 0.300f,   // 195/50R16
        .gear_count = 6,
        .gear_ratios = {0, 5.087f, 2.991f, 2.035f, 1.594f, 1.286f, 1.000f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            // Try PID 1310 (double byte) first, fallback to 111F (single byte) on consecutive failures
            .primary = OIL_TEMP_MODE_MAZDA_22_1310,
            .secondary = OIL_TEMP_MODE_MAZDA_22_111F,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        // .has_boost defaults to false (NA)
        .obd_timeout = 0x0A,  // 40ms timeout; Mazda CAN typically responds in 5-15ms, reduces NO DATA waits
        .poll_gap_ms = 1,     // Min slot interval 1ms (0=skip vTaskDelay, 1ms lets scheduler switch tasks)
    },
    {
        // BMW G-series (G20/G21/G22, B48/B58 turbo, ZF 8HP)
        // OBD-only fallback; CAN monitoring is disabled to avoid interleaving with OBD requests on the single-threaded ELM327 loop.
        // Standard OBD only responds to 7DF functional addressing, 7E0 physical gets no response; hence obd_functional_addr=true.
        // Oil temp: try PID 4402 (double byte) first, then PID D002 (oil pan backup), finally fallback to PID 03F3.
        .name = "BMW F/G",
        .final_drive_ratio = 2.813f,       // G20 330i final drive ratio
        .tire_rolling_radius_m = 0.330f,   // 225/45R18
        .gear_count = 8,                   // ZF 8HP 8-speed
        .gear_ratios = {0, 5.250f, 3.360f, 2.172f, 1.720f, 1.316f, 1.000f, 0.822f, 0.640f},  // ZF 8HP75
        .gear_tolerance = 0.09f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // Standard OBD2 PID 01 5C (reliable via OBDII)
            .secondary = OIL_TEMP_MODE_PID_5C,
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,
        .obd_gear_did = 0xDA2E,   // Mode 22 DID DA2E = EGS current gear; raw extended-address frame, see the "BMW F/G" override
        .forced_protocol = 6,
        .obd_functional_addr = true,
        .obd_timeout = 0x0F,
        .poll_gap_ms = 1,      // BRZ PID-style 1ms slot gap for faster RPM refresh
    },
    {
        // Toyota GR Supra A90 (2019+, BMW B58 3.0T / B48 2.0T, ZF 8HP51)
        // Mechanically the BMW CLAR platform with a BMW DME, so OBD behaves like BMW F/G: 11-bit CAN, 7DF functional.
        // RPM path optimized like the BRZ PID profile: obd_timeout 0x0A (40ms) + poll_gap_ms 1 for a faster refresh.
        // Engine oil temp: the B58 DME reports it via Mode 22 DID 4402 ("oil temperature after filter"),
        // °C = raw*0.75 - 48 (2 bytes, 7E0 physical) — same as BMW G-series. Verified against bmw_pid_data/b58_pid_data.h.
        // Alternative DIDs: 4408 ("unfiltered", °C = raw*0.1 - 273.14) and 4425 ("sump", °C = raw/10).
        // Fallback to standard 01 5C if 4402 is unavailable. Gear/final-drive are placeholders to refine per trim.
        // Engine oil pressure: B58 DME reports it via Mode 22 DID 4436 (absolute pressure in hPa, 7E0 physical,
        // unsigned 16-bit, raw×1). Enables OBD oil pressure so the external ADS1115 ADC is skipped for this car.
        .name = "Supra A90",
        .final_drive_ratio = 2.813f,       // placeholder from BMW F/G; 3.0T Supra is ~3.15
        .tire_rolling_radius_m = 0.330f,   // 225/45R18 placeholder
        .gear_count = 8,                   // ZF 8HP51 8-speed
        .gear_ratios = {0, 5.250f, 3.360f, 2.172f, 1.720f, 1.316f, 1.000f, 0.822f, 0.640f},  // ZF 8HP51
        .gear_tolerance = 0.09f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_BMW_G_22_4402,  // B58 Mode 22 DID 4402: °C = raw*0.75 - 48
            .secondary = OIL_TEMP_MODE_PID_5C,       // standard 01 5C fallback
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // B58/B48 turbo
        .obd_oil_pressure_did = 0x4436,    // Mode 22 DID 4436 (absolute hPa); supersedes the ADS1115 ADC
        .forced_protocol = 6,
        .obd_functional_addr = true,
        .obd_timeout = 0x0A,               // BRZ PID-style 40ms (faster than BMW F/G's 0x0F)
        .poll_gap_ms = 1,                  // BRZ PID-style 1ms polling
    },
    {
        // BMW G-series compatibility profile (G20/G21/G22/G80/G82, B48/B58 turbo, ZF 8HP)
        // CAN broadcast is disabled; use the same OBD-only request path as BMW F/G to keep the ELM327 loop serial.
        .name = "BMW G OBD",
        .final_drive_ratio = 2.813f,       // G20 330i final drive ratio
        .tire_rolling_radius_m = 0.330f,   // 225/45R18
        .gear_count = 8,                   // ZF 8HP 8-speed
        .gear_ratios = {0, 5.250f, 3.360f, 2.172f, 1.720f, 1.316f, 1.000f, 0.822f, 0.640f},  // ZF 8HP75
        .gear_tolerance = 0.09f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // CAN 0x3F9 provides oil temp directly; OBD 01 5C as fallback
            .secondary = OIL_TEMP_MODE_BMW_22_4402, // Mode 22 PID 4402 backup
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // B48/B58 turbo
        .forced_protocol = 7,              // PT-CAN auto-detect is unstable; lock to protocol 7
        .obd_functional_addr = true,       // 7DF functional addressing
        .obd_timeout = 0x0A,               // 40ms; BMW CAN responds quickly
        .poll_gap_ms = 1,
    },
    {
        // BMW E-series (E9x M3 S65 / E87 130i N52 / E9x N54 N55 / E46 E39). Standard mode 01 PIDs
        // go through 7DF functional addressing (the E-series DME does not answer physical 7E0 for
        // mode 01, same behaviour as BMW F/G). RPM/speed/coolant/intake/load/TPS all standard.
        // Oil temp & oil pressure use the N55 Mode 22 DIDs (bmw_pid_data/n55_pid_data.h) over the
        // 6F1 DME request header (see the override in vehicle_custom_config.h): 4402 oil temp
        // (°C = raw×0.75 − 48), 5822 oil temp (°C = raw − 60), 586F oil pressure (hPa, raw×1).
        // Confirmed on N55; N52/N54 likely share the same DIDs (unverified); S65/MSS60 may differ.
        .name = "BMW E",
        .final_drive_ratio = 3.846f,       // E92 M3 6MT final drive (M-DCT is 3.154)
        .tire_rolling_radius_m = 0.335f,   // rear 265/40R18
        .gear_count = 6,
        .gear_ratios = {0, 4.055f, 2.396f, 1.582f, 1.192f, 1.000f, 0.872f},  // Getrag GS6-53BZ 6MT
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // ignored when the override formula is present (see above)
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .forced_protocol = 6,              // ISO 15765-4 CAN 11-bit 500k
        .obd_functional_addr = true,       // 7DF functional addressing (same as phone apps)
        .obd_oil_pressure_did = 0x586F,    // N55 oil pressure DID (hPa, raw×1), read over ATSH6F1
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        // MINI John Cooper Works F56 (BMW B48 2.0T, FWD transverse)
        .name = "JCW F56",
        .final_drive_ratio = 3.824f,       // F56 JCW 6MT final drive ratio
        .tire_rolling_radius_m = 0.308f,   // Front wheels (FWD drive wheels) 205/45R17
        .gear_count = 6,
        .gear_ratios = {0, 3.923f, 2.136f, 1.276f, 0.921f, 0.756f, 0.628f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            // Boost pressure uses standard PID 010B (has_boost), no extra adaptation needed.
            // Oil temp: MINI/BMW enhanced Mode 22 PID 5822, °C = A-60 (community verified, same monitor as N18/N16/B48);
            // Fallback to standard 01 5C if unavailable.
            .primary = OIL_TEMP_MODE_MINI_22_5822,
            .secondary = OIL_TEMP_MODE_PID_5C,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // B48 turbo, boost pressure via standard 010B
    },
    {
        // MINI R55 Clubman (2008-2014, N14/N18 1.6T or N16 1.6 NA)
        // Uses standard OBD2 PIDs: RPM (01 0C), Speed (01 0D), Coolant (01 05), Oil Temp (01 5C),
        // Throttle (01 11), Fuel Level (01 2F), Engine Load (01 04).
        // Oil temp: try MINI Mode 22 PID 5822 first, fallback to standard 01 5C.
        .name = "MINI R55",
        .final_drive_ratio = 3.650f,       // R55 Cooper S 6MT final drive ratio (varies by trim)
        .tire_rolling_radius_m = 0.306f,   // 195/55R16 or 205/50R17 depending on trim
        .gear_count = 6,
        .gear_ratios = {0, 3.308f, 1.913f, 1.233f, 0.967f, 0.806f, 0.684f},  // Getrag GS6-55BG 6MT
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_MINI_22_5822,  // MINI/BMW Mode 22 PID 5822, °C = A-60
            .secondary = OIL_TEMP_MODE_PID_5C,      // Standard 01 5C fallback
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // N14/N18 turbo (Cooper S), set false for N16 NA base model if needed
        .forced_protocol = 6,              // ISO 15765-4 CAN 11-bit 500k
        .obd_functional_addr = true,       // 7DF functional addressing
        .obd_timeout = 0x0A,
        .poll_gap_ms = 1,
    },
    {
        // Porsche Gen2: 987.2/997.2 (2009-2012, DFI 9A1; NA; OBD fallback)
        .name = "POS 997.2",
        .final_drive_ratio = 3.44f,        // 997.2 PDK final drive ratio (user measured)
        .tire_rolling_radius_m = 0.325f,   // User measured rolling radius
        .gear_count = 7,                   // PDK 7-speed
        .gear_ratios = {0, 3.91f, 2.29f, 1.65f, 1.30f, 1.08f, 0.88f, 0.62f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            // CAN oil-temp paths are disabled here; use the standard OBD PID 01 5C fallback.
            .primary = OIL_TEMP_MODE_PID_5C,
            .secondary = OIL_TEMP_MODE_PID_5C,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = false,                // Naturally aspirated
        .forced_protocol = 6,
    },
    {
        // Porsche Gen1: 987.1/997.1 (2005-2008, M96/M97; OBD fallback)
        // Note: Gear ratios use Gen2 placeholders (987.1 differs slightly).
        .name = "POS 997.1",
        .final_drive_ratio = 3.89f,
        .tire_rolling_radius_m = 0.335f,
        .gear_count = 6,
        .gear_ratios = {0, 3.67f, 2.05f, 1.46f, 1.13f, 0.97f, 0.84f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            // CAN oil-temp paths are disabled here; use the standard OBD PID 01 5C fallback.
            .primary = OIL_TEMP_MODE_PID_5C,
            .secondary = OIL_TEMP_MODE_PID_5C,
            .tertiary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = false,
        .forced_protocol = 6,
    },
    {
        // Alfa Romeo Giulia 2.0T (GME 2.0 turbo + ZF 8HP50 8AT, RWD)
        // FCA Giorgio platform is 29-bit CAN: standard OBD (RPM/speed/coolant temp/intake temp/load/TPS/voltage/MAP)
        // goes through the 29-bit functional broadcast 18DB33F1 (protocol 7), same as Jeep/Honda Integra — NOT 11-bit 7DF.
        // Oil temp 01 5C is not supported; use FCA UDS extended addressing ATSH18DA10F1 + 22 13 02 instead
        // (see the override in vehicle_custom_config.h).
        // MY2018+ SGW only blocks write operations (code clearing/matching); read-only live data needs no bypass.
        .name = "Giulia 2.0T",
        .final_drive_ratio = 2.35f,        // RWD standard final drive (Q4 AWD is 2.65, adjust to the actual car)
        .tire_rolling_radius_m = 0.330f,   // 225/45R18, adjust to the actual tires
        .gear_count = 8,                   // ZF 8HP50
        .gear_ratios = {0, 5.000f, 3.200f, 2.143f, 1.720f, 1.313f, 1.000f, 0.823f, 0.640f},
        .gear_tolerance = 0.09f,
        .oil_temp_strategy = {
            // Oil temp is handled uniformly by the override formula (22 13 02 @18DA10F1), not the enum strategy
            .primary = OIL_TEMP_MODE_NONE,
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // boost via standard 01 0B (MAP absolute pressure)
        .forced_protocol = 7,              // ISO 15765-4 CAN 29-bit 500k (critical: not protocol 6)
        .obd_functional_addr = true,       // functional broadcast, not physical ECU address
        .obd_29bit_functional = true,      // 29-bit functional broadcast address (18DB33F1, not 7DF)
        .obd_timeout = 0x0F,
        .poll_gap_ms = 50,                 // extended UDS responses are a bit slow, keep it conservative
    },
    {
        // Jeep (generic placeholder; refine gear ratios/tire size once the exact model/engine/transmission is known)
        // Generic standard mode 01 PIDs (RPM/speed/coolant/intake/load/TPS/voltage) use the 29-bit functional
        // broadcast 18DB33F1 (forced protocol 7), same as Honda Integra. Oil temp goes through the generic
        // standard PID 01 5C — no manufacturer-specific CAN rules / formulas are applied.
        .name = "Jeep",
        .final_drive_ratio = 3.45f,        // Generic placeholder (Wrangler JL Pentastar ballpark)
        .tire_rolling_radius_m = 0.373f,   // Generic placeholder for 245/75R17
        .gear_count = 8,                   // Generic placeholder (8HP75-class 8-speed auto)
        .gear_ratios = {0, 4.710f, 3.140f, 2.100f, 1.670f, 1.290f, 1.000f, 0.840f, 0.670f},
        .gear_tolerance = 0.15f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // confirmed real sensor via probe (r=1.0)
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = false,                // set true if the specific engine is turbocharged (e.g. 2.0L Hurricane)
        .forced_protocol = 7,              // ISO 15765-4 CAN 29-bit 500k (critical: not protocol 6)
        .obd_functional_addr = true,       // functional broadcast, not physical ECU address
        .obd_29bit_functional = true,      // 29-bit functional broadcast address (18DB33F1, not 7DF)
        .obd_timeout = 0x0F,
    },
    {
        // Honda Integra/本田形格 (2023+, 11th gen Civic Si platform, L15C7 1.5T CVT)
        // This vehicle uses 29-bit CAN extended addressing for ALL OBD communications, including standard mode 01 PIDs.
        // Functional broadcast address is 18DB33F1 (not the typical 11-bit 7DF). Confirmed via real-world scan and
        // autosportlabs forum (https://forum.autosportlabs.com/viewtopic.php?t=4671): "Hondas need 29-bit extended
        // IDs for basic MODE $01 PIDs. Rather than a traditional 11-bit 0x7DF broadcast, it needs an extended 29-bit
        // 0x18DB33F1 broadcast".
        //
        // Probing via fake_elm327.py with Car Scanner's Honda profile confirmed that under header 18DB33F1, several
        // mode 22 UDS DIDs successfully mapped to gauges (RPM/speed/coolant temp/fuel rail pressure), while standard
        // mode 01 PIDs were NOT tested in that specific scan (the CSV contained only proprietary mode 22 requests).
        // This profile attempts standard mode 01 first under the 29-bit functional header; if those don't respond,
        // the vehicle will need a custom override table mapping mode 22 DIDs (22 F40C→RPM, 22 F40D→speed, etc.).
        //
        // Oil temperature: non-Type R Integra/Civic models do NOT have a physical oil temp sensor (per IntegraForums
        // discussion); the Type R's oil temp is calculated, not a direct sensor reading. Standard PID 01 5C will be
        // attempted as primary strategy, but it may return NO DATA or a placeholder/calculated value. If it fails
        // consistently, user can manually switch to a different vehicle profile or we add a custom mode 22 fallback.
        //
        // Gear ratios: placeholder values for a generic CVT (continuously variable, no discrete gears). Ratio-based
        // gear detection is disabled (gear_count=0). If the actual transmission reports gear position via CAN or a
        // mode 22 DID, a custom override can be added later.
        .name = "Honda Integra",
        .final_drive_ratio = 4.438f,       // L15C7 CVT final drive (approximation from 11th gen Civic CVT specs)
        .tire_rolling_radius_m = 0.325f,   // 215/50R17 or 215/55R16 depending on trim; this is a mid-range estimate
        .gear_count = 0,                   // CVT has no discrete gears; disable ratio-based gear detection
        .gear_ratios = {0},
        .gear_tolerance = 0.0f,
        .oil_temp_strategy = {
            .primary = OIL_TEMP_MODE_PID_5C,        // attempt standard PID first; may not be a real sensor
            .secondary = OIL_TEMP_MODE_NONE,
            .tertiary = OIL_TEMP_MODE_NONE,
            .quaternary = OIL_TEMP_MODE_NONE,
        },
        .has_boost = true,                 // L15C7 is turbocharged; boost pressure via standard 01 0B
        .forced_protocol = 7,              // ISO 15765-4 CAN 29-bit 500k (critical: not protocol 6)
        .obd_functional_addr = true,       // use functional broadcast, not physical ECU address
        .obd_29bit_functional = true,      // 29-bit functional broadcast address (18DB33F1, not 7DF)
        .obd_timeout = 0x19,               // default timeout; adjust if responses are slow
    },
    {
        // Toyota / Lexus / Scion 2000-2026, every model (Corolla, Camry, RAV4, Hilux, Vios, Yaris, Prius, ...). Standard
        // mode 01 on the engine ECU (7E0); oil temp, gear and oil pressure from the brand method lists above, each
        // falling back to the next until the car answers. Gear comes from the car or shows "--" (no guessing).
        .name = "Toyota",
        .final_drive_ratio = 0.0f,
        .tire_rolling_radius_m = 0.316f,
        .gear_count = 0,
        .gear_ratios = {0},
        .gear_tolerance = 0.0f,
        .oil_temp_strategy = { .primary = OIL_TEMP_MODE_PID_5C, .secondary = OIL_TEMP_MODE_NONE, .tertiary = OIL_TEMP_MODE_NONE, .quaternary = OIL_TEMP_MODE_NONE },
        .has_boost = true,                 // 010B on the turbo models (MAF engines just don't answer it)
        .forced_protocol = 0,
        .obd_functional_addr = false,      // physical 7E0: Toyota's engine ECU answers mode 01, 21 and 22 there
        .obd_timeout = 0x19,
        .methods = { [MV_OIL_TEMP] = MV_SET(TOYOTA_OIL_T), [MV_GEAR] = MV_SET(TOYOTA_GEAR), [MV_OIL_PRESSURE] = MV_SET(TOYOTA_OIL_P),
                     [MV_TRANS_TEMP] = MV_SET(TOYOTA_TRANS_T), [MV_KNOCK] = MV_SET(TOYOTA_KNOCK),
                     [MV_FUEL_LEVEL] = MV_SET(TOYOTA_FUEL_L), [MV_FUEL_PRESSURE] = MV_SET(TOYOTA_FUEL_P),
                     [MV_SHIFT] = MV_SET(TOYOTA_SHIFT) },
        .poll_gap_ms = 5,                  // the engine ECU answers quickly on CAN: a short gap keeps the RPM lively
    },
    {
        // Honda / Acura 2000-2026 (Civic, City, Accord, CR-V, HR-V, Jazz/Fit, ...). Standard PIDs on 7DF, or on the
        // 29-bit broadcast 18DB33F1 when the car turns out to be 29-bit CAN. Oil temp and gear only exist on the 29-bit
        // ECUs (newer Hondas); older ones give the standard PIDs.
        .name = "Honda",
        .final_drive_ratio = 0.0f,
        .tire_rolling_radius_m = 0.316f,
        .gear_count = 0,
        .gear_ratios = {0},
        .gear_tolerance = 0.0f,
        .oil_temp_strategy = { .primary = OIL_TEMP_MODE_PID_5C, .secondary = OIL_TEMP_MODE_NONE, .tertiary = OIL_TEMP_MODE_NONE, .quaternary = OIL_TEMP_MODE_NONE },
        .has_boost = true,
        .forced_protocol = 0,
        .obd_functional_addr = true,
        .auto_29bit_functional = true,
        .obd_timeout = 0x19,
        .methods = { [MV_OIL_TEMP] = MV_SET(HONDA_OIL_T), [MV_GEAR] = MV_SET(HONDA_GEAR), [MV_TRANS_TEMP] = MV_SET(HONDA_TRANS_T) },
    },
    {
        // Mazda 2000-2026 (Mazda2/3/6, CX-3/5/30/50/60/9, MX-5, BT-50, ...): standard mode 01 on 7E0, brand lists above.
        .name = "Mazda",
        .final_drive_ratio = 0.0f,
        .tire_rolling_radius_m = 0.316f,
        .gear_count = 0,
        .gear_ratios = {0},
        .gear_tolerance = 0.0f,
        .oil_temp_strategy = { .primary = OIL_TEMP_MODE_PID_5C, .secondary = OIL_TEMP_MODE_NONE, .tertiary = OIL_TEMP_MODE_NONE, .quaternary = OIL_TEMP_MODE_NONE },
        .has_boost = true,
        .forced_protocol = 0,
        .obd_functional_addr = false,
        .obd_timeout = 0x19,
        .methods = { [MV_OIL_TEMP] = MV_SET(MAZDA_OIL_T), [MV_GEAR] = MV_SET(MAZDA_GEAR), [MV_OIL_PRESSURE] = MV_SET(MAZDA_OIL_P),
                     [MV_TRANS_TEMP] = MV_SET(MAZDA_TRANS_T), [MV_KNOCK] = MV_SET(MAZDA_KNOCK) },
    },
    {
        // Nissan 2000-2026 (Almera, Sylphy/Sentra, X-Trail, Navara, Serena, ...). OBDb has no brand oil temp / gear for
        // Nissan, so: the standard PIDs on 7DF, oil temp from 01 5C where the car has it, gear shown as "--".
        .name = "Nissan",
        .final_drive_ratio = 0.0f,
        .tire_rolling_radius_m = 0.316f,
        .gear_count = 0,
        .gear_ratios = {0},
        .gear_tolerance = 0.0f,
        .oil_temp_strategy = { .primary = OIL_TEMP_MODE_PID_5C, .secondary = OIL_TEMP_MODE_NONE, .tertiary = OIL_TEMP_MODE_NONE, .quaternary = OIL_TEMP_MODE_NONE },
        .has_boost = true,
        .forced_protocol = 0,
        .obd_functional_addr = true,
        .obd_timeout = 0x19,
        .methods = { [MV_OIL_TEMP] = MV_SET(STD_OIL_T), [MV_TRANS_TEMP] = MV_SET(NISSAN_TRANS_T) },
    },
};

#define PROFILE_COUNT (sizeof(s_profiles) / sizeof(s_profiles[0]))

// Cached gear ranges for the active profile
static gear_ratio_range_t s_gear_ranges[VEHICLE_MAX_GEARS];
static uint8_t s_gear_range_count = 0;
static uint8_t s_active_idx = 0;
static bool s_ranges_dirty = true;

// Recalculate gear ratio ranges from profile
static void rebuild_gear_ranges(const vehicle_profile_t *p)
{
    s_gear_range_count = 0;
    for (uint8_t i = 1; i <= p->gear_count && i < VEHICLE_MAX_GEARS; i++) {
        float total = p->gear_ratios[i] * p->final_drive_ratio;
        s_gear_ranges[s_gear_range_count].min_ratio = total * (1.0f - p->gear_tolerance);
        s_gear_ranges[s_gear_range_count].max_ratio = total * (1.0f + p->gear_tolerance);
        s_gear_ranges[s_gear_range_count].gear = (enGear)i; // GEAR_1=1, GEAR_2=2, ...
        s_gear_range_count++;
    }
    s_ranges_dirty = false;
    ESP_LOGD(TAG, "Rebuilt gear ranges for '%s' (%d gears)", p->name, p->gear_count);
}

const vehicle_profile_t *vehicle_profile_get_all(uint8_t *count)
{
    if (count) *count = (uint8_t)PROFILE_COUNT;
    return s_profiles;
}

const vehicle_profile_t *vehicle_profile_get(uint8_t index)
{
    if (index >= PROFILE_COUNT) return &s_profiles[0];
    return &s_profiles[index];
}

const vehicle_profile_t *vehicle_profile_get_active(void)
{
    return vehicle_profile_get(s_active_idx);
}

void vehicle_profile_set_active(uint8_t index)
{
    if (index >= PROFILE_COUNT) index = 0;
    s_active_idx = index;
    s_ranges_dirty = true;
    obd_data_reset_temp_cache();

    // Save to NVS
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.vehicle_profile_idx = index;
    nvs_cfg_set(&cfg);

    ESP_LOGD(TAG, "Active profile set to [%d] '%s'", index, s_profiles[index].name);
}

float vehicle_profile_calc_constant(const vehicle_profile_t *p)
{
    if (!p) return 0;
    float denom = p->final_drive_ratio * 0.377f * p->tire_rolling_radius_m;
    if (denom == 0) return 0;
    return 1.0f / denom;
}

const gear_ratio_range_t *vehicle_profile_get_gear_ranges(uint8_t *count)
{
    if (s_ranges_dirty) {
        rebuild_gear_ranges(vehicle_profile_get_active());
    }
    if (count) *count = s_gear_range_count;
    return s_gear_ranges;
}

const oil_temp_strategy_t *vehicle_profile_get_oil_temp_strategy(void)
{
    const vehicle_profile_t *p = vehicle_profile_get_active();
    if (!p) return NULL;
    return &p->oil_temp_strategy;
}

const vehicle_override_t *vehicle_profile_get_override(void)
{
    const vehicle_profile_t *p = vehicle_profile_get_active();
    if (!p) return NULL;
    return vehicle_find_override(p->name);
}

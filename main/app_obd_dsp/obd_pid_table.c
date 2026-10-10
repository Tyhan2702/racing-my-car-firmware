// Mode 01 PID names and formulas (SAE J1979 / ISO 15031-5), see obd_pid_table.h. Values the gauge also uses
// elsewhere (5C oil temp, 2F fuel level, 42 module voltage) are kept here too, so the DATA page shows the raw PID.
#include "obd_pid_table.h"
#include <stdio.h>
#include <string.h>

enum {
    K_HEX = 0,   // raw bytes
    K_A,         // A
    K_A40,       // A - 40 °C
    K_PCT,       // A * 100 / 255 %
    K_TRIM,      // (A - 128) * 100 / 128 %
    K_A3,        // A * 3 kPa
    K_KPA,       // A kPa
    K_RPM,       // (256A + B) / 4
    K_KMH,       // A km/h
    K_ADV,       // A / 2 - 64 °
    K_MAF,       // (256A + B) / 100 g/s
    K_O2V,       // A / 200 V (B: trim)
    K_AB,        // 256A + B
    K_SEC,       // 256A + B s
    K_KM,        // 256A + B km
    K_MIN,       // 256A + B min
    K_RAIL079,   // (256A + B) * 0.079 kPa
    K_AB10,      // (256A + B) * 10 kPa
    K_LAMBDA,    // (256A + B) * 2 / 65536
    K_CAT,       // (256A + B) / 10 - 40 °C
    K_EVAP,      // signed (256A + B) / 4 Pa
    K_V1000,     // (256A + B) / 1000 V
    K_ABSLOAD,   // (256A + B) * 100 / 255 %
    K_TORQ,      // A - 125 %
    K_INJ,       // ((256A + B) - 26880) / 128 °
    K_FUELRATE,  // (256A + B) / 20 L/h
    K_AB200,     // (256A + B) / 200 kPa
    K_EVAPS,     // (256A + B) - 32767 Pa
    K_B40,       // B - 40 °C (A: sensors present)
    K_EGT,       // (256B + C) / 10 - 40 °C (A: sensors present)
    K_ODO,       // 32 bits / 10 km
    K_MON,       // monitor status: MIL and stored codes
    K_FUELTYPE,  // fuel type code
    K_NM,        // 256A + B Nm
};

static const obd_pid_info_t s_pids[] = {
    {0x01, K_MON, 1, "MIL / CODES"},       {0x03, K_HEX, 2, "FUEL SYSTEM"},
    {0x04, K_PCT, 1, "ENGINE LOAD"},       {0x05, K_A40, 1, "COOLANT TEMP"},
    {0x06, K_TRIM, 1, "STFT BANK 1"},      {0x07, K_TRIM, 1, "LTFT BANK 1"},
    {0x08, K_TRIM, 1, "STFT BANK 2"},      {0x09, K_TRIM, 1, "LTFT BANK 2"},
    {0x0A, K_A3, 1, "FUEL PRESSURE"},      {0x0B, K_KPA, 1, "MAP"},
    {0x0C, K_RPM, 2, "RPM"},               {0x0D, K_KMH, 1, "SPEED"},
    {0x0E, K_ADV, 1, "TIMING ADVANCE"},    {0x0F, K_A40, 1, "INTAKE TEMP"},
    {0x10, K_MAF, 2, "MAF"},               {0x11, K_PCT, 1, "THROTTLE"},
    {0x12, K_HEX, 1, "SECONDARY AIR"},     {0x13, K_HEX, 1, "O2 SENSORS"},
    {0x14, K_O2V, 2, "O2 B1 S1"},          {0x15, K_O2V, 2, "O2 B1 S2"},
    {0x16, K_O2V, 2, "O2 B1 S3"},          {0x17, K_O2V, 2, "O2 B1 S4"},
    {0x18, K_O2V, 2, "O2 B2 S1"},          {0x19, K_O2V, 2, "O2 B2 S2"},
    {0x1A, K_O2V, 2, "O2 B2 S3"},          {0x1B, K_O2V, 2, "O2 B2 S4"},
    {0x1C, K_A, 1, "OBD STANDARD"},        {0x1D, K_HEX, 1, "O2 SENSORS 2"},
    {0x1E, K_HEX, 1, "AUX INPUT"},         {0x1F, K_SEC, 2, "RUN TIME"},
    {0x21, K_KM, 2, "KM WITH MIL ON"},     {0x22, K_RAIL079, 2, "RAIL PRESS REL"},
    {0x23, K_AB10, 2, "RAIL PRESSURE"},
    {0x24, K_LAMBDA, 4, "LAMBDA S1 V"},    {0x25, K_LAMBDA, 4, "LAMBDA S2 V"},
    {0x26, K_LAMBDA, 4, "LAMBDA S3 V"},    {0x27, K_LAMBDA, 4, "LAMBDA S4 V"},
    {0x28, K_LAMBDA, 4, "LAMBDA S5 V"},    {0x29, K_LAMBDA, 4, "LAMBDA S6 V"},
    {0x2A, K_LAMBDA, 4, "LAMBDA S7 V"},    {0x2B, K_LAMBDA, 4, "LAMBDA S8 V"},
    {0x2C, K_PCT, 1, "EGR COMMANDED"},     {0x2D, K_TRIM, 1, "EGR ERROR"},
    {0x2E, K_PCT, 1, "EVAP PURGE"},        {0x2F, K_PCT, 1, "FUEL LEVEL"},
    {0x30, K_A, 1, "WARM-UPS SINCE CLR"},  {0x31, K_KM, 2, "KM SINCE CLR"},
    {0x32, K_EVAP, 2, "EVAP VAPOR"},       {0x33, K_KPA, 1, "BARO PRESSURE"},
    {0x34, K_LAMBDA, 4, "LAMBDA S1 MA"},   {0x35, K_LAMBDA, 4, "LAMBDA S2 MA"},
    {0x36, K_LAMBDA, 4, "LAMBDA S3 MA"},   {0x37, K_LAMBDA, 4, "LAMBDA S4 MA"},
    {0x38, K_LAMBDA, 4, "LAMBDA S5 MA"},   {0x39, K_LAMBDA, 4, "LAMBDA S6 MA"},
    {0x3A, K_LAMBDA, 4, "LAMBDA S7 MA"},   {0x3B, K_LAMBDA, 4, "LAMBDA S8 MA"},
    {0x3C, K_CAT, 2, "CAT TEMP B1 S1"},    {0x3D, K_CAT, 2, "CAT TEMP B2 S1"},
    {0x3E, K_CAT, 2, "CAT TEMP B1 S2"},    {0x3F, K_CAT, 2, "CAT TEMP B2 S2"},
    {0x41, K_HEX, 4, "MONITORS"},          {0x42, K_V1000, 2, "MODULE VOLTAGE"},
    {0x43, K_ABSLOAD, 2, "ABSOLUTE LOAD"}, {0x44, K_LAMBDA, 2, "COMMANDED LAMBDA"},
    {0x45, K_PCT, 1, "REL THROTTLE"},      {0x46, K_A40, 1, "AMBIENT TEMP"},
    {0x47, K_PCT, 1, "THROTTLE B"},        {0x48, K_PCT, 1, "THROTTLE C"},
    {0x49, K_PCT, 1, "PEDAL D"},           {0x4A, K_PCT, 1, "PEDAL E"},
    {0x4B, K_PCT, 1, "PEDAL F"},           {0x4C, K_PCT, 1, "CMD THROTTLE"},
    {0x4D, K_MIN, 2, "MIN WITH MIL ON"},   {0x4E, K_MIN, 2, "MIN SINCE CLR"},
    {0x51, K_FUELTYPE, 1, "FUEL TYPE"},    {0x52, K_PCT, 1, "ETHANOL"},
    {0x53, K_AB200, 2, "EVAP ABS PRESS"},  {0x54, K_EVAPS, 2, "EVAP PRESS"},
    {0x55, K_TRIM, 1, "STFT O2 B1"},       {0x56, K_TRIM, 1, "LTFT O2 B1"},
    {0x57, K_TRIM, 1, "STFT O2 B2"},       {0x58, K_TRIM, 1, "LTFT O2 B2"},
    {0x59, K_AB10, 2, "RAIL ABS PRESS"},   {0x5A, K_PCT, 1, "REL PEDAL"},
    {0x5B, K_PCT, 1, "HYBRID BATTERY"},    {0x5C, K_A40, 1, "OIL TEMP"},
    {0x5D, K_INJ, 2, "INJECTION TIMING"},  {0x5E, K_FUELRATE, 2, "FUEL RATE"},
    {0x5F, K_HEX, 1, "EMISSION STD"},
    {0x61, K_TORQ, 1, "DEMAND TORQUE"},    {0x62, K_TORQ, 1, "ACTUAL TORQUE"},
    {0x63, K_NM, 2, "REFERENCE TORQUE"},   {0x64, K_HEX, 5, "TORQUE POINTS"},
    {0x66, K_HEX, 5, "MAF SENSORS"},       {0x67, K_B40, 3, "COOLANT SENSORS"},
    {0x68, K_B40, 3, "INTAKE SENSORS"},    {0x6B, K_HEX, 5, "EGR TEMP"},
    {0x6C, K_HEX, 5, "THROTTLE ACTUATOR"}, {0x6F, K_HEX, 3, "TURBO INLET"},
    {0x70, K_HEX, 9, "BOOST CONTROL"},     {0x73, K_HEX, 5, "EXHAUST PRESS"},
    {0x74, K_HEX, 5, "TURBO RPM"},         {0x75, K_HEX, 7, "TURBO A TEMP"},
    {0x76, K_HEX, 7, "TURBO B TEMP"},      {0x77, K_HEX, 5, "CAC TEMP"},
    {0x78, K_EGT, 3, "EGT BANK 1"},        {0x79, K_EGT, 3, "EGT BANK 2"},
    {0x7A, K_HEX, 7, "DPF 1"},             {0x7B, K_HEX, 7, "DPF 2"},
    {0x7C, K_EGT, 3, "DPF TEMP"},          {0x7F, K_HEX, 13, "ENGINE RUN TIME"},
    {0x83, K_HEX, 5, "NOX SENSOR"},        {0x84, K_A40, 1, "MANIFOLD SURFACE"},
    {0x8D, K_PCT, 1, "THROTTLE G"},        {0x8E, K_TORQ, 1, "FRICTION TORQUE"},
    {0x9A, K_HEX, 6, "HYBRID SYSTEM"},     {0x9D, K_HEX, 4, "FUEL RATE ENG"},
    {0xA2, K_HEX, 2, "CYL FUEL RATE"},     {0xA4, K_HEX, 4, "TRANS GEAR"},
    {0xA6, K_ODO, 4, "ODOMETER"},
};

const obd_pid_info_t *obd_pid_info(uint8_t pid)
{
    for (size_t i = 0; i < sizeof(s_pids) / sizeof(s_pids[0]); i++)
        if (s_pids[i].pid == pid) return &s_pids[i];
    return NULL;
}
size_t obd_pid_count(void) { return sizeof(s_pids) / sizeof(s_pids[0]); }
const obd_pid_info_t *obd_pid_at(size_t i) { return i < obd_pid_count() ? &s_pids[i] : NULL; }

static const char *const s_fuel[] = {"-", "GASOLINE", "METHANOL", "ETHANOL", "DIESEL", "LPG", "CNG", "PROPANE",
                                     "ELECTRIC", "BIFUEL GAS", "BIFUEL METH", "BIFUEL ETH", "BIFUEL LPG", "BIFUEL CNG",
                                     "BIFUEL PROP", "BIFUEL ELEC", "BIFUEL MIX", "HYBRID GAS", "HYBRID ETH",
                                     "HYBRID DIESEL", "HYBRID ELEC", "HYBRID MIX", "HYBRID REGEN", "BIFUEL DIESEL"};

bool obd_pid_format(uint8_t pid, const uint8_t *d, uint8_t n, char *o, size_t ol)
{
    const obd_pid_info_t *p = obd_pid_info(pid);
    uint8_t need = p ? p->len : 1;
    if (n < 1) return false;
    int kind = p ? p->kind : K_HEX;
    if (kind != K_HEX && n < need) return false;
    int32_t A = d[0], B = n > 1 ? d[1] : 0, AB = (A << 8) | B;
    switch (kind) {
    case K_A:        snprintf(o, ol, "%ld", (long)A); break;
    case K_A40:      snprintf(o, ol, "%ld C", (long)(A - 40)); break;
    case K_PCT:      snprintf(o, ol, "%.1f %%", A * 100.0f / 255); break;
    case K_TRIM:     snprintf(o, ol, "%+.1f %%", (A - 128) * 100.0f / 128); break;
    case K_A3:       snprintf(o, ol, "%ld KPA", (long)(A * 3)); break;
    case K_KPA:      snprintf(o, ol, "%ld KPA", (long)A); break;
    case K_RPM:      snprintf(o, ol, "%ld RPM", (long)(AB / 4)); break;
    case K_KMH:      snprintf(o, ol, "%ld KM/H", (long)A); break;
    case K_ADV:      snprintf(o, ol, "%.1f DEG", A / 2.0f - 64); break;
    case K_MAF:      snprintf(o, ol, "%.2f G/S", AB / 100.0f); break;
    case K_O2V:      if (A == 0xFF) return false; snprintf(o, ol, "%.3f V", A / 200.0f); break;
    case K_AB:       snprintf(o, ol, "%ld", (long)AB); break;
    case K_SEC:      snprintf(o, ol, "%ld S", (long)AB); break;
    case K_KM:       snprintf(o, ol, "%ld KM", (long)AB); break;
    case K_MIN:      snprintf(o, ol, "%ld MIN", (long)AB); break;
    case K_RAIL079:  snprintf(o, ol, "%.1f KPA", AB * 0.079f); break;
    case K_AB10:     snprintf(o, ol, "%ld KPA", (long)(AB * 10)); break;
    case K_LAMBDA:   snprintf(o, ol, "%.3f L", AB * 2.0f / 65536); break;
    case K_CAT:      snprintf(o, ol, "%.0f C", AB / 10.0f - 40); break;
    case K_EVAP:     snprintf(o, ol, "%.1f PA", (int16_t)AB / 4.0f); break;
    case K_V1000:    snprintf(o, ol, "%.2f V", AB / 1000.0f); break;
    case K_ABSLOAD:  snprintf(o, ol, "%.0f %%", AB * 100.0f / 255); break;
    case K_TORQ:     snprintf(o, ol, "%ld %%", (long)(A - 125)); break;
    case K_INJ:      snprintf(o, ol, "%.2f DEG", (AB - 26880) / 128.0f); break;
    case K_FUELRATE: snprintf(o, ol, "%.2f L/H", AB / 20.0f); break;
    case K_AB200:    snprintf(o, ol, "%.2f KPA", AB / 200.0f); break;
    case K_EVAPS:    snprintf(o, ol, "%ld PA", (long)(AB - 32767)); break;
    case K_NM:       snprintf(o, ol, "%ld NM", (long)AB); break;
    case K_B40:      if (!(A & 1)) return false; snprintf(o, ol, "%ld C", (long)(B - 40)); break;
    case K_EGT:      if (!(A & 1)) return false; snprintf(o, ol, "%.0f C", (((int32_t)d[1] << 8) | d[2]) / 10.0f - 40); break;
    case K_ODO:      snprintf(o, ol, "%.1f KM", (((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3]) / 10.0f); break;
    case K_MON:      snprintf(o, ol, "MIL %s %d", (A & 0x80) ? "ON" : "OFF", (int)(A & 0x7F)); break;
    case K_FUELTYPE: snprintf(o, ol, "%s", A < (int32_t)(sizeof(s_fuel) / sizeof(s_fuel[0])) ? s_fuel[A] : "OTHER"); break;
    default: {
        size_t k = 0;
        for (uint8_t i = 0; i < n && i < 6 && k + 3 < ol; i++) k += snprintf(o + k, ol - k, i ? " %02X" : "%02X", d[i]);
        break;
    }
    }
    return true;
}

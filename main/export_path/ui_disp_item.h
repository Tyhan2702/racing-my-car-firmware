#pragma once
// ================================================================
//  ui_disp_item.h — the gauge's data items and their values for the start-up sweep
//  (the theme page runs its dials up to these and back when the OBD link comes up, ui.c my_timerMain).
//
//  Raw value conventions: temperature °C, percentage %, RPM rpm, speed km/h, voltage mV,
//              oil pressure / boost pressure / brake temp are all ×10 integers.
// ================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISP_ITEM_CLT = 0,
    DISP_ITEM_IAT,
    DISP_ITEM_OIL,
    DISP_ITEM_LOAD,
    DISP_ITEM_TPS,
    DISP_ITEM_RPM,
    DISP_ITEM_SPEED,
    DISP_ITEM_BAT,
    DISP_ITEM_OILP,
    DISP_ITEM_BKT,
    DISP_ITEM_BOOST,
    DISP_ITEM_AFR,
    DISP_ITEM_COUNT
} disp_item_t;

// Sweep animation value: r ∈ [0,1] → sweep peak raw value for this data item
int32_t disp_item_sweep_value(disp_item_t item, float r);

#ifdef __cplusplus
}
#endif

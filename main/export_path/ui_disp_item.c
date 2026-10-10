// ================================================================
//  ui_disp_item.c — start-up sweep values of the gauge's data items (see ui_disp_item.h)
// ================================================================

#include "ui_disp_item.h"

int32_t disp_item_sweep_value(disp_item_t item, float r)
{
    switch (item) {
        case DISP_ITEM_CLT:
        case DISP_ITEM_IAT:
        case DISP_ITEM_OIL:
            return (int32_t)(120.0f * r);
        case DISP_ITEM_LOAD:
        case DISP_ITEM_TPS:
            return (int32_t)(100.0f * r);
        case DISP_ITEM_RPM:
            return (int32_t)(8000.0f * r);   // = SWEEP_RPM_PEAK
        case DISP_ITEM_SPEED:
            return (int32_t)(999.0f * r);    // = SWEEP_SPEED_PEAK
        case DISP_ITEM_BAT:
            return (int32_t)(12000.0f + 2400.0f * r); // 12.0~14.4V
        case DISP_ITEM_OILP:
            return (int32_t)(100.0f * r); // 0.0~10.0bar (x10)
        case DISP_ITEM_BKT:
            return (int32_t)(600.0f * r); // 0.0~60.0'C (x10)
        case DISP_ITEM_BOOST:
            return (int32_t)(20.0f * r); // 0.0~2.0bar gauge pressure (x10)
        case DISP_ITEM_AFR:
            return (int32_t)(800.0f + 1400.0f * r); // 8.0~22.0:1 (x100)
        default:
            return 0;
    }
}

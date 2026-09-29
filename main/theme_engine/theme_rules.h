#pragma once
// ============================================================
//  theme_rules.h — Conditional Rules Engine for Theme Elements
//
//  Enables themes to define dynamic behavior: "when RPM >= 4000,
//  change portrait image and text color". Old themes without rules
//  continue to work unchanged.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "theme_interface.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Condition operators
typedef enum {
    RULE_OP_GT,        // >
    RULE_OP_GTE,       // >=
    RULE_OP_LT,        // <
    RULE_OP_LTE,       // <=
    RULE_OP_EQ,        // ==
    RULE_OP_NE,        // !=
} rule_operator_t;

// Condition combining modes
typedef enum {
    RULE_COMBINE_ALL,  // All conditions must be true (AND)
    RULE_COMBINE_ANY,  // At least one condition must be true (OR)
} rule_combine_mode_t;

// Single condition: "obd.rpm >= 4000"
typedef struct {
    char data_source[24];    // "obd.rpm", "obd.coolant_temp", etc.
    rule_operator_t op;
    int32_t value;           // Threshold value
} rule_condition_t;

// Action types
typedef enum {
    RULE_ACTION_SET_IMAGE,      // Change image asset
    RULE_ACTION_SET_COLOR,      // Change element color
    RULE_ACTION_SET_VISIBLE,    // Show/hide element
    RULE_ACTION_SET_TEXT,       // Change static text (labels only)
} rule_action_type_t;

// Single action to apply when rule triggers
typedef struct {
    char target_id[32];         // Element ID to modify
    rule_action_type_t type;
    union {
        char asset_name[32];    // For SET_IMAGE
        uint32_t color;         // For SET_COLOR (0xRRGGBB)
        bool visible;           // For SET_VISIBLE
        char text[48];          // For SET_TEXT
    };
} rule_action_t;

#define RULE_MAX_CONDITIONS 4
#define RULE_MAX_ACTIONS 8

// A complete rule: conditions + actions
typedef struct {
    char id[32];                              // Rule identifier
    uint8_t priority;                         // Higher priority overrides lower (0-255)
    rule_combine_mode_t combine_mode;         // How to combine multiple conditions
    rule_condition_t conditions[RULE_MAX_CONDITIONS];
    uint8_t condition_count;
    int32_t hysteresis;                       // Prevents flapping (optional, 0=disabled)
    rule_action_t actions[RULE_MAX_ACTIONS];
    uint8_t action_count;

    // Runtime state
    bool active;                              // Currently triggered
    int32_t trigger_value;                    // Value that triggered (for hysteresis)
} theme_rule_t;

#define THEME_MAX_RULES 16

// ============================================================
//  Public API
// ============================================================

/**
 * Parse rules from layout JSON and register them
 * Called during page creation, before elements are built
 *
 * @param layout_json The parsed layout.json cJSON object
 * @return ESP_OK on success, ESP_ERR_* on parse failure
 */
esp_err_t theme_rules_parse(cJSON *layout_json);

/**
 * Evaluate all rules against current OBD snapshot and apply triggered actions
 * Called from theme_update_data() after data bindings update
 *
 * @param obd Current OBD data snapshot
 */
void theme_rules_evaluate(const obd_snapshot_t *obd);

/**
 * Clear all rules (called on page unload/theme switch)
 */
void theme_rules_reset(void);

/**
 * Get current rule count (for diagnostics)
 */
uint8_t theme_rules_count(void);

/**
 * Register an element for rule actions (called during element creation)
 *
 * @param id Element ID from layout JSON
 * @param obj LVGL object
 * @param asset_name Current asset name (for image elements, NULL for others)
 */
void theme_rules_register_element(const char *id, lv_obj_t *obj, const char *asset_name);

#ifdef __cplusplus
}
#endif

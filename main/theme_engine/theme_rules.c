#include "theme_rules.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

#define TAG "theme_rules"

// Rule storage
static theme_rule_t s_rules[THEME_MAX_RULES];
static uint8_t s_rule_count = 0;

// Element lookup cache (populated during page creation)
typedef struct {
    char id[32];
    lv_obj_t *obj;
    char original_asset[32];  // For image elements
    uint32_t original_color;  // For color properties
    bool original_visible;
} element_state_t;

#define MAX_ELEMENTS 32
static element_state_t s_elements[MAX_ELEMENTS];
static uint8_t s_element_count = 0;

// Forward declarations
static bool eval_condition(const rule_condition_t *cond, const obd_snapshot_t *obd);
static bool resolve_data_source(const obd_snapshot_t *obd, const char *src, int32_t *out_value);
static void apply_action(const rule_action_t *action);
static element_state_t* find_element(const char *id);

// External function from theme_loader.c
extern const lv_img_dsc_t* theme_get_asset(const char *asset_name);


// ============================================================
//  Public API Implementation
// ============================================================

esp_err_t theme_rules_parse(cJSON *layout_json) {
    if (!layout_json) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *rules_array = cJSON_GetObjectItem(layout_json, "rules");
    if (!rules_array || !cJSON_IsArray(rules_array)) {
        // No rules defined - not an error, theme works without rules
        return ESP_OK;
    }

    s_rule_count = 0;
    cJSON *rule_obj = NULL;
    cJSON_ArrayForEach(rule_obj, rules_array) {
        if (s_rule_count >= THEME_MAX_RULES) {
            ESP_LOGW(TAG, "Rule table full, skipping remaining rules");
            break;
        }

        theme_rule_t *rule = &s_rules[s_rule_count];
        memset(rule, 0, sizeof(theme_rule_t));

        // Parse rule ID and priority
        cJSON *id = cJSON_GetObjectItem(rule_obj, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(rule->id, id->valuestring, sizeof(rule->id) - 1);
        } else {
            snprintf(rule->id, sizeof(rule->id), "rule_%d", s_rule_count);
        }

        cJSON *priority = cJSON_GetObjectItem(rule_obj, "priority");
        rule->priority = (priority && cJSON_IsNumber(priority)) ? priority->valueint : 0;

        // Parse "when" conditions
        cJSON *when = cJSON_GetObjectItem(rule_obj, "when");
        if (!when) {
            ESP_LOGW(TAG, "Rule '%s' missing 'when', skipping", rule->id);
            continue;
        }

        // Check if single condition or combined
        cJSON *combine = cJSON_GetObjectItem(when, "combine");
        if (combine && cJSON_IsString(combine)) {
            const char *mode = combine->valuestring;
            if (strcmp(mode, "all") == 0) {
                rule->combine_mode = RULE_COMBINE_ALL;
            } else if (strcmp(mode, "any") == 0) {
                rule->combine_mode = RULE_COMBINE_ANY;
            } else {
                ESP_LOGW(TAG, "Unknown combine mode '%s', defaulting to 'all'", mode);
                rule->combine_mode = RULE_COMBINE_ALL;
            }

            // Parse conditions array
            cJSON *conditions = cJSON_GetObjectItem(when, "conditions");
            if (conditions && cJSON_IsArray(conditions)) {
                cJSON *cond_obj = NULL;
                cJSON_ArrayForEach(cond_obj, conditions) {
                    if (rule->condition_count >= RULE_MAX_CONDITIONS) break;

                    rule_condition_t *cond = &rule->conditions[rule->condition_count];
                    cJSON *source = cJSON_GetObjectItem(cond_obj, "source");
                    cJSON *op = cJSON_GetObjectItem(cond_obj, "op");
                    cJSON *value = cJSON_GetObjectItem(cond_obj, "value");

                    if (!source || !op || !value) continue;

                    strncpy(cond->data_source, source->valuestring, sizeof(cond->data_source) - 1);

                    const char *op_str = op->valuestring;
                    if (strcmp(op_str, ">") == 0) cond->op = RULE_OP_GT;
                    else if (strcmp(op_str, ">=") == 0) cond->op = RULE_OP_GTE;
                    else if (strcmp(op_str, "<") == 0) cond->op = RULE_OP_LT;
                    else if (strcmp(op_str, "<=") == 0) cond->op = RULE_OP_LTE;
                    else if (strcmp(op_str, "==") == 0) cond->op = RULE_OP_EQ;
                    else if (strcmp(op_str, "!=") == 0) cond->op = RULE_OP_NE;
                    else continue;

                    cond->value = value->valueint;
                    rule->condition_count++;
                }
            }
        } else {
            // Single condition (legacy format)
            rule->combine_mode = RULE_COMBINE_ALL;
            rule_condition_t *cond = &rule->conditions[0];

            cJSON *source = cJSON_GetObjectItem(when, "source");
            cJSON *op = cJSON_GetObjectItem(when, "op");
            cJSON *value = cJSON_GetObjectItem(when, "value");

            if (!source || !op || !value) {
                ESP_LOGW(TAG, "Rule '%s' incomplete condition, skipping", rule->id);
                continue;
            }

            strncpy(cond->data_source, source->valuestring, sizeof(cond->data_source) - 1);

            const char *op_str = op->valuestring;
            if (strcmp(op_str, ">") == 0) cond->op = RULE_OP_GT;
            else if (strcmp(op_str, ">=") == 0) cond->op = RULE_OP_GTE;
            else if (strcmp(op_str, "<") == 0) cond->op = RULE_OP_LT;
            else if (strcmp(op_str, "<=") == 0) cond->op = RULE_OP_LTE;
            else if (strcmp(op_str, "==") == 0) cond->op = RULE_OP_EQ;
            else if (strcmp(op_str, "!=") == 0) cond->op = RULE_OP_NE;
            else {
                ESP_LOGW(TAG, "Unknown operator '%s'", op_str);
                continue;
            }

            cond->value = value->valueint;
            rule->condition_count = 1;
        }

        // Parse hysteresis (optional)
        cJSON *hysteresis = cJSON_GetObjectItem(when, "hysteresis");
        rule->hysteresis = (hysteresis && cJSON_IsNumber(hysteresis)) ? hysteresis->valueint : 0;

        // Parse "apply" actions
        cJSON *apply = cJSON_GetObjectItem(rule_obj, "apply");
        if (!apply || !cJSON_IsArray(apply)) {
            ESP_LOGW(TAG, "Rule '%s' missing 'apply', skipping", rule->id);
            continue;
        }

        cJSON *action_obj = NULL;
        cJSON_ArrayForEach(action_obj, apply) {
            if (rule->action_count >= RULE_MAX_ACTIONS) break;

            rule_action_t *action = &rule->actions[rule->action_count];
            cJSON *target = cJSON_GetObjectItem(action_obj, "target");
            cJSON *set = cJSON_GetObjectItem(action_obj, "set");

            if (!target || !set) continue;

            strncpy(action->target_id, target->valuestring, sizeof(action->target_id) - 1);

            // Determine action type from 'set' object
            cJSON *asset = cJSON_GetObjectItem(set, "asset");
            cJSON *color = cJSON_GetObjectItem(set, "color");
            cJSON *visible = cJSON_GetObjectItem(set, "visible");
            cJSON *text = cJSON_GetObjectItem(set, "text");

            if (asset && cJSON_IsString(asset)) {
                action->type = RULE_ACTION_SET_IMAGE;
                strncpy(action->asset_name, asset->valuestring, sizeof(action->asset_name) - 1);
                rule->action_count++;
            } else if (color && cJSON_IsString(color)) {
                action->type = RULE_ACTION_SET_COLOR;
                action->color = strtoul(color->valuestring, NULL, 16);
                rule->action_count++;
            } else if (visible && cJSON_IsBool(visible)) {
                action->type = RULE_ACTION_SET_VISIBLE;
                action->visible = cJSON_IsTrue(visible);
                rule->action_count++;
            } else if (text && cJSON_IsString(text)) {
                action->type = RULE_ACTION_SET_TEXT;
                strncpy(action->text, text->valuestring, sizeof(action->text) - 1);
                rule->action_count++;
            }
        }

        if (rule->condition_count > 0 && rule->action_count > 0) {
            ESP_LOGI(TAG, "Registered rule '%s': %d conditions, %d actions, priority=%d",
                     rule->id, rule->condition_count, rule->action_count, rule->priority);
            s_rule_count++;
        }
    }

    return ESP_OK;
}

void theme_rules_evaluate(const obd_snapshot_t *obd) {
    if (!obd || s_rule_count == 0) {
        return;
    }

    for (int i = 0; i < s_rule_count; i++) {
        theme_rule_t *rule = &s_rules[i];

        // Evaluate all conditions
        bool should_trigger = (rule->combine_mode == RULE_COMBINE_ALL);
        int32_t current_value = 0;

        for (int j = 0; j < rule->condition_count; j++) {
            bool cond_result = eval_condition(&rule->conditions[j], obd);

            // Track value for hysteresis (use first condition's data source)
            if (j == 0) {
                resolve_data_source(obd, rule->conditions[j].data_source, &current_value);
            }

            if (rule->combine_mode == RULE_COMBINE_ALL) {
                should_trigger = should_trigger && cond_result;
                if (!should_trigger) break; // Short-circuit AND
            } else { // RULE_COMBINE_ANY
                should_trigger = should_trigger || cond_result;
                if (should_trigger) break; // Short-circuit OR
            }
        }

        // Apply hysteresis
        bool state_changed = false;
        if (rule->hysteresis > 0 && rule->condition_count > 0) {
            int32_t threshold = rule->conditions[0].value;

            if (should_trigger && !rule->active) {
                // Trigger threshold reached
                rule->active = true;
                rule->trigger_value = current_value;
                state_changed = true;
            } else if (!should_trigger && rule->active) {
                // Check if we've fallen below threshold - hysteresis
                int32_t release_threshold = threshold - rule->hysteresis;
                if (current_value < release_threshold) {
                    rule->active = false;
                    state_changed = true;
                }
            }
        } else {
            // No hysteresis - direct state transition
            if (should_trigger != rule->active) {
                rule->active = should_trigger;
                state_changed = true;
            }
        }

        // Apply or revert actions only on state change
        if (state_changed) {
            if (rule->active) {
                ESP_LOGD(TAG, "Rule '%s' triggered", rule->id);
                for (int j = 0; j < rule->action_count; j++) {
                    apply_action(&rule->actions[j]);
                }
            } else {
                ESP_LOGD(TAG, "Rule '%s' released, restoring defaults", rule->id);
                // Restore original state for all affected elements
                for (int j = 0; j < rule->action_count; j++) {
                    element_state_t *elem = find_element(rule->actions[j].target_id);
                    if (!elem || !elem->obj) continue;

                    // Restore based on action type
                    switch (rule->actions[j].type) {
                        case RULE_ACTION_SET_IMAGE: {
                            const lv_img_dsc_t *img = theme_get_asset(elem->original_asset);
                            if (img) {
                                lv_img_set_src(elem->obj, img);
                            }
                            break;
                        }
                        case RULE_ACTION_SET_COLOR: {
                            lv_obj_set_style_text_color(elem->obj, lv_color_hex(elem->original_color), 0);
                            break;
                        }
                        case RULE_ACTION_SET_VISIBLE: {
                            if (elem->original_visible) {
                                lv_obj_clear_flag(elem->obj, LV_OBJ_FLAG_HIDDEN);
                            } else {
                                lv_obj_add_flag(elem->obj, LV_OBJ_FLAG_HIDDEN);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }
}

void theme_rules_reset(void) {
    s_rule_count = 0;
    s_element_count = 0;
    memset(s_rules, 0, sizeof(s_rules));
    memset(s_elements, 0, sizeof(s_elements));
}

uint8_t theme_rules_count(void) {
    return s_rule_count;
}

// ============================================================
//  Rule Registration (called from theme_loader during element creation)
// ============================================================

void theme_rules_register_element(const char *id, lv_obj_t *obj, const char *asset_name) {
    if (s_element_count >= MAX_ELEMENTS) {
        ESP_LOGW(TAG, "Element cache full, cannot register '%s'", id);
        return;
    }

    element_state_t *elem = &s_elements[s_element_count++];
    strncpy(elem->id, id, sizeof(elem->id) - 1);
    elem->obj = obj;

    if (asset_name) {
        strncpy(elem->original_asset, asset_name, sizeof(elem->original_asset) - 1);
    }

    // Capture original color (text color for labels, arc/bar indicator color)
    lv_color_t color = lv_obj_get_style_text_color(obj, 0);
    elem->original_color = lv_color_to32(color) & 0xFFFFFF;

    // Capture original visibility
    elem->original_visible = !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
//  Internal Helpers
// ============================================================

static bool eval_condition(const rule_condition_t *cond, const obd_snapshot_t *obd) {
    int32_t value = 0;
    if (!resolve_data_source(obd, cond->data_source, &value)) {
        return false;
    }

    switch (cond->op) {
        case RULE_OP_GT:  return value > cond->value;
        case RULE_OP_GTE: return value >= cond->value;
        case RULE_OP_LT:  return value < cond->value;
        case RULE_OP_LTE: return value <= cond->value;
        case RULE_OP_EQ:  return value == cond->value;
        case RULE_OP_NE:  return value != cond->value;
        default:          return false;
    }
}

static bool resolve_data_source(const obd_snapshot_t *obd, const char *src, int32_t *out_value) {
    if (strcmp(src, "obd.rpm") == 0) {
        *out_value = obd->rpm;
    } else if (strcmp(src, "obd.speed") == 0) {
        *out_value = obd->speed;
    } else if (strcmp(src, "obd.boost") == 0) {
        *out_value = obd->boost;
    } else if (strcmp(src, "obd.coolant_temp") == 0) {
        *out_value = obd->coolant_temp;
    } else if (strcmp(src, "obd.oil_pressure") == 0) {
        *out_value = obd->oil_pressure;
    } else if (strcmp(src, "obd.gear") == 0) {
        *out_value = obd->gear;
    } else if (strcmp(src, "obd.battery_voltage") == 0) {
        *out_value = obd->battery_voltage;
    } else if (strcmp(src, "obd.oil_temp") == 0) {
        *out_value = obd->oil_temp;
    } else if (strcmp(src, "obd.afr") == 0) {
        *out_value = obd->afr;
    } else if (strcmp(src, "obd.throttle") == 0) {
        *out_value = obd->throttle;
    } else if (strcmp(src, "obd.intake_temp") == 0) {
        *out_value = obd->intake_temp;
    } else {
        return false;
    }
    return true;
}

static void apply_action(const rule_action_t *action) {
    element_state_t *elem = find_element(action->target_id);
    if (!elem || !elem->obj) {
        ESP_LOGW(TAG, "Target element '%s' not found", action->target_id);
        return;
    }

    switch (action->type) {
        case RULE_ACTION_SET_IMAGE: {
            const lv_img_dsc_t *img = theme_get_asset(action->asset_name);
            if (img) {
                lv_img_set_src(elem->obj, img);
                ESP_LOGD(TAG, "Changed image '%s' to asset '%s'", elem->id, action->asset_name);
            } else {
                ESP_LOGW(TAG, "Asset '%s' not found", action->asset_name);
            }
            break;
        }
        case RULE_ACTION_SET_COLOR: {
            lv_obj_set_style_text_color(elem->obj, lv_color_hex(action->color), 0);
            ESP_LOGD(TAG, "Changed color '%s' to 0x%06X", elem->id, action->color);
            break;
        }
        case RULE_ACTION_SET_VISIBLE: {
            if (action->visible) {
                lv_obj_clear_flag(elem->obj, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(elem->obj, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGD(TAG, "Set visibility '%s' = %d", elem->id, action->visible);
            break;
        }
        case RULE_ACTION_SET_TEXT: {
            lv_label_set_text(elem->obj, action->text);
            ESP_LOGD(TAG, "Set text '%s' = '%s'", elem->id, action->text);
            break;
        }
    }
}

static element_state_t* find_element(const char *id) {
    for (int i = 0; i < s_element_count; i++) {
        if (strcmp(s_elements[i].id, id) == 0) {
            return &s_elements[i];
        }
    }
    return NULL;
}

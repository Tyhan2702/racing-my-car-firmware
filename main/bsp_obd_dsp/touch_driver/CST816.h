/**
 * @file
 * @brief ESP LCD touch: CST816S (I2C)
 */

#pragma once
#include "sdkconfig.h"   // CONFIG_* must be defined before the pin branch below (not force-included)
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_touch.h"

/**
 * @brief Create a new CST816S touch driver
 *
 * @note  The I2C bus must be initialized before use this function.
 *
 * @param i2c_bus I2C master bus handle
 * @param config Touch panel configuration
 * @param tp Touch panel handle
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_new_i2c_cst816(i2c_master_bus_handle_t i2c_bus, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

/**
 * @brief I2C address of the CST816S controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS    (0x15)

// I2C settings
#if CONFIG_OBD_HW_VERSION_V4_WS21
// 2.1" board: CST820 shares the main I2C bus (SCL=7 SDA=15); Touch_Init reuses I2C_GetBusHandle (no new bus).
// INT on IO16; RST is TCA9554 P1, pulsed in reset() via the shared expander (so GPIO rst stays NC).
#define I2C_Touch_SDA_IO            15
#define I2C_Touch_SCL_IO            7
#define I2C_Touch_INT_IO            16
#define I2C_Touch_RST_IO            -1
#else
#define I2C_Touch_SDA_IO            1               /*!< GPIO number used for I2C master data  */
#define I2C_Touch_SCL_IO            3               /*!< GPIO number used for I2C master clock */
#define I2C_Touch_INT_IO            4               /*!< GPIO number used for I2C master data  */
#define I2C_Touch_RST_IO            -1              /*!< GPIO number used for I2C master clock */
#endif
#define I2C_Touch_MASTER_FREQ_HZ    400000          /*!< I2C master clock frequency */

extern esp_lcd_touch_handle_t tp;

void Touch_Init(void);

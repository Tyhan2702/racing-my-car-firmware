// Waveshare ESP32-S3-Touch-LCD-2.1 (V4): ST7701S, 480x480, RGB565 16-bit parallel + 3-wire SPI init.
// LCD reset + 3-wire-SPI CS are on a TCA9554 I2C IO expander (P0 = LCD_RST, P2 = LCD_CS, P1 = TP_RST).
// Same public API as ST77916.h (panel_handle, LCD_Init, Backlight) so app_main / theme_engine stay the same.
#pragma once
#include "sdkconfig.h"
#if CONFIG_OBD_HW_VERSION_V4_WS21

#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_io_expander.h"
#include "lvgl.h"

#include "bsp_obd_dsp/touch_driver/CST816.h"

#define EXAMPLE_LCD_WIDTH                   (480)
#define EXAMPLE_LCD_HEIGHT                  (480)
#define EXAMPLE_LCD_COLOR_BITS              (16)

// Backlight (BL_PWM = GPIO6 on the 2.1 board)
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (6)
#ifndef EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL       (1)
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#endif

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_ResolutionRatio   LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty          ((1 << LEDC_ResolutionRatio) - 1)
#define Backlight_MAX   100

extern esp_lcd_panel_handle_t panel_handle;
extern uint8_t LCD_Backlight;

// Set the LVGL flush-complete callback (kept for API parity; RGB flushes synchronously, see app_main)
void LCD_SetFlushCallback(bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *), void *ctx);
void LCD_Init(void);
void Backlight_Init(void);
void Set_Backlight(uint8_t Light);
// The TCA9554 handle the LCD created (shared with touch for TP_RST on P1)
esp_io_expander_handle_t LCD_GetIoExpander(void);

#endif // CONFIG_OBD_HW_VERSION_V4_WS21

// Waveshare ESP32-S3-Touch-LCD-2.1 (V4) display bring-up — see ST7701.h.
#include "sdkconfig.h"
#include "bsp_obd_dsp/lcd_driver/ST7701.h"
#if CONFIG_OBD_HW_VERSION_V4_WS21

#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_st7701.h"
#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"

static const char *TAG_LCD = "ST7701";

esp_lcd_panel_handle_t panel_handle = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;
esp_io_expander_handle_t LCD_GetIoExpander(void) { return s_io_expander; }

/* LVGL flush-complete callback (RGB flushes synchronously; kept for API parity with ST77916) */
static bool (*s_flush_cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) = NULL;
static void *s_flush_ctx = NULL;
void LCD_SetFlushCallback(bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *), void *ctx)
{
    s_flush_cb = cb;
    s_flush_ctx = ctx;
}

// TCA9554 expander pins (schematic): P0 = LCD_RST, P1 = TP_RST, P2 = LCD_CS
#define EXP_LCD_RST   IO_EXPANDER_PIN_NUM_0
#define EXP_TP_RST    IO_EXPANDER_PIN_NUM_1
#define EXP_LCD_CS    IO_EXPANDER_PIN_NUM_2

// ST7701 3-wire SPI init lines (direct GPIO)
#define LCD_SPI_SCK   2
#define LCD_SPI_SDA   1

// ST7701S init for the Waveshare 2.1 480x480 panel (from the board's vendor init table).
static const st7701_lcd_init_cmd_t s_st7701_init[] = {
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t []){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t []){0x07, 0x02}, 2, 0},
    {0xCC, (uint8_t []){0x10}, 1, 0},
    {0xCD, (uint8_t []){0x08}, 1, 0},
    {0xB0, (uint8_t []){0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t []){0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 16, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x6D}, 1, 0},
    {0xB1, (uint8_t []){0x37}, 1, 0},
    {0xB2, (uint8_t []){0x81}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x43}, 1, 0},
    {0xB7, (uint8_t []){0x85}, 1, 0},
    {0xB8, (uint8_t []){0x20}, 1, 0},
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t []){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t []){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t []){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t []){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, (uint8_t []){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t []){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t []){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x66}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x20, (uint8_t []){0x00}, 0, 0},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

void LCD_Init(void)
{
    // The I2C master bus is already up (app_main calls I2C_Init() before LCD_Init()).
    i2c_master_bus_handle_t bus = I2C_GetBusHandle();

    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io_expander));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(s_io_expander, EXP_LCD_RST | EXP_TP_RST | EXP_LCD_CS, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXP_LCD_CS, 1));   // CS idle high
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXP_TP_RST, 1));   // touch out of reset

    // LCD hardware reset via the expander
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXP_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXP_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(120));

    // 3-wire SPI init line: SCK/SDA on GPIO, CS on the expander
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_EXPANDER, .cs_gpio_num = EXP_LCD_CS,
        .scl_io_type = IO_TYPE_GPIO,    .scl_gpio_num = LCD_SPI_SCK,
        .sda_io_type = IO_TYPE_GPIO,    .sda_gpio_num = LCD_SPI_SDA,
        .io_expander = s_io_expander,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &io_handle));

    // RGB panel: 16-bit RGB565, pins from the 2.1 schematic
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .psram_trans_align = 64,
        .data_width = 16,
        .bits_per_pixel = 16,
        .de_gpio_num = 40,
        .pclk_gpio_num = 41,
        .vsync_gpio_num = 39,
        .hsync_gpio_num = 38,
        .disp_gpio_num = -1,
        .num_fbs = 1,
        .bounce_buffer_size_px = 480 * 10,
        .data_gpio_nums = {
            5, 45, 48, 47, 21,          // B1..B5
            14, 13, 12, 11, 10, 9,      // G0..G5
            46, 3, 8, 18, 17,           // R1..R5
        },
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = 480,
            .v_res = 480,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 3,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = 1,
    };
    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = s_st7701_init,
        .init_cmds_size = sizeof(s_st7701_init) / sizeof(s_st7701_init[0]),
        .flags = {
            .mirror_by_cmd = 0,
            .enable_io_multiplex = 0,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,                              // RST toggled via the expander above
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    esp_lcd_panel_disp_on_off(panel_handle, true);
    ESP_LOGI(TAG_LCD, "ST7701 RGB 480x480 panel up");

    Backlight_Init();

    // Phase B: touch (CST820). Shares this expander's P1 for TP_RST and the main I2C bus.
    Touch_Init();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// Backlight (LEDC PWM on GPIO6)
uint8_t LCD_Backlight = 70;
static ledc_channel_config_t ledc_channel;
void Backlight_Init(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_LCD_PIN_NUM_BK_LIGHT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LS_MODE,
        .timer_num = LEDC_HS_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel.channel    = LEDC_HS_CH0_CHANNEL;
    ledc_channel.duty       = 0;
    ledc_channel.gpio_num   = EXAMPLE_LCD_PIN_NUM_BK_LIGHT;
    ledc_channel.speed_mode = LEDC_LS_MODE;
    ledc_channel.timer_sel  = LEDC_HS_TIMER;
    ledc_channel_config(&ledc_channel);
    ledc_fade_func_install(0);

    Set_Backlight(LCD_Backlight);
}
void Set_Backlight(uint8_t Light)
{
    if (Light > Backlight_MAX) Light = Backlight_MAX;
    uint16_t Duty = LEDC_MAX_Duty - (81 * (Backlight_MAX - Light));
    if (Light == 0) Duty = 0;
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, Duty);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

#endif // CONFIG_OBD_HW_VERSION_V4_WS21

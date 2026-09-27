#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_log.h"

/* Hardware configuration */

#define LCD_HOST SPI2_HOST

#define PIN_SCK      GPIO_NUM_12
#define PIN_MOSI     GPIO_NUM_11
#define PIN_MISO     GPIO_NUM_13
#define PIN_CS       GPIO_NUM_10
#define PIN_RS       GPIO_NUM_9
#define PIN_RST      GPIO_NUM_14
#define PIN_BK_LIGHT GPIO_NUM_2

#define PIN_TOUCH_SDA GPIO_NUM_4
#define PIN_TOUCH_SCL GPIO_NUM_5
#define PIN_TOUCH_RST GPIO_NUM_6
#define PIN_TOUCH_INT GPIO_NUM_7

/* LCD configuration */

#define LCD_H_RES (480U)
#define LCD_V_RES (320U)

#define LCD_CLK_HZ (20U * 1000U * 1000U)

#define CHUNK_PIXELS (240U)
#define CHUNK_BYTES  (CHUNK_PIXELS * 2U)

#define TOUCH_SQUARE_SIZE (20U)

/* ST7796 commands */

#define CMD_SWRESET (0x01U)
#define CMD_SLPOUT  (0x11U)
#define CMD_INVOFF  (0x20U)
#define CMD_INVON   (0x21U)
#define CMD_DISPON  (0x29U)
#define CMD_CASET   (0x2AU)
#define CMD_RASET   (0x2BU)
#define CMD_RAMWR   (0x2CU)
#define CMD_MADCTL  (0x36U)
#define CMD_COLMOD  (0x3AU)

/* MADCTL bits */

#define MADCTL_MY  (0x80U)
#define MADCTL_MX  (0x40U)
#define MADCTL_MV  (0x20U)
#define MADCTL_BGR (0x08U)

#define LCD_MADCTL_VALUE (MADCTL_MV | MADCTL_BGR)

/* LCD values */

#define LCD_COLMOD_RGB565 (0x55U)

#define COLOUR_RED   (0xF800U)
#define COLOUR_GREEN (0x07E0U)
#define COLOUR_BLUE  (0x001FU)
#define COLOUR_WHITE (0xFFFFU)
#define COLOUR_BLACK (0x0000U)

/* LCD timing */

#define LCD_RESET_LOW_MS     (100U)
#define LCD_RESET_HIGH_MS    (120U)
#define LCD_SWRESET_DELAY_MS (120U)
#define LCD_SLPOUT_DELAY_MS  (120U)

/* FT6336U configuration */

#define TOUCH_I2C_ADDR (0x38U)
#define I2C_CLK_HZ     (400000U)
#define I2C_TIMEOUT_MS (100U)
#define POLL_PERIOD_MS (50U)

#define TOUCH_RESET_LOW_MS  (20U)
#define TOUCH_RESET_HIGH_MS (100U)

/* FT6336U registers */

#define REG_TD_STATUS (0x02U)
#define REG_P1_XH     (0x03U)
#define REG_P1_XL     (0x04U)
#define REG_P1_YH     (0x05U)
#define REG_P1_YL     (0x06U)
#define REG_CHIP_ID   (0xA3U)
#define REG_VENDOR_ID (0xA8U)

#define TOUCH_COORD_MASK (0x0FU)
#define TOUCH_POINT_MASK (0x0FU)

#define TOUCH_ID_INVALID    (0x00U)
#define TOUCH_ID_INVALID_FF (0xFFU)

/* Touch raw coordinate limits */

#define TOUCH_RAW_X_MAX (LCD_V_RES - 1U)
#define TOUCH_RAW_Y_MAX (LCD_H_RES - 1U)

static const char* TAG_LCD = "LCD";
static const char* TAG_TOUCH = "TOUCH";

static spi_device_handle_t lcd_spi;
static i2c_master_bus_handle_t touch_i2c_bus;
static i2c_master_dev_handle_t touch_i2c_dev;

/* LCD GPIO initialization */

static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_RS) | (1ULL << PIN_RST) | (1ULL << PIN_BK_LIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RS, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BK_LIGHT, 0));
}

/* LCD SPI initialization */

static void lcd_spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = LCD_CLK_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 7,
    };

    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &dev_cfg, &lcd_spi));
}

/* Send one LCD command */

static void lcd_write_cmd(uint8_t cmd)
{
    spi_transaction_t trans = {
        .length = 8U,
        .tx_buffer = &cmd,
    };

    ESP_ERROR_CHECK(gpio_set_level(PIN_RS, 0));
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd_spi, &trans));
}

/* Send LCD data */

static void lcd_write_data(const uint8_t* data, size_t len)
{
    if (len == 0U)
    {
        return;
    }

    spi_transaction_t trans = {
        .length = len * 8U,
        .tx_buffer = data,
    };

    ESP_ERROR_CHECK(gpio_set_level(PIN_RS, 1));
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd_spi, &trans));
}

/* Reset LCD */

static void lcd_hardware_reset(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_LOW_MS));

    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_HIGH_MS));
}

/* Initialize ST7796 */

static void lcd_init(void)
{
    lcd_hardware_reset();

    lcd_write_cmd(CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(LCD_SWRESET_DELAY_MS));

    lcd_write_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(LCD_SLPOUT_DELAY_MS));

    lcd_write_cmd(CMD_MADCTL);

    uint8_t madctl = LCD_MADCTL_VALUE;
    lcd_write_data(&madctl, 1U);

    lcd_write_cmd(CMD_COLMOD);

    uint8_t colmod = LCD_COLMOD_RGB565;
    lcd_write_data(&colmod, 1U);

    lcd_write_cmd(CMD_INVON);
    lcd_write_cmd(CMD_DISPON);

    vTaskDelay(pdMS_TO_TICKS(20U));

    ESP_LOGI(TAG_LCD, "ST7796 initialization complete");
}

/* Set LCD drawing window */

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    lcd_write_cmd(CMD_CASET);

    data[0] = (uint8_t) (x0 >> 8);
    data[1] = (uint8_t) (x0 & 0xFFU);
    data[2] = (uint8_t) (x1 >> 8);
    data[3] = (uint8_t) (x1 & 0xFFU);

    lcd_write_data(data, sizeof(data));

    lcd_write_cmd(CMD_RASET);

    data[0] = (uint8_t) (y0 >> 8);
    data[1] = (uint8_t) (y0 & 0xFFU);
    data[2] = (uint8_t) (y1 >> 8);
    data[3] = (uint8_t) (y1 & 0xFFU);

    lcd_write_data(data, sizeof(data));

    lcd_write_cmd(CMD_RAMWR);
}

/* Fill LCD rectangle */

static esp_err_t lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
    if (w == 0U || h == 0U)
    {
        return ESP_OK;
    }

    if (((uint32_t) x + w) > LCD_H_RES)
    {
        ESP_LOGE(TAG_LCD, "Invalid X boundary");
        return ESP_ERR_INVALID_ARG;
    }

    if (((uint32_t) y + h) > LCD_V_RES)
    {
        ESP_LOGE(TAG_LCD, "Invalid Y boundary");
        return ESP_ERR_INVALID_ARG;
    }

    lcd_set_window(x, y, (uint16_t) (x + w - 1U), (uint16_t) (y + h - 1U));

    uint8_t pixel_buffer[CHUNK_BYTES];

    uint8_t high_byte = (uint8_t) (colour >> 8);
    uint8_t low_byte = (uint8_t) (colour & 0xFFU);

    for (uint32_t i = 0U; i < CHUNK_PIXELS; i++)
    {
        pixel_buffer[i * 2U] = high_byte;
        pixel_buffer[i * 2U + 1U] = low_byte;
    }

    uint32_t pixels_remaining = (uint32_t) w * (uint32_t) h;

    while (pixels_remaining > 0U)
    {
        uint32_t pixels_this_transfer;

        if (pixels_remaining > CHUNK_PIXELS)
        {
            pixels_this_transfer = CHUNK_PIXELS;
        }
        else
        {
            pixels_this_transfer = pixels_remaining;
        }

        lcd_write_data(pixel_buffer, pixels_this_transfer * 2U);

        pixels_remaining -= pixels_this_transfer;
    }

    return ESP_OK;
}

/* Initialize touch GPIO */

static void touch_gpio_init(void)
{
    gpio_config_t output_cfg = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&output_cfg));

    gpio_config_t input_cfg = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&input_cfg));

    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 1));
}

/* Reset touch controller */

static void touch_reset(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_LOW_MS));

    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_HIGH_MS));
}

/* Initialize touch I2C */

static void touch_i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &touch_i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = I2C_CLK_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(touch_i2c_bus, &dev_cfg, &touch_i2c_dev));
}

/* Read FT6336U register */

static esp_err_t touch_read(uint8_t reg, uint8_t* buf, size_t len)
{
    return i2c_master_transmit_receive(touch_i2c_dev, &reg, 1U, buf, len, I2C_TIMEOUT_MS);
}

/* Read controller IDs */

static esp_err_t touch_read_id(void)
{
    uint8_t chip_id = 0U;
    uint8_t vendor_id = 0U;

    esp_err_t ret = touch_read(REG_CHIP_ID, &chip_id, 1U);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG_TOUCH, "CHIP_ID read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = touch_read(REG_VENDOR_ID, &vendor_id, 1U);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG_TOUCH, "VENDOR_ID read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG_TOUCH, "chip_id=0x%02X vendor_id=0x%02X", chip_id, vendor_id);

    if (chip_id == TOUCH_ID_INVALID || chip_id == TOUCH_ID_INVALID_FF || vendor_id == TOUCH_ID_INVALID ||
        vendor_id == TOUCH_ID_INVALID_FF)
    {
        ESP_LOGW(TAG_TOUCH, "Unexpected controller ID");
    }

    return ESP_OK;
}

/* Read first touch point */

static esp_err_t touch_get_point(uint16_t* raw_x, uint16_t* raw_y, bool* touched)
{
    *raw_x = 0U;
    *raw_y = 0U;
    *touched = false;

    uint8_t status = 0U;

    esp_err_t ret = touch_read(REG_TD_STATUS, &status, 1U);

    if (ret != ESP_OK)
    {
        return ret;
    }

    uint8_t touch_count = status & TOUCH_POINT_MASK;

    if (touch_count == 0U)
    {
        return ESP_OK;
    }

    uint8_t coord[4] = {0U};

    ret = touch_read(REG_P1_XH, coord, sizeof(coord));

    if (ret != ESP_OK)
    {
        return ret;
    }

    *raw_x = ((uint16_t) (coord[0] & TOUCH_COORD_MASK) << 8U) | coord[1];

    *raw_y = ((uint16_t) (coord[2] & TOUCH_COORD_MASK) << 8U) | coord[3];

    *touched = true;

    return ESP_OK;
}

/* Map raw touch to LCD coordinates */

static void touch_map_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t* lcd_x, uint16_t* lcd_y)
{
    if (lcd_x == NULL || lcd_y == NULL)
    {
        return;
    }

    /*
     * Current mapping from hardware observation.
     * Portrait raw Y maps to landscape LCD X.
     * Portrait raw X is inverted and maps to LCD Y.
     * Observed raw range must be confirmed on this panel.
     */

    uint32_t mapped_x = raw_y;
    uint32_t mapped_y = TOUCH_RAW_X_MAX - raw_x;

    if (mapped_x >= LCD_H_RES)
    {
        mapped_x = LCD_H_RES - 1U;
    }

    if (mapped_y >= LCD_V_RES)
    {
        mapped_y = LCD_V_RES - 1U;
    }

    *lcd_x = (uint16_t) mapped_x;
    *lcd_y = (uint16_t) mapped_y;
}

/* Clamp square to LCD */

static void clamp_touch_square(uint16_t* x, uint16_t* y)
{
    uint16_t half = TOUCH_SQUARE_SIZE / 2U;

    if (*x < half)
    {
        *x = half;
    }

    if (*y < half)
    {
        *y = half;
    }

    if (*x > (LCD_H_RES - half))
    {
        *x = LCD_H_RES - half;
    }

    if (*y > (LCD_V_RES - half))
    {
        *y = LCD_V_RES - half;
    }
}

/* Main application */

void app_main(void)
{
    ESP_LOGI(TAG_LCD, "Starting Session 07");

    lcd_gpio_init();
    lcd_spi_init();
    lcd_init();

    ESP_ERROR_CHECK(gpio_set_level(PIN_BK_LIGHT, 1));

    ESP_ERROR_CHECK(lcd_fill_rect(0U, 0U, LCD_H_RES, LCD_V_RES, COLOUR_BLACK));

    touch_gpio_init();
    touch_reset();
    touch_i2c_init();

    esp_err_t ret = touch_read_id();

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG_TOUCH, "Touch ID read failed");
    }

    ESP_LOGI(TAG_TOUCH, "display ready, waiting for touch");

    while (1)
    {
        uint16_t raw_x = 0U;
        uint16_t raw_y = 0U;
        uint16_t lcd_x = 0U;
        uint16_t lcd_y = 0U;
        bool touched = false;

        ret = touch_get_point(&raw_x, &raw_y, &touched);

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG_TOUCH, "I2C read failed: %s", esp_err_to_name(ret));

            vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));

            continue;
        }

        if (touched)
        {
            ESP_LOGI(TAG_TOUCH, "raw @ x=%u y=%u", raw_x, raw_y);

            touch_map_coordinates(raw_x, raw_y, &lcd_x, &lcd_y);

            clamp_touch_square(&lcd_x, &lcd_y);

            ESP_LOGI(TAG_TOUCH, "touch @ x=%u y=%u", lcd_x, lcd_y);

            ESP_ERROR_CHECK(lcd_fill_rect(lcd_x - TOUCH_SQUARE_SIZE / 2U,
                                          lcd_y - TOUCH_SQUARE_SIZE / 2U,
                                          TOUCH_SQUARE_SIZE,
                                          TOUCH_SQUARE_SIZE,
                                          COLOUR_WHITE));
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

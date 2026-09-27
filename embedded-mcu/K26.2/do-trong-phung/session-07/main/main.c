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

/* LCD hardware */
#define LCD_HOST     SPI2_HOST
#define PIN_SCK      GPIO_NUM_12
#define PIN_MOSI     GPIO_NUM_11
#define PIN_MISO     GPIO_NUM_13
#define PIN_CS       GPIO_NUM_10
#define PIN_RS       GPIO_NUM_9
#define PIN_RST      GPIO_NUM_14
#define PIN_BK_LIGHT GPIO_NUM_2

/* Touch hardware */
#define TOUCH_I2C_PORT I2C_NUM_0
#define PIN_TOUCH_SDA  GPIO_NUM_4
#define PIN_TOUCH_SCL  GPIO_NUM_5
#define PIN_TOUCH_RST  GPIO_NUM_6
#define PIN_TOUCH_INT  GPIO_NUM_7
#define TOUCH_I2C_ADDR (0x38U)

#define I2C_CLK_HZ     (400000U)
#define I2C_TIMEOUT_MS (100U)
#define POLL_PERIOD_MS (50U)

/* LCD configuration */
#define LCD_H_RES    (480U)
#define LCD_V_RES    (320U)
#define LCD_CLK_HZ   (20 * 1000 * 1000)
#define CHUNK_PIXELS (240U)
#define CHUNK_BYTES  (CHUNK_PIXELS * 2U)

/* Touch configuration */
#define TOUCH_SQUARE_SIZE   (20U)
#define TOUCH_POINT_PRESENT (1U)

/* FT6336U registers */
#define REG_TD_STATUS (0x02U)
#define REG_P1_XH     (0x03U)
#define REG_P1_XL     (0x04U)
#define REG_P1_YH     (0x05U)
#define REG_P1_YL     (0x06U)
#define REG_CHIP_ID   (0xA3U)
#define REG_VENDOR_ID (0xA8U)

/* FT6336U masks */
#define TOUCH_POINT_COUNT_MASK (0x0FU)
#define TOUCH_COORD_MASK       (0x0FU)

/* Touch raw limits */
#define TOUCH_RAW_X_MAX (LCD_V_RES - 1U)

/* ST7796 commands */
#define CMD_SWRESET (0x01U)
#define CMD_SLPOUT  (0x11U)
#define CMD_INVON   (0x21U)
#define CMD_DISPON  (0x29U)
#define CMD_CASET   (0x2AU)
#define CMD_RASET   (0x2BU)
#define CMD_RAMWR   (0x2CU)
#define CMD_MADCTL  (0x36U)
#define CMD_COLMOD  (0x3AU)

/* MADCTL bits */
#define MADCTL_MV        (0x20U)
#define MADCTL_BGR       (0x08U)
#define LCD_MADCTL_VALUE (MADCTL_MV | MADCTL_BGR)

/* RGB565 colours */
#define COLOUR_RED   (0xF800U)
#define COLOUR_GREEN (0x07E0U)
#define COLOUR_BLUE  (0x001FU)
#define COLOUR_WHITE (0xFFFFU)
#define COLOUR_BLACK (0x0000U)

/* Timing */
#define LCD_RESET_LOW_MS     (100U)
#define LCD_RESET_HIGH_MS    (120U)
#define LCD_SWRESET_DELAY_MS (120U)
#define LCD_SLPOUT_DELAY_MS  (120U)
#define TOUCH_RESET_LOW_MS   (20U)
#define TOUCH_RESET_HIGH_MS  (100U)

static const char* TAG_LCD = "ST7796";
static const char* TAG_TOUCH = "TOUCH";

static spi_device_handle_t lcd_spi;
static i2c_master_bus_handle_t touch_i2c_bus;
static i2c_master_dev_handle_t touch_i2c_dev;

/* Configure LCD GPIO */
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

/* Initialize LCD SPI */
static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };

    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    ESP_ERROR_CHECK(ret);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_CLK_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 7,
    };

    ret = spi_bus_add_device(LCD_HOST, &devcfg, &lcd_spi);

    ESP_ERROR_CHECK(ret);
}

/* Send LCD command */
static void lcd_write_cmd(uint8_t cmd)
{
    spi_transaction_t trans = {
        .length = 8,
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

    uint8_t colmod = 0x55U;

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
static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
    uint32_t total_pixels;
    uint32_t pixels_remaining;

    uint8_t pixel_buffer[CHUNK_BYTES];

    uint8_t high_byte = (uint8_t) (colour >> 8);

    uint8_t low_byte = (uint8_t) (colour & 0xFFU);

    if (w == 0U || h == 0U)
    {
        return;
    }

    if (((uint32_t) x + w) > LCD_H_RES)
    {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }

    if (((uint32_t) y + h) > LCD_V_RES)
    {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }

    lcd_set_window(x, y, (uint16_t) (x + w - 1U), (uint16_t) (y + h - 1U));

    for (uint32_t i = 0; i < CHUNK_PIXELS; i++)
    {
        pixel_buffer[i * 2U] = high_byte;
        pixel_buffer[i * 2U + 1U] = low_byte;
    }

    total_pixels = (uint32_t) w * (uint32_t) h;

    pixels_remaining = total_pixels;

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
}

/* Configure touch GPIO */
static void touch_gpio_init(void)
{
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&rst_conf));

    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&int_conf));

    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 0));
}

/* Reset touch controller */
static void touch_reset(void)
{
    ESP_LOGI(TAG_TOUCH, "Resetting FT6336U");

    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 0));

    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_LOW_MS));

    ESP_ERROR_CHECK(gpio_set_level(PIN_TOUCH_RST, 1));

    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_HIGH_MS));
}

/* Initialize touch I2C */
static void touch_i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &touch_i2c_bus);

    ESP_ERROR_CHECK(ret);

    i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = I2C_CLK_HZ,
    };

    ret = i2c_master_bus_add_device(touch_i2c_bus, &touch_cfg, &touch_i2c_dev);

    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_TOUCH, "I2C initialized: SDA=%d SCL=%d addr=0x%02X", PIN_TOUCH_SDA, PIN_TOUCH_SCL, TOUCH_I2C_ADDR);
}

/* Read FT6336U register */
static esp_err_t touch_read(uint8_t reg, uint8_t* buf, size_t len)
{
    return i2c_master_transmit_receive(touch_i2c_dev, &reg, 1U, buf, len, I2C_TIMEOUT_MS);
}

/* Read touch controller IDs */
static void touch_read_id(void)
{
    uint8_t chip_id = 0U;
    uint8_t vendor_id = 0U;

    esp_err_t ret_chip = touch_read(REG_CHIP_ID, &chip_id, 1U);

    esp_err_t ret_vendor = touch_read(REG_VENDOR_ID, &vendor_id, 1U);

    if (ret_chip != ESP_OK || ret_vendor != ESP_OK)
    {
        ESP_LOGW(
            TAG_TOUCH, "ID read failed: chip=%s vendor=%s", esp_err_to_name(ret_chip), esp_err_to_name(ret_vendor));

        return;
    }

    ESP_LOGI(TAG_TOUCH, "chip_id=0x%02X vendor_id=0x%02X", chip_id, vendor_id);

    if (chip_id == 0x00U || chip_id == 0xFFU || vendor_id == 0x00U || vendor_id == 0xFFU)
    {
        ESP_LOGW(TAG_TOUCH, "Unexpected ID value");
    }
}

/* Clamp X coordinate */
static uint16_t clamp_touch_x(uint16_t x)
{
    const uint16_t max_x = (uint16_t) (LCD_H_RES - TOUCH_SQUARE_SIZE);

    if (x > max_x)
    {
        return max_x;
    }

    return x;
}

/* Clamp Y coordinate */
static uint16_t clamp_touch_y(uint16_t y)
{
    const uint16_t max_y = (uint16_t) (LCD_V_RES - TOUCH_SQUARE_SIZE);

    if (y > max_y)
    {
        return max_y;
    }

    return y;
}

/* Map touch to LCD coordinates */
static void touch_map_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t* lcd_x, uint16_t* lcd_y)
{
    uint16_t mapped_x = raw_y;
    uint16_t mapped_y;

    if (raw_x > TOUCH_RAW_X_MAX)
    {
        mapped_y = 0U;
    }
    else
    {
        mapped_y = (uint16_t) (TOUCH_RAW_X_MAX - raw_x);
    }

    mapped_x = clamp_touch_x(mapped_x);

    mapped_y = clamp_touch_y(mapped_y);

    *lcd_x = mapped_x;
    *lcd_y = mapped_y;
}

/* Read first touch point */
static esp_err_t touch_get_point(uint16_t* raw_x, uint16_t* raw_y, bool* touched)
{
    uint8_t status = 0U;

    esp_err_t ret = touch_read(REG_TD_STATUS, &status, 1U);

    if (ret != ESP_OK)
    {
        return ret;
    }

    uint8_t touch_count = status & TOUCH_POINT_COUNT_MASK;

    if (touch_count < TOUCH_POINT_PRESENT)
    {
        *touched = false;
        return ESP_OK;
    }

    uint8_t coord[4] = {0U, 0U, 0U, 0U};

    ret = touch_read(REG_P1_XH, coord, sizeof(coord));

    if (ret != ESP_OK)
    {
        return ret;
    }

    uint16_t x = (uint16_t) (((uint16_t) (coord[0] & TOUCH_COORD_MASK) << 8U) | (uint16_t) coord[1]);

    uint16_t y = (uint16_t) (((uint16_t) (coord[2] & TOUCH_COORD_MASK) << 8U) | (uint16_t) coord[3]);

    *raw_x = x;
    *raw_y = y;
    *touched = true;

    return ESP_OK;
}

/* Main application */
void app_main(void)
{
    ESP_LOGI(TAG_LCD, "Starting ST7796 + FT6336U");

    /* Initialize LCD GPIO */
    lcd_gpio_init();

    /* Initialize LCD SPI */
    lcd_spi_init();

    /* Initialize LCD */
    lcd_init();

    /* Turn on backlight */
    ESP_ERROR_CHECK(gpio_set_level(PIN_BK_LIGHT, 1));

    /* Clear display */
    lcd_fill_rect(0U, 0U, LCD_H_RES, LCD_V_RES, COLOUR_BLACK);

    /* Initialize touch GPIO */
    touch_gpio_init();

    /* Reset touch controller */
    touch_reset();

    /* Initialize I2C */
    touch_i2c_init();

    /* Read controller IDs */
    touch_read_id();

    ESP_LOGI(TAG_TOUCH, "display ready, waiting for touch");

    /* Poll touch at 20 Hz */
    while (1)
    {
        uint16_t raw_x = 0U;
        uint16_t raw_y = 0U;

        uint16_t lcd_x = 0U;
        uint16_t lcd_y = 0U;

        bool touched = false;

        esp_err_t ret = touch_get_point(&raw_x, &raw_y, &touched);

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG_TOUCH, "I2C touch read failed: %s", esp_err_to_name(ret));

            vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));

            continue;
        }

        if (touched)
        {
            touch_map_coordinates(raw_x, raw_y, &lcd_x, &lcd_y);

            ESP_LOGI(TAG_TOUCH, "touch raw=(%u,%u) -> lcd=(%u,%u)", raw_x, raw_y, lcd_x, lcd_y);

            lcd_fill_rect(lcd_x, lcd_y, TOUCH_SQUARE_SIZE, TOUCH_SQUARE_SIZE, COLOUR_WHITE);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

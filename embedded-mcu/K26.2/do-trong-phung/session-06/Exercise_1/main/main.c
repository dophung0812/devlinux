#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_log.h"


/* ============================================================
 * Hardware configuration
 * ============================================================ */

#define LCD_HOST                SPI2_HOST

#define PIN_SCK                 GPIO_NUM_12
#define PIN_MOSI                GPIO_NUM_11
#define PIN_MISO                GPIO_NUM_13
#define PIN_CS                  GPIO_NUM_10
#define PIN_RS                  GPIO_NUM_9
#define PIN_RST                 GPIO_NUM_14
#define PIN_BK_LIGHT            GPIO_NUM_2


/* ============================================================
 * LCD configuration
 * ============================================================ */

#define LCD_H_RES               (480U)
#define LCD_V_RES               (320U)

#define LCD_CLK_HZ              (20 * 1000 * 1000)

/*
 * 240 pixels x 2 bytes/pixel = 480 bytes.
 *
 * A reasonably large buffer reduces the number of SPI
 * transactions while keeping RAM usage small.
 */
#define CHUNK_PIXELS            (240U)
#define CHUNK_BYTES             (CHUNK_PIXELS * 2U)


/* ============================================================
 * ST7796 commands
 * ============================================================ */

#define CMD_SWRESET             (0x01U)
#define CMD_SLPOUT              (0x11U)
#define CMD_INVOFF              (0x20U)
#define CMD_INVON               (0x21U)
#define CMD_DISPON              (0x29U)

#define CMD_CASET               (0x2AU)
#define CMD_RASET               (0x2BU)
#define CMD_RAMWR              (0x2CU)

#define CMD_MADCTL              (0x36U)
#define CMD_COLMOD              (0x3AU)


/* ============================================================
 * MADCTL bits
 * ============================================================ */

#define MADCTL_MY               (0x80U)
#define MADCTL_MX               (0x40U)
#define MADCTL_MV               (0x20U)
#define MADCTL_BGR              (0x08U)


/*
 * Landscape orientation:
 *
 * MV = row/column exchange.
 *
 * BGR is set because the physical panel used by this module
 * expects BGR colour order.
 */
#define LCD_MADCTL_VALUE        (MADCTL_MV | MADCTL_BGR)


/* ============================================================
 * RGB565 colours
 * ============================================================ */

#define COLOUR_RED              (0xF800U)
#define COLOUR_GREEN            (0x07E0U)
#define COLOUR_BLUE             (0x001FU)
#define COLOUR_WHITE            (0xFFFFU)
#define COLOUR_BLACK            (0x0000U)


/* ============================================================
 * Timing
 * ============================================================ */

#define LCD_RESET_LOW_MS        (100U)
#define LCD_RESET_HIGH_MS       (120U)
#define LCD_SWRESET_DELAY_MS    (120U)
#define LCD_SLPOUT_DELAY_MS     (120U)

#define INITIAL_BARS_DELAY_MS   (3000U)
#define COLOUR_DELAY_MS         (1000U)


static const char *TAG = "ST7796";

static spi_device_handle_t lcd_spi;


/* ============================================================
 * GPIO helpers
 * ============================================================ */

static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << PIN_RS) |
            (1ULL << PIN_RST) |
            (1ULL << PIN_BK_LIGHT),

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


/* ============================================================
 * SPI initialization
 * ============================================================ */

static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,

        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        /*
         * Must be >= CHUNK_BYTES.
         */
        .max_transfer_sz = CHUNK_BYTES,
    };

    esp_err_t ret = spi_bus_initialize(
        LCD_HOST,
        &buscfg,
        SPI_DMA_CH_AUTO
    );

    ESP_ERROR_CHECK(ret);


    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_CLK_HZ,
        .mode = 0,

        /*
         * SPI driver automatically controls CS.
         */
        .spics_io_num = PIN_CS,

        .queue_size = 7,
    };

    ret = spi_bus_add_device(
        LCD_HOST,
        &devcfg,
        &lcd_spi
    );

    ESP_ERROR_CHECK(ret);
}


/* ============================================================
 * SPI transmit helpers
 * ============================================================ */

/*
 * Send exactly one command byte.
 *
 * RS = LOW -> command.
 */
static void lcd_write_cmd(uint8_t cmd)
{
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd,
    };

    ESP_ERROR_CHECK(gpio_set_level(PIN_RS, 0));

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(lcd_spi, &trans)
    );
}


/*
 * Send a data buffer.
 *
 * RS = HIGH -> data.
 *
 * All LCD data transfers go through this helper.
 */
static void lcd_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    spi_transaction_t trans = {
        .length = len * 8U,
        .tx_buffer = data,
    };

    ESP_ERROR_CHECK(gpio_set_level(PIN_RS, 1));

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(lcd_spi, &trans)
    );
}


/* ============================================================
 * Hardware reset
 * ============================================================ */

static void lcd_hardware_reset(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_LOW_MS));

    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_HIGH_MS));
}


/* ============================================================
 * ST7796 initialization
 * ============================================================ */

static void lcd_init(void)
{
    lcd_hardware_reset();

    /*
     * Software reset.
     */
    lcd_write_cmd(CMD_SWRESET);

    /*
     * ST7796 requires a delay after software reset.
     */
    vTaskDelay(pdMS_TO_TICKS(LCD_SWRESET_DELAY_MS));


    /*
     * Exit sleep mode.
     */
    lcd_write_cmd(CMD_SLPOUT);

    /*
     * Required delay after SLPOUT.
     */
    vTaskDelay(pdMS_TO_TICKS(LCD_SLPOUT_DELAY_MS));


    /*
     * Set landscape orientation.
     *
     * MV exchanges row and column addressing.
     *
     * BGR is enabled because the module's panel uses
     * BGR colour order.
     */
    lcd_write_cmd(CMD_MADCTL);

    uint8_t madctl = LCD_MADCTL_VALUE;
    lcd_write_data(&madctl, 1);


    /*
     * 16-bit RGB565 colour mode.
     *
     * 0x55 = 16 bits/pixel.
     */
    lcd_write_cmd(CMD_COLMOD);

    uint8_t colmod = 0x55;
    lcd_write_data(&colmod, 1);


    /*
     * This panel needs display inversion enabled.
     */
    lcd_write_cmd(CMD_INVON);


    /*
     * Turn display on.
     */
    lcd_write_cmd(CMD_DISPON);

    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "ST7796 initialization complete");
}


/* ============================================================
 * Set drawing window
 * ============================================================ */

static void lcd_set_window(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
)
{
    uint8_t data[4];


    /* --------------------------------------------------------
     * CASET - Column Address Set
     * -------------------------------------------------------- */

    lcd_write_cmd(CMD_CASET);

    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)(x0 & 0xFF);
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)(x1 & 0xFF);

    lcd_write_data(data, sizeof(data));


    /* --------------------------------------------------------
     * RASET - Row Address Set
     * -------------------------------------------------------- */

    lcd_write_cmd(CMD_RASET);

    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)(y0 & 0xFF);
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)(y1 & 0xFF);

    lcd_write_data(data, sizeof(data));


    /* --------------------------------------------------------
     * RAMWR - Memory Write
     * -------------------------------------------------------- */

    lcd_write_cmd(CMD_RAMWR);
}


/* ============================================================
 * Fill rectangle
 * ============================================================ */

static void lcd_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t colour
)
{
    uint32_t total_pixels;
    uint32_t pixels_remaining;

    uint8_t pixel_buffer[CHUNK_BYTES];

    uint8_t high_byte = (uint8_t)(colour >> 8);
    uint8_t low_byte = (uint8_t)(colour & 0xFF);


    /*
     * Check rectangle boundaries.
     */
    if (w == 0 || h == 0) {
        return;
    }

    if ((x + w) > LCD_H_RES) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }

    if ((y + h) > LCD_V_RES) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }


    /*
     * Set the LCD address window.
     */
    lcd_set_window(
        x,
        y,
        (uint16_t)(x + w - 1U),
        (uint16_t)(y + h - 1U)
    );


    /*
     * Prepare a buffer containing many RGB565 pixels.
     *
     * RGB565 is transmitted:
     *
     *     high byte first
     *     low byte second
     */
    for (uint32_t i = 0; i < CHUNK_PIXELS; i++) {
        pixel_buffer[i * 2U] = high_byte;
        pixel_buffer[i * 2U + 1U] = low_byte;
    }


    total_pixels = (uint32_t)w * (uint32_t)h;
    pixels_remaining = total_pixels;


    /*
     * Send many pixels per SPI transaction.
     *
     * We never send one transaction per pixel.
     */
    while (pixels_remaining > 0) {

        uint32_t pixels_this_transfer;

        if (pixels_remaining > CHUNK_PIXELS) {
            pixels_this_transfer = CHUNK_PIXELS;
        } else {
            pixels_this_transfer = pixels_remaining;
        }

        lcd_write_data(
            pixel_buffer,
            pixels_this_transfer * 2U
        );

        pixels_remaining -= pixels_this_transfer;
    }
}


/* ============================================================
 * Draw the initial three colour bars
 * ============================================================ */

static void lcd_draw_colour_bars(void)
{
    const uint16_t bar_height = LCD_V_RES / 3U;

    /*
     * Top: red
     */
    lcd_fill_rect(
        0,
        0,
        LCD_H_RES,
        bar_height,
        COLOUR_RED
    );


    /*
     * Middle: green
     */
    lcd_fill_rect(
        0,
        bar_height,
        LCD_H_RES,
        bar_height,
        COLOUR_GREEN
    );


    /*
     * Bottom: blue.
     *
     * Use LCD_V_RES - 2*bar_height so that any remainder
     * caused by integer division belongs to the last bar.
     */
    lcd_fill_rect(
        0,
        bar_height * 2U,
        LCD_H_RES,
        LCD_V_RES - (bar_height * 2U),
        COLOUR_BLUE
    );
}


/* ============================================================
 * Main application
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ST7796 SPI demo");


    /*
     * 1. Configure LCD GPIO pins.
     */
    lcd_gpio_init();


    /*
     * 2. Initialize SPI2 and attach the LCD device.
     */
    lcd_spi_init();


    /*
     * 3. Initialize ST7796.
     */
    lcd_init();


    /*
     * 4. Turn on backlight.
     */
    ESP_ERROR_CHECK(
        gpio_set_level(PIN_BK_LIGHT, 1)
    );


    /*
     * 5. Draw the photograph-friendly test pattern.
     */
    ESP_LOGI(TAG, "Drawing RGB colour bars");

    lcd_draw_colour_bars();


    /*
     * Hold bars for 3 seconds.
     */
    vTaskDelay(pdMS_TO_TICKS(INITIAL_BARS_DELAY_MS));


    /*
     * 6. Forever cycle:
     *
     * RED -> GREEN -> BLUE -> WHITE -> BLACK
     */
    const uint16_t colours[] = {
        COLOUR_RED,
        COLOUR_GREEN,
        COLOUR_BLUE,
        COLOUR_WHITE,
        COLOUR_BLACK,
    };

    const size_t colour_count =
        sizeof(colours) / sizeof(colours[0]);


    size_t index = 0;

    while (1) {

        ESP_LOGI(TAG, "Filling screen with colour 0x%04X",
                 colours[index]);

        lcd_fill_rect(
            0,
            0,
            LCD_H_RES,
            LCD_V_RES,
            colours[index]
        );

        vTaskDelay(
            pdMS_TO_TICKS(COLOUR_DELAY_MS)
        );

        index++;

        if (index >= colour_count) {
            index = 0;
        }
    }
}
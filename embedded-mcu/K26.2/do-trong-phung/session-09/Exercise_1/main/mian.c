#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define LCD_WIDTH        (480U)
#define LCD_HEIGHT       (320U)
#define LCD_SPI_HOST     (SPI2_HOST)
#define LCD_SPI_CLOCK_HZ (20000000U)
#define LCD_PIN_SCLK     (12)
#define LCD_PIN_MOSI     (11)
#define LCD_PIN_MISO     (13)
#define LCD_PIN_CS       (10)
#define LCD_PIN_DC       (9)
#define LCD_PIN_RST      (14)
#define LCD_PIN_BL       (2)

#define ADC_UNIT_USED   ADC_UNIT_1
#define ADC_CHANNEL_POT ADC_CHANNEL_0
#define ADC_PIN_POT     (1)
#define ADC_ATTEN_USED                                                                                                 \
    ADC_ATTEN_DB_12 /* ADC input range is approximately 0 to 3.1 V at 12 dB attenuation on ESP32-S3. */
#define ADC_BITWIDTH_USED                                                                                              \
    ADC_BITWIDTH_DEFAULT /* ESP32-S3 default ADC resolution is 12-bit, giving raw codes from 0 to 4095. */
#define ADC_SAMPLE_COUNT     (16U)
#define ADC_UPDATE_PERIOD_MS (200U)
#define ADC_MAX_RAW          (4095U)
#define ADC_VOLTAGE_MAX_MV   (3300U)

#define BAR_X        (40U)
#define BAR_Y        (140U)
#define BAR_MAX_W    (400U)
#define BAR_H        (40U)
#define BAR_BORDER_W (2U)

#define COLOR_BLACK (0x0000U)
#define COLOR_WHITE (0xFFFFU)
#define COLOR_BLUE  (0x001FU)

static const char* TAG = "ADC";

static spi_device_handle_t lcd_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;

static void lcd_write_cmd(uint8_t cmd)
{
    spi_transaction_t transaction = {.length = 8, .tx_buffer = &cmd};

    gpio_set_level(LCD_PIN_DC, 0);
    ESP_ERROR_CHECK(spi_device_transmit(lcd_handle, &transaction));
}

static void lcd_write_data(const uint8_t* data, size_t length)
{
    spi_transaction_t transaction = {.length = length * 8, .tx_buffer = data};

    gpio_set_level(LCD_PIN_DC, 1);
    ESP_ERROR_CHECK(spi_device_transmit(lcd_handle, &transaction));
}

static void lcd_reset(void)
{
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    lcd_write_cmd(0x2A);

    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;

    lcd_write_data(data, sizeof(data));

    lcd_write_cmd(0x2B);

    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;

    lcd_write_data(data, sizeof(data));

    lcd_write_cmd(0x2C);
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    static uint16_t line_buffer[LCD_WIDTH];

    for (uint16_t i = 0; i < width; i++)
    {
        line_buffer[i] = color;
    }

    lcd_set_window(x, y, x + width - 1, y + height - 1);

    for (uint16_t row = 0; row < height; row++)
    {
        lcd_write_data((const uint8_t*) line_buffer, width * 2);
    }
}

static void lcd_init(void)
{
    spi_bus_config_t bus_config = {.mosi_io_num = LCD_PIN_MOSI,
                                   .miso_io_num = LCD_PIN_MISO,
                                   .sclk_io_num = LCD_PIN_SCLK,
                                   .quadwp_io_num = -1,
                                   .quadhd_io_num = -1,
                                   .max_transfer_sz = LCD_WIDTH * 2};

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ, .mode = 0, .spics_io_num = LCD_PIN_CS, .queue_size = 1};

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &device_config, &lcd_handle));

    gpio_config_t dc_config = {.pin_bit_mask = 1ULL << LCD_PIN_DC,
                               .mode = GPIO_MODE_OUTPUT,
                               .pull_up_en = GPIO_PULLUP_DISABLE,
                               .pull_down_en = GPIO_PULLDOWN_DISABLE,
                               .intr_type = GPIO_INTR_DISABLE};

    gpio_config_t rst_config = {.pin_bit_mask = 1ULL << LCD_PIN_RST,
                                .mode = GPIO_MODE_OUTPUT,
                                .pull_up_en = GPIO_PULLUP_DISABLE,
                                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                .intr_type = GPIO_INTR_DISABLE};

    gpio_config_t bl_config = {.pin_bit_mask = 1ULL << LCD_PIN_BL,
                               .mode = GPIO_MODE_OUTPUT,
                               .pull_up_en = GPIO_PULLUP_DISABLE,
                               .pull_down_en = GPIO_PULLDOWN_DISABLE,
                               .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&dc_config));
    ESP_ERROR_CHECK(gpio_config(&rst_config));
    ESP_ERROR_CHECK(gpio_config(&bl_config));

    gpio_set_level(LCD_PIN_BL, 1);

    lcd_reset();

    lcd_write_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t madctl = 0x28;
    lcd_write_cmd(0x36);
    lcd_write_data(&madctl, 1);

    uint8_t colmod = 0x55;
    lcd_write_cmd(0x3A);
    lcd_write_data(&colmod, 1);

    lcd_write_cmd(0x21);
    lcd_write_cmd(0x29);

    vTaskDelay(pdMS_TO_TICKS(20));
}

static void lcd_draw_bar_outline(void)
{
    lcd_fill_rect(BAR_X, BAR_Y, BAR_MAX_W, BAR_H, COLOR_WHITE);

    lcd_fill_rect(BAR_X + BAR_BORDER_W,
                  BAR_Y + BAR_BORDER_W,
                  BAR_MAX_W - 2 * BAR_BORDER_W,
                  BAR_H - 2 * BAR_BORDER_W,
                  COLOR_BLACK);
}

static void lcd_update_bar(uint16_t old_width, uint16_t new_width)
{
    if (new_width > old_width)
    {
        lcd_fill_rect(
            BAR_X + old_width, BAR_Y + BAR_BORDER_W, new_width - old_width, BAR_H - 2 * BAR_BORDER_W, COLOR_BLUE);
    }
    else if (new_width < old_width)
    {
        lcd_fill_rect(
            BAR_X + new_width, BAR_Y + BAR_BORDER_W, old_width - new_width, BAR_H - 2 * BAR_BORDER_W, COLOR_BLACK);
    }
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = ADC_UNIT_USED, .ulp_mode = ADC_ULP_MODE_DISABLE};

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {.atten = ADC_ATTEN_USED, .bitwidth = ADC_BITWIDTH_USED};

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_POT, &channel_config));
}

static void adc_calibration_init(void)
{
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_USED, .chan = ADC_CHANNEL_POT, .atten = ADC_ATTEN_USED, .bitwidth = ADC_BITWIDTH_USED};

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

    if (ret == ESP_OK)
    {
        cali_enabled = true;
        ESP_LOGI(TAG, "ADC curve fitting calibration enabled");
    }
    else
    {
        cali_enabled = false;
        cali_handle = NULL;

        ESP_LOGW(TAG, "ADC calibration unavailable: %s, using fallback", esp_err_to_name(ret));
    }
}

static bool adc_read_average(int* raw_average)
{
    uint32_t raw_sum = 0;

    for (uint32_t i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        int raw_value = 0;

        esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL_POT, &raw_value);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));

            return false;
        }

        raw_sum += (uint32_t) raw_value;
    }

    *raw_average = (int) (raw_sum / ADC_SAMPLE_COUNT);

    return true;
}

static int adc_raw_to_voltage(int raw_value)
{
    int voltage_mv = 0;

    if (cali_enabled)
    {
        esp_err_t ret = adc_cali_raw_to_voltage(cali_handle, raw_value, &voltage_mv);

        if (ret == ESP_OK)
        {
            return voltage_mv;
        }

        ESP_LOGW(TAG, "Calibration conversion failed: %s, using fallback", esp_err_to_name(ret));
    }

    voltage_mv = ((int64_t) raw_value * ADC_VOLTAGE_MAX_MV) / ADC_MAX_RAW;

    return voltage_mv;
}

static uint16_t voltage_to_bar_width(int voltage_mv)
{
    uint32_t bar_width;

    if (voltage_mv <= 0)
    {
        return 0;
    }

    if (voltage_mv >= ADC_VOLTAGE_MAX_MV)
    {
        return BAR_MAX_W;
    }

    bar_width = ((uint32_t) voltage_mv * BAR_MAX_W) / ADC_VOLTAGE_MAX_MV;

    return (uint16_t) bar_width;
}

void app_main(void)
{
    uint16_t old_bar_width = 0;

    lcd_init();

    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

    lcd_draw_bar_outline();

    adc_init();
    adc_calibration_init();

    ESP_LOGI(TAG, "ADC1 channel %d on GPIO%d", ADC_CHANNEL_POT, ADC_PIN_POT);

    ESP_LOGI(TAG, "ADC sample count = %" PRIu32, ADC_SAMPLE_COUNT);

    /* Record the real raw minimum and maximum measured on the potentiometer here after testing. */

    while (1)
    {
        int raw_average = 0;
        int voltage_mv = 0;
        uint16_t new_bar_width = 0;

        if (adc_read_average(&raw_average))
        {
            voltage_mv = adc_raw_to_voltage(raw_average);

            if (voltage_mv < 0)
            {
                voltage_mv = 0;
            }

            if (voltage_mv > ADC_VOLTAGE_MAX_MV)
            {
                voltage_mv = ADC_VOLTAGE_MAX_MV;
            }

            new_bar_width = voltage_to_bar_width(voltage_mv);

            ESP_LOGI(TAG, "raw=%4d -> %4d mV", raw_average, voltage_mv);

            /* Only the changed strip is redrawn so the whole 480x320 screen is not repainted. */
            lcd_update_bar(old_bar_width, new_bar_width);

            old_bar_width = new_bar_width;
        }

        vTaskDelay(pdMS_TO_TICKS(ADC_UPDATE_PERIOD_MS));
    }
}

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define POT_ADC_UNIT     ADC_UNIT_1
#define POT_ADC_CHANNEL  ADC_CHANNEL_3
#define POT_ADC_ATTEN    ADC_ATTEN_DB_12
#define POT_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

#define POT_PIN GPIO_NUM_4
#define LED_PIN GPIO_NUM_15

#define SAMPLE_COUNT     (8U)
#define REPORT_PERIOD_MS (500U)

#define ALARM_ON_MV  (2000)
#define ALARM_OFF_MV (1800)

static const char* TAG = "VMETER";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;

static void led_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = POT_ADC_UNIT,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_ADC_CHANNEL, &chan_cfg));
}

static void adc_calibration_init(void)
{
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = POT_ADC_UNIT,
        .chan = POT_ADC_CHANNEL,
        .atten = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
    };

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);

    if (ret == ESP_OK)
    {
        cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled");
    }
    else
    {
        ESP_LOGW(TAG, "ADC calibration unavailable");
    }
}

static bool read_adc_average(int* raw_average)
{
    int raw = 0;
    int raw_sum = 0;
    unsigned int valid_samples = 0;

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++)
    {
        esp_err_t ret = adc_oneshot_read(adc_handle, POT_ADC_CHANNEL, &raw);

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
            continue;
        }

        raw_sum += raw;
        valid_samples++;
    }

    if (valid_samples == 0)
    {
        return false;
    }

    *raw_average = raw_sum / valid_samples;

    return true;
}

static void app_main(void)
{
    bool led_on = false;

    led_init();
    adc_init();
    adc_calibration_init();

    while (1)
    {
        int raw = 0;

        if (!read_adc_average(&raw))
        {
            vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
            continue;
        }

        if (cali_enabled)
        {
            int voltage_mv = 0;

            esp_err_t ret = adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);

            if (ret == ESP_OK)
            {
                ESP_LOGI(TAG, "raw=%d -> %d mV", raw, voltage_mv);

                if (!led_on && voltage_mv > ALARM_ON_MV)
                {
                    led_on = true;
                    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 1));

                    ESP_LOGI(TAG, "ALARM ON (%d mV > %d mV)", voltage_mv, ALARM_ON_MV);
                }
                else if (led_on && voltage_mv < ALARM_OFF_MV)
                {
                    led_on = false;
                    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));

                    ESP_LOGI(TAG, "ALARM OFF (%d mV < %d mV)", voltage_mv, ALARM_OFF_MV);
                }
            }
            else
            {
                ESP_LOGW(TAG, "Calibration conversion failed, raw=%d", raw);
            }
        }
        else
        {
            ESP_LOGI(TAG, "raw=%d", raw);
        }

        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    }
}

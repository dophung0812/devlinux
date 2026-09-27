#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"

#include "esp_log.h"
#include "esp_attr.h"

#define BTN_PIN GPIO_NUM_0
#define LED_PIN GPIO_NUM_15

#define TIMER_RESOLUTION_HZ (1000000U)
#define TIMER_ALARM_TICKS   (1000ULL)

#define PRINT_PERIOD_MS (100U)
#define BUTTON_POLL_MS  (5U)
#define DEBOUNCE_MS     (20U)
#define LONG_PRESS_MS   (1000U)

#define LED_HALF_PERIOD_MS (500U)

static const char* TAG = "WATCH";

static volatile uint32_t elapsed_ms = 0;

static gptimer_handle_t timer_handle = NULL;

static portMUX_TYPE elapsed_lock = portMUX_INITIALIZER_UNLOCKED;

static bool stopwatch_running = false;

static bool IRAM_ATTR on_timer_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx)
{
    portENTER_CRITICAL_ISR(&elapsed_lock);
    elapsed_ms++;
    portEXIT_CRITICAL_ISR(&elapsed_lock);

    return false;
}

static void led_init(void)
{
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_config));
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
}

static void button_init(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BTN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));
}

static void timer_init(void)
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RESOLUTION_HZ,
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer_handle));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = TIMER_ALARM_TICKS,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer_handle, &alarm_config));

    gptimer_event_callbacks_t callbacks = {
        .on_alarm = on_timer_alarm,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer_handle, &callbacks, NULL));

    ESP_ERROR_CHECK(gptimer_enable(timer_handle));
}

static void stopwatch_start(void)
{
    esp_err_t ret = gptimer_start(timer_handle);

    if (ret == ESP_OK)
    {
        stopwatch_running = true;
        ESP_LOGI(TAG, "started");
    }
    else
    {
        ESP_LOGE(TAG, "gptimer_start failed: %s", esp_err_to_name(ret));
    }
}

static void stopwatch_pause(void)
{
    esp_err_t ret = gptimer_stop(timer_handle);

    if (ret == ESP_OK)
    {
        stopwatch_running = false;

        uint32_t ms;

        portENTER_CRITICAL(&elapsed_lock);
        ms = elapsed_ms;
        portEXIT_CRITICAL(&elapsed_lock);

        ESP_LOGI(TAG,
                 "paused at %02lu:%02lu.%03lu",
                 (unsigned long) (ms / 60000U),
                 (unsigned long) ((ms / 1000U) % 60U),
                 (unsigned long) (ms % 1000U));
    }
    else
    {
        ESP_LOGE(TAG, "gptimer_stop failed: %s", esp_err_to_name(ret));
    }
}

static void stopwatch_reset(void)
{
    esp_err_t ret = gptimer_stop(timer_handle);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "gptimer_stop failed during reset: %s", esp_err_to_name(ret));
        return;
    }

    portENTER_CRITICAL(&elapsed_lock);
    elapsed_ms = 0;
    portEXIT_CRITICAL(&elapsed_lock);

    stopwatch_running = false;

    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));

    ESP_LOGI(TAG, "reset to 00:00.000");
}

static void stopwatch_toggle(void)
{
    if (stopwatch_running)
    {
        stopwatch_pause();
    }
    else
    {
        stopwatch_start();
    }
}

static void print_task(void* arg)
{
    while (1)
    {
        uint32_t ms;

        portENTER_CRITICAL(&elapsed_lock);
        ms = elapsed_ms;
        portEXIT_CRITICAL(&elapsed_lock);

        uint32_t minutes = ms / 60000U;
        uint32_t seconds = (ms / 1000U) % 60U;
        uint32_t milliseconds = ms % 1000U;

        if (stopwatch_running)
        {
            ESP_LOGI(TAG,
                     "%02lu:%02lu.%03lu",
                     (unsigned long) minutes,
                     (unsigned long) seconds,
                     (unsigned long) milliseconds);

            uint32_t led_state = (ms / LED_HALF_PERIOD_MS) % 2U;

            ESP_ERROR_CHECK(gpio_set_level(LED_PIN, led_state));
        }
        else
        {
            ESP_LOGI(TAG,
                     "%02lu:%02lu.%03lu [paused]",
                     (unsigned long) minutes,
                     (unsigned long) seconds,
                     (unsigned long) milliseconds);

            ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
        }

        vTaskDelay(pdMS_TO_TICKS(PRINT_PERIOD_MS));
    }
}

static void button_task(void* arg)
{
    bool stable_pressed = false;
    bool last_raw_pressed = false;

    uint32_t debounce_start = 0;
    uint32_t press_start = 0;

    bool long_press_handled = false;

    while (1)
    {
        bool raw_pressed = (gpio_get_level(BTN_PIN) == 0);

        if (raw_pressed != last_raw_pressed)
        {
            last_raw_pressed = raw_pressed;
            debounce_start = xTaskGetTickCount();
        }

        uint32_t now = xTaskGetTickCount();

        uint32_t debounce_time = pdTICKS_TO_MS(now - debounce_start);

        if (debounce_time >= DEBOUNCE_MS && raw_pressed != stable_pressed)
        {
            stable_pressed = raw_pressed;

            if (stable_pressed)
            {
                press_start = now;
                long_press_handled = false;
            }
            else
            {
                uint32_t press_time = pdTICKS_TO_MS(now - press_start);

                if (!long_press_handled && press_time < LONG_PRESS_MS)
                {
                    stopwatch_toggle();
                }
            }
        }

        if (stable_pressed && !long_press_handled)
        {
            uint32_t hold_time = pdTICKS_TO_MS(now - press_start);

            if (hold_time >= LONG_PRESS_MS)
            {
                stopwatch_reset();
                long_press_handled = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void app_main(void)
{
    led_init();
    button_init();
    timer_init();

    xTaskCreate(print_task, "print_task", 4096, NULL, 5, NULL);

    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
}

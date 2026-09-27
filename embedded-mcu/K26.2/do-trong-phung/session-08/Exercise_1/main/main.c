#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/gptimer.h"

#define SOFT_TIMER_PERIOD_US    (1000000ULL)
#define GPTIMER_RESOLUTION_HZ   (1000000U)
#define GPTIMER_ALARM_PERIOD_US (250000ULL)
#define WDT_TIMEOUT_MS          (5000U)
#define STALL_DURATION_MS       (8000U)
#define MAIN_TASK_PERIOD_MS     (1000U)
#define FIX_YIELD_DELAY_MS      (10U)
#define WDT_FIX_MODE            (0)

static const char* TAG = "TIMERS";
static volatile uint32_t hw_alarm_count = 0;
static esp_timer_handle_t soft_timer_handle = NULL;
static gptimer_handle_t hw_timer_handle = NULL;

static void soft_timer_cb(void* arg)
{
    static uint32_t uptime_seconds = 0;
    uptime_seconds++;
    ESP_LOGI(TAG, "uptime = %" PRIu32 " s", uptime_seconds);
}

static bool IRAM_ATTR hw_timer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx)
{
    hw_alarm_count++;
    return false;
}

/* Deliberately bad version: uncomment this to trigger the watchdog. */
/*
static void stall_cpu_bad(uint32_t duration_ms)
{
    int64_t start_time = esp_timer_get_time();
    int64_t duration_us = (int64_t)duration_ms * 1000LL;

    ESP_LOGW(TAG, "BAD TEST: busy-wait for %" PRIu32 " ms", duration_ms);

    while ((esp_timer_get_time() - start_time) < duration_us) {
    }

    ESP_LOGW(TAG, "BAD TEST: busy-wait finished");
}
*/

static void stall_cpu_feed_wdt(uint32_t duration_ms)
{
    int64_t start_time = esp_timer_get_time();
    int64_t duration_us = (int64_t) duration_ms * 1000LL;

    ESP_LOGW(TAG, "FIX #1: busy-wait with watchdog feeding");

    while ((esp_timer_get_time() - start_time) < duration_us)
    {
        esp_task_wdt_reset();
    }

    ESP_LOGI(TAG, "FIX #1: operation finished");
}

static void stall_cpu_yield(uint32_t duration_ms)
{
    int64_t start_time = esp_timer_get_time();
    int64_t duration_us = (int64_t) duration_ms * 1000LL;

    ESP_LOGW(TAG, "FIX #2: long operation with CPU yielding");

    while ((esp_timer_get_time() - start_time) < duration_us)
    {
        vTaskDelay(pdMS_TO_TICKS(FIX_YIELD_DELAY_MS));
    }

    ESP_LOGI(TAG, "FIX #2: operation finished");
}

static void init_soft_timer(void)
{
    const esp_timer_create_args_t soft_args = {.callback = soft_timer_cb,
                                               .arg = NULL,
                                               .dispatch_method = ESP_TIMER_TASK,
                                               .name = "uptime",
                                               .skip_unhandled_events = false};

    ESP_ERROR_CHECK(esp_timer_create(&soft_args, &soft_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(soft_timer_handle, SOFT_TIMER_PERIOD_US));

    ESP_LOGI(TAG, "esp_timer started");
}

static void init_hw_timer(void)
{
    const gptimer_config_t hw_cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = GPTIMER_RESOLUTION_HZ};

    ESP_ERROR_CHECK(gptimer_new_timer(&hw_cfg, &hw_timer_handle));

    const gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = GPTIMER_ALARM_PERIOD_US, .reload_count = 0, .flags.auto_reload_on_alarm = true};

    ESP_ERROR_CHECK(gptimer_set_alarm_action(hw_timer_handle, &alarm_cfg));

    const gptimer_event_callbacks_t callbacks = {.on_alarm = hw_timer_cb};

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(hw_timer_handle, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(hw_timer_handle));
    ESP_ERROR_CHECK(gptimer_start(hw_timer_handle));

    ESP_LOGI(TAG, "gptimer started");
}

static void init_task_watchdog(void)
{
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS, .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = true};

    esp_err_t ret = esp_task_wdt_init(&twdt_config);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "TWDT initialized");
    }
    else if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGI(TAG, "TWDT already initialized, reconfiguring");
        ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
    }
    else
    {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "Current task subscribed to TWDT");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Session 08");

    init_soft_timer();
    init_hw_timer();
    init_task_watchdog();

    ESP_LOGI(TAG, "All timers and watchdog are running");

    /* Deliberate watchdog test: uncomment to trigger panic and reboot. */
    /*
    stall_cpu_bad(STALL_DURATION_MS);
    */

#if WDT_FIX_MODE == 1
    stall_cpu_feed_wdt(STALL_DURATION_MS);
#else
    stall_cpu_yield(STALL_DURATION_MS);
#endif

    ESP_LOGI(TAG, "Fixed operation completed");

    while (1)
    {
        ESP_LOGI(TAG, "hw_alarm_count = %" PRIu32, hw_alarm_count);
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_PERIOD_MS));
    }
}

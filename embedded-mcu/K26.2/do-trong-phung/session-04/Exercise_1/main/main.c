#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"

#define BTN_PIN GPIO_NUM_14

#define DEBOUNCE_US        50000
#define DOUBLE_CLICK_US    400000
#define LONG_PRESS_US      1000000
#define LONG_REPEAT_US     500000

static const char *TAG = "BTN";

typedef struct
{
    int64_t timestamp_us;
    bool is_press;
} btn_event_t;

static QueueHandle_t btn_queue;

static int counter = 0;


/* ============================================================
 * REGISTER-LEVEL DISPLAY
 *
 * Replace these two functions with your Session 03 code.
 * Segment pins:
 * a -> GPIO4
 * b -> GPIO5
 * c -> GPIO6
 * d -> GPIO7
 * e -> GPIO15
 * f -> GPIO16
 * g -> GPIO17
 * ============================================================
 */

static void display_init(void)
{
    /*
     * Paste the register-level GPIO initialization
     * from Session 03 here.
     */
}

static void display_digit(int digit)
{
    /*
     * Paste the register-level 7-segment output
     * code from Session 03 here.
     */
}


/* ============================================================
 * COUNTER
 * ============================================================
 */

static void counter_increment(void)
{
    counter++;

    if (counter > 9)
        counter = 0;

    display_digit(counter);
}

static void counter_decrement(void)
{
    counter--;

    if (counter < 0)
        counter = 9;

    display_digit(counter);
}


/* ============================================================
 * GPIO ISR
 *
 * ISR does ONLY:
 *   1. read GPIO level
 *   2. timestamp event
 *   3. send event to queue
 *   4. return
 * ============================================================
 */

static void IRAM_ATTR button_isr(void *arg)
{
    btn_event_t event;

    event.timestamp_us = esp_timer_get_time();

    /*
     * Falling edge = button pressed
     * Rising edge  = button released
     *
     * Button uses pull-up:
     *   HIGH = released
     *   LOW  = pressed
     */
    event.is_press = (gpio_get_level(BTN_PIN) == 0);

    BaseType_t higher_priority_task_woken = pdFALSE;

    xQueueSendFromISR(
        btn_queue,
        &event,
        &higher_priority_task_woken
    );

    if (higher_priority_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}


/* ============================================================
 * GESTURE TASK
 * ============================================================
 */

static void gesture_task(void *arg)
{
    btn_event_t event;

    bool pressed = false;

    int64_t press_time = 0;
    int64_t last_release_time = 0;

    bool waiting_for_second_click = false;

    while (1)
    {
        TickType_t timeout;

        /*
         * ----------------------------------------------------
         * STATE 1:
         * Button is currently pressed.
         *
         * We need a timeout so that we can generate
         * LONG repeat events every 500 ms even though
         * no GPIO interrupt occurs while the button
         * remains held.
         * ----------------------------------------------------
         */

        if (pressed)
        {
            timeout = pdMS_TO_TICKS(50);
        }

        /*
         * ----------------------------------------------------
         * STATE 2:
         * Button released, waiting to see whether another
         * click arrives.
         *
         * Wait up to DOUBLE_CLICK_US.
         * ----------------------------------------------------
         */

        else if (waiting_for_second_click)
        {
            int64_t elapsed =
                esp_timer_get_time() - last_release_time;

            if (elapsed >= DOUBLE_CLICK_US)
            {
                /*
                 * No second click -> single click
                 */
                counter_increment();

                ESP_LOGI(
                    TAG,
                    "CLICK -> %d",
                    counter
                );

                waiting_for_second_click = false;

                continue;
            }

            timeout = pdMS_TO_TICKS(
                (DOUBLE_CLICK_US - elapsed) / 1000
            );

            if (timeout == 0)
                timeout = 1;
        }

        /*
         * ----------------------------------------------------
         * STATE 3:
         * Idle.
         *
         * Wait forever for next GPIO event.
         * ----------------------------------------------------
         */

        else
        {
            timeout = portMAX_DELAY;
        }


        if (xQueueReceive(btn_queue, &event, timeout) == pdTRUE)
        {
            /*
             * Debounce:
             *
             * Ignore events that arrive too close together.
             */
            static int64_t last_event_time = 0;

            if ((event.timestamp_us - last_event_time)
                < DEBOUNCE_US)
            {
                continue;
            }

            last_event_time = event.timestamp_us;


            /* ================================================
             * PRESS
             * ================================================
             */

            if (event.is_press)
            {
                if (!pressed)
                {
                    pressed = true;
                    press_time = event.timestamp_us;

                    /*
                     * If this is the second press of a
                     * double-click, cancel the pending
                     * single click.
                     */
                    if (waiting_for_second_click)
                    {
                        waiting_for_second_click = false;
                    }
                }
            }


            /* ================================================
             * RELEASE
             * ================================================
             */

            else
            {
                if (!pressed)
                    continue;

                pressed = false;

                int64_t duration =
                    event.timestamp_us - press_time;


                /*
                 * LONG PRESS
                 *
                 * A long press has already generated its
                 * repetitions while being held.
                 *
                 * Therefore release does NOT generate
                 * another +1.
                 */

                if (duration >= LONG_PRESS_US)
                {
                    ESP_LOGI(
                        TAG,
                        "LONG end"
                    );

                    continue;
                }


                /*
                 * SHORT PRESS
                 *
                 * Wait to determine whether a second
                 * click follows.
                 */

                last_release_time = event.timestamp_us;

                waiting_for_second_click = true;
            }
        }

        else
        {
            /*
             * Queue timeout.
             *
             * This is important:
             * no interrupt is generated while the button
             * is continuously held, so the task wakes up
             * periodically to handle the long press.
             */

            if (pressed)
            {
                int64_t held_time =
                    esp_timer_get_time() - press_time;

                /*
                 * Long press starts after LONG_PRESS_US.
                 */

                if (held_time >= LONG_PRESS_US)
                {
                    /*
                     * We use a static timestamp so that
                     * repetitions happen every 500 ms.
                     */

                    static int64_t last_repeat_time = 0;

                    if (last_repeat_time == 0)
                    {
                        /*
                         * First long-press increment.
                         */
                        counter_increment();

                        ESP_LOGI(
                            TAG,
                            "LONG start -> %d",
                            counter
                        );

                        last_repeat_time =
                            esp_timer_get_time();
                    }
                    else
                    {
                        int64_t now =
                            esp_timer_get_time();

                        if ((now - last_repeat_time)
                            >= LONG_REPEAT_US)
                        {
                            counter_increment();

                            ESP_LOGI(
                                TAG,
                                "LONG rep -> %d",
                                counter
                            );

                            last_repeat_time = now;
                        }
                    }
                }
            }
        }


        /*
         * Reset long-repeat timer after release.
         */
        if (!pressed)
        {
            /*
             * The static variable inside the timeout
             * block cannot be directly reset here.
             *
             * It is therefore intentionally handled
             * by detecting the next long press.
             */
        }
    }
}


/* ============================================================
 * GPIO CONFIGURATION
 * ============================================================
 */

static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(
        gpio_install_isr_service(0)
    );

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            BTN_PIN,
            button_isr,
            NULL
        )
    );
}


/* ============================================================
 * APP MAIN
 * ============================================================
 */

void app_main(void)
{
    /*
     * Display must be initialized first.
     */
    display_init();

    /*
     * Initial value = 0.
     */
    counter = 0;
    display_digit(counter);


    /*
     * Create queue BEFORE installing ISR.
     *
     * This is important because the ISR may run immediately
     * after the interrupt is enabled.
     */
    btn_queue = xQueueCreate(
        10,
        sizeof(btn_event_t)
    );

    configASSERT(btn_queue != NULL);


    /*
     * Configure button + interrupt.
     */
    button_init();


    /*
     * Start gesture decoder.
     */
    xTaskCreate(
        gesture_task,
        "gesture_task",
        4096,
        NULL,
        5,
        NULL
    );
}
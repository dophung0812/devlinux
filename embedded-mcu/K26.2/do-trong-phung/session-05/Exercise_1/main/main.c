/*
 * DevKitC-1 v1.1, RGB LED on GPIO38
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "led_strip.h"

/* =========================
 * UART configuration
 * ========================= */
#define UART_PORT_NUM       UART_NUM_0
#define UART_TX_PIN         GPIO_NUM_43
#define UART_RX_PIN         GPIO_NUM_44
#define UART_BAUD_RATE      115200UL

#define UART_BUF_SIZE       1024U
#define UART_QUEUE_SIZE     10U

/* =========================
 * Command buffer
 * ========================= */
#define CMD_BUF_SIZE        64U

/* =========================
 * RGB LED
 * ========================= */
#define RGB_LED_PIN         GPIO_NUM_38

/* =========================
 * Command strings
 * ========================= */
#define CMD_LED_ON          "LED_ON"
#define CMD_LED_OFF         "LED_OFF"
#define CMD_RED             "RED"
#define CMD_GREEN           "GREEN"
#define CMD_BLUE            "BLUE"

/* =========================
 * UART control characters
 * ========================= */
#define CHAR_BACKSPACE      '\b'
#define CHAR_DELETE         0x7F
#define CHAR_CR             '\r'
#define CHAR_LF             '\n'

#define TAG "UART_CONSOLE"

/* UART event queue */
static QueueHandle_t uart_queue;

/* LED handle */
static led_strip_handle_t led_strip;

/* Command buffer */
static char cmd_buf[CMD_BUF_SIZE];
static size_t cmd_len = 0U;


/* =========================================================
 * LED functions
 * ========================================================= */

static void set_led_color(uint8_t red, uint8_t green, uint8_t blue)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_off(void)
{
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}


/* =========================================================
 * Command handling
 * ========================================================= */

static void handle_command(const char *cmd)
{
    ESP_LOGI(TAG, "Received command: \"%s\"", cmd);

    if (strcmp(cmd, CMD_LED_ON) == 0) {
        set_led_color(255, 255, 255);
        ESP_LOGI(TAG, "LED -> WHITE");
    }
    else if (strcmp(cmd, CMD_LED_OFF) == 0) {
        led_off();
        ESP_LOGI(TAG, "LED -> OFF");
    }
    else if (strcmp(cmd, CMD_RED) == 0) {
        set_led_color(255, 0, 0);
        ESP_LOGI(TAG, "LED -> RED");
    }
    else if (strcmp(cmd, CMD_GREEN) == 0) {
        set_led_color(0, 255, 0);
        ESP_LOGI(TAG, "LED -> GREEN");
    }
    else if (strcmp(cmd, CMD_BLUE) == 0) {
        set_led_color(0, 0, 255);
        ESP_LOGI(TAG, "LED -> BLUE");
    }
    else {
        ESP_LOGW(TAG, "Unknown command: \"%s\"", cmd);
    }
}


/* =========================================================
 * UART event task
 * ========================================================= */

static void uart_event_task(void *arg)
{
    uart_event_t event;

    uint8_t data[UART_BUF_SIZE];

    while (1) {

        if (xQueueReceive(uart_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event.type) {

        case UART_DATA:
        {
            int len = uart_read_bytes(
                UART_PORT_NUM,
                data,
                event.size,
                portMAX_DELAY
            );

            for (int i = 0; i < len; i++) {

                uint8_t ch = data[i];

                /* =========================
                 * Enter
                 * ========================= */
                if (ch == CHAR_CR || ch == CHAR_LF) {

                    /*
                     * Ignore an empty line.
                     */
                    if (cmd_len > 0U) {

                        cmd_buf[cmd_len] = '\0';

                        /* Echo newline */
                        const char newline[] = "\r\n";
                        uart_write_bytes(
                            UART_PORT_NUM,
                            newline,
                            sizeof(newline) - 1U
                        );

                        handle_command(cmd_buf);

                        cmd_len = 0U;
                        cmd_buf[0] = '\0';
                    }
                    else {
                        const char newline[] = "\r\n";

                        uart_write_bytes(
                            UART_PORT_NUM,
                            newline,
                            sizeof(newline) - 1U
                        );
                    }

                    continue;
                }

                /* =========================
                 * Backspace / DEL
                 * ========================= */
                if (ch == CHAR_BACKSPACE || ch == CHAR_DELETE) {

                    if (cmd_len > 0U) {

                        cmd_len--;

                        cmd_buf[cmd_len] = '\0';

                        /*
                         * Erase the previous character
                         * from the terminal:
                         *
                         * \b -> move cursor left
                         * ' ' -> overwrite character
                         * \b -> move cursor left again
                         */
                        const char erase[] = "\b \b";

                        uart_write_bytes(
                            UART_PORT_NUM,
                            erase,
                            sizeof(erase) - 1U
                        );
                    }

                    continue;
                }

                /* =========================
                 * Printable character
                 * ========================= */
                if (isprint(ch)) {

                    /*
                     * Reserve one byte for '\0'.
                     */
                    if (cmd_len < CMD_BUF_SIZE - 1U) {

                        cmd_buf[cmd_len] = (char)ch;
                        cmd_len++;

                        /*
                         * Echo character back.
                         */
                        uart_write_bytes(
                            UART_PORT_NUM,
                            (const char *)&ch,
                            1
                        );
                    }
                    else {

                        /*
                         * Buffer is full.
                         * Do not write beyond its boundary.
                         */
                        const char warning[] = "\a";

                        uart_write_bytes(
                            UART_PORT_NUM,
                            warning,
                            sizeof(warning) - 1U
                        );

                        ESP_LOGW(
                            TAG,
                            "Command too long, input truncated"
                        );
                    }

                    continue;
                }

                /*
                 * Ignore other non-printable characters.
                 */
            }

            break;
        }

        /* =========================
         * FIFO overflow
         * ========================= */
        case UART_FIFO_OVF:

            ESP_LOGW(TAG, "UART FIFO overflow, input flushed");

            uart_flush_input(UART_PORT_NUM);
            xQueueReset(uart_queue);

            cmd_len = 0U;
            cmd_buf[0] = '\0';

            break;

        /* =========================
         * UART ring buffer full
         * ========================= */
        case UART_BUFFER_FULL:

            ESP_LOGW(TAG, "UART buffer full, input flushed");

            uart_flush_input(UART_PORT_NUM);
            xQueueReset(uart_queue);

            cmd_len = 0U;
            cmd_buf[0] = '\0';

            break;

        default:

            ESP_LOGW(
                TAG,
                "Unhandled UART event: %d",
                event.type
            );

            break;
        }
    }
}


/* =========================================================
 * Application entry point
 * ========================================================= */

void app_main(void)
{
    /* =====================================================
     * Initialize WS2812 RGB LED
     * ===================================================== */

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_PIN,
        .max_leds = 1,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &led_strip
        )
    );

    /* Start with LED OFF */
    led_off();


    /* =====================================================
     * Configure UART
     * ===================================================== */

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(
        uart_param_config(
            UART_PORT_NUM,
            &uart_config
        )
    );

    ESP_ERROR_CHECK(
        uart_set_pin(
            UART_PORT_NUM,
            UART_TX_PIN,
            UART_RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    /*
     * Install UART driver with event queue.
     */
    ESP_ERROR_CHECK(
        uart_driver_install(
            UART_PORT_NUM,
            UART_BUF_SIZE,
            UART_BUF_SIZE,
            UART_QUEUE_SIZE,
            &uart_queue,
            0
        )
    );


    /* =====================================================
     * Create UART event task
     * ===================================================== */

    BaseType_t task_result = xTaskCreatePinnedToCore(
        uart_event_task,
        "uart_event_task",
        4096,
        NULL,
        10,
        NULL,
        0
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART event task");
        return;
    }


    ESP_LOGI(
        TAG,
        "Console ready on UART0, 115200-8-N-1"
    );
}
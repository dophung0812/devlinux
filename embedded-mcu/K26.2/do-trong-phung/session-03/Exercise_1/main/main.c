/*
 * Assignment: Session 03 - Exercise 1
 *
 * Board:
 *     ESP32-S3 DevKitC-1
 *
 * Display:
 *     COMMON CATHODE
 *     Common pin -> GND
 *
 * Wiring:
 *     segment a -> GPIO4
 *     segment b -> GPIO5
 *     segment c -> GPIO6
 *     segment d -> GPIO7
 *     segment e -> GPIO15
 *     segment f -> GPIO16
 *     segment g -> GPIO17
 *     segment dp -> GPIO18 (optional, not used)
 *
 *     button -> GPIO14 -> GND
 *
 * Important:
 *     - Register access only for GPIO.
 *     - No driver/gpio.h
 *     - No gpio_config()
 *     - No gpio_set_level()
 *     - No gpio_get_level()
 *     - Polling only. No GPIO interrupt.
 */

#include <stdint.h>
#include <stdbool.h>

#include "esp_timer.h"


/* ============================================================
 * GPIO NUMBERS
 * ============================================================ */

#define SEG_A_PIN              (4U)
#define SEG_B_PIN              (5U)
#define SEG_C_PIN              (6U)
#define SEG_D_PIN              (7U)
#define SEG_E_PIN              (15U)
#define SEG_F_PIN              (16U)
#define SEG_G_PIN              (17U)
#define SEG_DP_PIN             (18U)

#define BTN_PIN                (14U)


/* ============================================================
 * GPIO REGISTER BASE / OFFSETS
 * ============================================================ */

#define GPIO_BASE_ADDRESS              (0x60004000UL)

#define GPIO_OUT_OFFSET                (0x0004UL)
#define GPIO_OUT_W1TS_OFFSET           (0x0008UL)
#define GPIO_OUT_W1TC_OFFSET           (0x000CUL)

#define GPIO_ENABLE_OFFSET             (0x0020UL)
#define GPIO_ENABLE_W1TS_OFFSET        (0x0024UL)
#define GPIO_ENABLE_W1TC_OFFSET        (0x0028UL)

#define GPIO_IN_OFFSET                 (0x003CUL)

#define GPIO_FUNC_OUT_SEL_BASE_OFFSET  (0x0554UL)

#define GPIO_OUTPUT_SIGNAL             (256U)


/* ============================================================
 * IO MUX REGISTER
 *
 * ESP32-S3:
 *
 * GPIO0 IO_MUX register starts at:
 *
 *     IO_MUX_BASE + 0x04
 *
 * GPIO1:
 *
 *     IO_MUX_BASE + 0x08
 *
 * Therefore:
 *
 *     GPIOn = IO_MUX_BASE + 0x04 + n * 4
 * ============================================================ */

#define IO_MUX_BASE_ADDRESS            (0x60009000UL)
#define IO_MUX_GPIO0_OFFSET            (0x0004UL)
#define IO_MUX_GPIO_STRIDE             (0x0004UL)


/* ============================================================
 * IO MUX BIT POSITIONS
 * ============================================================ */

/* Function select */
#define IO_MUX_MCU_SEL_SHIFT           (12U)
#define IO_MUX_MCU_SEL_MASK            (0x7U)

/* GPIO function */
#define IO_MUX_GPIO_FUNCTION            (1U)

/* Pull-up */
#define IO_MUX_FUN_PU_BIT              (8U)

/* Input enable */
#define IO_MUX_FUN_IE_BIT              (9U)


/* ============================================================
 * GPIO MATRIX REGISTER BIT POSITIONS
 *
 * OUT_SEL:
 *     bits [8:0]
 *
 * OEN_SEL:
 *     bit 10
 *
 * OUT_INV:
 *     bit 9
 *
 * OEN_INV:
 *     bit 11
 * ============================================================ */

#define GPIO_MATRIX_OUT_SEL_MASK       (0x1FFU)
#define GPIO_MATRIX_OUT_INV_BIT       (9U)
#define GPIO_MATRIX_OEN_SEL_BIT       (10U)
#define GPIO_MATRIX_OEN_INV_BIT       (11U)


/* ============================================================
 * TIMING CONSTANTS
 * ============================================================ */

#define POLL_PERIOD_MS                 (5U)

#define DEBOUNCE_MS                   (25U)
#define DOUBLE_CLICK_MS               (350U)
#define LONG_PRESS_MS                 (800U)
#define REPEAT_PERIOD_MS              (500U)


#define MICROSECONDS_PER_MILLISECOND  (1000LL)


/* ============================================================
 * BUTTON LOGIC LEVEL
 *
 * Button:
 *
 * GPIO14 ---- internal pull-up ---- 3V3
 * GPIO14 ---- button ------------ GND
 *
 * Therefore:
 *
 *     released = 1
 *     pressed  = 0
 * ============================================================ */

#define BUTTON_RELEASED_LEVEL         (1U)
#define BUTTON_PRESSED_LEVEL          (0U)


/* ============================================================
 * DISPLAY CONFIGURATION
 *
 * 0 -> common cathode
 * 1 -> common anode
 *
 * Change ONLY this constant when changing display type.
 * ============================================================ */

#define DISPLAY_COMMON_ANODE          (0U)


/* ============================================================
 * SEGMENT MAP
 *
 * Bit 0 -> a
 * Bit 1 -> b
 * Bit 2 -> c
 * Bit 3 -> d
 * Bit 4 -> e
 * Bit 5 -> f
 * Bit 6 -> g
 *
 * Common cathode:
 *
 *     1 = segment ON
 *     0 = segment OFF
 * ============================================================ */

static const uint8_t SEGMENT_MAP[10] =
{
    0x3FU,  /* 0 */
    0x06U,  /* 1 */
    0x5BU,  /* 2 */
    0x4FU,  /* 3 */
    0x66U,  /* 4 */
    0x6DU,  /* 5 */
    0x7DU,  /* 6 */
    0x07U,  /* 7 */
    0x7FU,  /* 8 */
    0x6FU   /* 9 */
};


/* ============================================================
 * SEGMENT GPIO ARRAY
 * ============================================================ */

static const uint8_t SEGMENT_PINS[7] =
{
    SEG_A_PIN,
    SEG_B_PIN,
    SEG_C_PIN,
    SEG_D_PIN,
    SEG_E_PIN,
    SEG_F_PIN,
    SEG_G_PIN
};


/* ============================================================
 * VOLATILE REGISTER ACCESS
 * ============================================================ */

static inline volatile uint32_t *reg32(uint32_t address)
{
    return (volatile uint32_t *)address;
}


/* ============================================================
 * REGISTER ADDRESS HELPERS
 * ============================================================ */

static inline uint32_t gpio_out_w1ts_address(void)
{
    return GPIO_BASE_ADDRESS + GPIO_OUT_W1TS_OFFSET;
}


static inline uint32_t gpio_out_w1tc_address(void)
{
    return GPIO_BASE_ADDRESS + GPIO_OUT_W1TC_OFFSET;
}


static inline uint32_t gpio_enable_w1ts_address(void)
{
    return GPIO_BASE_ADDRESS + GPIO_ENABLE_W1TS_OFFSET;
}


static inline uint32_t gpio_enable_w1tc_address(void)
{
    return GPIO_BASE_ADDRESS + GPIO_ENABLE_W1TC_OFFSET;
}


static inline uint32_t gpio_in_address(void)
{
    return GPIO_BASE_ADDRESS + GPIO_IN_OFFSET;
}


static inline uint32_t io_mux_address(uint32_t gpio)
{
    return IO_MUX_BASE_ADDRESS
           + IO_MUX_GPIO0_OFFSET
           + gpio * IO_MUX_GPIO_STRIDE;
}


static inline uint32_t gpio_matrix_out_address(uint32_t gpio)
{
    return GPIO_BASE_ADDRESS
           + GPIO_FUNC_OUT_SEL_BASE_OFFSET
           + gpio * sizeof(uint32_t);
}


/* ============================================================
 * IO MUX CONFIGURATION
 * ============================================================ */

static void iomux_configure_gpio(
    uint32_t gpio,
    bool input_enable,
    bool pullup_enable)
{
    volatile uint32_t *reg =
        reg32(io_mux_address(gpio));

    uint32_t value = *reg;

    /* Select GPIO function */
    value &= ~(IO_MUX_MCU_SEL_MASK << IO_MUX_MCU_SEL_SHIFT);

    value |=
        (IO_MUX_GPIO_FUNCTION << IO_MUX_MCU_SEL_SHIFT);

    /* Input enable */
    if (input_enable)
    {
        value |= (1UL << IO_MUX_FUN_IE_BIT);
    }
    else
    {
        value &= ~(1UL << IO_MUX_FUN_IE_BIT);
    }

    /* Pull-up */
    if (pullup_enable)
    {
        value |= (1UL << IO_MUX_FUN_PU_BIT);
    }
    else
    {
        value &= ~(1UL << IO_MUX_FUN_PU_BIT);
    }

    *reg = value;
}


/* ============================================================
 * GPIO MATRIX OUTPUT CONFIGURATION
 * ============================================================ */

static void gpio_matrix_output_configure(uint32_t gpio)
{
    volatile uint32_t *reg =
        reg32(gpio_matrix_out_address(gpio));

    /*
     * GPIO_OUTPUT_SIGNAL = 256
     *
     * This selects the simple GPIO output signal.
     *
     * OEN_SEL = 0:
     * output enable is controlled by GPIO_ENABLE register.
     *
     * OUT_INV = 0:
     * no GPIO matrix inversion.
     *
     * OEN_INV = 0:
     * no output-enable inversion.
     */

    uint32_t value = 0U;

    value |=
        (GPIO_OUTPUT_SIGNAL & GPIO_MATRIX_OUT_SEL_MASK);

    value &= ~(1UL << GPIO_MATRIX_OUT_INV_BIT);
    value &= ~(1UL << GPIO_MATRIX_OEN_SEL_BIT);
    value &= ~(1UL << GPIO_MATRIX_OEN_INV_BIT);

    *reg = value;
}


/* ============================================================
 * GPIO OUTPUT ENABLE
 * ============================================================ */

static void gpio_output_enable(uint32_t gpio)
{
    volatile uint32_t *reg =
        reg32(gpio_enable_w1ts_address());

    *reg = (1UL << gpio);
}


/* ============================================================
 * GPIO INPUT READ
 * ============================================================ */

static uint32_t gpio_input_read(uint32_t gpio)
{
    volatile uint32_t *reg =
        reg32(gpio_in_address());

    return ((*reg >> gpio) & 1UL);
}


/* ============================================================
 * BUILD SEGMENT MASK
 *
 * Convert logical segment pattern:
 *
 *     bit0 -> a
 *     bit1 -> b
 *     ...
 *     bit6 -> g
 *
 * into actual GPIO bit positions.
 * ============================================================ */

static uint32_t build_segment_mask(uint8_t pattern)
{
    uint32_t mask = 0U;

    for (uint32_t segment = 0U;
         segment < 7U;
         segment++)
    {
        if ((pattern & (1U << segment)) != 0U)
        {
            mask |= (1UL << SEGMENT_PINS[segment]);
        }
    }

    return mask;
}


/* ============================================================
 * DISPLAY ONE DIGIT
 * ============================================================ */

static void display_digit(uint8_t digit)
{
    uint8_t pattern = SEGMENT_MAP[digit];

    /*
     * This is the ONE inversion required when changing
     * between common-cathode and common-anode displays.
     */
    if (DISPLAY_COMMON_ANODE != 0U)
    {
        pattern = (uint8_t)(~pattern);
    }

    uint32_t desired_mask =
        build_segment_mask(pattern);

    uint32_t segment_mask =
        0U;

    for (uint32_t segment = 0U;
         segment < 7U;
         segment++)
    {
        segment_mask |=
            (1UL << SEGMENT_PINS[segment]);
    }

    /*
     * First turn OFF all seven segments.
     *
     * GPIO_OUT_W1TC:
     *
     * writing 1 -> clear corresponding output bit.
     *
     * This is safer than read-modify-write on GPIO_OUT_REG.
     */
    volatile uint32_t *clear_reg =
        reg32(gpio_out_w1tc_address());

    *clear_reg = segment_mask;


    /*
     * Turn ON required segments.
     *
     * GPIO_OUT_W1TS:
     *
     * writing 1 -> set corresponding output bit.
     */
    volatile uint32_t *set_reg =
        reg32(gpio_out_w1ts_address());

    *set_reg = desired_mask;
}


/* ============================================================
 * DISPLAY INITIALIZATION
 * ============================================================ */

static void display_init(void)
{
    for (uint32_t segment = 0U;
         segment < 7U;
         segment++)
    {
        uint32_t gpio =
            SEGMENT_PINS[segment];

        /*
         * GPIO function.
         *
         * No input required for display outputs.
         * No pull-up required.
         */
        iomux_configure_gpio(
            gpio,
            false,
            false
        );

        /*
         * Route GPIO output through GPIO matrix.
         */
        gpio_matrix_output_configure(gpio);

        /*
         * Enable GPIO output driver.
         */
        gpio_output_enable(gpio);
    }

    /*
     * Start with all segments OFF.
     */
    display_digit(0U);
}


/* ============================================================
 * BUTTON INITIALIZATION
 * ============================================================ */

static void button_init(void)
{
    /*
     * Button is:
     *
     * GPIO14 ---- internal pull-up ---- 3V3
     * GPIO14 ---- button ------------ GND
     *
     * Therefore:
     *
     * released = HIGH
     * pressed  = LOW
     */
    iomux_configure_gpio(
        BTN_PIN,
        true,
        true
    );
}


/* ============================================================
 * COUNTER
 * ============================================================ */

static uint8_t counter = 0U;


static void counter_increment(void)
{
    counter++;

    if (counter >= 10U)
    {
        counter = 0U;
    }

    display_digit(counter);
}


static void counter_decrement(void)
{
    if (counter == 0U)
    {
        counter = 9U;
    }
    else
    {
        counter--;
    }

    display_digit(counter);
}


/* ============================================================
 * TIME HELPER
 * ============================================================ */

static int64_t milliseconds_since(int64_t timestamp_us)
{
    int64_t now_us =
        esp_timer_get_time();

    return
        (now_us - timestamp_us)
        / MICROSECONDS_PER_MILLISECOND;
}


/* ============================================================
 * MAIN
 * ============================================================ */

void app_main(void)
{
    display_init();
    button_init();

    /*
     * Initial raw/stable state.
     *
     * Button should normally be released after reset.
     */
    uint32_t raw_state =
        gpio_input_read(BTN_PIN);

    uint32_t stable_state =
        raw_state;

    uint32_t last_raw_state =
        raw_state;

    int64_t last_raw_change_time =
        esp_timer_get_time();


    /*
     * Long press state.
     */
    bool long_press_started = false;

    int64_t press_start_time = 0;
    int64_t next_repeat_time = 0;


    /*
     * Pending single click.
     *
     * We cannot immediately increment after the first
     * release because a second click may follow.
     */
    bool pending_click = false;

    int64_t pending_click_time = 0;


    while (true)
    {
        int64_t now =
            esp_timer_get_time();


        /* ====================================================
         * 1. POLL BUTTON
         * ==================================================== */

        raw_state =
            gpio_input_read(BTN_PIN);


        /* ====================================================
         * 2. DEBOUNCE
         *
         * If raw state changes, start the debounce timer.
         *
         * Only accept the new state if it stays unchanged
         * for DEBOUNCE_MS.
         * ==================================================== */

        if (raw_state != last_raw_state)
        {
            last_raw_state =
                raw_state;

            last_raw_change_time =
                now;
        }
        else
        {
            int64_t stable_time_ms =
                (now - last_raw_change_time)
                / MICROSECONDS_PER_MILLISECOND;

            if ((raw_state != stable_state) &&
                (stable_time_ms >= DEBOUNCE_MS))
            {
                uint32_t old_state =
                    stable_state;

                stable_state =
                    raw_state;


                /* ============================================
                 * 3. PRESS EVENT
                 *
                 * HIGH -> LOW
                 * ============================================ */

                if ((old_state == BUTTON_RELEASED_LEVEL) &&
                    (stable_state == BUTTON_PRESSED_LEVEL))
                {
                    press_start_time =
                        now;

                    long_press_started =
                        false;

                    next_repeat_time =
                        now + (LONG_PRESS_MS *
                               MICROSECONDS_PER_MILLISECOND);
                }


                /* ============================================
                 * 4. RELEASE EVENT
                 *
                 * LOW -> HIGH
                 * ============================================ */

                else if ((old_state == BUTTON_PRESSED_LEVEL) &&
                         (stable_state == BUTTON_RELEASED_LEVEL))
                {
                    /*
                     * If a long press already started,
                     * release does NOT create a click.
                     */
                    if (long_press_started)
                    {
                        long_press_started =
                            false;
                    }
                    else
                    {
                        /*
                         * This was a short press.
                         *
                         * It may be:
                         *
                         *     single click
                         *
                         * or
                         *
                         *     first half of double click
                         */
                        if (pending_click)
                        {
                            int64_t gap_ms =
                                (now - pending_click_time)
                                / MICROSECONDS_PER_MILLISECOND;

                            if (gap_ms <= DOUBLE_CLICK_MS)
                            {
                                /*
                                 * Second click arrived
                                 * within allowed window.
                                 *
                                 * DOUBLE CLICK -> -1
                                 */
                                counter_decrement();

                                pending_click =
                                    false;
                            }
                            else
                            {
                                /*
                                 * Previous pending click has
                                 * expired.
                                 *
                                 * Commit previous single click.
                                 *
                                 * Current release becomes
                                 * a new pending click.
                                 */
                                counter_increment();

                                pending_click_time =
                                    now;
                            }
                        }
                        else
                        {
                            /*
                             * First click:
                             *
                             * Do not increment yet.
                             *
                             * Wait DOUBLE_CLICK_MS to see
                             * whether another click arrives.
                             */
                            pending_click =
                                true;

                            pending_click_time =
                                now;
                        }
                    }
                }
            }
        }


        /* ====================================================
         * 5. LONG PRESS DETECTION + AUTO REPEAT
         * ==================================================== */

        if (stable_state == BUTTON_PRESSED_LEVEL)
        {
            if (!long_press_started)
            {
                int64_t hold_time_ms =
                    (now - press_start_time)
                    / MICROSECONDS_PER_MILLISECOND;

                if (hold_time_ms >= LONG_PRESS_MS)
                {
                    /*
                     * Long press starts.
                     *
                     * Immediately increment once.
                     */
                    counter_increment();

                    long_press_started =
                        true;

                    next_repeat_time =
                        now + (REPEAT_PERIOD_MS *
                               MICROSECONDS_PER_MILLISECOND);
                }
            }
            else
            {
                /*
                 * Long press already active.
                 *
                 * Repeat every REPEAT_PERIOD_MS.
                 */
                if (now >= next_repeat_time)
                {
                    counter_increment();

                    next_repeat_time +=
                        REPEAT_PERIOD_MS *
                        MICROSECONDS_PER_MILLISECOND;
                }
            }
        }


        /* ====================================================
         * 6. COMMIT PENDING SINGLE CLICK
         *
         * If no second click arrives before the timeout,
         * the pending click becomes a single click.
         * ==================================================== */

        if (pending_click)
        {
            int64_t pending_time_ms =
                (now - pending_click_time)
                / MICROSECONDS_PER_MILLISECOND;

            if (pending_time_ms > DOUBLE_CLICK_MS)
            {
                counter_increment();

                pending_click =
                    false;
            }
        }


        /* ====================================================
         * 7. POLLING PERIOD
         * ==================================================== */

        while (
            (esp_timer_get_time() - now)
            <
            (POLL_PERIOD_MS *
             MICROSECONDS_PER_MILLISECOND)
        )
        {
            /*
             * Busy wait.
             *
             * This is intentionally simple because the
             * assignment is about register-level GPIO
             * polling.
             */
        }
    }
}
# Session 08 - Timer Notes

## 1. esp_timer vs gptimer

`esp_timer` is a software timer. Its callback normally runs in the ESP timer task context, so normal task-level operations such as `ESP_LOGI()` are allowed.

`gptimer` is a hardware general-purpose timer. Its alarm callback runs in interrupt context, so the callback must be short and should only update a variable or flag before returning.

If a high-priority task continuously hogs the CPU, the `esp_timer` task may not get CPU time to execute its callback on schedule. The `gptimer` hardware counter can continue counting independently, but its interrupt callback also needs CPU time to execute, so the callback can be delayed while the CPU is occupied by a higher-priority task.

For a sensor that must be sampled at exactly 1 kHz, `gptimer` is the more appropriate timer because the timing source is hardware-based and can generate an interrupt every 1 ms. The ISR should only capture the sample or set a flag, while the heavier processing can be performed by a normal task.

## 2. Which task caused the watchdog panic?

When the deliberately bad busy-wait is executed, the watchdog can report an idle task as the task that failed to reset the watchdog.

The reason is that the busy-wait keeps the CPU occupied and does not yield to FreeRTOS. The idle task normally needs CPU time to run and perform its watchdog-related activity.

Therefore, the task reported by the watchdog does not necessarily have to be the task containing the infinite or long-running loop. The task that is reported is the task that failed to perform its required watchdog activity within the configured timeout.

In this exercise, the current `app_main` task is subscribed to the Task Watchdog, while the idle tasks are also monitored through `idle_core_mask`. A CPU-hogging task can therefore prevent an idle task from running long enough to satisfy its watchdog requirements.

## 3. The two watchdog fixes

The first fix calls `esp_task_wdt_reset()` repeatedly inside the long-running loop. This prevents the subscribed task from reaching the watchdog timeout, but the task still occupies the CPU continuously.

The second fix calls `vTaskDelay()` periodically inside the loop. This allows the FreeRTOS scheduler to run other tasks and allows the idle task to receive CPU time.

The second fix addresses the underlying problem because the original problem is that the task was hogging the CPU without yielding.

The first fix mainly prevents the watchdog from detecting the long-running operation. It is useful when a long operation genuinely has to continue executing, but it does not solve the CPU starvation caused by continuously occupying the CPU.


# ISR Notes — Exercise 1

## 1. Why must a flag shared between the ISR and a task be `volatile`?

A variable shared between an ISR and a task can change asynchronously from the point of view of the task.

For example:

```c
while (!flag) {
}
```

If `flag` is not declared `volatile`, the compiler is allowed to assume that its value cannot unexpectedly change because there is no normal C statement inside the loop that modifies it.

Therefore, the compiler may read `flag` once, keep the value in a register, and transform the loop into behavior equivalent to continuously testing the cached value. The task may then never observe that the ISR has changed `flag`.

The fact that an ISR can modify the variable does not automatically prevent this compiler optimization. `volatile` tells the compiler that every access to the variable is observable and that it must actually perform the read rather than assuming that the value remains unchanged.

For example:

```c
volatile bool flag;
```

However, `volatile` only controls compiler optimization. It does not by itself provide synchronization or make a compound operation atomic. When possible, using a FreeRTOS primitive such as a queue with `xQueueSendFromISR()` is preferable because it provides a safe communication mechanism between the ISR and the task.

---

## 2. Why are `ESP_LOGI()` and `vTaskDelay()` dangerous inside the ISR?

An ISR runs in interrupt context, not normal task context.

Functions such as `vTaskDelay()` are task-level operations. They may cause the current task to block and therefore cannot be used normally from an ISR.

`ESP_LOGI()` is also inappropriate inside a GPIO ISR because logging can involve relatively expensive operations, locks, buffers, and code that is not safe to execute from interrupt context. Some code called indirectly by a logging function may also reside in flash or otherwise not be ISR-safe.

The important mechanism is that an ISR has strict **interrupt-context and latency constraints**. It must execute quickly and only call functions explicitly designed to be ISR-safe.

For this reason, the GPIO ISR in this exercise performs only three operations:

1. Read the GPIO level.
2. Obtain a timestamp.
3. Send the event to a FreeRTOS queue using `xQueueSendFromISR()`.

The gesture decoding, logging, counter arithmetic and display updates are performed by the normal FreeRTOS task.

---

## 3. Comparison between the polling and interrupt-driven implementations

The interrupt-driven implementation is more responsive because the CPU can react as soon as the GPIO interrupt occurs. In the polling implementation, the button is only detected when the polling loop checks the GPIO.

In the worst case, the polling implementation has to wait approximately one complete polling period before detecting an edge. If the loop checks the GPIO every 10 ms, the additional detection latency can be almost 10 ms.

The interrupt-driven implementation does not have this polling-period limitation. The GPIO hardware detects the edge and invokes the ISR, so the software response begins with the interrupt latency, which is normally much smaller than a polling interval.

The interrupt-driven implementation also uses less CPU while the button is untouched. With polling, the CPU repeatedly executes the button-reading code even when nothing happens. With interrupts, the gesture task can block on the event queue, so there is no continuous button-checking loop consuming CPU cycles.

The polling implementation was easier to get right initially because the program continuously observes the button state and all timing decisions can be made from the same loop. The interrupt-driven implementation requires a clearer separation between the ISR and the task, an event queue, debounce handling, and timeout-based processing for the long press.

The long-press behavior is especially important. While the button is continuously held, there are no new GPIO edges, so the ISR cannot generate the 500 ms repeat events. The gesture task therefore needs a timeout instead of blocking forever. This allows it to periodically wake up and generate the required `+1` events while still receiving the release interrupt immediately.

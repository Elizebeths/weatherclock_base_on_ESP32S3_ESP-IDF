/** ESP32-S3 GPIO abstraction. Pins are signed GPIO numbers, without bank mapping.
 * Invalid SoC pins (including -1 for an unconnected pin) are rejected.
 * Configure directions before level operations; LCD init does this for its pins.
 * Invalid writes/toggles are ignored; invalid reads return false.
 * IRQ attach configures input and the chosen edge, preserving pull resistors.
 * Each GPIO has its own interrupt. Registry capacity is APP_GPIO_IRQ_MAX.
 * Passing NULL callback detaches. ISR service is installed once or reused.
 * Callbacks run in ISR context: do not block or call non-ISR RTOS APIs.
 * Attach/detach are task-only. A callback already in flight may finish after
 * detach, so its context must remain alive until that callback has finished.
 * gpio_port_tick_ms forwards the common monotonic millisecond timer.
 */

#ifndef GPIO_PORT_H
#define GPIO_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "common/port.h"

/* 直接保存 GPIO 编号；使用有符号整数，避免未连接标记 -1 被转换为正数。 */
typedef int gpio_pin_t;

typedef enum {
  GPIO_EDGE_RISING = 0,
  GPIO_EDGE_FALLING,
  GPIO_EDGE_BOTH
} gpio_edge_t;

typedef void (*gpio_irq_cb_t)(gpio_pin_t pin, void *arg);

void gpio_port_write(gpio_pin_t pin, bool level);

bool gpio_port_read(gpio_pin_t pin);

void gpio_port_toggle(gpio_pin_t pin);

port_status_t gpio_port_irq_attach(gpio_pin_t pin, gpio_edge_t edge,
                                   gpio_irq_cb_t cb, void *arg);

void gpio_port_irq_detach(gpio_pin_t pin);

bool gpio_port_irq_is_attached(gpio_pin_t pin);

uint32_t gpio_port_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif

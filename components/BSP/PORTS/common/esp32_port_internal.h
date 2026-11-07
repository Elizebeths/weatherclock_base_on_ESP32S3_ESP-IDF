/* ESP-IDF implementation helpers. Not part of the portable public API. */
#ifndef ESP32_PORT_INTERNAL_H
#define ESP32_PORT_INTERNAL_H

#include <limits.h>
#include "common/os_port.h"
#include "common/port.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio/gpio_port.h"

static inline TickType_t port_ticks(uint32_t ms) {
  if (ms == 0) return 0;
  /* Reserve portMAX_DELAY for internal, explicitly infinite waits. */
  uint64_t ticks = ((uint64_t)ms * configTICK_RATE_HZ + 999) / 1000;
  return ticks >= portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)ticks;
}

static inline port_status_t port_from_esp(esp_err_t err) {
  if (err == ESP_OK) return PORT_OK;
  if (err == ESP_ERR_TIMEOUT) return PORT_TIMEOUT;
  return PORT_ERROR;
}

/* Explicit bus-lock APIs cannot return failure: fail visibly on timeout. */
static inline void port_lock_required(os_mutex_t *lock) {
  os_port_mutex_init(lock);
  APP_ASSERT(os_port_mutex_lock(lock, APP_BUS_LOCK_TIMEOUT_MS));
}

int port_gpio_number(gpio_pin_t pin);
port_status_t port_gpio_output(gpio_pin_t pin, bool initial);

#endif

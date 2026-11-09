#include "common/port.h"
#include <stdlib.h>
#include "common/esp32_port_internal.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

static portMUX_TYPE s_critical_lock = portMUX_INITIALIZER_UNLOCKED;

const char *port_status_str(port_status_t status) {
  switch (status) {
  case PORT_OK:
    return "OK";
  case PORT_TIMEOUT:
    return "TIMEOUT";
  case PORT_ERROR:
    return "ERROR";
  case PORT_BUSY:
    return "BUSY";
  default:
    return "UNKNOWN";
  }
}

uint32_t port_cycles(void) {
#if APP_PORT_DWT_ENABLE
  return esp_cpu_get_cycle_count();
#else
  return 0;
#endif
}

uint32_t port_clk_hz(void) {
  return (uint32_t)esp_clk_cpu_freq();
}

uint32_t port_cycles_per_us(void) {
  return port_clk_hz() / 1000000U;
}

uint32_t port_time_us(void) {
  return (uint32_t)esp_timer_get_time();
}

uint32_t port_time_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void port_delay_us(uint32_t delay_us) {
  if (delay_us)
    esp_rom_delay_us(delay_us);
}

void port_delay_ms(uint32_t delay_ms) {
  if (delay_ms)
    vTaskDelay(port_ticks(delay_ms));
}

void port_enter_critical(void) {
  portENTER_CRITICAL_SAFE(&s_critical_lock);
}

void port_exit_critical(void) {
  portEXIT_CRITICAL_SAFE(&s_critical_lock);
}

void port_system_reset(void) {
  esp_restart();
}

void port_assert_failed(const char *source_file, int source_line) {
  ESP_EARLY_LOGE("port", "Assertion failed: %s:%d", source_file, source_line);
  abort();
}

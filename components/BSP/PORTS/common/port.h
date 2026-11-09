/** ESP32-S3 common port. Public API is independent of ESP-IDF.
 * No explicit initialization is required. Milliseconds/microseconds use esp_timer and
 * wrap at 32 bits. Cycles are per-core CPU cycles; compare on one core with
 * a stable CPU frequency. APP_PORT_DWT_ENABLE retains its historical name.
 * Delay-ms yields to FreeRTOS; delay-us busy waits. Neither is ISR-safe.
 * Critical sections are recursive, multicore spinlocks and ISR-safe; keep
 * them short and exit on the same core. Assert failure invokes ESP-IDF panic.
 * CPU reset restarts the chip. Diagnostic error strings have static lifetime.
 */

#ifndef PORT_H
#define PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef enum {
  PORT_OK = 0,
  PORT_TIMEOUT,
  PORT_ERROR,
  PORT_BUSY
} port_status_t;

const char *port_status_str(port_status_t status);

uint32_t port_cycles(void);

uint32_t port_cycles_per_us(void);

uint32_t port_time_us(void);

uint32_t port_time_ms(void);

void port_delay_ms(uint32_t ms);

void port_delay_us(uint32_t us);

void port_enter_critical(void);

void port_exit_critical(void);

uint32_t port_clk_hz(void);

void port_system_reset(void);

void port_assert_failed(const char *file, int line);

#define APP_STATIC_ASSERT(cond_, tag_)                                         \
  typedef char app_static_assert_##tag_[(cond_) ? 1 : -1]

#define APP_ASSERT(cond_)                                                      \
  do {                                                                         \
    if (!(cond_)) {                                                            \
      port_assert_failed(__FILE__, __LINE__);                                  \
    }                                                                          \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif

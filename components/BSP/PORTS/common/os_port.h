/** ESP32-S3 mutex and delay adapter backed by FreeRTOS.
 * Pass non-NULL, zero-initialized mutexes with application lifetime; do not copy
 * or move them after init. A static pool provides 24 recursive mutexes.
 * Init is idempotent and retains existing state. Pool exhaustion leaves handle
 * NULL; lock reports failure. No mutex heap allocation.
 * Timeout 0 means try immediately; waits are rounded up to an OS tick.
 * APIs are task-only. Unlock requires ownership.
 * Initialize each mutex before use; no global OS setup is required.
 */

#ifndef OS_PORT_H
#define OS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  void *handle;
} os_mutex_t;

void os_port_mutex_init(os_mutex_t *m);

bool os_port_mutex_lock(os_mutex_t *m, uint32_t timeout_ms);

void os_port_mutex_unlock(os_mutex_t *m);

void os_port_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif

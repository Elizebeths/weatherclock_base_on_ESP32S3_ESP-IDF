/** ESP32-S3 OS adapter. ESP-IDF always runs FreeRTOS; APP_OS_FREERTOS
 * selects application task creation versus app_loop polling only. Mutexes and
 * semaphores use real FreeRTOS synchronization in both modes.
 * Pass non-NULL, zero-initialized objects with application lifetime; do not copy
 * or move them after init. Create each task once, before concurrent use.
 * Fixed, static pools: 24 recursive mutexes, 16 counting semaphores, 4 tasks.
 * Mutex/sem init is idempotent and retains existing state. Pool exhaustion leaves handle
 * NULL; lock/take/task_create report failure. No OS-object heap allocation.
 * Timeout 0 means try immediately; waits are rounded up to an OS tick.
 * APIs are task-only except sem_give_isr, which requires ISR context.
 * sem_init requires max > 0 and initial <= max. Unlock requires ownership.
 * task_create stack_words is in 32-bit words (1..1024), converted to IDF bytes.
 * Polling mode only records the entry; RTOS task entries that return suspend.
 * Initialize each mutex or semaphore before use; no global OS setup is required.
 */

#ifndef OS_PORT_H
#define OS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef struct {
  void *handle;
} os_mutex_t;

typedef struct {
  void *handle;
} os_sem_t;

typedef void (*os_task_fn)(void *arg);

typedef struct {
  os_task_fn fn;
  void *arg;
  const char *name;
  void *handle;
} os_task_t;

bool os_port_is_rtos(void);

void os_port_mutex_init(os_mutex_t *m);

bool os_port_mutex_lock(os_mutex_t *m, uint32_t timeout_ms);

void os_port_mutex_unlock(os_mutex_t *m);

void os_port_sem_init(os_sem_t *s, uint32_t initial, uint32_t max);

bool os_port_sem_take(os_sem_t *s, uint32_t timeout_ms);

void os_port_sem_give(os_sem_t *s);

void os_port_sem_give_isr(os_sem_t *s);

bool os_port_task_create(os_task_t *t, os_task_fn fn, const char *name, void *arg,
                         uint32_t stack_words, uint32_t priority);

void os_port_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif

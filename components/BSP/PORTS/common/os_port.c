/* ESP-IDF 互斥锁与延时适配，使用真实的 FreeRTOS 同步机制。
 * 静态资源池包含 24 个递归互斥锁，适用于 ESP32-S3 双核。
 * 对象在应用运行期间持续有效；重复初始化不会重置已有状态。
 */
#include "common/esp32_port_internal.h"
#include "freertos/semphr.h"

static StaticSemaphore_t s_mutex_storage[24];
static unsigned s_mutex_count;

void os_port_mutex_init(os_mutex_t *mutex) {
  port_enter_critical();
  if (!mutex->handle && s_mutex_count < 24) {
    mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&s_mutex_storage[s_mutex_count++]);
  }
  port_exit_critical();
}

bool os_port_mutex_lock(os_mutex_t *mutex, uint32_t timeout_ms) {
  if (!mutex->handle)
    return false;
  return xSemaphoreTakeRecursive(mutex->handle, port_ticks(timeout_ms)) == pdTRUE;
}

void os_port_mutex_unlock(os_mutex_t *mutex) {
  APP_ASSERT(xSemaphoreGiveRecursive(mutex->handle) == pdTRUE);
}

void os_port_delay_ms(uint32_t delay_ms) {
  port_delay_ms(delay_ms);
}

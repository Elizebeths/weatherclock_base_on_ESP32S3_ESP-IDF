/* ESP-IDF 始终运行 FreeRTOS；APP_OS_FREERTOS 仅决定创建应用任务，
 * 还是由 app_loop() 轮询执行应用逻辑。两种模式均使用真实的同步机制，
 * 同样适用于第二个处理器核心。静态资源池包含 24 个互斥锁、16 个信号量、
 * 4 个任务，每个任务的栈容量为 1024 个字。
 * 对象在应用运行期间持续有效；重复初始化不会重置已有状态。
 */
#include "common/esp32_port_internal.h"
#include "freertos/semphr.h"
#include <stddef.h>

static StaticSemaphore_t s_mutex_storage[24], s_semaphore_storage[16];
static unsigned s_mutex_count, s_semaphore_count;
#if APP_OS_FREERTOS
static StaticTask_t s_task_storage[4];
static uint32_t s_task_stacks[4][1024];
static unsigned s_task_count;

static void run_task_entry(void *task_context) {
  os_task_t *task = task_context;
  task->fn(task->arg);
  vTaskSuspend(NULL); /* 静态任务存储在应用运行期间持续保留。 */
}
#endif

bool os_port_is_rtos(void) {
  return APP_OS_FREERTOS != 0;
}

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

void os_port_sem_init(os_sem_t *semaphore, uint32_t initial_count, uint32_t max_count) {
  APP_ASSERT(max_count > 0 && initial_count <= max_count);
  port_enter_critical();
  if (!semaphore->handle && s_semaphore_count < 16) {
    semaphore->handle = xSemaphoreCreateCountingStatic(max_count, initial_count, &s_semaphore_storage[s_semaphore_count++]);
  }
  port_exit_critical();
}

bool os_port_sem_take(os_sem_t *semaphore, uint32_t timeout_ms) {
  if (!semaphore->handle)
    return false;
  return xSemaphoreTake(semaphore->handle, port_ticks(timeout_ms)) == pdTRUE;
}

void os_port_sem_give(os_sem_t *semaphore) {
  if (!semaphore->handle)
    return;
  (void)xSemaphoreGive(semaphore->handle); /* 计数达到上限后不再增加，这是预期行为。 */
}

void os_port_sem_give_isr(os_sem_t *semaphore) {
  if (!semaphore->handle)
    return;
  BaseType_t higher_priority_task_woken = pdFALSE;
  (void)xSemaphoreGiveFromISR(semaphore->handle, &higher_priority_task_woken);
  if (higher_priority_task_woken)
    portYIELD_FROM_ISR();
}

bool os_port_task_create(os_task_t *task, os_task_fn task_function, const char *task_name, void *task_context, uint32_t stack_words, uint32_t priority) {
  APP_ASSERT(task != NULL && task_function != NULL && task->fn == NULL);
#if APP_OS_FREERTOS
  if (!stack_words || stack_words > 1024 || priority >= configMAX_PRIORITIES)
    return false;
  port_enter_critical();
  unsigned task_slot = s_task_count;
  if (task_slot < 4)
    ++s_task_count;
  port_exit_critical();
  if (task_slot >= 4)
    return false;
#else
  (void)stack_words;
  (void)priority;
#endif
  task->fn = task_function;
  task->arg = task_context;
  task->name = task_name;
#if APP_OS_FREERTOS
  /* ESP-IDF 的栈深度以字节为单位，与上游 FreeRTOS 不同。 */
  task->handle = xTaskCreateStatic(run_task_entry, task_name ? task_name : "port", stack_words * sizeof(uint32_t), task, priority, (StackType_t *)s_task_stacks[task_slot], &s_task_storage[task_slot]);
  return task->handle != NULL;
#else
  task->handle = NULL;
  return true;
#endif
}

void os_port_delay_ms(uint32_t delay_ms) {
  port_delay_ms(delay_ms);
}

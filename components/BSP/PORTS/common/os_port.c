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
static os_mutex_t *s_mutex_owners[24];
static os_sem_t *s_semaphore_owners[16];
static unsigned s_mutex_count, s_semaphore_count;
#if APP_OS_FREERTOS
static StaticTask_t s_task_storage[4];
static uint32_t s_task_stacks[4][1024];
static os_task_t *s_task_owners[4];
static unsigned s_task_count;

static void run_task_entry(void *task_context) {
  os_task_t *task = task_context;
  task->fn(task->arg);
  vTaskSuspend(NULL); /* 静态任务存储在应用运行期间持续保留。 */
}
#endif

void os_port_init(void) {
  port_init();
}

bool os_port_is_rtos(void) {
  return APP_OS_FREERTOS != 0;
}

void os_port_mutex_init(os_mutex_t *mutex) {
  if (!mutex)
    return;
  port_enter_critical();
  for (unsigned pool_index = 0; pool_index < s_mutex_count; ++pool_index) {
    if (s_mutex_owners[pool_index] == mutex) {
      port_exit_critical();
      return;
    }
  }
  mutex->handle = NULL;
  mutex->depth = 0;
  if (s_mutex_count < 24) {
    s_mutex_owners[s_mutex_count] = mutex;
    mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&s_mutex_storage[s_mutex_count++]);
  }
  port_exit_critical();
}

bool os_port_mutex_lock(os_mutex_t *mutex, uint32_t timeout_ms) {
  if (!mutex || !mutex->handle || xPortInIsrContext())
    return false;
  if (xSemaphoreTakeRecursive(mutex->handle, port_ticks(timeout_ms)) != pdTRUE)
    return false;
  port_enter_critical();
  ++mutex->depth;
  port_exit_critical();
  return true;
}

void os_port_mutex_unlock(os_mutex_t *mutex) {
  if (!mutex || !mutex->handle || xPortInIsrContext())
    return;
  APP_ASSERT(xSemaphoreGetMutexHolder(mutex->handle) == xTaskGetCurrentTaskHandle());
  port_enter_critical();
  APP_ASSERT(mutex->depth > 0);
  --mutex->depth;
  port_exit_critical();
  APP_ASSERT(xSemaphoreGiveRecursive(mutex->handle) == pdTRUE);
}

void os_port_sem_init(os_sem_t *semaphore, uint32_t initial_count, uint32_t max_count) {
  if (!semaphore)
    return;
  if (!max_count)
    max_count = 1;
  if (initial_count > max_count)
    initial_count = max_count;
  port_enter_critical();
  for (unsigned pool_index = 0; pool_index < s_semaphore_count; ++pool_index) {
    if (s_semaphore_owners[pool_index] == semaphore) {
      port_exit_critical();
      return;
    }
  }
  semaphore->handle = NULL;
  if (s_semaphore_count < 16) {
    semaphore->max = max_count;
    semaphore->count = initial_count;
    s_semaphore_owners[s_semaphore_count] = semaphore;
    semaphore->handle = xSemaphoreCreateCountingStatic(max_count, initial_count, &s_semaphore_storage[s_semaphore_count++]);
  }
  port_exit_critical();
}

bool os_port_sem_take(os_sem_t *semaphore, uint32_t timeout_ms) {
  if (!semaphore || !semaphore->handle || xPortInIsrContext())
    return false;
  bool acquired = xSemaphoreTake(semaphore->handle, port_ticks(timeout_ms)) == pdTRUE;
  port_enter_critical();
  semaphore->count = uxSemaphoreGetCount(semaphore->handle);
  port_exit_critical();
  return acquired;
}

void os_port_sem_give(os_sem_t *semaphore) {
  if (xPortInIsrContext()) {
    os_port_sem_give_isr(semaphore);
    return;
  }
  if (!semaphore || !semaphore->handle)
    return;
  (void)xSemaphoreGive(semaphore->handle); /* 计数达到上限后不再增加，这是预期行为。 */
  port_enter_critical();
  semaphore->count = uxSemaphoreGetCount(semaphore->handle);
  port_exit_critical();
}

void os_port_sem_give_isr(os_sem_t *semaphore) {
  /* SPI 同步传输可能在任务上下文中调用完成回调。 */
  if (!xPortInIsrContext()) {
    os_port_sem_give(semaphore);
    return;
  }
  if (!semaphore || !semaphore->handle)
    return;
  BaseType_t higher_priority_task_woken = pdFALSE;
  (void)xSemaphoreGiveFromISR(semaphore->handle, &higher_priority_task_woken);
  port_enter_critical();
  semaphore->count = uxSemaphoreGetCountFromISR(semaphore->handle);
  port_exit_critical();
  if (higher_priority_task_woken)
    portYIELD_FROM_ISR();
}

bool os_port_task_create(os_task_t *task, os_task_fn task_function, const char *task_name, void *task_context, uint32_t stack_words, uint32_t priority) {
  if (!task || !task_function)
    return false;
#if APP_OS_FREERTOS
  if (!stack_words || stack_words > 1024 || priority >= configMAX_PRIORITIES)
    return false;
  port_enter_critical();
  for (unsigned pool_index = 0; pool_index < s_task_count; ++pool_index) {
    if (s_task_owners[pool_index] == task) {
      bool matches_existing_task = task->handle && task->fn == task_function && task->arg == task_context;
      port_exit_critical();
      return matches_existing_task;
    }
  }
  unsigned task_slot = s_task_count;
  if (task_slot < 4) {
    s_task_owners[task_slot] = task;
    ++s_task_count;
  }
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

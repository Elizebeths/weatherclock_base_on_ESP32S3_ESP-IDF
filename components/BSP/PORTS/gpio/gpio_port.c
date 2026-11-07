#include "gpio/gpio_port.h"
#include "common/esp32_port_internal.h"
#include <stddef.h>

typedef struct {
  gpio_pin_t pin;
  gpio_irq_cb_t callback;
  void *callback_context;
} gpio_irq_slot_t;

static gpio_irq_slot_t s_irq_slots[APP_GPIO_IRQ_MAX];
static os_mutex_t s_irq_mutex;
static bool s_isr_service_ready;

int port_gpio_number(gpio_pin_t pin) {
  return GPIO_IS_VALID_GPIO(pin) ? pin : -1;
}

port_status_t port_gpio_output(gpio_pin_t pin, bool initial_level) {
  int gpio_number = port_gpio_number(pin);
  if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_number))
    return PORT_ERROR;
  esp_err_t esp_error = gpio_set_level(gpio_number, initial_level);
  if (esp_error == ESP_OK)
    esp_error = gpio_set_direction(gpio_number, GPIO_MODE_INPUT_OUTPUT);
  return port_from_esp(esp_error);
}

void gpio_port_write(gpio_pin_t pin, bool level) {
  int gpio_number = port_gpio_number(pin);
  if (GPIO_IS_VALID_OUTPUT_GPIO(gpio_number))
    (void)gpio_set_level(gpio_number, level);
}

bool gpio_port_read(gpio_pin_t pin) {
  int gpio_number = port_gpio_number(pin);
  return gpio_number >= 0 && gpio_get_level(gpio_number) != 0;
}

void gpio_port_toggle(gpio_pin_t pin) {
  port_enter_critical();
  gpio_port_write(pin, !gpio_port_read(pin));
  port_exit_critical();
}

static void dispatch_gpio_interrupt(void *slot_context) {
  gpio_irq_slot_t *irq_slot = slot_context;
  port_enter_critical();
  gpio_irq_cb_t callback = irq_slot->callback;
  void *callback_context = irq_slot->callback_context;
  gpio_pin_t pin = irq_slot->pin;
  port_exit_critical();
  if (callback)
    callback(pin, callback_context);
}

static gpio_irq_slot_t *find_irq_slot(gpio_pin_t pin) {
  for (unsigned slot_index = 0; slot_index < APP_GPIO_IRQ_MAX; ++slot_index)
    if (s_irq_slots[slot_index].callback && s_irq_slots[slot_index].pin == pin)
      return &s_irq_slots[slot_index];
  return NULL;
}

port_status_t gpio_port_irq_attach(gpio_pin_t pin, gpio_edge_t edge, gpio_irq_cb_t callback, void *callback_context) {
  if (!callback) {
    gpio_port_irq_detach(pin);
    return PORT_OK;
  }
  int gpio_number = port_gpio_number(pin);
  if (gpio_number < 0 || edge > GPIO_EDGE_BOTH || (int)edge < 0 || xPortInIsrContext())
    return PORT_ERROR;
  port_lock_required(&s_irq_mutex);
  esp_err_t esp_error = ESP_OK;
  if (!s_isr_service_ready) {
    esp_error = gpio_install_isr_service(0);
    /* ESP_ERR_INVALID_STATE 表示其他组件已安装中断服务。 */
    if (esp_error == ESP_OK || esp_error == ESP_ERR_INVALID_STATE)
      s_isr_service_ready = true;
  }
  if (!s_isr_service_ready) {
    os_port_mutex_unlock(&s_irq_mutex);
    return PORT_ERROR;
  }
  gpio_irq_slot_t *irq_slot = find_irq_slot(pin);
  if (irq_slot) {
    (void)gpio_intr_disable(gpio_number);
    (void)gpio_isr_handler_remove(gpio_number);
  } else {
    for (unsigned slot_index = 0; slot_index < APP_GPIO_IRQ_MAX; ++slot_index)
      if (!s_irq_slots[slot_index].callback) {
        irq_slot = &s_irq_slots[slot_index];
        break;
      }
  }
  if (!irq_slot) {
    os_port_mutex_unlock(&s_irq_mutex);
    return PORT_BUSY;
  }
  port_enter_critical();
  irq_slot->pin = pin;
  irq_slot->callback = callback;
  irq_slot->callback_context = callback_context;
  port_exit_critical();
  esp_error = gpio_set_direction(gpio_number, GPIO_MODE_INPUT);
  gpio_int_type_t interrupt_type = edge == GPIO_EDGE_RISING ? GPIO_INTR_POSEDGE : edge == GPIO_EDGE_FALLING ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE;
  if (esp_error == ESP_OK)
    esp_error = gpio_set_intr_type(gpio_number, interrupt_type);
  if (esp_error == ESP_OK)
    esp_error = gpio_isr_handler_add(gpio_number, dispatch_gpio_interrupt, irq_slot);
  if (esp_error == ESP_OK)
    esp_error = gpio_intr_enable(gpio_number);
  if (esp_error != ESP_OK) {
    (void)gpio_intr_disable(gpio_number);
    (void)gpio_isr_handler_remove(gpio_number);
    port_enter_critical();
    irq_slot->callback = NULL;
    port_exit_critical();
  }
  os_port_mutex_unlock(&s_irq_mutex);
  return port_from_esp(esp_error);
}

void gpio_port_irq_detach(gpio_pin_t pin) {
  int gpio_number = port_gpio_number(pin);
  if (gpio_number < 0 || xPortInIsrContext())
    return;
  port_lock_required(&s_irq_mutex);
  gpio_irq_slot_t *irq_slot = find_irq_slot(pin);
  if (irq_slot) {
    (void)gpio_intr_disable(gpio_number);
    (void)gpio_isr_handler_remove(gpio_number);
    port_enter_critical();
    irq_slot->callback = NULL;
    irq_slot->callback_context = NULL;
    port_exit_critical();
  }
  os_port_mutex_unlock(&s_irq_mutex);
}

bool gpio_port_irq_is_attached(gpio_pin_t pin) {
  port_enter_critical();
  bool attached = find_irq_slot(pin) != NULL;
  port_exit_critical();
  return attached;
}

uint32_t gpio_port_tick_ms(void) {
  return port_time_ms();
}

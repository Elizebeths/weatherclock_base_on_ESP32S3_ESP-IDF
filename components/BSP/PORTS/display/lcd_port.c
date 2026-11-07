#include "display/lcd_port.h"
#include "board.h"
#include "bus/spi_port.h"
#include "common/esp32_port_internal.h"

static os_mutex_t s_display_mutex;
static bool s_display_initialized, s_bus_error;

void lcd_port_pin_write(lcd_port_pin_t pin, bool level) {
  switch (pin) {
  case LCD_PIN_CS:
    gpio_port_write(BOARD_TFT_CS_GPIO, level);
    break;
  case LCD_PIN_DC:
    gpio_port_write(BOARD_TFT_DC_GPIO, level);
    break;
  case LCD_PIN_RST:
    gpio_port_write(BOARD_TFT_RST_GPIO, level);
    break;
  case LCD_PIN_BL:
    gpio_port_write(BOARD_TFT_BL_GPIO, level == (BOARD_TFT_BL_ACTIVE_HIGH != 0));
    break;
  default:
    break;
  }
}

port_status_t lcd_port_init(void) {
  os_port_mutex_init(&s_display_mutex);
  if (!os_port_mutex_lock(&s_display_mutex, APP_BUS_LOCK_TIMEOUT_MS))
    return PORT_BUSY;
  port_status_t status = PORT_OK;
  if (!s_display_initialized) {
    status = port_gpio_output(BOARD_TFT_CS_GPIO, true);
    if (status == PORT_OK)
      status = port_gpio_output(BOARD_TFT_DC_GPIO, true);
    if (status == PORT_OK)
      status = port_gpio_output(BOARD_TFT_RST_GPIO, true);
    if (status == PORT_OK)
      status = port_gpio_output(BOARD_TFT_BL_GPIO, !BOARD_TFT_BL_ACTIVE_HIGH);
    if (status == PORT_OK)
      status = spi_port_init();
    s_display_initialized = status == PORT_OK;
  }
  os_port_mutex_unlock(&s_display_mutex);
  return status;
}

static void finish_display_transfer(port_status_t status) {
  if (status != PORT_OK && status != PORT_BUSY) {
    port_enter_critical();
    s_bus_error = true;
    port_exit_critical();
  }
  if (spi_port_busy())
    (void)spi_port_abort();
  /* 硬件可能仍在发送数据时，不释放片选信号。 */
  if (!spi_port_busy()) {
    lcd_port_pin_write(LCD_PIN_CS, true);
    lcd_port_pin_write(LCD_PIN_DC, true);
  }
}

/* 显示锁覆盖整个片选和数据/命令控制过程。总线使用递归锁，
 * 避免命令及参数传输被存储设备的总线访问打断。 */
static port_status_t begin_display_session(void) {
  if (!s_display_initialized || lcd_port_is_bus_error())
    return PORT_ERROR;
  if (!os_port_mutex_lock(&s_display_mutex, APP_SPI_LCD_TIMEOUT_MS))
    return PORT_BUSY;
  /* 等待上一轮显示操作结束后，再次检查总线错误状态。 */
  if (lcd_port_is_bus_error()) {
    os_port_mutex_unlock(&s_display_mutex);
    return PORT_ERROR;
  }
  spi_port_lock();
  if (spi_port_busy()) {
    spi_port_unlock();
    os_port_mutex_unlock(&s_display_mutex);
    return PORT_BUSY;
  }
  lcd_port_pin_write(LCD_PIN_CS, false);
  return PORT_OK;
}

static bool end_display_session(port_status_t status) {
  finish_display_transfer(status);
  bool transfer_complete = !spi_port_busy();
  spi_port_unlock();
  os_port_mutex_unlock(&s_display_mutex);
  return transfer_complete;
}

port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t length) {
  if (!data || !length)
    return PORT_ERROR;
  port_status_t status = begin_display_session();
  if (status != PORT_OK)
    return status;
  status = spi_port_tx(data, length, APP_SPI_LCD_TIMEOUT_MS);
  end_display_session(status);
  return status;
}

port_status_t lcd_port_cmd_dat(uint8_t command, const uint8_t *parameters, uint16_t length) {
  if (length && !parameters)
    return PORT_ERROR;
  port_status_t status = begin_display_session();
  if (status != PORT_OK)
    return status;
  lcd_port_pin_write(LCD_PIN_DC, false);
  status = spi_port_tx(&command, 1, APP_SPI_LCD_TIMEOUT_MS);
  lcd_port_pin_write(LCD_PIN_DC, true);
  if (status == PORT_OK && length)
    status = spi_port_tx(parameters, length, APP_SPI_LCD_TIMEOUT_MS);
  end_display_session(status);
  return status;
}

port_status_t lcd_port_spi_tx_dma(const uint8_t *data, uint32_t length, lcd_port_done_cb_t callback, void *callback_context) {
  if (!data || !length || length > APP_SPI_LCD_DMA_MAX_BYTES || length > UINT16_MAX)
    return PORT_ERROR;
  port_status_t status = begin_display_session();
  if (status != PORT_OK)
    return status;
  status = spi_port_tx(data, length, APP_SPI_LCD_TIMEOUT_MS);
  bool transfer_complete = end_display_session(status);
  /* 先释放锁和片选，再调用可能再次发起传输的回调。 */
  if (transfer_complete && callback)
    callback(status, callback_context);
  return status;
}

bool lcd_port_spi_busy(void) {
  return spi_port_busy();
}

void lcd_port_spi_abort(void) {
  port_lock_required(&s_display_mutex);
  spi_port_lock();
  if (spi_port_abort() == PORT_OK) {
    lcd_port_pin_write(LCD_PIN_CS, true);
    lcd_port_pin_write(LCD_PIN_DC, true);
  }
  spi_port_unlock();
  os_port_mutex_unlock(&s_display_mutex);
}

void lcd_port_delay_ms(uint32_t delay_ms) {
  port_delay_ms(delay_ms);
}

void lcd_port_enter_critical(void) {
  port_enter_critical();
}

void lcd_port_exit_critical(void) {
  port_exit_critical();
}

uint32_t lcd_port_cycles(void) {
  return port_cycles();
}

bool lcd_port_is_bus_error(void) {
  port_enter_critical();
  bool bus_error = s_bus_error;
  port_exit_critical();
  return bus_error;
}

void lcd_port_clear_bus_error(void) {
  port_lock_required(&s_display_mutex);
  if (!spi_port_busy()) {
    port_enter_critical();
    s_bus_error = false;
    port_exit_critical();
  }
  os_port_mutex_unlock(&s_display_mutex);
}

#include "display/lcd_port.h"
#include "board.h"
#include "bus/spi_port.h"
#include "common/esp32_port_internal.h"

static os_mutex_t s_display_mutex;
static bool s_display_initialized;

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

/* 显示锁覆盖整个片选和数据/命令控制过程。总线使用递归锁，
 * 避免命令及参数传输被存储设备的总线访问打断。 */
static port_status_t begin_display_session(void) {
  if (!s_display_initialized)
    return PORT_ERROR;
  if (!os_port_mutex_lock(&s_display_mutex, APP_SPI_LCD_TIMEOUT_MS))
    return PORT_BUSY;
  spi_port_lock();
  port_status_t status = spi_port_wait_idle(APP_SPI_LCD_TIMEOUT_MS);
  if (status != PORT_OK) {
    spi_port_unlock();
    os_port_mutex_unlock(&s_display_mutex);
    return status;
  }
  /* 上次超时可能保留了 CS/DC；事务完成后先结束旧会话，再开始新会话。 */
  lcd_port_pin_write(LCD_PIN_CS, true);
  lcd_port_pin_write(LCD_PIN_DC, true);
  lcd_port_pin_write(LCD_PIN_CS, false);
  return PORT_OK;
}

static void end_display_session(void) {
  if (spi_port_wait_idle(APP_SPI_LCD_TIMEOUT_MS) == PORT_OK) {
    lcd_port_pin_write(LCD_PIN_CS, true);
    lcd_port_pin_write(LCD_PIN_DC, true);
  }
  spi_port_unlock();
  os_port_mutex_unlock(&s_display_mutex);
}

port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t length) {
  if (!data || !length)
    return PORT_ERROR;
  port_status_t status = begin_display_session();
  if (status != PORT_OK)
    return status;
  status = spi_port_tx(data, length, APP_SPI_LCD_TIMEOUT_MS);
  end_display_session();
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
  if (status == PORT_OK) {
    lcd_port_pin_write(LCD_PIN_DC, true);
    if (length)
      status = spi_port_tx(parameters, length, APP_SPI_LCD_TIMEOUT_MS);
  }
  end_display_session();
  return status;
}

port_status_t lcd_port_spi_tx_pixels(const uint8_t *data, uint32_t length) {
  if (!data || !length || length > APP_SPI_LCD_DMA_MAX_BYTES || length > UINT16_MAX)
    return PORT_ERROR;
  return lcd_port_spi_tx(data, (uint16_t)length);
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

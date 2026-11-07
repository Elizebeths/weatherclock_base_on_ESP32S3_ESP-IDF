/** ESP32-S3 display port. init configures CS/DC/RST high, backlight off,
 * and initializes SPI once. Backlight true means on, with board polarity applied.
 * SPI calls require init and nonempty data; cmd_dat permits len=0 with dat=NULL.
 * One recursive display lock and bus lock cover each complete CS/DC session.
 * cmd_dat holds CS low across command and parameters; DC returns to data mode.
 * The DMA entry uses the documented synchronous fallback; completion callbacks
 * run in task context after releasing locks and CS, and may submit another block.
 * Validation or busy rejection does not trigger cb. A hardware timeout retains
 * CS while still busy; recovery abort drains the transaction before releasing CS.
 * If abort cannot finish, busy remains true and no completion callback fires.
 * Hardware failures latch bus_error; future transfers immediately fail until
 * clear_bus_error is called with no active transfer. Busy/invalid arguments do
 * not latch hardware failure. All transfer/recovery operations are task-only.
 * The delay/cycle/critical helpers forward to common/port.h.
 */

#ifndef LCD_PORT_H
#define LCD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "common/port.h"

typedef enum {
  LCD_PIN_CS = 0,
  LCD_PIN_DC,
  LCD_PIN_RST,
  LCD_PIN_BL,
  LCD_PIN_MAX
} lcd_port_pin_t;

typedef void (*lcd_port_done_cb_t)(port_status_t status, void *arg);

port_status_t lcd_port_init(void);

port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t len);

port_status_t lcd_port_cmd_dat(uint8_t cmd, const uint8_t *dat, uint16_t len);

port_status_t lcd_port_spi_tx_dma(const uint8_t *data, uint32_t len,
                                  lcd_port_done_cb_t cb, void *arg);

bool lcd_port_spi_busy(void);

void lcd_port_spi_abort(void);

void lcd_port_pin_write(lcd_port_pin_t pin, bool level);

void lcd_port_delay_ms(uint32_t ms);

void lcd_port_enter_critical(void);

void lcd_port_exit_critical(void);

uint32_t lcd_port_cycles(void);

bool lcd_port_is_bus_error(void);

void lcd_port_clear_bus_error(void);

#ifdef __cplusplus
}
#endif

#endif

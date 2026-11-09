/** ESP32-S3 display port. init configures CS/DC/RST high, backlight off,
 * and initializes SPI once. Backlight true means on, with board polarity applied.
 * SPI calls require init and nonempty data; cmd_dat permits len=0 with dat=NULL.
 * One recursive display lock and bus lock cover each complete CS/DC session.
 * cmd_dat holds CS low across command and parameters; DC returns to data mode.
 * Pixel blocks are sent synchronously and report completion via the return value.
 * After a timeout, CS/DC are retained until SPI confirms completion. Session
 * cleanup and the next session both wait with a finite timeout before changing
 * these pins. Errors are returned per call; later calls can try again.
 * All transfer operations are task-only.
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

port_status_t lcd_port_init(void);

port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t len);

port_status_t lcd_port_cmd_dat(uint8_t cmd, const uint8_t *dat, uint16_t len);

port_status_t lcd_port_spi_tx_pixels(const uint8_t *data, uint32_t len);

void lcd_port_pin_write(lcd_port_pin_t pin, bool level);

void lcd_port_delay_ms(uint32_t ms);

void lcd_port_enter_critical(void);

void lcd_port_exit_critical(void);

uint32_t lcd_port_cycles(void);

#ifdef __cplusplus
}
#endif

#endif

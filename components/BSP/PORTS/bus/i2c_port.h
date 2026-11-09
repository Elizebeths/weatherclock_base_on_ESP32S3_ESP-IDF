/** ESP32-S3 I2C master, using the ESP-IDF master driver.
 * init creates the configured controller once; clk_hz=0 selects the configured
 * default, subsequent nonzero clock mismatches fail. SCL is at most 400 kHz.
 * All addresses are 7-bit.
 * write/read buffers and lengths must be nonzero; timeout is 1..INT_MAX ms.
 * write_read and mem_read use repeated START in one transaction. A sensor's
 * conversion delay must be handled as separate write / delay / read calls.
 * mem_write uses one transaction with an 8-bit or big-endian 16-bit register
 * prefix; 8-bit mode rejects registers greater than 255.
 * Transfers share a recursive task lock. APIs return PORT_BUSY on lock timeout.
 * Explicit void lock() asserts on timeout; unlock() requires ownership.
 * Probe maps NACK to PORT_TIMEOUT, and tries up to trials times.
 * Scan stops when found[] is full; returns the stored count.
 * All bus operations are task-only. Initialize before concurrent use.
 */

#ifndef I2C_PORT_H
#define I2C_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "common/port.h"

port_status_t i2c_port_init(uint32_t clk_hz);

bool i2c_port_is_ready(void);

port_status_t i2c_port_write(uint8_t addr7, const uint8_t *buf, uint16_t len,
                             uint32_t timeout_ms);

port_status_t i2c_port_read(uint8_t addr7, uint8_t *buf, uint16_t len,
                            uint32_t timeout_ms);

port_status_t i2c_port_write_read(uint8_t addr7, const uint8_t *wbuf,
                                  uint16_t wlen, uint8_t *rbuf, uint16_t rlen,
                                  uint32_t timeout_ms);

typedef enum {
  I2C_PORT_MEMADDR_8BIT = 0,
  I2C_PORT_MEMADDR_16BIT
} i2c_port_memaddr_size_t;

port_status_t i2c_port_mem_write(uint8_t addr7, uint16_t mem_addr,
                                 i2c_port_memaddr_size_t mem_addr_size,
                                 const uint8_t *buf, uint16_t len,
                                 uint32_t timeout_ms);

port_status_t i2c_port_mem_read(uint8_t addr7, uint16_t mem_addr,
                                i2c_port_memaddr_size_t mem_addr_size,
                                uint8_t *buf, uint16_t len,
                                uint32_t timeout_ms);

port_status_t i2c_port_probe(uint8_t addr7, uint32_t trials, uint32_t timeout_ms);

uint8_t i2c_port_scan(uint8_t *found, uint8_t max_count);

void i2c_port_lock(void);

void i2c_port_unlock(void);

uint8_t i2c_port_bus_index(void);

uint32_t i2c_port_clock_hz(void);

#ifdef __cplusplus
}
#endif

#endif

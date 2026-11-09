/** ESP32-S3 SPI master. init owns the configured GPSPI bus and creates
 * one mode-0, 8-bit, MSB-first device, with software CS handled by the caller.
 * Transfers complete synchronously, with DMA disabled.
 * Transfers use persistent 64-byte FIFO bounce buffers, additionally capped by
 * APP_SPI_LCD_CHUNK_BYTES. Each queue/completion wait has a finite timeout.
 * A timeout can leave a transaction owned by the driver. The next transfer
 * first waits for that transaction; live descriptors and buffers are not reused.
 * wait_idle returns PORT_OK only when no transaction remains owned by the driver.
 * Call it under the bus lock before changing CS/DC after a failed transfer.
 * txrx requires at least one buffer; rx requires a configured MISO pin.
 * Data length must be nonzero.
 * timeout_ms must be finite and nonzero. Recursive locks are task-only; explicit
 * void lock() asserts on timeout.
 * Initialize once before concurrent use. CS is never changed by this bus layer.
 */

#ifndef SPI_PORT_H
#define SPI_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "common/port.h"

port_status_t spi_port_init(void);

bool spi_port_is_ready(void);

port_status_t spi_port_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

port_status_t spi_port_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout_ms);

port_status_t spi_port_wait_idle(uint32_t timeout_ms);

void spi_port_lock(void);

void spi_port_unlock(void);

uint32_t spi_port_clock_hz(void);

#ifdef __cplusplus
}
#endif

#endif

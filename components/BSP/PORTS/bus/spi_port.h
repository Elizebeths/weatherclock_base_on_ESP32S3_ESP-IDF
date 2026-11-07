/** ESP32-S3 SPI master. init owns the configured GPSPI bus and creates
 * one mode-0, 8-bit, MSB-first device, with software CS handled by the caller.
 * This implementation uses the documented synchronous DMA fallback, with DMA
 * disabled. tx_dma completes synchronously and calls cb before returning for
 * completed transfers. Validation/busy rejection does not call cb.
 * Transfers use persistent 64-byte FIFO bounce buffers, additionally capped by
 * APP_SPI_LCD_CHUNK_BYTES. Each queue/completion wait has a finite timeout.
 * A timeout can leave a transaction owned by the driver: busy stays true until
 * abort() drains it. abort may itself time out and does not forcibly reset a
 * shared peripheral; it never reuses a live descriptor or caller buffer.
 * A still-pending timeout does not call cb. The caller must recover with abort.
 * txrx requires at least one buffer; rx requires a configured MISO pin.
 * Data length must be nonzero; DMA entry additionally enforces its size limit.
 * timeout_ms must be finite and nonzero. Recursive locks are task-only; explicit
 * void lock() asserts on timeout. last_error is the last raw ESP-IDF error.
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

typedef void (*spi_port_done_cb_t)(port_status_t status, void *arg);

port_status_t spi_port_init(void);

bool spi_port_is_ready(void);

port_status_t spi_port_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

port_status_t spi_port_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout_ms);

port_status_t spi_port_tx_dma(const uint8_t *data, uint32_t len,
                              spi_port_done_cb_t cb, void *arg);

bool spi_port_busy(void);

port_status_t spi_port_abort(void);

void spi_port_lock(void);

void spi_port_unlock(void);

uint32_t spi_port_lock_depth(void);

uint32_t spi_port_clock_hz(void);

uint32_t spi_port_last_error(void);

#ifdef __cplusplus
}
#endif

#endif

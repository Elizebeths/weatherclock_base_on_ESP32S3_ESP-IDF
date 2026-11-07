/* 使用有超时限制的同步传输，明确禁用 ESP-IDF DMA。
 * 常驻的 64 字节中转缓冲区使超时后的传输不依赖调用方缓冲区的生命周期。
 * 未完成的事务保持忙状态，直到中止操作取回事务结果；
 * 驱动持有事务描述符期间，不得复用该描述符。
 */
#include "bus/spi_port.h"
#include "board.h"
#include "common/esp32_port_internal.h"
#include "driver/spi_master.h"
#include <string.h>

static spi_device_handle_t s_device_handle;
static os_mutex_t s_bus_mutex;
static spi_transaction_t s_transaction;
static uint8_t s_tx_buffer[64], s_rx_buffer[64];
static bool s_transaction_pending, s_transfer_busy;
static uint32_t s_last_error;
static int s_actual_clock_khz;

static port_status_t record_spi_result(esp_err_t esp_error) {
  if (esp_error != ESP_OK)
    s_last_error = (uint32_t)esp_error;
  return port_from_esp(esp_error);
}

static void set_transfer_busy(bool busy) {
  port_enter_critical();
  s_transfer_busy = busy;
  port_exit_critical();
}

port_status_t spi_port_init(void) {
  os_port_mutex_init(&s_bus_mutex);
  if (!os_port_mutex_lock(&s_bus_mutex, APP_BUS_LOCK_TIMEOUT_MS))
    return PORT_BUSY;
  esp_err_t esp_error = ESP_OK;
  if (!s_device_handle) {
    int sck_gpio = BOARD_TFT_SCK_GPIO, mosi_gpio = BOARD_TFT_MOSI_GPIO;
    int miso_gpio = BOARD_TFT_MISO_GPIO;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(sck_gpio) || !GPIO_IS_VALID_OUTPUT_GPIO(mosi_gpio) || sck_gpio == mosi_gpio || (miso_gpio != -1 && (!GPIO_IS_VALID_GPIO(miso_gpio) || miso_gpio == sck_gpio || miso_gpio == mosi_gpio))) {
      os_port_mutex_unlock(&s_bus_mutex);
      return PORT_ERROR;
    }
    spi_bus_config_t bus_config = {
      .mosi_io_num = mosi_gpio,
      .miso_io_num = miso_gpio,
      .sclk_io_num = sck_gpio,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = sizeof(s_tx_buffer)
    };
    esp_error = spi_bus_initialize(BOARD_SPI_LCD_INDEX, &bus_config, SPI_DMA_DISABLED);
    if (esp_error == ESP_OK) {
      spi_device_interface_config_t device_config = {
        .clock_speed_hz = APP_SPI_LCD_CLK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1
      };
      esp_error = spi_bus_add_device(BOARD_SPI_LCD_INDEX, &device_config, &s_device_handle);
      if (esp_error != ESP_OK)
        (void)spi_bus_free(BOARD_SPI_LCD_INDEX);
      else
        (void)spi_device_get_actual_freq(s_device_handle, &s_actual_clock_khz);
    }
  }
  port_status_t status = record_spi_result(esp_error);
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

bool spi_port_is_ready(void) {
  return s_device_handle != NULL;
}

port_status_t spi_port_txrx(const uint8_t *tx_data, uint8_t *rx_data, uint16_t transfer_length, uint32_t timeout_ms) {
  if (!s_device_handle || (!tx_data && !rx_data) || !transfer_length || !timeout_ms || timeout_ms == UINT32_MAX || (rx_data && BOARD_TFT_MISO_GPIO < 0))
    return PORT_ERROR;
  if (!os_port_mutex_lock(&s_bus_mutex, timeout_ms))
    return PORT_BUSY;
  if (s_transaction_pending) {
    os_port_mutex_unlock(&s_bus_mutex);
    return PORT_BUSY;
  }
  set_transfer_busy(true);
  esp_err_t esp_error = ESP_OK;
  unsigned transferred_bytes = 0;
  while (transferred_bytes < transfer_length) {
    unsigned chunk_length = transfer_length - transferred_bytes;
    if (chunk_length > sizeof(s_tx_buffer))
      chunk_length = sizeof(s_tx_buffer);
    if (chunk_length > APP_SPI_LCD_CHUNK_BYTES)
      chunk_length = APP_SPI_LCD_CHUNK_BYTES;
    APP_ASSERT(chunk_length > 0);
    if (tx_data)
      memcpy(s_tx_buffer, tx_data + transferred_bytes, chunk_length);
    else
      memset(s_tx_buffer, 0xff, chunk_length);
    memset(&s_transaction, 0, sizeof(s_transaction));
    s_transaction.length = chunk_length * 8;
    s_transaction.tx_buffer = s_tx_buffer;
    s_transaction.rx_buffer = rx_data ? s_rx_buffer : NULL;
    esp_error = spi_device_queue_trans(s_device_handle, &s_transaction, port_ticks(timeout_ms));
    if (esp_error != ESP_OK)
      break;
    s_transaction_pending = true;
    spi_transaction_t *completed_transaction;
    esp_error = spi_device_get_trans_result(s_device_handle, &completed_transaction, port_ticks(timeout_ms));
    if (esp_error != ESP_OK)
      break;
    s_transaction_pending = false;
    if (rx_data)
      memcpy(rx_data + transferred_bytes, s_rx_buffer, chunk_length);
    transferred_bytes += chunk_length;
  }
  set_transfer_busy(s_transaction_pending);
  port_status_t status = record_spi_result(esp_error);
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

port_status_t spi_port_tx(const uint8_t *data, uint16_t length, uint32_t timeout_ms) {
  return spi_port_txrx(data, NULL, length, timeout_ms);
}

port_status_t spi_port_tx_dma(const uint8_t *data, uint32_t length, spi_port_done_cb_t callback, void *callback_context) {
  if (!data || !length || length > APP_SPI_LCD_DMA_MAX_BYTES || length > UINT16_MAX)
    return PORT_ERROR;
  os_port_mutex_init(&s_bus_mutex);
  if (!os_port_mutex_lock(&s_bus_mutex, APP_SPI_LCD_TIMEOUT_MS))
    return PORT_BUSY;
  port_status_t status = spi_port_tx(data, length, APP_SPI_LCD_TIMEOUT_MS);
  bool transfer_complete = !s_transaction_pending;
  os_port_mutex_unlock(&s_bus_mutex);
  if (status != PORT_BUSY && transfer_complete && callback)
    callback(status, callback_context);
  return status;
}

bool spi_port_busy(void) {
  port_enter_critical();
  bool busy = s_transfer_busy;
  port_exit_critical();
  return busy;
}

port_status_t spi_port_abort(void) {
  os_port_mutex_init(&s_bus_mutex);
  if (!os_port_mutex_lock(&s_bus_mutex, APP_SPI_LCD_TIMEOUT_MS))
    return PORT_BUSY;
  esp_err_t esp_error = ESP_OK;
  if (s_transaction_pending) {
    spi_transaction_t *completed_transaction;
    esp_error = spi_device_get_trans_result(s_device_handle, &completed_transaction, port_ticks(APP_SPI_LCD_TIMEOUT_MS));
    if (esp_error == ESP_OK)
      s_transaction_pending = false;
  }
  set_transfer_busy(s_transaction_pending);
  port_status_t status = record_spi_result(esp_error);
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

void spi_port_lock(void) {
  port_lock_required(&s_bus_mutex);
}

void spi_port_unlock(void) {
  os_port_mutex_unlock(&s_bus_mutex);
}

uint32_t spi_port_lock_depth(void) {
  port_enter_critical();
  uint32_t lock_depth = s_bus_mutex.depth;
  port_exit_critical();
  return lock_depth;
}

uint32_t spi_port_clock_hz(void) {
  return s_device_handle ? (uint32_t)s_actual_clock_khz * 1000 : 0;
}

uint32_t spi_port_last_error(void) {
  return s_last_error;
}

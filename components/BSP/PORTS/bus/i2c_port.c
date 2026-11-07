#include "bus/i2c_port.h"
#include "board.h"
#include "common/esp32_port_internal.h"
#include "driver/i2c_master.h"

static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_device_handles[128];
static os_mutex_t s_bus_mutex;
static uint32_t s_clock_hz, s_last_error;

static port_status_t record_i2c_result(esp_err_t esp_error) {
  if (esp_error != ESP_OK)
    s_last_error = (uint32_t)esp_error;
  return port_from_esp(esp_error);
}

port_status_t i2c_port_init(uint32_t clock_hz) {
  if (clock_hz > 400000U)
    return PORT_ERROR;
  os_port_mutex_init(&s_bus_mutex);
  if (!os_port_mutex_lock(&s_bus_mutex, APP_BUS_LOCK_TIMEOUT_MS))
    return PORT_BUSY;

  port_status_t status;

  if (s_bus_handle)
    status = !clock_hz || clock_hz == s_clock_hz ? PORT_OK : PORT_ERROR;
  else {
    int scl_gpio = port_gpio_number(BOARD_I2C_SCL_GPIO), sda_gpio = port_gpio_number(BOARD_I2C_SDA_GPIO);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(scl_gpio) || !GPIO_IS_VALID_OUTPUT_GPIO(sda_gpio) || scl_gpio == sda_gpio)
      status = PORT_ERROR;
    else {
      i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_BUS_INDEX,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
      };
      status = record_i2c_result(i2c_new_master_bus(&bus_config, &s_bus_handle));
      if (status == PORT_OK)
        s_clock_hz = clock_hz ? clock_hz : APP_I2C_CLK_HZ;
    }
  }
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

bool i2c_port_is_ready(void) {
  return s_bus_handle != NULL;
}

static esp_err_t get_or_create_device(uint8_t device_address, i2c_master_dev_handle_t *device_handle) {
  if (!s_device_handles[device_address]) {
    i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = device_address,
      .scl_speed_hz = s_clock_hz
    };
    esp_err_t esp_error = i2c_master_bus_add_device(s_bus_handle, &device_config, &s_device_handles[device_address]);
    if (esp_error != ESP_OK)
      return esp_error;
  }
  *device_handle = s_device_handles[device_address];
  return ESP_OK;
}

static bool is_transfer_valid(uint8_t device_address, const void *buffer, uint16_t length, uint32_t timeout_ms) {
  return s_bus_handle && device_address < 128 && buffer && length && timeout_ms && timeout_ms <= INT_MAX;
}

/* 所有访问共用同一把递归锁，组合写读操作也在同一次加锁期间完成。 */
static port_status_t transfer_i2c_data(uint8_t device_address, const uint8_t *tx_data, uint16_t tx_length, uint8_t *rx_data, uint16_t rx_length, uint32_t timeout_ms) {
  if ((!tx_length && !rx_length) || (tx_length && !is_transfer_valid(device_address, tx_data, tx_length, timeout_ms)) || (rx_length && !is_transfer_valid(device_address, rx_data, rx_length, timeout_ms)))
    return PORT_ERROR;
  if (!os_port_mutex_lock(&s_bus_mutex, timeout_ms))
    return PORT_BUSY;
  i2c_master_dev_handle_t device_handle;
  esp_err_t esp_error = get_or_create_device(device_address, &device_handle);
  if (esp_error == ESP_OK) {
    if (tx_length && rx_length)
      esp_error = i2c_master_transmit_receive(device_handle, tx_data, tx_length, rx_data, rx_length, (int)timeout_ms);
    else if (tx_length)
      esp_error = i2c_master_transmit(device_handle, tx_data, tx_length, (int)timeout_ms);
    else
      esp_error = i2c_master_receive(device_handle, rx_data, rx_length, (int)timeout_ms);
  }
  port_status_t status = record_i2c_result(esp_error);
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

port_status_t i2c_port_write(uint8_t device_address, const uint8_t *data, uint16_t length, uint32_t timeout_ms) {
  return transfer_i2c_data(device_address, data, length, NULL, 0, timeout_ms);
}

port_status_t i2c_port_read(uint8_t device_address, uint8_t *data, uint16_t length, uint32_t timeout_ms) {
  return transfer_i2c_data(device_address, NULL, 0, data, length, timeout_ms);
}

port_status_t i2c_port_write_read(uint8_t device_address, const uint8_t *write_data, uint16_t write_length, uint8_t *read_data, uint16_t read_length, uint32_t timeout_ms) {
  if (!write_length || !read_length)
    return PORT_ERROR;
  return transfer_i2c_data(device_address, write_data, write_length, read_data, read_length, timeout_ms);
}

static unsigned encode_register_address(uint16_t register_address, i2c_port_memaddr_size_t address_size, uint8_t *address_bytes) {
  if (address_size == I2C_PORT_MEMADDR_8BIT) {
    if (register_address > 255)
      return 0;
    address_bytes[0] = register_address;
    return 1;
  }
  if (address_size == I2C_PORT_MEMADDR_16BIT) {
    address_bytes[0] = register_address >> 8;
    address_bytes[1] = register_address;
    return 2;
  }
  return 0;
}

port_status_t i2c_port_mem_write(uint8_t device_address, uint16_t register_address, i2c_port_memaddr_size_t address_size, const uint8_t *data, uint16_t length, uint32_t timeout_ms) {
  uint8_t register_address_bytes[2];
  unsigned address_length = encode_register_address(register_address, address_size, register_address_bytes);
  if (!address_length || !is_transfer_valid(device_address, data, length, timeout_ms))
    return PORT_ERROR;
  /* 使用多缓冲区发送，避免分配可能达到 64 KiB 的临时缓冲区。 */
  if (!os_port_mutex_lock(&s_bus_mutex, timeout_ms))
    return PORT_BUSY;
  i2c_master_dev_handle_t device_handle;
  esp_err_t esp_error = get_or_create_device(device_address, &device_handle);
  if (esp_error == ESP_OK) {
    i2c_master_transmit_multi_buffer_info_t tx_buffers[2] = {
        {.write_buffer = register_address_bytes, .buffer_size = address_length},
        {.write_buffer = data, .buffer_size = length},
    };
    esp_error = i2c_master_multi_buffer_transmit(device_handle, tx_buffers, 2, (int)timeout_ms);
  }
  port_status_t status = record_i2c_result(esp_error);
  os_port_mutex_unlock(&s_bus_mutex);
  return status;
}

port_status_t i2c_port_mem_read(uint8_t device_address, uint16_t register_address, i2c_port_memaddr_size_t address_size, uint8_t *data, uint16_t length, uint32_t timeout_ms) {
  uint8_t register_address_bytes[2];
  unsigned address_length = encode_register_address(register_address, address_size, register_address_bytes);
  if (!address_length || !is_transfer_valid(device_address, data, length, timeout_ms))
    return PORT_ERROR;
  return transfer_i2c_data(device_address, register_address_bytes, address_length, data, length, timeout_ms);
}

port_status_t i2c_port_probe(uint8_t device_address, uint32_t max_attempts, uint32_t timeout_ms) {
  if (!s_bus_handle || device_address >= 128 || !max_attempts || !timeout_ms || timeout_ms > INT_MAX)
    return PORT_ERROR;
  if (!os_port_mutex_lock(&s_bus_mutex, timeout_ms))
    return PORT_BUSY;
  esp_err_t esp_error = ESP_ERR_NOT_FOUND;
  for (uint32_t attempt = 0; attempt < max_attempts; ++attempt) {
    esp_error = i2c_master_probe(s_bus_handle, device_address, (int)timeout_ms);
    if (esp_error == ESP_OK || (esp_error != ESP_ERR_TIMEOUT && esp_error != ESP_ERR_NOT_FOUND))
      break;
  }
  if (esp_error != ESP_OK)
    s_last_error = (uint32_t)esp_error;
  os_port_mutex_unlock(&s_bus_mutex);
  return esp_error == ESP_ERR_NOT_FOUND ? PORT_TIMEOUT : port_from_esp(esp_error);
}

uint8_t i2c_port_scan(uint8_t *found_addresses, uint8_t address_capacity) {
  if (!found_addresses || !address_capacity || !s_bus_handle)
    return 0;
  uint8_t found_count = 0;
  for (unsigned device_address = APP_I2C_SCAN_ADDR_MIN; device_address <= APP_I2C_SCAN_ADDR_MAX && device_address < 128; ++device_address) {
    if (i2c_port_probe(device_address, 1, APP_I2C_TIMEOUT_MS) == PORT_OK)
      found_addresses[found_count++] = device_address;
    if (found_count == address_capacity)
      break;
  }
  return found_count;
}

void i2c_port_lock(void) {
  port_lock_required(&s_bus_mutex);
}

void i2c_port_unlock(void) {
  os_port_mutex_unlock(&s_bus_mutex);
}

uint32_t i2c_port_lock_depth(void) {
  port_enter_critical();
  uint32_t lock_depth = s_bus_mutex.depth;
  port_exit_critical();
  return lock_depth;
}

uint8_t i2c_port_bus_index(void) {
  return BOARD_I2C_BUS_INDEX;
}

uint32_t i2c_port_clock_hz(void) {
  return s_clock_hz;
}

uint32_t i2c_port_last_error(void) {
  return s_last_error;
}

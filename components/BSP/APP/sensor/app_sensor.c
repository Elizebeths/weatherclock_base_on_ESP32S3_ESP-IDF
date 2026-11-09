/**
 ******************************************************************************
 * @file    app_sensor.c
 * @brief   L3 传感器业务实现：初始化、周期采集、结果日志
 *
 * @note    数据流：app_sensor → sensor.h（L1 统一抽象）→ sensor/dht20.c（型号实现）
 *          → bus/i2c_port（L0）→ HAL。本层不含任何硬件符号，也不自行延时 ——
 *          转换等待已由 L1 承担。
 *
 * 型号无关性：本文件唯一提到型号的地方是 sensor_bind_()，换 SHT30 时
 *   把它改成 sht30_device() 即可，其余代码与日志格式均不变。
 ******************************************************************************
 */

#include "sensor/app_sensor.h"

#include "app_config.h"
#include "common/port.h"
#include "sensor/dht20.h"

#include "esp_log.h"

static const char *TAG = "sensor";

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

static bool s_ready = false;    /**< 器件是否已初始化成功 */
/** @brief 当前绑定的传感器型号（换型号只改这一行） */
static sensor_dev_t *sensor_bind_(void) { return dht20_device(); }

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

void app_sensor_log_sample(const sensor_sample_t *sample) {
  if (sample == NULL) {
    return;
  }

  const int32_t t = sample->temp_c_x10;
  const uint32_t h = sample->humi_rh_x10;
  /* 量程已由 L1 校验（|t| <= 800），此处取绝对值不会溢出 */
  const uint32_t t_abs = (uint32_t)((t < 0) ? -t : t);

  /* 整数打印避免依赖浮点 printf（nano.specs 下 %f 不可用） */
  ESP_LOGI(TAG, "T=%s%u.%u C, RH=%u.%u %%", (t < 0) ? "-" : "", (unsigned)(t_abs / 10U), (unsigned)(t_abs % 10U), (unsigned)(h / 10U), (unsigned)(h % 10U));
}

void app_sensor_init(void) {
  sensor_dev_t *const dev = sensor_bind_();
  const port_status_t st = sensor_dev_init(dev);

  if (st == PORT_OK) {
    s_ready = true;
    ESP_LOGI(TAG, "%s ready (addr=0x%02X)", sensor_dev_name(dev), (unsigned)dev->addr7);
    return;
  }

  s_ready = false;
  ESP_LOGE(TAG, "%s init failed: %s (addr=0x%02X)", sensor_dev_name(dev), port_status_str(st), (unsigned)dev->addr7);
}

bool app_sensor_ready(void) { return s_ready; }

bool app_sensor_poll(sensor_sample_t *out) {
  sensor_dev_t *const dev = sensor_bind_();

  if (!s_ready) {
    /* 自愈：允许运行期重新初始化（更换器件/接线复位后无需重启 MCU） */
    app_sensor_init();
    if (!s_ready)		return false;
  }

  sensor_sample_t sample;
  const port_status_t st = sensor_dev_read(dev, &sample);// 调用传感器读取，存入sample
  if (st != PORT_OK) {
    ESP_LOGE(TAG, "%s read failed: %s", sensor_dev_name(dev), port_status_str(st));
    return false;
  }

  *out = sample;
  app_sensor_log_sample(&sample);
  return true;
}

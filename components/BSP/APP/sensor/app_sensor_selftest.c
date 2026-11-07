/**
 ******************************************************************************
 * @file    app_sensor_selftest.c
 * @brief   L3 传感器上电自检：扫描总线 → 探测 → 初始化 → 读一次并输出
 *
 * @note    分层位置：L3（App/sensor）。整段由 APP_SENSOR_ENABLE_SELFTEST
 *          条件编译，产品版置 0 即剔除，不占用 Flash。
 *
 * 适配来源
 *   二级 · 自有模块：旧的 dht20.c 只在 main 里被裸调用一次，出错时既无返回值
 *                    也无日志，无法区分「没接」与「驱动写错」。本自检把两类
 *                    故障分开上报。
 *   一级 · 大型开源：Linux i2cdetect 的分步排查思路 —— 先确认总线上有没有
 *                    器件，再确认器件能不能干活。
 *
 * 排障指引（一路看串口日志即可定位）：
 *   ① "i2c scan found no device"  → 查 PB6/PB7 接线、上拉电阻与模块供电
 *   ② 扫描有器件但没有 0x38       → 地址不对（换型号？）或器件损坏
 *   ③ "no ack"                    → 器件在总线上但不应答，多为供电不足/时序问题
 *   ④ 读失败 PORT_TIMEOUT         → 转换未在窗口内完成，查上拉强度与总线速率
 *   ⑤ 读失败 PORT_ERROR           → CRC 不符或数值越界，多为走线干扰
 ******************************************************************************
 */

#include "sensor/app_sensor.h"

#include "app_config.h"

#if (APP_SENSOR_ENABLE_SELFTEST)

#include "bus/i2c_port.h"
#include "common/port.h"
#include "sensor/dht20.h"

#include "esp_log.h"

static const char *TAG = "sensor";

/** 扫描结果最多登记的器件数（本阶段 I2C1 上预期为 1~2 个） */
#define APP_SENSOR_SCAN_MAX 8U

void app_sensor_selftest(void) {
  ESP_LOGI(TAG, "sensor selftest start");

  /* 1) 扫描总线：把「器件没接好」与「驱动写错」彻底分开 */
  uint8_t found[APP_SENSOR_SCAN_MAX] = {0};
  const uint8_t count = i2c_port_scan(found, (uint8_t)APP_SENSOR_SCAN_MAX);
  if (count == 0U) {
    ESP_LOGE(TAG, "i2c scan found no device (check wiring / pull-ups / power)");
  } else {
    for (uint8_t i = 0U; i < count; i++) {
      ESP_LOGI(TAG, "i2c device at 0x%02X", (unsigned)found[i]);
    }
  }

  sensor_dev_t *const dev = dht20_device();

  /* 2) 探测器件是否应答（内部多重试，覆盖上电抖动） */
  port_status_t st = sensor_dev_probe(dev);
  if (st != PORT_OK) {
    ESP_LOGE(TAG, "%s no ack at 0x%02X (%s)", sensor_dev_name(dev),(unsigned)dev->addr7, port_status_str(st));
    return;
  }
  ESP_LOGI(TAG, "%s ack ok", sensor_dev_name(dev));

  /* 3) 初始化：读状态字，必要时下发 0xBE 0x08 0x00 使能校准 */
  st = sensor_dev_init(dev);
  if (st != PORT_OK) {
    ESP_LOGE(TAG, "%s init failed (%s)", sensor_dev_name(dev), port_status_str(st));
    return;
  }

  /* 4) 读一次温湿度并输出（阻塞约 APP_DHT20_CONVERT_MIN_MS） */
  sensor_sample_t sample;
  st = sensor_dev_read(dev, &sample);
  if (st != PORT_OK) {
    ESP_LOGE(TAG, "%s read failed (%s)", sensor_dev_name(dev), port_status_str(st));
    return;
  }
  app_sensor_log_sample(&sample);
  ESP_LOGI(TAG, "sensor selftest done");
}

#else /* APP_SENSOR_ENABLE_SELFTEST */

void app_sensor_selftest(void) {
  /* 产品版：自检关闭，空实现（不产生任何代码） */
}

#endif /* APP_SENSOR_ENABLE_SELFTEST */

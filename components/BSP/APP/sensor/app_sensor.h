/**
 ******************************************************************************
 * @file    app_sensor.h
 * @brief   L3 传感器业务接口（初始化编排 / 周期采集 / 上电自检）
 *
 * @note    分层位置：L3（App/sensor）。经 L1 的 sensor.h 统一抽象访问器件，
 *          不出现 HAL / STM32 符号，也不直接触碰 i2c_port ——
 *          换型号（SHT30 等）时本层只需改一处包含的型号头文件。
 *
 *
 ******************************************************************************
 */

#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sensor/sensor.h" /* sensor_sample_t（0.1 ℃ / 0.1 %RH） */

/**
 * @brief 传感器业务初始化（幂等）
 * @note  内部完成器件上电等待与校准检查；失败只记录日志并置为「未就绪」，
 *        不阻塞启动流程 —— 传感器缺失不应让显示等功能不可用。
 */
void app_sensor_init(void);

/**
 * @brief 周期采集一次温湿度
 * @note  未就绪时会先尝试一次自愈式重初始化（覆盖接线松动、上电顺序等情况）；
 *        正常一帧阻塞约 APP_DHT20_CONVERT_MIN_MS，仅由传感器任务按
 *        APP_SENSOR_PERIOD_MS 调用；成功时写入 out 并返回 true。
 */
bool app_sensor_poll(sensor_sample_t *out);

/**
 * @brief 上电自检：扫描总线 → 探测 0x38 → 初始化 → 读一次并输出日志
 * @note  由 APP_SENSOR_ENABLE_SELFTEST 条件编译；关闭时为空实现。
 */
void app_sensor_selftest(void);

/** @brief 传感器当前是否已初始化成功（供显示/业务层决定是否展示温湿度） */
bool app_sensor_ready(void);

/** @brief 把采样值打印成一行日志（如 "T=26.5 C, RH=51.2 %"），供自检与周期采集共用 */
void app_sensor_log_sample(const sensor_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* APP_SENSOR_H */

/**
 ******************************************************************************
 * @file    app_main.h
 * @brief   L3 应用层启动装配入口（模块注册 / 主循环）
 *
 * @note    分层位置：L3（App）。不含 HAL / STM32 符号。
 *
 * 适配来源
 *   一级 · 大型开源：LVGL 示例的 lv_port / app 装配入口（初始化顺序与主循环分离）
 *   二级 · 自有模块：无（旧 lcd 工程把 LCD_Test() 直接写在 main.c，本层把它收敛）
 ******************************************************************************
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 应用装配：按依赖顺序初始化各应用模块（显示 / 传感器）
 * @note  由 Core/Src/main.c 在完成外设初始化（MX_*_Init）后调用一次。
 *        内部会触发各模块初始化与（条件编译的）上电自检。
 */
void app_init(void);

/**
 * @brief 将当前有效网络时间推进一秒，自动处理日期进位与公历闰年。
 * @note  未校时或日期无效时保持原值；由主循环按经过的整秒串行调用。
 */
void app_time_poll(void);

/**
 * @brief 主循环体：由 main.c 的 while(1) 周期调用
 * @note  依次驱动：传感器周期采集、显示刷新调度。
 *        输入处理等后续能力在此继续展开。
 */
void app_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */

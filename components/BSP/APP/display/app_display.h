/**
 ******************************************************************************
 * @file    app_display.h
 * @brief   L3 显示业务接口（初始化编排 / 周期刷新 / 上电自检）
 *
 * @note    分层位置：L3（App/display）。经 L1 的 display/lcd.h 聚合门面访问
 *          显示能力，不出现 HAL / STM32 符号，也不触碰驱动层内部头。
 *
 * 刷新模型：本层只做两件事 ——
 *   1) 提供「屏幕重绘回调」（被刷新调度器在需要重绘的区域上调用）；
 *   2) 内容变化时调用 lcd_invalidate() 标脏；
 *   3) 周期入口 app_display_poll() 由 app_loop() 调用，负责在裸机模式下
 *      驱动刷新（RTOS 模式下刷新由显示任务自动完成，该调用为空操作）。
 *  不再需要旧实现里的「首帧标志位 s_chrome_painted」之类的手工状态：
 *  刷新时机与范围完全由脏区决定。
 ******************************************************************************
 */

#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示业务初始化：设备初始化 → 刷新调度器注册 → 首帧上屏 → 开背光
 * @note  内部完成：lcd_dev_init() → lcd_refresh_init() → 整屏标脏并冲刷 →
 *        lcd_bl_on()。顺序保证「画面稳定后才开背光」，不会闪白屏。
 *        自检开启（APP_LCD_ENABLE_SELFTEST）时改为呈现自检图案。
 */
void app_display_init(void);

/**
 * @brief 周期入口：更新动态内容（标脏）并驱动刷新
 * @note  由 app_loop() 每次循环调用；内部按 APP_DISPLAY_PERIOD_MS 限频。
 */
void app_display_poll(void);

/**
 * @brief 上电自检：背光闪烁 3 次 → 呈现自检图案 → 总线失败时快闪 10 次
 * @note  由 APP_LCD_ENABLE_SELFTEST 条件编译；关闭时为空实现。
 *        自检**不再自行初始化设备**（旧实现重复调用 lcd_dev_init），
 *        设备初始化统一由 app_display_init() 负责。
 */
void app_display_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */

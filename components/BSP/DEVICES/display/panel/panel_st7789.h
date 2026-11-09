/**
 ******************************************************************************
 * @file    panel_st7789.h
 * @brief   L1 驱动层：ST7789 面板驱动（初始化序列 / 窗口 / 像素提交）
 *
 * @note    分层位置：L1（Devices/display/panel）。只依赖 L0（lcd_port、
 *          os_port）与 lcd_types；**不含任何 HAL / STM32 符号**。
 *          本层是「唯一把像素送上总线」的层：窗口（含面板显存偏移）在此收敛，
 *          上层坐标永远是可见区坐标。
 *
 * 同步提交模型：
 *   panel_blit / panel_fill 会把像素流切成不超过 DISPLAY_DMA_BLOCK_MAX 的块，
 *   持有面板互斥锁逐块同步发送，失败时立即返回错误。
 *   底层 SPI 当前禁用 DMA，使用持久缓冲及有限超时等待事务完成。
 ******************************************************************************
 */

#ifndef PANEL_ST7789_H
#define PANEL_ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "display/lcd_types.h"

/**
 * @brief 初始化面板驱动：准备互斥量，并初始化 L0 显示 Port
 * @retval LCD_OK        就绪
 * @retval LCD_ERR_STATE 底层显示 Port / 总线未就绪
 * @note  幂等；不包含复位与寄存器序列（见 panel_start）。
 */
lcd_err_t panel_init(void);

/**
 * @brief 执行面板上电时序：硬件复位 → 初始化序列表 → 打开显示
 * @retval LCD_OK      完成
 * @retval LCD_ERR_BUS 总线写入失败
 * @note  不含清屏与背光：清屏与背光由设备层（lcd_dev）编排，
 *        以保证「上电不闪白屏」的顺序（先出图、后开背光）。
 * @note  阻塞，内部按序列表的延时等待，总耗时与旧驱动逐条一致。
 */
lcd_err_t panel_start(void);

/**
 * @brief 设置绘图窗口并发出写显存命令（坐标已含面板显存偏移）
 * @param x,y 左上角（可见区坐标）
 * @param w,h 宽高（像素，>0）；区域须落在屏内
 */
lcd_err_t panel_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief 提交一块 RGB565 位图（阻塞直到传输完成）
 * @param x,y     左上角（可见区坐标）
 * @param w,h     尺寸（>0，须落在屏内）
 * @param rgb565  像素数据：每像素 2 字节、高字节在前、行优先
 */
lcd_err_t panel_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t *rgb565);

/**
 * @brief 以单色填充矩形（阻塞直到传输完成）
 * @note  内部构造纯色缓冲后按块重复提交，靠面板地址自增覆盖整块；
 *        与旧实现「逐行一次轮询事务」相比，事务数与 CPU 占用显著下降。
 */
lcd_err_t panel_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     lcd_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_ST7789_H */

/**
 ******************************************************************************
 * @file    app_display_selftest.c
 * @brief   L3 上电自检：背光自检 + 完整测试画面（核对方向/颜色/边缘/字模）
 *
 * @note    分层位置：L3（App/display）。整段由 APP_LCD_ENABLE_SELFTEST
 *          条件编译，产品版置 0 即完全剔除，不占 Flash。
 *
 * @note    与旧实现的三处差别：
 *          1) **不再自行初始化设备**：旧 app_display_selftest 内部又调了一次
 *             lcd_dev_init()（与 app_display_init 重复），现由调用方统一负责；
 *          2) 绘制改用统一绘图 API（画布 + 游程原语），不再逐像素设窗；
 *          3) 清屏/底色不再依赖「先整块清屏再重绘」的旧约定。
 *
 * @note    本文件刻意走**直写面板**的画布：它验证的是「绘图 API → 面板」
 *          这条最底层通路（绕过条带缓冲），与常规界面走的两条通路互为补充。
 ******************************************************************************
 */

#include "display/app_display.h"

#include <stdint.h>

#include "app_config.h"

#if (APP_LCD_ENABLE_SELFTEST)

#include "display/lcd.h"
#include "display/lcd_port.h" /* 允许逻辑层使用的 L0 延时钩子 */

/* ==========================================================================
 * 16x16 棋盘格测试图（RGB565，高字节在前，行优先，存放于 Flash）
 * 以 4x4 像素为一个色块，红(0xF800)与白(0xFFFF)交替
 * ========================================================================== */
#define LCD_BMP_PIX_RED_ 0xF8, 0x00
#define LCD_BMP_PIX_WHT_ 0xFF, 0xFF
#define LCD_BMP_BLK_RED_                                                       \
  LCD_BMP_PIX_RED_, LCD_BMP_PIX_RED_, LCD_BMP_PIX_RED_, LCD_BMP_PIX_RED_
#define LCD_BMP_BLK_WHT_                                                       \
  LCD_BMP_PIX_WHT_, LCD_BMP_PIX_WHT_, LCD_BMP_PIX_WHT_, LCD_BMP_PIX_WHT_
#define LCD_BMP_ROW_A_                                                         \
  LCD_BMP_BLK_RED_, LCD_BMP_BLK_WHT_, LCD_BMP_BLK_RED_, LCD_BMP_BLK_WHT_
#define LCD_BMP_ROW_B_                                                         \
  LCD_BMP_BLK_WHT_, LCD_BMP_BLK_RED_, LCD_BMP_BLK_WHT_, LCD_BMP_BLK_RED_

static const uint8_t s_test_bmp[16U * 16U * 2U] = {
    LCD_BMP_ROW_A_, LCD_BMP_ROW_A_, LCD_BMP_ROW_A_, LCD_BMP_ROW_A_,
    LCD_BMP_ROW_B_, LCD_BMP_ROW_B_, LCD_BMP_ROW_B_, LCD_BMP_ROW_B_,
    LCD_BMP_ROW_A_, LCD_BMP_ROW_A_, LCD_BMP_ROW_A_, LCD_BMP_ROW_A_,
    LCD_BMP_ROW_B_, LCD_BMP_ROW_B_, LCD_BMP_ROW_B_, LCD_BMP_ROW_B_,
};

/* 编译期校验测试图字节数，防止宏展开出错导致静默补零 */
typedef char lcd_test_bmp_size_check_[(sizeof(s_test_bmp) == 16U * 16U * 2U)
                                          ? 1
                                          : -1];

/**
 * @brief 点亮自检画面：色条 / 边框 / 文本 / 数字 / 定点小数 / 圆 / 小图
 * @note  用于核对方向、颜色通道与边缘偏移
 */
static void lcd_show_test_pattern_(void) {
  lcd_canvas_t c;
  lcd_canvas_direct(&c, NULL);

  /* 先铺黑底：自检图案只绘制局部元素（色条/边框/文字/圆/小图），
   * 直写画布不做清屏，未覆盖的区域会残留显存内容 —— 表现为「花屏背景」。
   * 铺设底色后，自检画面与设备初始化的清屏效果相互独立，任一处失效都能兜住。 */
  lcd_draw_clear(&c, LCD_COLOR_BLACK);

  /* 顶部三段色条（红/绿/蓝，用于核对 RGB 通道是否错位） */
  const uint16_t third = (uint16_t)(LCD_WIDTH / 3U);
  lcd_draw_box(&c, 0, 0, third, 40U, LCD_COLOR_BLOOD);
  lcd_draw_box(&c, (int32_t)third, 0, third, 40U, LCD_COLOR_GREEN);
  lcd_draw_box(&c, (int32_t)(third * 2U), 0, (uint16_t)(LCD_WIDTH - third * 2U),
               40U, LCD_COLOR_AZURE);

  /* 边框：核对四边是否贴边、有无偏移/镜像 */
  const lcd_rect_t frame = lcd_rect_screen();
  lcd_draw_rect_outline(&c, &frame, LCD_COLOR_MILK);

  /* 文本与数字（透明样式：可叠加在边框与色条之上） */
  const lcd_font_t *const f = &LCD_FONT_ASCII16X32;
  const lcd_text_style_t white = lcd_text_style_transparent(LCD_COLOR_MILK);
  lcd_text_string(&c, f, 0, 64, "Weather Clock", &white);
  lcd_text_string(&c, f, 0, 96, "LCD OK 240x320", &white);
  lcd_text_u32(&c, f, 0, 128, 12345U, 5U, '0', &white);
  lcd_text_i32(&c, f, 0, 160, -42, 4U, '0', &white);
  /* 26.5 以「缩放整数 + 小数位数」表达，不依赖浮点库 */
  const lcd_text_style_t yellow = lcd_text_style_transparent(LCD_COLOR_YELLOW);
  lcd_text_fixed(&c, f, 0, 192, 26500, 3U, 2U, ' ', &yellow);

  /* 实心圆 + Flash 小图 */
  lcd_draw_circle_filled(&c, (int32_t)(LCD_WIDTH / 2U), 260, 40U,
                         LCD_COLOR_PURPLE);
  lcd_draw_bitmap(&c, 200, 124, 16U, 16U, s_test_bmp);
}

void app_display_selftest(void) {
  /* 1) 背光自检：闪烁 3 次
   *    - 正常闪烁：MCU 在运行、BL 接线与极性正确，问题只可能在总线/初始化
   *    - 完全不闪：查 BL 接线、模块供电(部分模块 VCC 必须 5V)、极性是否低有效 */
  for (uint8_t i = 0U; i < 3U; i++) {
    lcd_bl_on();
    lcd_port_delay_ms(200U);
    lcd_bl_off();
    lcd_port_delay_ms(200U);
  }

  /* 2) 显示完整测试图案（设备初始化已由 app_display_init 完成） */
  lcd_show_test_pattern_();

  /* 3) 总线失败时用「快闪 10 次」提示（正常应保持常亮） */
  if (lcd_is_bus_error()) {
    for (uint8_t i = 0U; i < 10U; i++) {
      lcd_bl_on();
      lcd_port_delay_ms(80U);
      lcd_bl_off();
      lcd_port_delay_ms(80U);
    }
  }
}

#else /* APP_LCD_ENABLE_SELFTEST */

void app_display_selftest(void) {
  /* 产品版：自检关闭，空实现（不产生任何代码） */
}

#endif /* APP_LCD_ENABLE_SELFTEST */

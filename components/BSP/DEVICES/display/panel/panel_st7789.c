/**
 ******************************************************************************
 * @file    panel_st7789.c
 * @brief   L1 驱动层实现：ST7789 面板（序列表初始化、窗口、同步像素提交）
 *
 * @note    提交策略：像素流按 DISPLAY_DMA_BLOCK_MAX 分块，同步发送每块，
 *          直接检查返回值；任一块失败即停止提交。
 *
 * @note    阻塞语义：panel_blit / panel_fill 会阻塞调用方直到本块数据传完。
 *          当前由应用轮询调用，提交期间会阻塞主循环。
 ******************************************************************************
 */

#include "display/panel/panel_st7789.h"

#include <stddef.h>

#include "common/os_port.h"
#include "common/port.h"
#include "display/lcd_port.h"
#include "display/panel/st7789_regs.h"

/** 面板互斥锁的等待上限（毫秒）；传输超时由 lcd_port 控制。 */
#define PANEL_TX_TIMEOUT_MS 1000U

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

/** 纯色填充用的行/整块缓冲（构造一次、重复提交） */
static uint8_t s_fill_buf[DISPLAY_WIDTH * DISPLAY_FILL_LINES * DISPLAY_BPP];

static os_mutex_t s_lock;
static bool s_inited = false;

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

static lcd_err_t panel_map_(port_status_t st) {
  switch (st) {
  case PORT_OK:
    return LCD_OK;
  case PORT_BUSY:
    return LCD_ERR_BUSY;
  case PORT_TIMEOUT:
  case PORT_ERROR:
  default:
    return LCD_ERR_BUS;
  }
}

/** @brief 在面板锁内逐块同步提交；纯色填充重复使用同一缓冲。 */
static lcd_err_t panel_tx_run_(const uint8_t *data, uint32_t total,
                               uint32_t block, bool repeat) {
  uint32_t sent = 0U;
  while (sent < total) {
    const uint32_t remaining = total - sent;
    const uint32_t n = (remaining > block) ? block : remaining;
    const uint8_t *p = repeat ? data : (data + sent);

    const port_status_t st = lcd_port_spi_tx_pixels(p, n);
    if (st != PORT_OK) {
      return panel_map_(st);
    }

    sent += n;
  }

  return LCD_OK;
}

/** @brief 把 16 位值写成「高字节在前」的两个字节 */
static void panel_put_u16_(uint8_t *out, uint16_t v) {
  out[0] = (uint8_t)(v >> 8U);
  out[1] = (uint8_t)v;
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

lcd_err_t panel_init(void) {
  os_port_mutex_init(&s_lock);

  const port_status_t st = lcd_port_init();
  s_inited = (st == PORT_OK);
  return panel_map_(st);
}

lcd_err_t panel_start(void) {
  if (!s_inited) {
    return LCD_ERR_STATE;
  }

  /* 硬件复位：拉低 ST7789_RESET_LOW_MS → 拉高并等待稳定 */
  lcd_port_pin_write(LCD_PIN_RST, false);
  lcd_port_delay_ms(ST7789_RESET_LOW_MS);
  lcd_port_pin_write(LCD_PIN_RST, true);
  lcd_port_delay_ms(ST7789_RESET_SETTLE_MS);

  for (uint16_t i = 0U; i < ST7789_INIT_SEQ_LEN; i++) {
    const st7789_init_step_t *const step = &ST7789_INIT_SEQ[i];
    if (step->cmd != ST7789_SEQ_DELAY_ONLY) {
      if (lcd_port_cmd_dat(step->cmd, step->dat, (uint16_t)step->len) != PORT_OK) {
        return LCD_ERR_BUS;
      }
    }
    if (step->delay_ms != 0U) {
      lcd_port_delay_ms((uint32_t)step->delay_ms);
    }
  }

  return LCD_OK;
}

/* ==========================================================================
 * 窗口
 * ========================================================================== */

lcd_err_t panel_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if ((w == 0U) || (h == 0U) || ((uint32_t)x + w > LCD_WIDTH) ||
      ((uint32_t)y + h > LCD_HEIGHT)) {
    return LCD_ERR_PARAM;
  }

  /* 面板显存偏移只在这里叠加：上层永远用可见区坐标 */
  const uint16_t xs = (uint16_t)(x + DISPLAY_PANEL_COL_OFFSET);
  const uint16_t ys = (uint16_t)(y + DISPLAY_PANEL_ROW_OFFSET);
  const uint16_t xe = (uint16_t)(xs + w - 1U);
  const uint16_t ye = (uint16_t)(ys + h - 1U);

  uint8_t col[4];
  uint8_t row[4];
  panel_put_u16_(&col[0], xs);
  panel_put_u16_(&col[2], xe);
  panel_put_u16_(&row[0], ys);
  panel_put_u16_(&row[2], ye);

  if (lcd_port_cmd_dat(ST7789_CASET, col, 4U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  if (lcd_port_cmd_dat(ST7789_RASET, row, 4U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  if (lcd_port_cmd_dat(ST7789_RAMWR, NULL, 0U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  return LCD_OK;
}

/* ==========================================================================
 * 像素提交
 * ========================================================================== */

lcd_err_t panel_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t *rgb565) {
  if (rgb565 == NULL) {
    return LCD_ERR_PARAM;
  }
  if (!os_port_mutex_lock(&s_lock, PANEL_TX_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }

  lcd_err_t err = panel_set_window(x, y, w, h);
  if (err == LCD_OK) {
    const uint32_t bytes = (uint32_t)w * (uint32_t)h * DISPLAY_BPP;
    err = panel_tx_run_(rgb565, bytes, DISPLAY_DMA_BLOCK_MAX, false);
  }

  os_port_mutex_unlock(&s_lock);
  return err;
}

lcd_err_t panel_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     lcd_color_t color) {
  if (!os_port_mutex_lock(&s_lock, PANEL_TX_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }

  lcd_err_t err = panel_set_window(x, y, w, h);
  if (err == LCD_OK) {
    const uint32_t total = (uint32_t)w * (uint32_t)h * DISPLAY_BPP;
    const uint32_t cap = (uint32_t)sizeof(s_fill_buf);
    const uint32_t filled_bytes = (total > cap) ? cap : total;

    for (uint32_t i = 0U; i < filled_bytes; i += 2U) {
      panel_put_u16_(&s_fill_buf[i], (uint16_t)color);
    }

    err = panel_tx_run_(s_fill_buf, total, filled_bytes, true);
  }

  os_port_mutex_unlock(&s_lock);
  return err;
}

/**
 ******************************************************************************
 * @file    app_display.c
 * @brief   L3 显示业务实现：三段式布局（顶栏 / 主内容 / 导航栏）+ 局部刷新
 *
 * @note    数据流：app_display → display/lcd.h（dev / refresh / canvas /
 *          draw / text）→ panel_st7789 → lcd_port → spi_port → HAL。
 *          本层不含任何硬件符号。
 *
 * @note    刷新策略（与旧实现的根本差别）：
 *          1) **不再手工清屏**：重绘回调总是先在条带内铺底色再画内容，
 *             底色覆盖旧内容，因此不存在残影，也不需要「首帧标志位」；
 *          2) **只重绘脏区**：内容变化时用 lcd_invalidate() 标出精确矩形，
 *             调度器只在那些区域上回调本文件的重绘函数；
 *          3) **按需绘制**：重绘回调对每个布局元素先判断是否与 area 相交，
 *             不相交则完全跳过，代价与脏区面积成正比而非与整屏成正比。
 ******************************************************************************
 */

#include "display/app_display.h"

#include "app_config.h"
#include "common/port.h"
#include "sensor/app_sensor.h"
#include "display/lcd.h"
#include "sensor/sensor.h"
#include "net/app_net.h"


/* ==========================================================================
 * 布局（像素坐标；字体 16x32）
 * ========================================================================== */

#define UI_TOP_Y     0U   /**< 顶栏起点 */
#define UI_TOP_H     32U  /**< 顶栏高度 */
#define UI_MAIN_Y    32U  /**< 主内容区起点 */
#define UI_MAIN_H    256U /**< 主内容区高度（32 .. 287） */
#define UI_NAV_Y     288U /**< 导航栏起点 */
#define UI_NAV_H     32U  /**< 导航栏高度 */

#define UI_PITCH     40U /**< 行间距（32 字高 + 8 留白） */

/**
 * 布局完整性（编译期）：三段区域必须**恰好首尾相接铺满整屏**。
 * 重绘回调是按区域调用的，条带缓冲会被复用；只要有一段区域没被铺底色，
 * 该区域就会残留上一条带/上次刷新的内容，表现为「花屏背景」。
 * 这三条断言把该约束从「注释里的约定」提升为编译期强制。
 */
APP_STATIC_ASSERT((UI_TOP_Y + UI_TOP_H) == UI_MAIN_Y, disp_band_top);
APP_STATIC_ASSERT((UI_MAIN_Y + UI_MAIN_H) == UI_NAV_Y, disp_band_middle);
APP_STATIC_ASSERT((UI_NAV_Y + UI_NAV_H) == LCD_HEIGHT, disp_band_bottom);

#define UI_BG        LCD_COLOR_BLACK
#define UI_TOP_BG    LCD_COLOR_GREEN
#define UI_TEXT      LCD_COLOR_MILK
#define UI_ACCENT    LCD_COLOR_YELLOW

/* 动态行：只在这些行内容变化时才标脏 */
#define UI_LINE_TITLE_Y UI_MAIN_Y										/**< y=32  标题 */
#define UI_LINE_INDOORTEMP_Y  (UI_MAIN_Y + UI_PITCH)					/**< y=72  室内温度 */
#define UI_LINE_INDOORHUMI_Y  (UI_MAIN_Y + 2U * UI_PITCH)				/**< y=112 室内湿度 */
#define UI_LINE_TICK_Y  (UI_MAIN_Y + 3U * UI_PITCH)						/**< y=152 计数 */
#define UI_LINE_OUTDOORTEMP_Y    (UI_MAIN_Y + 4U * UI_PITCH)			/**< y=192 室外温度 */
#define UI_LINE_OUTDOORHUMI_Y (UI_MAIN_Y + 5U * UI_PITCH)				/**< y=232 室外湿度 */

#define UI_TEXT_X 0 /**< 文本左端 */

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

static const lcd_font_t *const s_font = &LCD_FONT_ASCII16X32;

static bool s_ready = false;
static uint32_t s_last_poll_indoor_ms = 0U;
static uint32_t s_last_poll_outdoor_ms = 0U;
static uint32_t s_tick = 0U;

/* 最新室内采样值（仅值变化时才标脏，避免无谓刷新） */
static int32_t s_shown_temp_indoor_x10 = 0; /**< 0.1 ℃ */
static uint32_t s_shown_humi_indoor_x10 = 0U; /**< 0.1 %RH */
static bool s_shown_valid_indoor = false;

/* 最新室外采样值（仅值变化时才标脏，避免无谓刷新） */
static int32_t s_shown_temp_outdoor_x10 = 0; /**< 0.1 ℃ */
static uint32_t s_shown_humi_outdoor_x10 = 0U; /**< 0.1 %RH */

extern bool request_valid;
extern int request_temp;
extern uint8_t request_humi;
extern int request_year;
extern uint8_t request_month;
extern uint8_t request_day;
extern uint8_t request_hour;
extern uint8_t request_min;
extern uint8_t request_sec;


/* ==========================================================================
 * 绘制辅助
 * ========================================================================== */

/** @brief 判断给定屏幕矩形是否与本次重绘区域相交 */
static bool app_display_owns_(const lcd_rect_t *area, int32_t x, int32_t y,
                              uint16_t w, uint16_t h) {
  if ((x < 0) || (y < 0)) {
    return true; /* 负坐标元素一律绘制，由画布裁剪 */
  }
  const lcd_rect_t box = lcd_rect_make((uint16_t)x, (uint16_t)y, w, h);
  lcd_rect_t vis;
  return lcd_rect_intersect(area, &box, &vis);
}

/** @brief 按需绘制一行文本（与 area 无交集则跳过） */
static void app_display_text_(const lcd_rect_t *area, lcd_canvas_t *c, int32_t x,
                              int32_t y, const char *s,
                              const lcd_text_style_t *style) {
  const uint16_t w = lcd_text_measure(s_font, s);
  if (app_display_owns_(area, x, y, w, lcd_text_glyph_h(s_font))) {
    lcd_text_string(c, s_font, x, y, s, style);
  }
}

/* ==========================================================================
 * 屏幕重绘回调（由刷新调度器在脏区上调用）
 * ========================================================================== */

static void app_display_render(const lcd_rect_t *area, lcd_canvas_t *canvas) {
  const lcd_text_style_t on_top = lcd_text_style_opaque(UI_TEXT, UI_TOP_BG);
  const lcd_text_style_t on_bg = lcd_text_style_opaque(UI_TEXT, UI_BG);
  const lcd_text_style_t on_bg_accent = lcd_text_style_opaque(UI_ACCENT, UI_BG);
  const lcd_text_style_t indoortemp_val = lcd_text_style_opaque(LCD_COLOR_BLOOD, UI_BG);// 指定气温值的字体颜色为红，背景为黑。后续可以添加温度过高或者过低时变色，以及做异常值遗弃
  const lcd_text_style_t indoorhumi_val = lcd_text_style_opaque(LCD_COLOR_AZURE, UI_BG);
  const lcd_text_style_t tick_val = lcd_text_style_opaque(UI_ACCENT, UI_BG);
  const lcd_text_style_t outdoortemp_val = lcd_text_style_opaque(LCD_COLOR_BLOOD, UI_BG);
  const lcd_text_style_t outdoorhumi_val = lcd_text_style_opaque(LCD_COLOR_AZURE, UI_BG);

  /* ---- 顶栏 ---- */
  if (app_display_owns_(area, 0, (int32_t)UI_TOP_Y, LCD_WIDTH, UI_TOP_H)) {
    lcd_draw_box(canvas, 0, (int32_t)UI_TOP_Y, LCD_WIDTH, UI_TOP_H, UI_TOP_BG);
    app_display_text_(area, canvas, 8, (int32_t)UI_TOP_Y, "Weather Clock", &on_top);
  }

  /* ---- 主内容区 ---- */
  if (app_display_owns_(area, 0, (int32_t)UI_MAIN_Y, LCD_WIDTH, UI_MAIN_H)) {
    lcd_draw_box(canvas, 0, (int32_t)UI_MAIN_Y, LCD_WIDTH, UI_MAIN_H, UI_BG);
  }

  app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_TITLE_Y, "SMART WEATHER", &on_bg_accent);

  /* 室内温湿度：有有效采样时显示定点数值（无浮点依赖），否则显示占位 */
  if (s_shown_valid_indoor) {
    app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_INDOORTEMP_Y, "RoomTemp", &on_bg);
    lcd_text_fixed(canvas, s_font, UI_TEXT_X + 8 * 16, (int32_t)UI_LINE_INDOORTEMP_Y, s_shown_temp_indoor_x10, 1U, 3U, ' ', &indoortemp_val);
    app_display_text_(area, canvas, UI_TEXT_X + 14 * 16, (int32_t)UI_LINE_INDOORTEMP_Y, "C", &on_bg);

    app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_INDOORHUMI_Y, "RoomHumi", &on_bg);
    lcd_text_fixed(canvas, s_font, UI_TEXT_X + 8 * 16, (int32_t)UI_LINE_INDOORHUMI_Y, s_shown_humi_indoor_x10, 1U, 3U, ' ', &indoorhumi_val);
    app_display_text_(area, canvas, UI_TEXT_X + 14 * 16, (int32_t)UI_LINE_INDOORHUMI_Y, "%", &on_bg);
  } else {
    app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_INDOORTEMP_Y, "RoomTemp  --.- C", &on_bg);
    app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_INDOORHUMI_Y, "RoomHumi  --.- %", &on_bg);
  }

  /* 运行计数：验证「只有变化的区域被刷新」 */
  app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_TICK_Y, "Time:", &on_bg);
  lcd_text_u32(canvas, s_font, UI_TEXT_X + 5 * 16, (int32_t)UI_LINE_TICK_Y, request_hour, 2U, ' ', &tick_val);
  app_display_text_(area, canvas, UI_TEXT_X + 7 * 16, (int32_t)UI_LINE_TICK_Y, ":", &on_bg);
  lcd_text_u32(canvas, s_font, UI_TEXT_X + 8 * 16, (int32_t)UI_LINE_TICK_Y, request_min, 2U, ' ', &tick_val);
  app_display_text_(area, canvas, UI_TEXT_X + 10 * 16, (int32_t)UI_LINE_TICK_Y, ":", &on_bg);
  lcd_text_u32(canvas, s_font, UI_TEXT_X + 11 * 16, (int32_t)UI_LINE_TICK_Y, request_sec, 2U, ' ', &tick_val);


  /* 室外温湿度：有有效采样时显示定点数值（无浮点依赖），否则显示占位 */
    if (request_valid) {
      app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_OUTDOORTEMP_Y, "ExitTemp", &on_bg);
      lcd_text_fixed(canvas, s_font, UI_TEXT_X + 8 * 16, (int32_t)UI_LINE_OUTDOORTEMP_Y, s_shown_temp_outdoor_x10, 1U, 3U, ' ', &outdoortemp_val);
      app_display_text_(area, canvas, UI_TEXT_X + 14 * 16, (int32_t)UI_LINE_OUTDOORTEMP_Y, "C", &on_bg);

      app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_OUTDOORHUMI_Y, "ExitHumi", &on_bg);
      lcd_text_fixed(canvas, s_font, UI_TEXT_X + 8 * 16, (int32_t)UI_LINE_OUTDOORHUMI_Y, s_shown_humi_outdoor_x10, 1U, 3U, ' ', &outdoorhumi_val);
      app_display_text_(area, canvas, UI_TEXT_X + 14 * 16, (int32_t)UI_LINE_OUTDOORHUMI_Y, "%", &on_bg);
    } else {
      app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_OUTDOORTEMP_Y, "ExitTemp  --.- C", &on_bg);
      app_display_text_(area, canvas, UI_TEXT_X, (int32_t)UI_LINE_OUTDOORHUMI_Y, "ExitHumi  --.- %", &on_bg);
    }


  /* ---- 导航栏 ---- */
  if (app_display_owns_(area, 0, (int32_t)UI_NAV_Y, LCD_WIDTH, UI_NAV_H)) {
    lcd_draw_box(canvas, 0, (int32_t)UI_NAV_Y, LCD_WIDTH, UI_NAV_H, UI_BG);
    app_display_text_(area, canvas, 0, (int32_t)UI_NAV_Y, "Desigend by xzb", &on_bg);
  }
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

void app_display_init(void) {
  s_ready = false;

  const lcd_err_t err = lcd_dev_init();
  if (err != LCD_OK) {
    return;
  }

  const lcd_err_t rerr = lcd_refresh_init(app_display_render);
  if (rerr != LCD_OK) {
    return;
  }

#if (APP_LCD_ENABLE_SELFTEST)
  /* 自检模式：呈现自检图案并保持不变（不置 s_ready，poll 因此不接管画面） */
  app_display_selftest();
#else
  /* 常规模式：首帧整屏标脏并立即冲刷，画面稳定后才开背光 */
  s_ready = true;
  (void)lcd_invalidate_screen();
  (void)lcd_refresh_flush();
#endif

  lcd_bl_on();
  s_last_poll_indoor_ms = port_time_ms();
}

void app_display_poll(void) {
  if (!s_ready)		return;

  const uint32_t now = port_time_ms();

  // 室内温湿度刷新
  if ((uint32_t)(now - s_last_poll_indoor_ms) >= APP_DISPLAY_PERIOD_INDOOR_MS) {
	s_last_poll_indoor_ms = now;
    s_tick++;

    /* 只有真正变化的行才标脏 —— 静态内容零刷新开销 */
    const lcd_rect_t tick_line = lcd_rect_make(0U, (uint16_t)UI_LINE_TICK_Y, LCD_WIDTH, 32U);

    //标记脏区
    (void)lcd_invalidate(&tick_line);

    const sensor_sample_t *const s = app_sensor_last();//获取传感器数据的最新值，放入s中
    if (s != NULL) {
      if (!s_shown_valid_indoor || (s->temp_c_x10 != s_shown_temp_indoor_x10))	// 数值与上次不同则刷新数值
      {
    	  s_shown_temp_indoor_x10 = s->temp_c_x10;								// 取出最新值并更新
		const lcd_rect_t r = lcd_rect_make(0U, (uint16_t)UI_LINE_INDOORTEMP_Y, LCD_WIDTH, 32U);

		//标记脏区
		(void)lcd_invalidate(&r);
      }


      if (!s_shown_valid_indoor || (s->humi_rh_x10 != s_shown_humi_indoor_x10)) {
    	  s_shown_humi_indoor_x10 = s->humi_rh_x10;
        const lcd_rect_t r = lcd_rect_make(0U, (uint16_t)UI_LINE_INDOORHUMI_Y, LCD_WIDTH, 32U);

        //标记脏区
        (void)lcd_invalidate(&r);
      }
      s_shown_valid_indoor = true;
    }
  }

  // 室外温湿度刷新
    if ((uint32_t)(now - s_last_poll_outdoor_ms) >= APP_DISPLAY_PERIOD_OUTDOOR_MS) {
       s_last_poll_outdoor_ms = now;

      /* 只有真正变化的行才标脏 —— 静态内容零刷新开销 */
      const lcd_rect_t tick_line = lcd_rect_make(0U, (uint16_t)UI_LINE_TICK_Y, LCD_WIDTH, 32U);

      //标记脏区
      (void)lcd_invalidate(&tick_line);

      //获取网络数据的最新值，放入s中，暂时先对其温湿度用固定值
      //const sensor_sample_t *const s = app_sensor_last();

      if (/*s != NULL*/1) {

        if (!request_valid || (request_temp * 10 != s_shown_temp_outdoor_x10))	// 数值与上次不同则刷新数值
        {
			s_shown_temp_outdoor_x10 = request_temp * 10;								// 取出最新值并更新
			const lcd_rect_t r = lcd_rect_make(0U, (uint16_t)UI_LINE_OUTDOORTEMP_Y, LCD_WIDTH, 32U);

			//标记脏区
			(void)lcd_invalidate(&r);
        }


        if (!request_valid || (request_humi * 10 != s_shown_humi_outdoor_x10))
        {
			s_shown_humi_outdoor_x10 = request_humi * 10;
			const lcd_rect_t r = lcd_rect_make(0U, (uint16_t)UI_LINE_OUTDOORHUMI_Y, LCD_WIDTH, 32U);

			//标记脏区
			(void)lcd_invalidate(&r);
        }
        request_valid = true;
      }
    }

  // 冲刷脏区
  lcd_refresh_poll();
}

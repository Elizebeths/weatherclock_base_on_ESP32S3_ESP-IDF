/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   编译期功能开关与阈值（周期 / 超时 / 分块 / 日志级别）
 *
 * @note    分层位置：Config/。任何层都可以包含本文件；本文件不包含任何头文件，
 *          以保持零依赖（避免 Config → Ports → Config 的循环包含）。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：lcdLib 的 lcd.h「用户配置区」
 *                    （LCD_SPI_TIMEOUT_MS 100ms、LCD_SPI_CHUNK_BYTES 4096、
 *                      LCD_BL_ACTIVE_HIGH、LCD_WIDTH/HEIGHT、字库几何）
 *                    与 iic_DHT20 的 Core/Src/i2c.c（I2C1 @ 100 kHz）
 *
 * 对应《PH1 显示攻坚专题》的性能要求：
 *   SPI 预分频 4 → 2（18 → 36 MHz，按 18/27/36 MHz 分档压测，不稳回退并记录）
 ******************************************************************************
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ==========================================================================
 * 一、OS 适配（Ports/common/os_port）
 *
 * APP_OS_FREERTOS：
 *   0（默认）= 任务只登记，显示刷新由 app_loop() 轮询驱动。
 *   1        = 显示刷新由独立 FreeRTOS 任务驱动。
 * ESP-IDF 自带并启动 FreeRTOS；两种模式均使用真实互斥量和信号量，
 * 以保证 ESP32-S3 双核安全，无需手动启动调度器。
 * OS 对象使用静态池；应用任务栈上限为 1024 个 32 位字。
 * ========================================================================== */
#define APP_OS_FREERTOS 0

/** 显示刷新任务：栈深度（字）与优先级（数值越大越高） */
#define APP_DISPLAY_TASK_STACK_WORDS 1024U
#define APP_DISPLAY_TASK_PRIORITY    4U

/** 刷新任务等待「有脏区」的最长阻塞时间(ms)；仅 RTOS 模式使用 */
#define APP_DISPLAY_DIRTY_TIMEOUT_MS 1000U

/* ==========================================================================
 * 二、显示 SPI（TFT / ST7789）
 * ========================================================================== */

/**
 * 旧平台预分频记录，保留兼容。ESP32-S3 的实际目标频率由
 * 本文件的 APP_SPI_LCD_CLK_HZ 设置，不使用本宏。
 */
#define APP_SPI_LCD_PRESCALER_LOG2 1U

/** ESP32-S3 显示 SPI 目标频率（Hz） */
#define APP_SPI_LCD_CLK_HZ 36000000U

/** 轮询传输超时(ms)。不使用 HAL_MAX_DELAY，避免总线异常时整机假死 */
#define APP_SPI_LCD_TIMEOUT_MS 100U

/** 轮询路径的单次分块字节数（须 <= 65535；旧 lcd.h 的 LCD_SPI_CHUNK_BYTES） */
#define APP_SPI_LCD_CHUNK_BYTES 4096U

/** 单块提交上限；当前 ESP32-S3 DMA 入口使用同步 FIFO 回退 */
#define APP_SPI_LCD_DMA_MAX_BYTES 65535U

#define APP_DISPLAY_PERIOD_INDOOR_MS 1000
#define APP_DISPLAY_PERIOD_OUTDOOR_MS 1000

/* ==========================================================================
 * 三、总线锁与 I2C
 * ========================================================================== */

/** 递归总线锁等待上限(ms)；显式 void lock() 超时触发断言 */
#define APP_BUS_LOCK_TIMEOUT_MS 50U

/** I2C 控制器时钟频率（Hz）；默认 100 kHz */
#define APP_I2C_CLK_HZ 100000U

/** I2C 单次事务超时(ms)。替代旧 dht20.c 的 HAL_MAX_DELAY 死等 */
#define APP_I2C_TIMEOUT_MS 50U

/** I2C 地址扫描范围（7 位地址） */
#define APP_I2C_SCAN_ADDR_MIN 0x08U
#define APP_I2C_SCAN_ADDR_MAX 0x77U

/* ==========================================================================
 * 三·B、温湿度传感器（DHT20/AHT20，复用 I2C1）
 * 设计要点：旧 App/dht20 把 HAL_Delay(100) / HAL_Delay(500) 硬编码在函数体内，
 *           且用 HAL_MAX_DELAY 无限等待。此处把时序收敛为编译期常量，并以
 *           「轮询就绪 + 上限」替代固定长延时 —— 这是本次性能收益的主要来源。
 * ========================================================================== */

/**
 * 上电稳定等待(ms)。
 * DHT20 数据手册要求上电后 >=40 ms 才可通信；这里沿用旧实现验证过的 100 ms。
 * 若实测稳定可下调至 50，缩短上电到首帧的时延。
 */
#define APP_DHT20_POWER_ON_DELAY_MS 100U

/**
 * 单次转换的等待上限(ms)。DHT20 转换时间为 ~80 ms；
 * 调用方按 APP_DHT20_POLL_INTERVAL_MS 轮询就绪位，命中即返回，
 * 超时仍未就绪则判定失败（旧实现固定 HAL_Delay(500)，白等 400+ ms）。
 */
#define APP_DHT20_CONVERT_TIMEOUT_MS 100U

/**
 * 转换最短静默等待(ms)：先按器件规格（约 80 ms）等满，再开始轮询就绪位，
 * 避免在转换期内做无意义的空读 —— 典型情况 1 次读即可命中。
 * 必须 <= APP_DHT20_CONVERT_TIMEOUT_MS，否则驱动会直接把首轮读判为超时。
 */
#define APP_DHT20_CONVERT_MIN_MS 80U

/** 就绪轮询间隔(ms)：两次读状态之间的等待步长 */
#define APP_DHT20_POLL_INTERVAL_MS 10U

/** 器件在线探测的重试次数（覆盖上电抖动；旧实现无重试机制） */
#define APP_DHT20_PROBE_TRIALS 3U

/**
 * 是否校验测量结果尾字节 CRC8（多项式 0x31，初值 0xFF，覆盖前 6 字节）。
 * 置 0 可省去校验开销，但会丢失数据完整性保障；建议量产保持 1。
 */
#define APP_DHT20_CRC_ENABLE 1

/** 温湿度采样周期(ms)：正常运行时两次采样之间的间隔（自检路径不使用） */
#define APP_SENSOR_PERIOD_MS 10000U

#define APP_NET_PERIOD_MS 30000U

#define APP_NET_TIME_MS 1000U

/**
 * 是否启用传感器上电自检（语义与 APP_LCD_ENABLE_SELFTEST 一致）：
 * 扫描总线确认 0x38 在线 → 读取一次温湿度 → 分级日志输出；置 0 时为空实现。
 */
#define APP_SENSOR_ENABLE_SELFTEST 1

/* ==========================================================================
 * 四、日志与调试
 * ========================================================================== */

/* 日志统一使用 ESP_LOG；默认级别与编译期上限由 ESP-IDF
 * sdkconfig 中的 CONFIG_LOG_DEFAULT_LEVEL / CONFIG_LOG_MAXIMUM_LEVEL 配置。 */

/** 是否启用 CPU 周期计数（保留历史宏名；关闭后 port_cycles 返回 0） */
#define APP_PORT_DWT_ENABLE 1

/** 是否启用显示上电自检（PH1 的 app_display_selftest.c 用；产品版置 0）
 *  0 = 正常显示业务 UI（app_display_render 接管画面）
 *  1 = 上电自检图案，业务 UI 不接管 */
#define APP_LCD_ENABLE_SELFTEST 0

/* ==========================================================================
 * 五、GPIO 中断
 * ========================================================================== */

/** 历史平台优先级记录；ESP32 ISR 服务使用驱动默认优先级，不读取此宏 */
#define APP_GPIO_IRQ_PRIORITY 5U

/** 同一时刻可注册的 GPIO 中断数量上限 */
#define APP_GPIO_IRQ_MAX 8U

/* 注：断言宏（APP_STATIC_ASSERT / APP_ASSERT）与 port_assert_failed() 定义在
 *     Ports/common/port.h 中 —— 它们依赖 port 层的实现，不放配置头。 */

#endif /* APP_CONFIG_H */

/**
 * @file board.h
 * @brief ESP32-S3 板级接线配置：定义外设使用的 GPIO、控制器编号和传感器地址。
 *
 * 所有 *_GPIO 均直接使用 ESP32-S3 的 GPIO 编号，不是开发板排针的物理脚位序号。
 * 修改接线时直接修改对应的 *_GPIO，不需要分组或引脚转换。
 * 可选的屏幕 MISO 使用 -1 表示未连接；实际 GPIO 有效性由驱动检查。
 */
#ifndef BOARD_H
#define BOARD_H /* 头文件保护，防止重复包含 */

/* 屏幕 SPI 接线及控制信号。MOSI/MISO 的方向均以 ESP32-S3 为主机。 */
#define BOARD_TFT_SCK_GPIO  4       /* SPI 时钟输出，连接屏幕 SCK/CLK */
#define BOARD_TFT_MOSI_GPIO 5       /* SPI 数据输出，连接屏幕 MOSI/SDA/DIN */
#define BOARD_TFT_RST_GPIO  6       /* 屏幕硬件复位控制 */
#define BOARD_TFT_DC_GPIO   7       /* 命令/数据选择：区分控制命令与显示数据 */
#define BOARD_TFT_CS_GPIO   15      /* 屏幕片选，选择当前 SPI 设备 */
#define BOARD_TFT_BL_GPIO   16      /* 屏幕背光开关控制，有效电平见 BOARD_TFT_BL_ACTIVE_HIGH */
#define BOARD_TFT_MISO_GPIO (-1)    /* SPI 数据输入；-1 表示不连接，当前仅向屏幕发送数据 */

/* I2C 总线接线，供温湿度传感器等 I2C 设备使用。 */
#define BOARD_I2C_SCL_GPIO  10    /* I2C 时钟线 */
#define BOARD_I2C_SDA_GPIO  11    /* I2C 双向数据线 */

/* ESP-IDF 硬件控制器编号，与 GPIO 编号无关。 */
#define BOARD_SPI_LCD_INDEX 2   /* 屏幕使用 SPI2（GPSPI2）控制器 */
#define BOARD_I2C_BUS_INDEX 0   /* I2C 设备使用 I2C0 控制器 */

/* 背光控制极性，由屏幕驱动将“点亮/熄灭”转换为实际输出电平。 */
#define BOARD_TFT_BL_ACTIVE_HIGH 1 /* 1：高电平点亮背光；0：低电平点亮 */

/* 温湿度传感器的 7 位 I2C 地址，不含读写位；当前由 dht20 驱动使用，宏名沿用 AHT20。 */
#define BOARD_AHT20_I2C_ADDR7 0x38U

#endif

# ESP32-S3 Weather Clock

基于 **ESP32-S3 + ESP-IDF** 的天气时钟项目，使用 ST7789 彩屏显示室内温湿度、网络天气和时间。代码按应用、设备驱动、硬件适配和配置分层，便于修改接线、替换屏幕或扩展功能。

## 当前功能

- **室内温湿度**：通过 I²C 读取 DHT20，默认每 10 秒采样一次，支持 CRC 校验和上电自检。
- **网络天气**：以 Wi-Fi STA 模式联网，通过明文 HTTP 请求 `wttr.in`，默认查询武汉温度和湿度，启动联网后请求一次，之后每 30 秒更新。
- **网络时间**：解析 HTTP 响应头的 `Date`，转换为北京时间（UTC+8）。
- **彩屏显示**：ST7789、240 × 320、RGB565，显示室内温湿度、室外温湿度和时分秒。
- **局部刷新**：使用脏矩形和条带缓冲，无需整屏帧缓冲；默认条带占用 960 B，脏区表最多保存 8 个矩形，溢出时退化为整屏标脏。
- **基础外设**：包含 LED 和 BOOT 按键初始化、按键扫描接口，当前主循环未绑定按键交互。

> 当前为开发中的基础版本。`app_time_poll()` 已实现日历进位，但尚未接入周期调用，因此时间目前随网络请求更新，不会独立每秒走时。HTTP 失败和响应解析的应用层处理也仍需完善，网络异常时可能出现错误数据或运行异常。

## 硬件与接线

- ESP32-S3 开发板。
- ST7789 SPI 彩屏，当前配置为 240 × 320。
- DHT20 I²C 温湿度传感器，7 位地址 `0x38`；代码中部分宏沿用 AHT20 命名，其他型号需自行确认兼容性。
- 可访问互联网的 2.4 GHz Wi-Fi。

接线以 `components/BSP/CONFIG/board.h` 为准，GPIO 编号不是排针物理序号。

| 外设 | 信号 | ESP32-S3 GPIO |
| --- | --- | --- |
| ST7789 | SCK / CLK | 4 |
| ST7789 | MOSI / SDA / DIN | 5 |
| ST7789 | RST | 6 |
| ST7789 | DC | 7 |
| ST7789 | CS | 15 |
| ST7789 | BL | 16，高电平点亮 |
| ST7789 | MISO | 不连接 |
| DHT20 | SCL | 10 |
| DHT20 | SDA | 11 |
| LED | 控制引脚 | 2 |
| BOOT 按键 | 输入引脚 | 0，低电平按下 |

各模块需共地，供电按模块规格连接，GPIO 信号使用 3.3 V 电平。LED 和 BOOT 引脚分别在 `APP/LED/led.h`、`APP/KEY/key.h` 中配置，不同开发板需核对实际接线。

## 开发环境

- ESP-IDF 工具链及其配套 Python、CMake、Ninja。
- 当前 `sdkconfig` 标记的生成版本为 **ESP-IDF 6.1.0**，目标芯片为 `esp32s3`。建议先使用对应开发环境复现，其他版本兼容性尚未验证。
- 串口或开发板支持的 USB 烧录连接。

当前配置为 **2 MB Flash、单应用分区、未启用 PSRAM**。仓库附带 `partitions-16MiB.csv`，但当前 `sdkconfig` **没有启用**该分区表；文件名不代表当前实际 Flash 配置。

## 配置

### Wi-Fi

编辑 `components/BSP/DEVICES/net/Device_net.h`：

```c
#define ESP_WIFI_SSID "your-wifi-ssid"
#define ESP_WIFI_PASS "your-wifi-password"
#define ESP_MAXIMUM_RETRY 5
```

当前使用源码内配置，没有配网界面。仓库保留现有 Wi-Fi 配置；使用时请替换为自己的网络信息。

### 城市与时区

编辑 `components/BSP/APP/net/app_net.c` 的 `request`：

```c
.host = "wttr.in",
.port = "80",
.path = "/Wuhan?format=%25t%7C%25h&m",
.accept = "text/plain",
.timeout_ms = 5000,
```

将 `Wuhan` 替换为目标城市的英文名；格式参数用于获取摄氏温度和湿度。当前时区由 `app_net_parse()` 中的 `+8` 小时逻辑固定为北京时间，未实现通用时区或夏令时配置。

### 显示与采样

| 文件 | 配置内容 |
| --- | --- |
| `components/BSP/CONFIG/board.h` | 屏幕、I²C 引脚及传感器地址 |
| `components/BSP/CONFIG/display_cfg.h` | 分辨率、面板偏移、条带缓冲、脏区容量 |
| `components/BSP/CONFIG/app_config.h` | SPI/I²C 频率、采样周期、网络更新周期、自检开关 |

默认 SPI 目标频率为 36 MHz，I²C 为 100 kHz。显示由应用轮询驱动，ESP-IDF 本身运行 FreeRTOS。

当前 SPI 使用**同步 FIFO 传输，未启用 DMA**，内部通过 64 B 缓冲分块发送。配置中保留的 `DMA` 命名不代表已实现异步 DMA 刷新。

## 初始化与驱动接口

`main/main.c` 初始化 LED、按键并调用 `app_init()`，随后每轮执行 `app_loop()` 并延时 10 ms。应用层依次初始化显示、传感器与网络；主循环负责周期采样、天气更新和显示轮询。

- **公共适配**：时间与 OS 适配无需显式调用 `port_init()` 或 `os_port_init()`；相关接口已移除。互斥量由使用模块按需初始化，Wi-Fi 连接状态使用 FreeRTOS 事件组。
- **显示初始化**：`lcd_dev_init()` 内部完成显示 GPIO、SPI 与面板初始化，无需预先初始化显示总线；背光由应用在首帧绘制后点亮。
- **传感器初始化**：`dht20_init()` 内部初始化 I²C，首次调用执行上电等待，后续通信失败重试不重复该等待。
- **错误返回**：显示操作通过 `lcd_err_t` 返回结果，画布绘制错误保存在 `lcd_canvas_t.err`；不再提供全局总线错误查询与清除接口。显示自检检查本次绘制结果，失败时以背光快闪 10 次提示。
- **SPI 超时处理**：超时后事务可能仍由 ESP-IDF 驱动持有。总线层在下次传输前等待旧事务完成，显示适配层在修改 CS/DC 前调用 `spi_port_wait_idle()`，避免复用仍在使用的事务和缓冲区。
- **接口调用约定**：画布、字体、传感器输出等参数需满足对应头文件约定；例如 `dht20_read()` 的输出指针必须非空，`lcd_dirty_take_all()` 的输出数组至少容纳 `DISPLAY_DIRTY_MAX` 个矩形。

## 编译与烧录

先进入已激活 ESP-IDF 的终端，例如运行本机 ESP-IDF 安装目录中的 `export.sh`。然后执行：

```bash
git clone https://github.com/Elizebeths/weatherclock_base_on_ESP32S3_ESP-IDF.git
cd weatherclock_base_on_ESP32S3_ESP-IDF

# 先按上文修改 Wi-Fi 和接线配置
idf.py menuconfig
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

仓库的 `sdkconfig` 已设置目标芯片为 `esp32s3`。串口名称按本机设备替换，例如 Linux 的 `/dev/ttyACM0` 或 macOS 的 `/dev/cu.usbmodem...`。串口监视器通常使用 `Ctrl+]` 退出。

如需使用附带的 16 MiB 分区表，先确认硬件确实具有 16 MiB Flash，再在 `menuconfig` 中将 Flash size 配置为 16 MB，并选择 Custom partition table，填写 `partitions-16MiB.csv`。当前应用未实现 OTA，也未挂载该分区表中的 FAT/SPIFFS 数据分区。

## 目录结构

```text
.
├── CMakeLists.txt             # ESP-IDF 工程入口
├── CMakePresets.json          # 开发与生产构建目录预设
├── sdkconfig                 # 当前 ESP32-S3 工程配置
├── partitions-16MiB.csv       # 可选分区表，当前未启用
├── main/
│   └── main.c                # app_main()、初始化与主循环
└── components/
    ├── BSP/
    │   ├── APP/              # 显示、传感器、网络、LED 和按键应用
    │   ├── CONFIG/           # 接线、功能与显示配置
    │   ├── DEVICES/          # 设备层
    │   │   ├── display/      # 显示生命周期、错误码与公共类型
    │   │   │   ├── gfx/      # 画布、绘图、脏区与条带刷新
    │   │   │   ├── panel/    # ST7789 面板与寄存器序列
    │   │   │   └── text/     # 字库、文本与定点数绘制
    │   │   ├── sensor/       # DHT20 驱动与采样类型
    │   │   └── net/          # Wi-Fi 与 HTTP
    │   └── PORTS/            # ESP-IDF 总线、GPIO、OS、时间与显示适配
    └── Middlewares/          # 中间件预留目录
```

## 调试提示与当前限制

- **屏幕不亮或显示偏移**：核对 `board.h` 的接线、背光极性以及 `display_cfg.h` 中的分辨率和面板偏移；可通过 `APP_LCD_ENABLE_SELFTEST=1` 启用显示自检。
- **室内数据显示占位符**：查看传感器自检日志，确认 I²C 接线、供电和 `0x38` 地址；上电自检默认启用。
- **网络数据异常**：检查 Wi-Fi、DNS 和到 `wttr.in:80` 的连通性。当前应用层没有充分检查 HTTP 返回结果、状态码和字段解析是否成功，需要完善后再用于长期无人值守运行。
- **时间不逐秒变化**：目前 `app_time_poll()` 没有被调用，需后续接入周期调度。
- **刷新短暂停顿**：网络请求和传感器采样使用阻塞调用，会影响界面轮询刷新。
- **传输失败后画面缺失**：`lcd_refresh_flush()` 返回本轮错误，失败及尚未提交的脏区不会自动重排。总线恢复后需重新调用 `lcd_invalidate()` 或 `lcd_invalidate_screen()` 标记刷新；当前轮询入口未向应用上报刷新错误。

本次整理仅核对源码与文档，未执行固件编译或硬件验证。

## 许可证

仓库包含 [GNU GPL v3](LICENSE) 许可证，使用与分发请遵守相应条款。

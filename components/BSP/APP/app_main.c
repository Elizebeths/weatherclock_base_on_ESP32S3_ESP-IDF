#include "app_main.h"
#include "app_config.h"
#include "common/os_port.h"
#include "common/port.h"
#include "display/app_display.h"
#include "net/app_net.h"
#include "sensor/app_sensor.h"

extern int request_year;
extern uint8_t request_month;
extern uint8_t request_day;
extern uint8_t request_hour;
extern uint8_t request_min;
extern uint8_t request_sec;

void app_time_poll(void)
{
    static const uint8_t days_in_month[] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    /* 尚未取得有效网络时间时不走时，也避免非法月份访问数组。 */
    if ((request_year < 1) || (request_month < 1U) || (request_month > 12U) || (request_hour > 23U) ||
        (request_min > 59U) || (request_sec > 59U))
    {
        return;
    }

    uint8_t month_days = days_in_month[request_month - 1U];
    if ((request_month == 2U) && (request_year % 4 == 0) && ((request_year % 100 != 0) || (request_year % 400 == 0)))
    {
        month_days = 29U;
    }
    if ((request_day < 1U) || (request_day > month_days))
    {
        return;
    }

    if (++request_sec < 60U)
        return;
    request_sec = 0U;
    if (++request_min < 60U)
        return;
    request_min = 0U;
    if (++request_hour < 24U)
        return;
    request_hour = 0U;
    if (++request_day <= month_days)
        return;
    request_day = 1U;
    if (++request_month <= 12U)
        return;
    request_month = 1U;
    ++request_year;
}

void app_init(void)
{
    app_display_init();
    /* 传感器：先建立业务状态，再跑一次上电自检（自检关闭时为空实现） */
    app_sensor_init();
    app_sensor_selftest();
    app_net_init();
}

void app_loop(void)
{
    /* 传感器：按 APP_SENSOR_PERIOD_MS 周期采样（内部单帧阻塞约一次转换时间） */
    static uint32_t s_last_sensor_ms = 0U;
    static uint32_t s_last_net_ms = 0U;
    const uint32_t now_ms = port_time_ms();

    if ((uint32_t)(now_ms - s_last_sensor_ms) >= APP_SENSOR_PERIOD_MS)
    {
        s_last_sensor_ms = now_ms;
        app_sensor_poll();
    }

    if ((uint32_t)(now_ms - s_last_net_ms) >= APP_NET_PERIOD_MS)
    {
        s_last_net_ms = now_ms;
        // app_net_poll();// 进入周期程序

        // static bool request_weather_valid = false;
        // static bool request_time_valid = false;
        app_net_update();
    }

    /* 显示：更新动态内容并驱动刷新 */
    app_display_poll();
}

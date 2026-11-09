#include "app_main.h"
#include "app_config.h"
#include "display/app_display.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net/app_net.h"
#include "sensor/app_sensor.h"
#include <stdlib.h>
#include <time.h>

/* 单元素队列只保留最新快照；生产者覆盖写入，显示任务接收时复制数据。 */
static QueueHandle_t s_sensor_queue;
static QueueHandle_t s_net_queue;

static void ui_task(void *arg)
{
    sensor_sample_t sensor;
    app_net_sample_t net;
    /* 首次收到有效数据后保持置位，避免将未初始化的快照传给显示模块。 */
    bool has_sensor = false;
    bool has_net = false;

    app_display_init();
    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
        /* 非阻塞读取；没有新数据时沿用任务内保存的上一份快照。 */
        if (xQueueReceive(s_sensor_queue, &sensor, 0) == pdTRUE)    has_sensor = true;
        if (xQueueReceive(s_net_queue, &net, 0) == pdTRUE)          has_net = true;

        struct tm clock;
        if (has_net)
        {
            /* 单调时钟补偿任务延迟；用 UTC+8 的时间戳经 gmtime_r 得到北京时间。 */
            time_t now = net.utc + (esp_timer_get_time() - net.synced_at_us) / 1000000 + 8 * 3600;
            gmtime_r(&now, &clock);
        }
        /* NULL 表示尚无有效数据；显示模块据此处理未就绪状态。 */
        app_display_poll(has_sensor ? &sensor : NULL, has_net ? &net : NULL, has_net ? &clock : NULL);
        /* 以预定唤醒时刻为基准维持刷新周期，减少处理耗时造成的周期漂移。 */
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_UI_PERIOD_MS));
    }
}

static void sensor_task(void *arg)
{
    app_sensor_init();
    /* 自检是否执行由传感器模块的编译配置决定。 */
    app_sensor_selftest();
    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
        sensor_sample_t sample;
        /* 失败时不发布数据，显示任务继续保留此前的有效读数。 */
        if (app_sensor_poll(&sample))   xQueueOverwrite(s_sensor_queue, &sample);
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));
    }
}

static void net_task(void *arg)
{
    /* 初始化会启动异步 Wi-Fi 连接，连接结果通过事件组通知。 */
    app_net_init();
    for (;;)
    {
        // 判断网络连接状态，等待连接结果
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((bits & WIFI_CONNECTED_BIT) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(APP_NET_RETRY_MS));
            app_net_reconnect();
            continue;
        }

        app_net_sample_t sample;
        /* 天气及时间均解析成功后才发布，避免显示任务收到不完整快照。 */
        const bool success = app_net_update(&sample);
        if (success)    xQueueOverwrite(s_net_queue, &sample);

        /* 完成后计时，不补跑请求；断线事件可提前结束等待。 */
        xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(success ? APP_NET_PERIOD_MS : APP_NET_RETRY_MS));
    }
}

/** @brief 设置时区并创建通信队列和三个业务任务，仅在启动时调用一次。 */
void app_init(void)
{
    /* HTTP Date 按 UTC 解析；显示的北京时间通过显式偏移计算。 */
    ESP_ERROR_CHECK(setenv("TZ", "UTC0", 1) == 0 ? ESP_OK : ESP_FAIL);
    tzset();

    /* 先创建全部队列，再启动任务，确保任务运行时通信资源已就绪。 */
    s_sensor_queue = xQueueCreate(1, sizeof(sensor_sample_t));
    s_net_queue = xQueueCreate(1, sizeof(app_net_sample_t));
    ESP_ERROR_CHECK(s_sensor_queue && s_net_queue ? ESP_OK : ESP_ERR_NO_MEM);

    /* 各任务的栈大小和优先级由 app_config.h 配置，创建失败则报告错误并终止。 */
    BaseType_t ret = xTaskCreate(ui_task, "ui", APP_UI_STACK_BYTES, NULL, APP_UI_PRIORITY, NULL);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ret = xTaskCreate(sensor_task, "sensor", APP_SENSOR_STACK_BYTES, NULL, APP_SENSOR_PRIORITY, NULL);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ret = xTaskCreate(net_task, "net", APP_NET_STACK_BYTES, NULL, APP_NET_PRIORITY, NULL);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

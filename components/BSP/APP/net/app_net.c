#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "net/Device_net.h"
#include "esp_log.h"
#include <string.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>

static const char *TAG = "app_net";

extern EventGroupHandle_t s_wifi_event_group;
char date[64]; // 存放 Date 字段的值
int request_year = 2026;
uint8_t request_month = 11;
uint8_t request_day = 4;
uint8_t request_hour = 12;
uint8_t request_min = 0;
uint8_t request_sec = 0;

bool request_valid = false;
int request_temp = 0;
uint8_t request_humi = 0;

const Device_http_request_t request =   {
                                            .host = "wttr.in",
                                            .port = "80",
                                            .path = "/Wuhan?format=%25t%7C%25h&m",
                                            .accept = "text/plain",
                                            .timeout_ms = 5000,
                                        };
char body[1024];
Device_http_response_t response;

// 从 date 和 body 中解析出年月日时分秒、温度和湿度，存入全局变量，时间转换为北京时间，request_valid 标记是否成功解析。
static void app_net_parse(void)
{
    char month[4];
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    sscanf(date, "%*s %hhu %3s %d %hhu:%hhu:%hhu",
           &request_day, month, &request_year,
           &request_hour, &request_min, &request_sec);

    request_month = (strstr(months, month) - months) / 3 + 1;

    days[1] += (request_year % 4 == 0 &&
               (request_year % 100 != 0 || request_year % 400 == 0));

    request_hour += 8;
    if (request_hour >= 24)
    {
        request_hour -= 24;
        if (++request_day > days[request_month - 1])
        {
            request_day = 1;
            if (++request_month > 12)
            {
                request_month = 1;
                ++request_year;
            }
        }
    }

    sscanf(body, "%d%*[^|]|%hhu", &request_temp, &request_humi);
    request_valid = true;
}

void app_net_init()
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    Device_net_connect();

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);

    if (bits & WIFI_CONNECTED_BIT)
    {
        // 初始化时请求一次数据
        Device_net_http_get_data(&request, date, sizeof(date), body, sizeof(body), &response);
        app_net_parse();
        // ESP_LOGI(TAG, "HTTP status code: %d", response.status_code);
        // ESP_LOGI(TAG, "HTTP date: %s", date);
        // ESP_LOGI(TAG, "HTTP body size: %zu", response.body_size);
        // ESP_LOGI(TAG, "HTTP body: %s", body);
    }
    else
    {
        ESP_LOGE(TAG, "Wi-Fi unavailable, skip weather request");
    }
    ESP_LOGI(TAG, "现在的时间是%d年%d月%d日 %d:%d:%d",request_year,request_month,request_day,request_hour,request_min,request_sec);
    ESP_LOGI(TAG, "天气数据为%dC  %d%%",request_temp,request_humi);
}

void app_net_update(void)
{
    Device_net_http_get_data(&request, date, sizeof(date), body, sizeof(body), &response);
    app_net_parse();
}
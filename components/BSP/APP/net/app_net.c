#include "net/app_net.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_net";

static const Device_http_request_t request = {
    .host = "wttr.in",
    .port = "80",
    .path = "/Wuhan?format=%25t%7C%25h&m",
    .accept = "text/plain",
    .timeout_ms = 5000,
};

static bool app_net_parse(const char *date, const char *body, app_net_sample_t *out)
{
    char month[4], zone[4];
    int day, year, hour, minute, second, used = 0;
    if (sscanf(date, "%*3s, %2d %3s %4d %2d:%2d:%2d %3s %n",
               &day, month, &year, &hour, &minute, &second, zone, &used) != 7 ||
        used == 0 || date[used] != '\0' || strcmp(zone, "GMT") != 0 ||
        year < 1970 || year > 9999 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month_index = 0;
    while (month_index < 12 && strcmp(month, months[month_index]) != 0)
        ++month_index;
    if (month_index == 12)
        return false;

    struct tm utc = {.tm_year = year - 1900, .tm_mon = month_index, .tm_mday = day,
                     .tm_hour = hour, .tm_min = minute, .tm_sec = second, .tm_isdst = 0};
    /* app_init() 已固定 TZ=UTC0；mktime 会规范化非法日期，需检查规范化结果。 */
    time_t epoch = mktime(&utc);
    if (epoch == (time_t)-1 || utc.tm_year != year - 1900 ||
        utc.tm_mon != month_index || utc.tm_mday != day)
        return false;

    char *end;
    errno = 0;
    long temp = strtol(body, &end, 10);
    if (end == body || errno == ERANGE || temp < -100 || temp > 100)
        return false;
    /* wttr.in 的公制温度通常带 UTF-8 的 °C，也接受无单位整数。 */
    if (strncmp(end, "\xC2\xB0" "C", 3) == 0)
        end += 3;
    else if (*end == 'C')
        ++end;
    if (*end++ != '|')
        return false;
    const char *humidity = end;
    errno = 0;
    long humi = strtol(humidity, &end, 10);
    if (end == humidity || errno == ERANGE || humi < 0 || humi > 100)
        return false;
    if (*end == '%')
        ++end;
    while (isspace((unsigned char)*end))
        ++end;
    if (*end != '\0')
        return false;

    out->temp_c = (int)temp;
    out->humi_rh = (uint8_t)humi;
    out->utc = epoch;
    return true;
}

void app_net_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Device_net_connect();
}

void app_net_reconnect(void)
{   
    if (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT))
    {
        /* 清除旧失败状态；连接调用立即失败时重新置位，以便继续重试。 */
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        if (Device_net_reconnect() != ESP_OK)   xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    
}

bool app_net_update(app_net_sample_t *out)
{
    char date[64];
    char body[1024];
    Device_http_response_t response;
    app_net_sample_t sample = {0};
    esp_err_t ret = Device_net_http_get_data(&request, date, sizeof(date), body, sizeof(body), &response);
    if (ret != ESP_OK || response.status_code != 200 || !app_net_parse(date, body, &sample))
    {
        ESP_LOGW(TAG, "weather update failed (err=%d, status=%d)", (int)ret, response.status_code);
        return false;
    }
    sample.synced_at_us = esp_timer_get_time();
    *out = sample;
    return true;
}

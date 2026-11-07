#include "net/Device_net.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>


#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>

static const char *TAG = "wifi station";
static int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // 判断事件类型
    // WIFI事件，再细分是连接事件还是断开事件
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // 连接事件则连接到指定的 AP
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // 断开事件则尝试重新连接
        if (s_retry_num < ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            // 如果重试次数超过最大值，则设置失败位
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    // IP事件
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void Device_net_connect()
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理程序
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_sta_config_t sta_config = {
        .ssid = ESP_WIFI_SSID,
        .password = ESP_WIFI_PASS,
        .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
        .sae_h2e_identifier = H2E_IDENTIFIER,
        .disable_wpa3_compatible_mode = 0,
    };
    wifi_config_t wifi_config = {.sta = sta_config};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_station finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static esp_err_t Device_net_http_parse(char *response, char *date, size_t date_capacity, char *body, size_t body_capacity,
                                       Device_http_response_t *out_response)
{
    // 提取状态码
    char *header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL || sscanf(response, "HTTP/%*s %d", &out_response->status_code) != 1)
    {
        return ESP_FAIL;
    }

    // 只在响应头中逐行查找 Date，字段名区分大小写。
    const char *date_value = NULL;
    size_t date_length = 0;
    const char *line = strstr(response, "\r\n") + 2;
    while (line < header_end)
    {
        const char *line_end = strstr(line, "\r\n");
        if (line_end - line >= 5 && strncmp(line, "Date:", 5) == 0)
        {
            date_value = line + 5;
            while (date_value < line_end && (*date_value == ' ' || *date_value == '\t'))
            {
                date_value++;
            }
            const char *value_end = line_end;
            while (value_end > date_value && (value_end[-1] == ' ' || value_end[-1] == '\t'))
            {
                value_end--;
            }
            date_length = value_end - date_value;
            break;
        }
        line = line_end + 2;
    }

    // 空行之后就是正文
    char *content = header_end + 4;
    size_t body_size = strlen(content);
    if (date_length >= date_capacity || body_size >= body_capacity)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if (date_value != NULL)
    {
        memcpy(date, date_value, date_length);
    }
    date[date_length] = '\0';
    out_response->body_size = body_size;
    memcpy(body, content, out_response->body_size + 1);
    return ESP_OK;
}

esp_err_t Device_net_http_get_data(const Device_http_request_t *request, char *date, size_t date_capacity, char *body, size_t body_capacity,
                                   Device_http_response_t *out_response)
{
    if (request == NULL || date == NULL || date_capacity == 0 || body == NULL || body_capacity == 0 || out_response == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    // 清空回传参数
    date[0] = '\0';
    body[0] = '\0';
    out_response->status_code = 0;
    out_response->body_size = 0;

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };
    struct addrinfo *addresses = NULL;

    // DNS解析
    int err = getaddrinfo(request->host, request->port, &hints, &addresses);
    if (err != 0 || addresses == NULL)
    {
        ESP_LOGE(TAG, "DNS lookup failed: %d", err);
        return ESP_FAIL;
    }

    // 遍历解析结果，尝试连接
    int sock = -1;
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next)
    {
        sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock < 0)
        {
            continue;
        }
        const struct timeval timeout_s = {.tv_sec = request->timeout_ms / 1000, .tv_usec = (request->timeout_ms % 1000) * 1000};
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_s, sizeof(timeout_s)) != 0 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout_s, sizeof(timeout_s)) != 0)
        {
            ESP_LOGE(TAG, "Unable to set socket timeout: errno %d", errno);
            close(sock);
            sock = -1;
            continue;
        }
        if (connect(sock, address->ai_addr, address->ai_addrlen) == 0)
        {
            break;
        }
        ESP_LOGW(TAG, "Connection failed: errno %d", errno);
        close(sock);
        sock = -1;
    }
    freeaddrinfo(addresses);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to connect to %s:%s", request->host, request->port);
        return ESP_FAIL;
    }

    // 构造HTTP请求
    char request_buffer[512];
    int request_buffer_len =
        snprintf(request_buffer, sizeof(request_buffer), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.0\r\nConnection: close\r\n\r\n",
                 request->path, request->host);

    // 逐步发送HTTP请求，确保完整发送
    size_t sent = 0;
    while (sent < request_buffer_len)
    {
        // send 返回 0 时表示连接已关闭，返回负数表示出错，返回正数表示发送的字节数
        int count = send(sock, request_buffer + sent, request_buffer_len - sent, 0);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            ESP_LOGE(TAG, "HTTP request failed: errno %d", errno);
            close(sock);
            return ESP_FAIL;
        }
        sent += count;
    }

    // 等待直至接收HTTP响应完成
    char response_buffer[2048]; // 存放完整的响应包
    size_t received = 0;        // 已接收大小
    esp_err_t result = ESP_OK;
    while (1)
    {
        size_t available = sizeof(response_buffer) - received - 1; // response_buffer 剩余空间

        if (available == 0)
        {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }

        int count = recv(sock, response_buffer + received, available, 0);

        if (count < 0)
        {
            result = ESP_FAIL;
            break;
        }

        if (count == 0)
        {
            response_buffer[received] = '\0';
            result = Device_net_http_parse(response_buffer, date, date_capacity, body, body_capacity, out_response);
            break;
        }
        received += count;
    }
    putchar('\n');
    close(sock);
    return result;
}

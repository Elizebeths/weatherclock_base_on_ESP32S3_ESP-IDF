#ifndef DEVICE_NET_H
#define DEVICE_NET_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define ESP_WIFI_SSID "www"
#define ESP_WIFI_PASS "www123456"
#define ESP_MAXIMUM_RETRY 5

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define H2E_IDENTIFIER ""

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

/* FreeRTOS 事件组，用来表示WIFI连接状态 */
extern EventGroupHandle_t s_wifi_event_group;

/* 事件组允许每个事件有多个位，但我们只关心两个事件：
 * - 我们通过一个 IP 连接到了 AP
 * - 在达到最大重试次数后，我们还是没能连接上 */
//  定义事件组位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef struct
{
    const char *host;    /* 例如 wttr.in */
    const char *port;       /* 当前为 80 */
    const char *path;    /* 包含查询参数 */
    const char *accept;  /* NULL 使用默认值 */
    uint32_t timeout_ms; /* 单次阻塞收发超时 */
} Device_http_request_t;

typedef struct
{
    int status_code;  /* HTTP 状态码 */
    size_t body_size; /* 正文长度，不包含 '\0' */
} Device_http_response_t;

// 连接WIFI
void Device_net_connect(void);

/*
 * body 只返回正文，不包含响应头。
 * 调用者提供缓冲区，成功时以 '\0' 结尾。
 * 缓冲区不足、超时或响应不完整时返回错误，不静默截断。
 * ESP_OK 表示 HTTP 响应完整获取；404、500 等通过 out_response 的 status_code 返回。
 * 每次调用自行建立和关闭连接，所有失败路径都释放资源。
 * 支持明文 HTTP。
*/
esp_err_t Device_net_http_get_data(const Device_http_request_t *request, char *date, size_t date_capacity, char *body, size_t body_capacity,
                                   Device_http_response_t *out_response);

#endif /* DEVCIE_NET_H */

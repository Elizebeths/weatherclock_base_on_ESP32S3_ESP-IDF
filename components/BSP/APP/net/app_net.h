#ifndef APP_NET_H
#define APP_NET_H
#include "net/Device_net.h"

typedef struct
{
    const char *city; /* 例如 Wuhan */
    uint32_t timeout_ms;
} App_weather_request_t;

extern int request_year;
extern uint8_t request_month;
extern uint8_t request_day;
extern uint8_t request_hour;
extern uint8_t request_min;
extern uint8_t request_sec;

extern bool request_valid;
extern int request_temp;
extern uint8_t request_humi;
/* 
 * 这个函数负责：
 * 1. 校验城市参数，进行 URL 编码。
 * 2. 选择天气服务地址，构造请求路径。
 * 3. 调用 Device_net_http_get()。
 * 4. 检查 HTTP 状态码和业务响应。
 * 5. 返回天气文本，长度不包含末尾 '\0'。
 * 天气服务地址、format=3、请求头等细节都封装在 app 内。app_net_data_fetch() 不直接打印，方便调用方将结果用于串口、屏幕或其他用途。
 * 
*/
esp_err_t app_net_data_fetch(const App_weather_request_t *request, char *text, size_t text_capacity, size_t *out_text_size);

void app_net_init(void);
void app_net_update(void);

#endif
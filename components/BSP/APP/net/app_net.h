#ifndef APP_NET_H
#define APP_NET_H
#include "net/Device_net.h"
#include <stdbool.h>
#include <time.h>

typedef struct
{
    const char *city; /* 例如 Wuhan */
    uint32_t timeout_ms;
} App_weather_request_t;

/* 一次成功响应的完整快照，通过队列复制给显示任务。 */
typedef struct
{
    int temp_c;
    uint8_t humi_rh;
    time_t utc;
    int64_t synced_at_us;
} app_net_sample_t;

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

/* 以下接口仅由网络任务调用；初始化启动异步连接，不等待联网。 */
void app_net_init(void);
/* 请求、状态码及字段均有效才写入 out；失败时保留调用方旧数据。 */
bool app_net_update(app_net_sample_t *out);

void app_net_reconnect(void);

#endif
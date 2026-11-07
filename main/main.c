#include "KEY/key.h"
#include "LED/led.h"
#include "app_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    led_init();
    key_init();
    app_init();

    while (1)
    {
        app_loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#include "led.h"
#include "esp_err.h"

void led_init(void)
{
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT;

    io_conf.pull_up_en = GPIO_PULLUP_DISABLE; 
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << LED_GPIO_PIN);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    LED(PIN_RESET);

}

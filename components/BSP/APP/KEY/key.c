#include "key.h" 

void key_init(void)
{
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;

    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; 
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << BOOT_GPIO_PIN);

    gpio_config(&io_conf);
}

uint8_t key_scan(uint8_t mode)
{
    uint8_t key_val = 0;
    static uint8_t key_boot = 1;
    if (mode) {
        key_boot = 1;
    }

    // key_boot == 1表示允许检测，且检测到按键按下（高电平）
    if (key_boot && (BOOT == BOOT_PRES)) {
        vTaskDelay(10);
        key_boot = 0;
        if (BOOT == BOOT_PRES) {
            key_val = 1;
        }
    } else if (BOOT == !BOOT_PRES) {
        key_boot = 1;
    }
    return key_val;
}
#ifndef __LED_H__
#define __LED_H__

#include "driver/gpio.h"

#define LED_GPIO_PIN GPIO_NUM_2

typedef enum {
    PIN_RESET = 0,
    PIN_SET = 1
} GPIO_OUTPUT_STATE;

#define LED(x){ x ? \
    gpio_set_level(LED_GPIO_PIN, PIN_SET) :\
    gpio_set_level(LED_GPIO_PIN, PIN_RESET);}

#define LED_TOGGLE(){\
    int level = gpio_get_level(LED_GPIO_PIN);\
    gpio_set_level(LED_GPIO_PIN, !level);\
}

void led_init(void);

#endif

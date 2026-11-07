set (srcs_dirs      APP
                    APP/LED
                    APP/KEY
                    APP/display
                    APP/sensor
                    APP/net
                    
                    DEVICES/display
                    DEVICES/display/gfx
                    DEVICES/display/panel
                    DEVICES/display/text
                    DEVICES/sensor
                    DEVICES/net

                    PORTS/common
                    PORTS/bus
                    PORTS/gpio
                    PORTS/display)

        

set(include_dirs    APP
                    DEVICES
                    CONFIG
                    PORTS)


set(requires    esp_driver_gpio
                esp_timer
                esp_driver_gptimer
                esp_driver_ledc
                esp_driver_i2c
                esp_driver_spi
                esp_wifi
                nvs_flash
                esp_netif
                esp_event
                esp_hw_support
                esp_system
                esp_rom
                freertos)

idf_component_register(
    SRC_DIRS ${srcs_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -ffast-math
    -O3
    -Wno-error=format
    -Wno-format
)

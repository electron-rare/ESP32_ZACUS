idf_component_register(
    SRCS
        "main.c"
        "../../../lib/espnow_common/espnow_slave.c"
    INCLUDE_DIRS
        "."
        "../../../lib/espnow_common"
    REQUIRES
        freertos
        driver
        esp_wifi
        esp_now
        nvs_flash
        led_strip
        esp_common
)

# Link math library for sinf()
target_link_libraries(${COMPONENT_LIB} PUBLIC m)

idf_component_register(
    SRCS "main.c" "voice_ws_client.c" "scenario_server.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        json
        nvs_flash
        spiffs
)

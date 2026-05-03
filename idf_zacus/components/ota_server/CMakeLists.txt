idf_component_register(
    SRCS
        "ota_server.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        app_update
        esp_timer
        esp_system
        nvs_flash
        mbedtls
        freertos
)

# Firmware metadata injected at compile time
# Override these in the puzzle's CMakeLists.txt before adding the component
if(NOT DEFINED OTA_FIRMWARE_NAME)
    set(OTA_FIRMWARE_NAME "zacus_puzzle")
endif()

if(NOT DEFINED OTA_FIRMWARE_VERSION)
    set(OTA_FIRMWARE_VERSION "1.0.0")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OTA_FIRMWARE_NAME="${OTA_FIRMWARE_NAME}"
    OTA_FIRMWARE_VERSION="${OTA_FIRMWARE_VERSION}"
)

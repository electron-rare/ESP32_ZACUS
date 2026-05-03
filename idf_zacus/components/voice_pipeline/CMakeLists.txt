idf_component_register(
    SRCS
        "voice_pipeline.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_i2s
        esp_timer
        esp_system
        nvs_flash
        freertos
        log
        media_manager
        npc_engine
        hints_client
        # Slice 6: esp-sr managed component (Espressif AFE + WakeNet9).
        # Listed in main/idf_component.yml so the component manager
        # pulls it; this REQUIRES line is what lets us include
        # esp_afe_*.h / esp_wn_*.h from voice_pipeline.c.
        espressif__esp-sr
)

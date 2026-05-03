## Zacus npc_engine — ESP-IDF port (slice 4, P1).
##
## Ports the Arduino `npc_engine` decision module
## (ui_freenove_allinone/src/npc/npc_engine.cpp, 198 LOC) into an IDF
## component. The Arduino sources were already pure C with `extern "C"`
## guards and zero Arduino-runtime calls, so the port reuses the same
## state machine verbatim and only adapts the wrapper layer (init/update/
## trigger entry points) to the IDF idioms (esp_err_t returns, esp_log,
## media_manager integration). The hint-request HTTP path remains a stub
## (callback invoked synchronously with a hardcoded text) until the
## hints-engine HTTP client lands in slice 6.
##
## Persistence of "cues already played" lives in RAM only at this slice;
## an NVS-backed log can be added once a real trigger source feeds the
## engine.

idf_component_register(
    SRCS
        "npc_engine.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        media_manager
        esp_timer
        esp_system
        nvs_flash
        freertos
        log
)

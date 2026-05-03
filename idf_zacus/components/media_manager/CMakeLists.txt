## Zacus media_manager — ESP-IDF port (slice 3, P1).
##
## Ports the Arduino `MediaManager` C++ class
## (ui_freenove_allinone/src/system/media/media_manager.cpp, 416 LOC) into a
## C-only ESP-IDF component. Public API exposes catalog browsing + play/stop
## + recording control. Real MP3 decoding is intentionally deferred to a
## later slice (see TODO in media_manager.c). The recorder code path is
## stubbed in the same way to keep the dependency surface minimal until the
## ES8388 / I2S microphone wiring is brought up under IDF.

idf_component_register(
    SRCS
        "media_manager.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
        esp_system
        joltwallet__littlefs
)

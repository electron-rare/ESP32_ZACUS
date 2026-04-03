cmake_minimum_required(VERSION 3.16)

# ESP-IDF project for P6 Symboles Alchimiques (NFC-based)
# Target: ESP32-DevKitC-32E + MFRC522 SPI
# Build: idf.py build  |  Flash: idf.py -p /dev/ttyUSB0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(p6_symboles_nfc)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/espnow_common
)

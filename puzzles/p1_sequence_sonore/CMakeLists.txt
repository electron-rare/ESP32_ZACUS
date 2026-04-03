cmake_minimum_required(VERSION 3.16)

# ESP-IDF project for P1 Séquence Sonore puzzle
# Target: ESP32-S3-DevKitC-1-N16R8
# Build: idf.py build  |  Flash: idf.py -p /dev/ttyUSB0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(p1_sequence_sonore)

# Add the shared ESP-NOW slave library
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/espnow_common
)

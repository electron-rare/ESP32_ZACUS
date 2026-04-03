cmake_minimum_required(VERSION 3.16)

# ESP-IDF project for P5 Code Morse puzzle
# Target: ESP32-DevKitC-32E
# Build: idf.py build  |  Flash: idf.py -p /dev/ttyUSB0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(p5_morse)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/espnow_common
)

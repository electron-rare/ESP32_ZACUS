cmake_minimum_required(VERSION 3.16)

# ESP-IDF project for P7 Coffre Final
# Target: ESP32-DevKitC-32E + 4×3 keypad + SG90 servo + SSD1306 OLED
# Build: idf.py build  |  Flash: idf.py -p /dev/ttyUSB0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(p7_coffre)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/espnow_common
)

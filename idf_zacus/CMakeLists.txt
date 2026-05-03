# Zacus master ESP-IDF project
# Coexists with the Arduino tree under ui_freenove_allinone/ — this scaffold
# is the future home of the master firmware (P1 of the voice pipeline spec
# 2026-05-03-voice-pipeline-esp-sr-design.md).

cmake_minimum_required(VERSION 3.16)

# Local components live under idf_zacus/components/ (e.g. ota_server inherited
# from the 2026-04-03 IDF bootstrap).
set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zacus_master)

cmake_minimum_required(VERSION 3.16)

# Flashable first-trial firmware for the Waveshare ESP32-P4-ETH board.
# The protocol core remains a separate ESP-IDF component one directory above.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arstack61850_esp32p4_sv_trial)

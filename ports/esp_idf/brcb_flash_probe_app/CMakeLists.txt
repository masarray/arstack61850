cmake_minimum_required(VERSION 3.16)

# Destructive-on-probe-sector flash geometry/latency measurement image.
# The BRCB journal banks themselves are never touched by the probe routine.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arstack61850_esp32p4_brcb_flash_probe)

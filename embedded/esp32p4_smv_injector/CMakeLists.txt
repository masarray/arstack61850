cmake_minimum_required(VERSION 3.16)

# Keep the device identity version aligned with the Studio P0 release bundle.
# ESP-IDF exports this as the application version through esp_app_desc.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arstack_esp32p4_smv_injector)

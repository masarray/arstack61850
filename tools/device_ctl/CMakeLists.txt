cmake_minimum_required(VERSION 3.20)

project(ARStack61850SvDeviceCtl
    VERSION 0.1.0
    DESCRIPTION "Windows USB serial control client for the ESP32-P4 SV injector"
    LANGUAGES CXX)

add_executable(ariec61850_sv_device_ctl
    ${CMAKE_CURRENT_LIST_DIR}/../sv_device_ctl.cpp)

target_compile_features(ariec61850_sv_device_ctl PRIVATE cxx_std_20)
target_include_directories(ariec61850_sv_device_ctl PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..)

if(MSVC)
    target_compile_options(ariec61850_sv_device_ctl PRIVATE
        /W4 /WX /permissive-)
else()
    target_compile_options(ariec61850_sv_device_ctl PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

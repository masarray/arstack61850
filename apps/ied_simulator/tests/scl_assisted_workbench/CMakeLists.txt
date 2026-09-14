cmake_minimum_required(VERSION 3.24)

project(ARStackSclAssistedWorkbenchQA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Reuse the exact desktop build graph so the P3 QA is linked against the same
# core, compatibility server, Qt version and controller implementation as the
# shipped workbench. The parent app target is configured but this QA target can
# still be built independently.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ied-simulator)

add_executable(ied_mms_scl_assisted_workbench_qa
    ${CMAKE_CURRENT_LIST_DIR}/../../src/ied_mms_scl_assisted_workbench_qa.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/MmsClientController.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/MmsClientController.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/MmsLiveTreeModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/MmsLiveTreeModel.hpp)

target_link_libraries(ied_mms_scl_assisted_workbench_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ariec61850_ied_simulator_server_core
    ARIEC61850::core)

target_include_directories(ied_mms_scl_assisted_workbench_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src)
target_compile_features(ied_mms_scl_assisted_workbench_qa PRIVATE cxx_std_20)

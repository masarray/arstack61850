cmake_minimum_required(VERSION 3.24)

project(ARStackStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

# Reuse the repository's IEC 61850/SCL implementation directly. The desktop UI
# is a presentation/control plane; it must not grow a second SCL semantic engine.
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/ariec61850-core EXCLUDE_FROM_ALL)

qt_add_executable(arstack_studio
    src/main.cpp
    src/SclProfileModel.cpp
    src/SclProfileModel.hpp
)

qt_add_qml_module(arstack_studio
    URI ARStack.Studio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(arstack_studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    ARIEC61850::core
)

target_compile_features(arstack_studio PRIVATE cxx_std_20)

if(WIN32)
    set_target_properties(arstack_studio PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

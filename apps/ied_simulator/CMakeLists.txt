cmake_minimum_required(VERSION 3.24)

project(ARStackIedSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

if(MSVC)
    # Visual Studio 18 no longer enables unwind semantics implicitly for these
    # standalone Ninja builds; the shared core uses standard C++ exceptions.
    add_compile_options(/EHsc)
endif()

# The desktop shell reuses the repository parser and the runnable MMS server.
# Keeping those implementations in one place avoids a GUI-only protocol fork.
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/ariec61850-core EXCLUDE_FROM_ALL)

qt_add_executable(arstack_ied_simulator
    src/main.cpp
    resources.qrc
)

qt_add_qml_module(arstack_ied_simulator
    URI ARStack.IedSimulator
    VERSION 1.0
    SOURCES
        src/IedSimulatorController.cpp
        src/IedSimulatorController.hpp
    QML_FILES
        qml/Main.qml
        qml/AppTheme.qml
        qml/SurfaceCard.qml
        qml/ActionButton.qml
        qml/StatusPill.qml
        qml/ToggleSwitch.qml
        qml/LaunchWorkspace.qml
        qml/RuntimeWorkspace.qml
)

target_link_libraries(arstack_ied_simulator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    ARIEC61850::core
)

target_include_directories(arstack_ied_simulator PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_features(arstack_ied_simulator PRIVATE cxx_std_20)
add_dependencies(arstack_ied_simulator ariec61850_static_ied_server)

if(WIN32)
    set_target_properties(arstack_ied_simulator PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

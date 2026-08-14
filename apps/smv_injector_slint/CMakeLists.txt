cmake_minimum_required(VERSION 3.21)

project(ARStack61850SmvSlintShell
    VERSION 0.1.0
    DESCRIPTION "Native Slint UX shell for the ARStack61850 SMV Injector"
    LANGUAGES CXX)

option(ARSTACK_SLINT_FETCH "Fetch Slint from source when no installed package is found" ON)
set(ARSTACK_SLINT_VERSION "1.17.1" CACHE STRING "Preferred Slint C++ version")

find_package(Slint ${ARSTACK_SLINT_VERSION} CONFIG QUIET)

if(NOT Slint_FOUND)
    if(NOT ARSTACK_SLINT_FETCH)
        message(FATAL_ERROR
            "Slint ${ARSTACK_SLINT_VERSION} was not found. Install the Slint C++ SDK, set CMAKE_PREFIX_PATH, "
            "or configure with -DARSTACK_SLINT_FETCH=ON.")
    endif()

    include(FetchContent)
    FetchContent_Declare(
        Slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG v${ARSTACK_SLINT_VERSION}
        SOURCE_SUBDIR api/cpp)
    FetchContent_MakeAvailable(Slint)
endif()

add_executable(arstack_smv_slint_shell
    src/main.cpp
    src/serial_device.cpp)

target_compile_features(arstack_smv_slint_shell PRIVATE cxx_std_20)
target_link_libraries(arstack_smv_slint_shell PRIVATE Slint::Slint)

slint_target_sources(arstack_smv_slint_shell
    ui/app-window.slint)

if(WIN32)
    add_custom_command(TARGET arstack_smv_slint_shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:arstack_smv_slint_shell>
            $<TARGET_FILE_DIR:arstack_smv_slint_shell>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying Slint runtime dependencies")
endif()

if(MSVC)
    target_compile_options(arstack_smv_slint_shell PRIVATE /W4 /permissive-)
else()
    target_compile_options(arstack_smv_slint_shell PRIVATE -Wall -Wextra -Wpedantic)
endif()

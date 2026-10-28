cmake_minimum_required(VERSION 3.24)

project(ARStackStudio VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort Concurrent)
qt_standard_project_setup(REQUIRES 6.5)

# Reuse the repository's IEC 61850/SCL implementation directly. The desktop UI
# is a presentation/control plane; it must not grow a second SCL semantic engine.
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/ariec61850-core EXCLUDE_FROM_ALL)

set(ARSTACK_STUDIO_CORE_SOURCES
    src/SclProfileModel.cpp
    src/SclProfileModel.hpp
    src/M4StreamScenarioModel.cpp
    src/M4StreamScenarioModel.hpp
    src/M4MultiSvUploadTransaction.cpp
    src/M4MultiSvUploadTransaction.hpp
    src/M4ThreeMuScenarioApply.cpp
    src/M4ThreeMuScenarioApply.hpp
    src/M4ThreeMuBoardReadback.cpp
    src/M4ThreeMuBoardReadback.hpp
    src/DeviceIoWorker.cpp
    src/DeviceIoWorker.hpp
    src/DeviceController.cpp
    src/DeviceController.hpp
    src/StudioDeviceController.hpp
    src/SingleInstanceGuard.hpp
    src/FirmwareWorker.cpp
    src/FirmwareWorker.hpp
    src/FirmwareManager.cpp
    src/FirmwareManager.hpp
    src/SmartSessionController.cpp
    src/SmartSessionController.hpp
)

qt_add_executable(arstack_studio
    src/main.cpp
    src/StudioClipboard.cpp
    src/StudioClipboard.hpp
    src/SvCaptureController.cpp
    src/SvCaptureController.hpp
    ${ARSTACK_STUDIO_CORE_SOURCES}
)

# S8 is intentionally a separate, headless executable. It links the same
# production controller/worker sources as Studio but exposes no test command in
# the release application. Friend-only seams let the harness inject deterministic
# transport/session events without introducing a second supervisor implementation.
qt_add_executable(arstack_session_harness
    src/DeterministicSessionHarnessMain.cpp
    src/DeterministicSessionHarness.cpp
    src/DeterministicSessionHarness.hpp
    ${ARSTACK_STUDIO_CORE_SOURCES}
)

# Offline production wire decoder regression (no Npcap or hardware required).
# Isolated platform GUI clipboard verification (same source linked in Studio).
# No QML stubs, no fake service: tests actual QGuiApplication OS clipboard.
qt_add_executable(arstack_clipboard_harness
    src/ClipboardHarness.cpp
    src/StudioClipboard.cpp
    src/StudioClipboard.hpp
)
target_link_libraries(arstack_clipboard_harness PRIVATE Qt6::Core Qt6::Gui)
target_compile_features(arstack_clipboard_harness PRIVATE cxx_std_20)

qt_add_executable(arstack_capture_harness
    src/SvCaptureHarness.cpp
    src/SvCaptureController.cpp
    src/SvCaptureController.hpp
)
target_link_libraries(arstack_capture_harness PRIVATE
    Qt6::Core Qt6::Gui Qt6::Concurrent
)
target_compile_features(arstack_capture_harness PRIVATE cxx_std_20)

# RC2 is a fail-closed evidence evaluator, not a hardware simulator. CI self-tests
# its acceptance policy; a physical run feeds the same executable a machine-readable
# evidence JSON collected from Studio telemetry and an independent SV capture.
qt_add_executable(arstack_rc2_acceptance
    src/Rc2Acceptance.cpp
)
target_link_libraries(arstack_rc2_acceptance PRIVATE Qt6::Core)
target_compile_features(arstack_rc2_acceptance PRIVATE cxx_std_20)

# S8B adds a Windows-native OS/process ownership gate. It intentionally depends
# only on the production lock class and the pure serial-error policy. Do not add
# DeviceIoWorker.hpp as a target source: AUTOMOC would then generate metaobject
# calls for worker slots and pull the full serial worker into this lightweight
# process-ownership harness.
if(WIN32)
    qt_add_executable(arstack_windows_ownership_harness
        src/WindowsOwnershipHarness.cpp
    )
    target_link_libraries(arstack_windows_ownership_harness PRIVATE
        Qt6::Core
        Qt6::SerialPort
    )
    target_compile_features(arstack_windows_ownership_harness PRIVATE cxx_std_20)
endif()

qt_add_qml_module(arstack_studio
    URI ARStack.Studio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/StudioTheme.qml
        qml/SurfacePanel.qml
        qml/DockFrame.qml
        qml/DarkToolButton.qml
        qml/TelemetryDock.qml
        qml/WorkflowBar.qml
        qml/BoardConnectionIndicator.qml
        qml/ModernRibbon.qml
        qml/RibbonGroup.qml
        qml/RibbonAction.qml
        qml/CalmButton.qml
        qml/NumericField.qml
        qml/StateBadge.qml
        qml/ProfileInspector.qml
        qml/ConfigurationHub.qml
        qml/PtpExpertPanel.qml
        qml/StudioDarkComboBox.qml
        qml/StudioDarkCheckBox.qml
        qml/StudioActionMenu.qml
        qml/StudioMenuDivider.qml
        qml/StudioPtpSwitch.qml
        qml/StudioLinkSwitch.qml
        qml/StudioTimingTray.qml
        qml/DeviceExpertPanel.qml
        qml/SvCapturePanel.qml
        qml/FirmwarePanel.qml
        qml/WaveformExpertPanel.qml
        qml/SignalMatrix.qml
        qml/QualityEditor.qml
        qml/SignalPreview.qml
        qml/StatusToast.qml
    RESOURCES
        assets/InterVariable.ttf
        assets/INTER_LICENSE.txt
        assets/material_dark/play_arrow.svg
        assets/material_dark/stop.svg
        assets/material_dark/folder_open.svg
        assets/material_dark/dashboard.svg
        assets/material_dark/tune.svg
        assets/material_dark/monitor_heart.svg
        assets/material_dark/settings.svg
        assets/MATERIAL_ICONS_LICENSE.txt
        assets/material/close.svg
        assets/material/open_in_new.svg
        assets/material/keyboard_arrow_up.svg
        assets/material/keyboard_arrow_down.svg
        assets/material/tune.svg
        assets/material/block.svg
        assets/material/monitor_heart.svg
        assets/material/check_circle.svg
        assets/material/settings.svg
        assets/material/file_upload.svg
        assets/material/stop.svg
        assets/material/play_arrow.svg
        assets/material/dashboard.svg
        assets/material/folder_open.svg
        assets/material/search.svg
        assets/material/schedule.svg
        assets/material/content_copy.svg
        assets/material/sync.svg
        assets/material/done_all.svg
        assets/material/balance.svg
        assets/material/refresh.svg
        assets/material/device_hub.svg
        assets/material/help_outline.svg
        assets/material/smv_injector.svg
        assets/material/electric_bolt_run.svg
        assets/material/electric_bolt_stop.svg
)

qt_add_resources(arstack_studio "reference_templates"
    PREFIX "/arstack"
    FILES
        templates/arstack_4i4v_9-2le_reference.scd
)

qt_add_resources(arstack_session_harness "session_harness_reference_templates"
    PREFIX "/arstack"
    FILES
        templates/arstack_4i4v_9-2le_reference.scd
)

target_link_libraries(arstack_studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::SerialPort
    Qt6::Concurrent
    ARIEC61850::core
)

target_link_libraries(arstack_session_harness PRIVATE
    Qt6::Core
    Qt6::SerialPort
    ARIEC61850::core
)

target_compile_features(arstack_studio PRIVATE cxx_std_20)
target_compile_features(arstack_session_harness PRIVATE cxx_std_20)
target_compile_definitions(arstack_studio PRIVATE
    ARSTACK_STUDIO_VERSION="${PROJECT_VERSION}"
)

# Portable native GUI-side M4 pagination regression, no COM/ESP32 required.
qt_add_executable(arstack_m4_pages_harness
    src/M4StreamScenarioHarness.cpp
    src/M4StreamScenarioModel.cpp
    src/M4StreamScenarioModel.hpp
)
target_link_libraries(arstack_m4_pages_harness PRIVATE Qt6::Core)
target_compile_features(arstack_m4_pages_harness PRIVATE cxx_std_20)

# Exercise the actual production SclProfileModel, not a parallel XML decoder.
# The canonical ARM3 envelope is compiled once from the three exact SCD rows,
# preserving per-stream VLAN overrides and the firmware identity hash.
qt_add_executable(arstack_m4_compile_harness
    src/M4SclCompileHarness.cpp
    src/SclProfileModel.cpp
    src/SclProfileModel.hpp
)
target_link_libraries(arstack_m4_compile_harness PRIVATE
    Qt6::Core
    ARIEC61850::core
)
target_compile_definitions(arstack_m4_compile_harness PRIVATE
    ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/../.."
)
target_compile_features(arstack_m4_compile_harness PRIVATE cxx_std_20)

# M4 host control-plane transaction regression (no COM hardware required).
# Uses production SCD model + production serial transaction state machine.
qt_add_executable(arstack_m4_upload_harness
    src/M4MultiSvUploadHarness.cpp
    src/M4MultiSvUploadTransaction.cpp
    src/M4MultiSvUploadTransaction.hpp
    src/M4ThreeMuScenarioApply.cpp
    src/M4ThreeMuScenarioApply.hpp
    src/M4ThreeMuBoardReadback.cpp
    src/M4ThreeMuBoardReadback.hpp
    src/SclProfileModel.cpp
    src/SclProfileModel.hpp
)
target_link_libraries(arstack_m4_upload_harness PRIVATE
    Qt6::Core
    ARIEC61850::core
)
target_compile_definitions(arstack_m4_upload_harness PRIVATE
    ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/../.."
)
target_compile_features(arstack_m4_upload_harness PRIVATE cxx_std_20)



# Opt-in Windows engineering preview identity; OFF for all stable builds.
option(ARSTACK_STUDIO_ENGINEERING_PREVIEW "Mark this Studio build as an isolated preview" OFF)
if(ARSTACK_STUDIO_ENGINEERING_PREVIEW)
    target_compile_definitions(arstack_studio PRIVATE ARSTACK_STUDIO_ENGINEERING_PREVIEW=1)
endif()
target_compile_definitions(arstack_session_harness PRIVATE
    ARSTACK_STUDIO_VERSION="${PROJECT_VERSION}"
)

# Use the existing Studio-specific artwork for every branding surface.
# This is intentionally separate from root assets/ used by Workbench.
set(ARSTACK_STUDIO_BRAND_DIR "${CMAKE_CURRENT_LIST_DIR}/../../assets/studio_icon")
qt_add_resources(arstack_studio "studio_branding"
    PREFIX "/studio"
    BASE "${ARSTACK_STUDIO_BRAND_DIR}"
    FILES
        "${ARSTACK_STUDIO_BRAND_DIR}/android-chrome-192x192.png"
        "${ARSTACK_STUDIO_BRAND_DIR}/android-chrome-512x512.png"
)

if(WIN32)
    # The exact same embedded icon is copied to stage/ for Inno Setup.
    # No Python icon generation or alternative branding paths.
    set(ARSTACK_STUDIO_ICON "${CMAKE_CURRENT_BINARY_DIR}/ARStackStudio.ico")
    configure_file("${ARSTACK_STUDIO_BRAND_DIR}/ARStackStudio.ico"
                   "${ARSTACK_STUDIO_ICON}" COPYONLY)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ARStackStudio.rc"
         "IDI_ARSTACK ICON \"${ARSTACK_STUDIO_ICON}\"\n")
    target_sources(arstack_studio PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/ARStackStudio.rc")
    set_target_properties(arstack_studio PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
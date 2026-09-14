cmake_minimum_required(VERSION 3.24)

project(ARStackStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort)
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

# RC2 is a fail-closed evidence evaluator, not a hardware simulator. CI self-tests
# its acceptance policy; a physical run feeds the same executable a machine-readable
# evidence JSON collected from Studio telemetry and an independent SV capture.
qt_add_executable(arstack_rc2_acceptance
    src/Rc2Acceptance.cpp
)
target_link_libraries(arstack_rc2_acceptance PRIVATE Qt6::Core)
target_compile_features(arstack_rc2_acceptance PRIVATE cxx_std_20)

# Physical-board QA is deliberately built outside the frozen release branch. The
# runner links the exact production controller/supervisor sources from the frozen
# candidate and emits incomplete/fail-closed RC2 evidence. It never enters the
# public Studio package and cannot move the release baseline by itself.
qt_add_executable(arstack_board_acceptance
    src/BoardAcceptanceRunner.cpp
    ${ARSTACK_STUDIO_CORE_SOURCES}
)
qt_add_resources(arstack_board_acceptance "board_acceptance_reference_templates"
    PREFIX "/arstack"
    FILES
        templates/arstack_4i4v_9-2le_reference.scd
)
target_link_libraries(arstack_board_acceptance PRIVATE
    Qt6::Core
    Qt6::SerialPort
    ARIEC61850::core
)
target_compile_features(arstack_board_acceptance PRIVATE cxx_std_20)
target_compile_definitions(arstack_board_acceptance PRIVATE
    ARSTACK_STUDIO_VERSION="${PROJECT_VERSION}"
    ARSTACK_BOARD_CANDIDATE_SHA="ed46f3bed73d398d71be2eacf34c4995325c3cad"
)

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
        qml/CalmButton.qml
        qml/NumericField.qml
        qml/StateBadge.qml
        qml/ProfileInspector.qml
        qml/ConfigurationHub.qml
        qml/PtpExpertPanel.qml
        qml/DeviceExpertPanel.qml
        qml/FirmwarePanel.qml
        qml/WaveformExpertPanel.qml
        qml/SignalMatrix.qml
        qml/QualityEditor.qml
        qml/SignalPreview.qml
        qml/StatusToast.qml
    RESOURCES
        assets/InterVariable.ttf
        assets/INTER_LICENSE.txt
        assets/LUCIDE_LICENSE.txt
        assets/lucide/x.svg
        assets/lucide/external-link.svg
        assets/lucide/chevron-up.svg
        assets/lucide/chevron-down.svg
        assets/lucide/radio-tower.svg
        assets/lucide/scale.svg
        assets/lucide/circle-off.svg
        assets/lucide/activity.svg
        assets/lucide/circle-check.svg
        assets/lucide/settings-2.svg
        assets/lucide/upload.svg
        assets/lucide/square.svg
        assets/lucide/play.svg
        assets/lucide/panels-top-left.svg
        assets/lucide/folder-open.svg
        assets/lucide/scan-search.svg
        assets/lucide/clock-3.svg
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
target_compile_definitions(arstack_session_harness PRIVATE
    ARSTACK_STUDIO_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    set_target_properties(arstack_studio PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

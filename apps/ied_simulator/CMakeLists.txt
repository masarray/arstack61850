cmake_minimum_required(VERSION 3.24)

project(ARStackIedSimulator VERSION 0.2.0 LANGUAGES CXX)

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

if(WIN32)
    set(ARSTACK_APP_ICON "${CMAKE_CURRENT_LIST_DIR}/../../assets/favicon.ico")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/packaging/windows/arstack_ied_simulator.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/arstack_ied_simulator.rc"
        @ONLY)
    target_sources(
        arstack_ied_simulator
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/arstack_ied_simulator.rc")
endif()

qt_add_qml_module(arstack_ied_simulator
    URI ARStack.IedSimulator
    VERSION 1.0
    SOURCES
        src/IedActivityModel.cpp
        src/IedActivityModel.hpp
        src/IedRuntimeGuardrails.cpp
        src/IedRuntimeGuardrails.hpp
        src/IedPointStore.cpp
        src/IedPointStore.hpp
        src/IedFleetController.cpp
        src/IedFleetControllerAsync.cpp
        src/IedFleetController.hpp
        src/IedCommissioningModel.cpp
        src/IedCommissioningRuntime.cpp
        src/IedGoosePublication.cpp
        src/IedCommissioningModel.hpp
        src/IedNavigationModel.cpp
        src/IedNavigationModel.hpp
        src/IedSignalModel.cpp
        src/IedSignalModel.hpp
        src/MmsLiveTreeModel.cpp
        src/MmsLiveTreeModel.hpp
        src/IedEngineeringContextController.cpp
        src/IedEngineeringContextController.hpp
        src/MmsClientController.cpp
        src/MmsClientController.hpp
        src/IedBrowserSessionController.cpp
        src/IedBrowserSessionController.hpp
        src/MmsReportController.cpp
        src/MmsReportController.hpp
        src/MmsSettingGroupPolicy.hpp
        src/MmsFileSettingsController.cpp
        src/MmsFileSettingsController.hpp
        src/SclWorkspaceController.cpp
        src/SclWorkspaceController.hpp
        src/GooseMonitorController.cpp
        src/GooseMonitorController.hpp
        src/ProductHardeningController.cpp
        src/ProductHardeningController.hpp
    QML_FILES
        qml/Main.qml
        qml/AppTheme.qml
        qml/SurfaceCard.qml
        qml/ActionButton.qml
        qml/IconAction.qml
        qml/StatusPill.qml
        qml/ToggleSwitch.qml
        qml/LaunchWorkspace.qml
        qml/RuntimeWorkspace.qml
        qml/IedExplorer.qml
        qml/FleetWorkspace.qml
        qml/InteropWorkspace.qml
        qml/FileWorkspace.qml
        qml/IedBrowserWorkspace.qml
        qml/IedBrowserNavigation.qml
        qml/BrowserStatusConsole.qml
        qml/BrowserDataSetPane.qml
        qml/BrowserGoosePane.qml
        qml/SnifferWorkspace.qml
        qml/MmsClientWorkspace.qml
        qml/ReportsWorkspace.qml
        qml/FilesWorkspace.qml
        qml/SettingsWorkspace.qml
        qml/SclWorkspace.qml
        qml/GooseWorkspace.qml
        qml/ActivityMonitor.qml
        qml/CommissioningWorkspace.qml
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

add_executable(ied_setting_control_manifest_qa
    src/ied_setting_control_manifest_qa.cpp)
target_link_libraries(ied_setting_control_manifest_qa PRIVATE
    Qt6::Core)
target_include_directories(ied_setting_control_manifest_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_compile_features(ied_setting_control_manifest_qa PRIVATE cxx_std_20)

add_executable(ied_simulator_guardrails_qa
    src/ied_simulator_guardrails_qa.cpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp)
target_link_libraries(ied_simulator_guardrails_qa PRIVATE
    Qt6::Core
    Qt6::Qml)
target_include_directories(ied_simulator_guardrails_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_simulator_guardrails_qa PRIVATE cxx_std_20)

add_executable(ied_simulator_responsiveness_qa
    src/ied_simulator_responsiveness_qa.cpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp
    src/IedSignalModel.cpp
    src/IedSignalModel.hpp)
target_link_libraries(ied_simulator_responsiveness_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_simulator_responsiveness_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_simulator_responsiveness_qa PRIVATE cxx_std_20)

add_executable(ied_simulator_commissioning_qa
    src/ied_simulator_commissioning_qa.cpp
    src/IedCommissioningModel.cpp
    src/IedCommissioningRuntime.cpp
    src/IedGoosePublication.cpp
    src/IedCommissioningModel.hpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp)
target_link_libraries(ied_simulator_commissioning_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_simulator_commissioning_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_simulator_commissioning_qa PRIVATE cxx_std_20)

add_executable(ied_simulator_runtime_commissioning_qa
    src/ied_simulator_runtime_commissioning_qa.cpp
    src/IedCommissioningModel.cpp
    src/IedCommissioningRuntime.cpp
    src/IedGoosePublication.cpp
    src/IedCommissioningModel.hpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp)
target_link_libraries(ied_simulator_runtime_commissioning_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_simulator_runtime_commissioning_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_simulator_runtime_commissioning_qa PRIVATE cxx_std_20)

add_executable(ied_simulator_goose_publication_qa
    src/ied_simulator_goose_publication_qa.cpp)
target_link_libraries(ied_simulator_goose_publication_qa PRIVATE
    ARIEC61850::core)
target_compile_features(ied_simulator_goose_publication_qa PRIVATE cxx_std_20)

add_executable(ied_goose_workspace_qa
    src/ied_goose_workspace_qa.cpp
    src/GooseMonitorController.cpp
    src/GooseMonitorController.hpp)
target_link_libraries(ied_goose_workspace_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_goose_workspace_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_goose_workspace_qa PRIVATE cxx_std_20)

add_executable(ied_mms_client_workbench_qa
    src/ied_mms_client_workbench_qa.cpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsClientController.cpp
    src/MmsClientController.hpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp)
target_link_libraries(ied_mms_client_workbench_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ariec61850_ied_simulator_server_core
    ARIEC61850::core)
target_include_directories(ied_mms_client_workbench_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_mms_client_workbench_qa PRIVATE cxx_std_20)

add_executable(ied_report_workspace_qa
    src/ied_report_workspace_qa.cpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsReportController.cpp
    src/MmsReportController.hpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp
    src/IedSignalModel.cpp
    src/IedSignalModel.hpp)
target_link_libraries(ied_report_workspace_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_report_workspace_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_report_workspace_qa PRIVATE cxx_std_20)

add_executable(ied_file_settings_workspace_qa
    src/ied_file_settings_workspace_qa.cpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsFileSettingsController.cpp
    src/MmsFileSettingsController.hpp
    src/MmsSettingGroupPolicy.hpp)
target_link_libraries(ied_file_settings_workspace_qa PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Qml
    ariec61850_ied_simulator_server_core
    ARIEC61850::core)
target_include_directories(ied_file_settings_workspace_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_file_settings_workspace_qa PRIVATE cxx_std_20)

add_executable(ied_scl_workspace_qa
    src/ied_scl_workspace_qa.cpp
    src/SclWorkspaceController.cpp
    src/SclWorkspaceController.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp)
target_link_libraries(ied_scl_workspace_qa PRIVATE
    Qt6::Core
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_scl_workspace_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_scl_workspace_qa PRIVATE cxx_std_20)

add_executable(ied_product_hardening_qa
    src/ied_product_hardening_qa.cpp
    src/ProductHardeningController.cpp
    src/ProductHardeningController.hpp)
target_link_libraries(ied_product_hardening_qa PRIVATE
    Qt6::Core
    Qt6::Qml)
target_include_directories(ied_product_hardening_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_product_hardening_qa PRIVATE cxx_std_20)

add_executable(ied_browser_session_qa
    src/ied_browser_session_qa.cpp
    src/IedBrowserSessionController.cpp
    src/IedBrowserSessionController.hpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsClientController.cpp
    src/MmsClientController.hpp
    src/MmsReportController.cpp
    src/MmsReportController.hpp
    src/MmsFileSettingsController.cpp
    src/MmsFileSettingsController.hpp
    src/MmsSettingGroupPolicy.hpp
    src/IedActivityModel.cpp
    src/IedActivityModel.hpp
    src/IedRuntimeGuardrails.cpp
    src/IedRuntimeGuardrails.hpp
    src/IedPointStore.cpp
    src/IedPointStore.hpp
    src/IedFleetController.cpp
    src/IedFleetControllerAsync.cpp
    src/IedFleetController.hpp
    src/IedSignalModel.cpp
    src/IedSignalModel.hpp)
target_link_libraries(ied_browser_session_qa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    ariec61850_ied_simulator_server_core
    ARIEC61850::core)
target_include_directories(ied_browser_session_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_browser_session_qa PRIVATE cxx_std_20)

add_executable(ied_browser_large_model_qa
    src/ied_browser_large_model_qa.cpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp)
target_link_libraries(ied_browser_large_model_qa PRIVATE
    Qt6::Core
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_browser_large_model_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_browser_large_model_qa PRIVATE cxx_std_20)

add_executable(ied_engineering_context_qa
    src/ied_engineering_context_qa.cpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp)
target_link_libraries(ied_engineering_context_qa PRIVATE
    Qt6::Core
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_engineering_context_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_engineering_context_qa PRIVATE cxx_std_20)

add_executable(ied_discovery_scl_export_qa
    src/ied_discovery_scl_export_qa.cpp
    src/SclWorkspaceController.cpp
    src/SclWorkspaceController.hpp
    src/IedEngineeringContextController.cpp
    src/IedEngineeringContextController.hpp
    src/MmsLiveTreeModel.cpp
    src/MmsLiveTreeModel.hpp)
target_link_libraries(ied_discovery_scl_export_qa PRIVATE
    Qt6::Core
    Qt6::Qml
    ARIEC61850::core)
target_include_directories(ied_discovery_scl_export_qa PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(ied_discovery_scl_export_qa PRIVATE cxx_std_20)

# The regular library intentionally keeps a strict portable connection runtime.
# The desktop simulator needs the separate compatibility runtime validated
# against common engineering-client AARQ, selector, browse, Identify and
# FileDirectory traffic. Keep the two runtimes isolated so embedded behavior is
# unchanged. Reporting is wired here as an application feature over the same
# bounded per-association server rather than by launching another protocol fork.
add_library(ariec61850_ied_simulator_server_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src/asn1/ber_span_reader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/osi/tpkt_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/osi/cotp_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/osi/session_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/osi/presentation_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/acse/association_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/acse/association_compat_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/pdu_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/pdu_compat_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/services_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/write_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/data_set_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/information_report_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/buffered_selective_information_report_span.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_object_table.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_write.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_data_set_table.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_direct_control.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_setting_group.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/connection_runtime_iedsim.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_server_session.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_information_report.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_report_connection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_objects.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_control.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_objects.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_connection.cpp)
target_include_directories(ariec61850_ied_simulator_server_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_compile_features(ariec61850_ied_simulator_server_core PUBLIC cxx_std_20)

add_executable(ariec61850_ied_simulator_server
    ${CMAKE_CURRENT_LIST_DIR}/../../tools/static_ied_server.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../tools/static_ied_server_sigpipe.cpp)
target_link_libraries(ariec61850_ied_simulator_server PRIVATE
    ariec61850_ied_simulator_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_ied_simulator_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_ied_simulator_server PRIVATE cxx_std_20)
add_dependencies(ied_simulator_responsiveness_qa ariec61850_ied_simulator_server)
add_dependencies(ied_mms_client_workbench_qa ariec61850_ied_simulator_server)
add_dependencies(ied_report_workspace_qa ariec61850_ied_simulator_server)
add_dependencies(ied_file_settings_workspace_qa ariec61850_ied_simulator_server)
add_dependencies(
    ied_simulator_runtime_commissioning_qa
    ariec61850_ied_simulator_server
    ariec61850_mms_read_probe
    ariec61850_mms_brcb_event_probe)

# The public-wire URCB/BRCB regression clients are root ARIEC61850 tool targets.
# Reuse them here instead of defining GUI-local copies so the same sources are
# compiled by the normal GCC/Clang/MSVC toolchain matrix and by the Qt harness.
add_dependencies(
    arstack_ied_simulator
    ariec61850_ied_simulator_server
    ariec61850_mms_urcb_gi_probe
    ariec61850_mms_brcb_event_probe
    ariec61850_mms_sgcb_probe
    ariec61850_control_interop_probe)

if(WIN32)
    set_target_properties(arstack_ied_simulator PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

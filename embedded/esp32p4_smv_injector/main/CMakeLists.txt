set(AR_PTP_LAB_SRCS)
if(CONFIG_AR_PTP_LAB_TX)
    list(APPEND AR_PTP_LAB_SRCS "ptp_lab_task.cpp")
endif()

idf_component_register(
    SRCS
        "app_main.cpp"
        "ethernet_port.c"
        "live_control.cpp"
        "profile_control.cpp"
        "runtime_profile.cpp"
        "smp_synch_lab.cpp"
        ${AR_PTP_LAB_SRCS}
    INCLUDE_DIRS "."
    REQUIRES arstack_process_bus esp_eth esp_event esp_timer esp_driver_gptimer freertos)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

# Only the deterministic SV publisher translation unit is intercepted. The shim
# patches the already encoded smpSynch byte immediately before transmit; PTP and
# every other Ethernet user continue calling ESP-IDF's esp_eth_transmit directly.
set_property(SOURCE "app_main.cpp" APPEND PROPERTY COMPILE_OPTIONS
    "-include" "${CMAKE_CURRENT_LIST_DIR}/smp_synch_lab.hpp"
    "-DAR_SMP_SYNCH_INTERCEPT_TX=1")

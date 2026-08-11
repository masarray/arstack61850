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
        ${AR_PTP_LAB_SRCS}
    INCLUDE_DIRS "."
    REQUIRES arstack_process_bus esp_eth esp_event esp_timer esp_driver_gptimer freertos)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

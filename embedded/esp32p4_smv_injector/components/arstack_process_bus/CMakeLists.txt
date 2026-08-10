set(ARSTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${ARSTACK_ROOT}/src/asn1/ber.cpp"
        "${ARSTACK_ROOT}/src/ethernet/ethernet.cpp"
        "${ARSTACK_ROOT}/src/mms/utc_time.cpp"
        "${ARSTACK_ROOT}/src/sampled_values/pdu_codec.cpp"
        "${ARSTACK_ROOT}/src/sampled_values/frame_codec.cpp"
    INCLUDE_DIRS
        "${ARSTACK_ROOT}/include")

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Expose the existing client-side static report lifecycle harness as a normal
# tool target. P2 acceptance uses the same public runtime an interoperability
# client would use, not an ad-hoc test-only encoder.
replace_once(
    "CMakeLists.txt",
    "    add_executable(ariec61850_mms_read_probe tools/mms_read_probe.cpp)\n"
    "    target_link_libraries(ariec61850_mms_read_probe PRIVATE ARIEC61850::core)\n"
    "    target_compile_features(ariec61850_mms_read_probe PRIVATE cxx_std_20)\n"
    "    ariec61850_apply_warnings(ariec61850_mms_read_probe)\n"
    "    ariec61850_apply_sanitizers(ariec61850_mms_read_probe)\n\n",
    "    add_executable(ariec61850_mms_read_probe tools/mms_read_probe.cpp)\n"
    "    target_link_libraries(ariec61850_mms_read_probe PRIVATE ARIEC61850::core)\n"
    "    target_compile_features(ariec61850_mms_read_probe PRIVATE cxx_std_20)\n"
    "    ariec61850_apply_warnings(ariec61850_mms_read_probe)\n"
    "    ariec61850_apply_sanitizers(ariec61850_mms_read_probe)\n\n"
    "    add_executable(ariec61850_static_rcb_trial tools/static_rcb_trial.cpp)\n"
    "    target_link_libraries(ariec61850_static_rcb_trial PRIVATE ARIEC61850::core)\n"
    "    target_compile_features(ariec61850_static_rcb_trial PRIVATE cxx_std_20)\n"
    "    ariec61850_apply_warnings(ariec61850_static_rcb_trial)\n"
    "    ariec61850_apply_sanitizers(ariec61850_static_rcb_trial)\n\n",
)

# The portable URCB runtime delegates DataSet snapshot encoding to the existing
# static information-report encoder. Wire that implementation into every host
# server-core profile that now builds the URCB runtime.
replace_once(
    "CMakeLists.txt",
    "    src/mms/information_report_span.cpp\n"
    "    src/mms/static_urcb_runtime.cpp\n",
    "    src/mms/information_report_span.cpp\n"
    "    src/mms/static_information_report.cpp\n"
    "    src/mms/static_urcb_runtime.cpp\n",
)
replace_once(
    "apps/ied_simulator/CMakeLists.txt",
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/information_report_span.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_runtime.cpp\n",
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/information_report_span.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_information_report.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_runtime.cpp\n",
)
replace_once(
    "tools/static_ied_server_iedsim/CMakeLists.txt",
    "    \"${ARSTACK_ROOT}/src/mms/information_report_span.cpp\"\n"
    "    \"${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp\"\n",
    "    \"${ARSTACK_ROOT}/src/mms/information_report_span.cpp\"\n"
    "    \"${ARSTACK_ROOT}/src/mms/static_information_report.cpp\"\n"
    "    \"${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp\"\n",
)

# The persistent manifest report definition replaces P0's local ParsedRcb type.
replace_once(
    "tools/static_ied_server.cpp",
    "    const auto add_rcb_object = [&](const ParsedRcb& rcb,\n",
    "    const auto add_rcb_object = [&](const ManifestReportControl& rcb,\n",
)

# Host interoperability discovery needs the exact flattened RCB attributes in
# GetNameList. The dispatcher deliberately keeps root-only discovery as its
# strict default; enable aliases only for the manifest-backed desktop simulator.
replace_once(
    "tools/static_ied_server.cpp",
    "    mms::MmsStaticDispatchPolicy dispatch_policy;\n"
    "    const mms::MmsStaticApplicationDispatcher dispatcher{\n",
    "    mms::MmsStaticDispatchPolicy dispatch_policy;\n"
    "    dispatch_policy.advertise_flattened_child_aliases = manifest_model != nullptr;\n"
    "    const mms::MmsStaticApplicationDispatcher dispatcher{\n",
)

# IEC 61850 report AccessResults are ordered inclusion, optional data-reference
# list, values, then optional reason-for-inclusion list. The high-level mapper
# previously consumed values before data references; old tests mirrored that
# mistake. Align it with the static wire encoder used by the server.
replace_once(
    "src/mms/reporting.cpp",
    "    const auto included_count = frame.included_data_set_indexes.size();\n"
    "    std::vector<MmsInformationReportItem> value_items;\n"
    "    for (std::size_t i = 0U; i < included_count; ++i) value_items.push_back(require_item(report, cursor++));\n"
    "    std::vector<std::string> data_references(included_count);\n"
    "    if (frame.header.optional_fields.has(\"data-reference\")) {\n"
    "        for (auto& reference : data_references) {\n"
    "            const auto value = string_value(require_value(report, cursor++, \"data-reference\"));\n"
    "            if (!value) throw MmsReportingFormatError(\"MMS report data-reference is not a visible string.\");\n"
    "            reference = *value;\n"
    "        }\n"
    "    }\n",
    "    const auto included_count = frame.included_data_set_indexes.size();\n"
    "    std::vector<std::string> data_references(included_count);\n"
    "    if (frame.header.optional_fields.has(\"data-reference\")) {\n"
    "        for (auto& reference : data_references) {\n"
    "            const auto value = string_value(require_value(report, cursor++, \"data-reference\"));\n"
    "            if (!value) throw MmsReportingFormatError(\"MMS report data-reference is not a visible string.\");\n"
    "            reference = *value;\n"
    "        }\n"
    "    }\n"
    "    std::vector<MmsInformationReportItem> value_items;\n"
    "    for (std::size_t i = 0U; i < included_count; ++i) value_items.push_back(require_item(report, cursor++));\n",
)

# Correct the realistic unit fixture so it is independent evidence for the wire
# ordering rather than reproducing the previous mapper implementation.
replace_once(
    "tests/test_reporting.cpp",
    "    add(MmsDataValue::bit_string(6U, inclusion));\n"
    "    add(MmsDataValue::boolean(true));\n"
    "    add(MmsDataValue::boolean(false));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind1.stVal\"));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind2.stVal\"));\n",
    "    add(MmsDataValue::bit_string(6U, inclusion));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind1.stVal\"));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind2.stVal\"));\n"
    "    add(MmsDataValue::boolean(true));\n"
    "    add(MmsDataValue::boolean(false));\n",
)
replace_once(
    "tests/test_reporting.cpp",
    "    report.items[9U] = {9U, std::nullopt, 3U};\n",
    "    report.items[11U] = {11U, std::nullopt, 3U};\n",
)

# Let the static-session selector discover the only URCB naturally. The test
# verifies the selected RCB/DataSet binding from STATIC_PLAN rather than
# steering selection with a pre-filter.
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '                    "--preferred-rcb",\n'
    '                    "MU01LD0/LLN0$RP$urcb01",\n'
    '                    "--dataset-ref",\n'
    '                    "MU01LD0/LLN0$dsSV",\n',
    "",
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '            evidence = re.search(r"REPORT_EVIDENCE received=(\\d+) decodeFailures=(\\d+)", report_trial.stdout)\n',
    '            if "STATIC_PLAN selectedRcb=MU01LD0/LLN0.urcb01 mode=URCB dataSet=MU01LD0/LLN0$dsSV" not in report_trial.stdout:\n'
    '                raise RuntimeError(f"P2 static RCB/DataSet binding mismatch: {report_trial.stdout}")\n'
    '            evidence = re.search(r"REPORT_EVIDENCE received=(\\d+) decodeFailures=(\\d+)", report_trial.stdout)\n',
)

# Diagnostic inventory is intentionally part of the trial output. It makes a
# failed interoperability selection actionable.
replace_once(
    "tools/static_rcb_trial.cpp",
    "        auto discovery = live_session.discover(discovery_options);\n\n"
    "        mms::MmsStaticReportSessionOptions session_options;\n",
    "        auto discovery = live_session.discover(discovery_options);\n"
    "        std::cout << \"DISCOVERY_REPORTING inventoryRcb=\"\n"
    "                  << discovery.report_inventory.report_controls.size()\n"
    "                  << \" probedRcb=\" << discovery.report_controls.size()\n"
    "                  << \" inventoryDataSet=\" << discovery.report_inventory.data_sets.size()\n"
    "                  << \" directoryDataSet=\" << discovery.data_set_directories.size() << '\\n';\n"
    "        for (const auto& evidence : discovery.report_controls) {\n"
    "            std::cout << \"DISCOVERY_RCB ref=\" << evidence.candidate.reference\n"
    "                      << \" mode=\" << evidence.candidate.mode()\n"
    "                      << \" success=\" << (evidence.success() ? \"true\" : \"false\");\n"
    "            if (evidence.state) {\n"
    "                const auto& state = *evidence.state;\n"
    "                std::cout << \" rptEna=\"\n"
    "                          << (state.report_enabled ? (*state.report_enabled ? \"true\" : \"false\") : \"unset\")\n"
    "                          << \" datSet=\" << state.data_set_reference\n"
    "                          << \" resv=\"\n"
    "                          << (state.reserved ? (*state.reserved ? \"true\" : \"false\") : \"unset\")\n"
    "                          << \" diagnostics=\" << state.diagnostics.size();\n"
    "                for (const auto& diagnostic : state.diagnostics) {\n"
    "                    std::cout << \" [\" << diagnostic << ']';\n"
    "                }\n"
    "            } else {\n"
    "                std::cout << \" error=\" << evidence.error;\n"
    "            }\n"
    "            std::cout << '\\n';\n"
    "        }\n"
    "        for (const auto& evidence : discovery.data_set_directories) {\n"
    "            std::cout << \"DISCOVERY_DATASET ref=\" << evidence.candidate.reference\n"
    "                      << \" success=\" << (evidence.success() ? \"true\" : \"false\")\n"
    "                      << \" members=\"\n"
    "                      << (evidence.directory ? evidence.directory->members.size() : 0U)\n"
    "                      << \" error=\" << evidence.error << '\\n';\n"
    "        }\n"
    "        std::cout.flush();\n\n"
    "        mms::MmsStaticReportSessionOptions session_options;\n",
)

print("P2 reporting repair applied")

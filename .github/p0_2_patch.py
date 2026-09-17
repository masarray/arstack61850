from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# IEC 61850 report payload order after inclusion is grouped:
# DataRef* (optional), Value*, ReasonForInclusion* (optional).
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
    "    }\n"
    "    std::vector<MmsReportBitField> reasons(included_count);",
    "    const auto included_count = frame.included_data_set_indexes.size();\n"
    "    // IEC 61850-8-1 report access results are grouped after the\n"
    "    // inclusion bitstring: DataRef* (optional), Value*, then\n"
    "    // ReasonForInclusion* (optional). Keep the cursor in that exact\n"
    "    // order so metadata can never be projected as process values.\n"
    "    std::vector<std::string> data_references(included_count);\n"
    "    if (frame.header.optional_fields.has(\"data-reference\")) {\n"
    "        for (auto& reference : data_references) {\n"
    "            const auto value = string_value(require_value(report, cursor++, \"data-reference\"));\n"
    "            if (!value) throw MmsReportingFormatError(\"MMS report data-reference is not a visible string.\");\n"
    "            reference = *value;\n"
    "        }\n"
    "    }\n"
    "    std::vector<MmsInformationReportItem> value_items;\n"
    "    value_items.reserve(included_count);\n"
    "    for (std::size_t i = 0U; i < included_count; ++i) {\n"
    "        value_items.push_back(require_item(report, cursor++));\n"
    "    }\n"
    "    std::vector<MmsReportBitField> reasons(included_count);",
)

# Correct the owning fixture to canonical ordering.
replace_once(
    "tests/test_reporting.cpp",
    "    add(MmsDataValue::bit_string(6U, inclusion));\n"
    "    add(MmsDataValue::boolean(true));\n"
    "    add(MmsDataValue::boolean(false));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind1.stVal\"));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind2.stVal\"));\n"
    "    add(MmsDataValue::bit_string(2U, reason_a));",
    "    add(MmsDataValue::bit_string(6U, inclusion));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind1.stVal\"));\n"
    "    add(MmsDataValue::visible_string(\"LD0/GGIO1.Ind2.stVal\"));\n"
    "    add(MmsDataValue::boolean(true));\n"
    "    add(MmsDataValue::boolean(false));\n"
    "    add(MmsDataValue::bit_string(2U, reason_a));",
)
replace_once(
    "tests/test_reporting.cpp",
    "    report.items[9U] = {9U, std::nullopt, 3U};",
    "    report.items[11U] = {11U, std::nullopt, 3U};",
)

matrix = r'''void canonical_report_payload_order_matrix() {
    struct Case final {
        bool data_reference;
        bool reason;
        std::uint8_t option_mask;
    };
    constexpr std::array<Case, 4U> cases{{
        {false, false, 0x00U},
        {true,  false, 0x04U},
        {false, true,  0x10U},
        {true,  true,  0x14U},
    }};
    const std::array<std::uint8_t, 1U> inclusion{0xC0U};
    const std::array<std::uint8_t, 1U> reason_a{0x40U};
    const std::array<std::uint8_t, 1U> reason_b{0x08U};
    const auto directory = MmsDataSetDirectoryCodec::decode_response(
        MmsDataSetDirectoryCodec::encode_response_pdu(directory_fixture()), 9U);

    for (const auto& test : cases) {
        const std::array<std::uint8_t, 2U> options{test.option_mask, 0x00U};
        MmsInformationReport report;
        auto add = [&report](MmsDataValue value) {
            report.items.push_back({report.items.size(), std::move(value), std::nullopt});
        };
        add(MmsDataValue::visible_string("LD0/LLN0.RP.matrix"));
        add(MmsDataValue::bit_string(6U, options));
        add(MmsDataValue::bit_string(6U, inclusion));
        if (test.data_reference) {
            add(MmsDataValue::visible_string("LD0/GGIO1.Ind1.stVal"));
            add(MmsDataValue::visible_string("LD0/GGIO1.Ind2.stVal"));
        }
        add(MmsDataValue::boolean(true));
        add(MmsDataValue::boolean(false));
        if (test.reason) {
            add(MmsDataValue::bit_string(2U, reason_a));
            add(MmsDataValue::bit_string(2U, reason_b));
        }

        const auto frame = MmsReportFrameMapper::map(report, directory.members);
        require(frame.values.size() == 2U, "Canonical report matrix value count mismatch.");
        require(frame.values[0].value && frame.values[1].value &&
                    frame.values[0].value->kind() == MmsDataKind::boolean &&
                    frame.values[1].value->kind() == MmsDataKind::boolean &&
                    std::get<bool>(frame.values[0].value->value()) &&
                    !std::get<bool>(frame.values[1].value->value()),
                "Canonical report matrix consumed metadata as a process value.");
        require(test.data_reference
                    ? frame.values[0].data_reference == "LD0/GGIO1.Ind1.stVal" &&
                      frame.values[1].data_reference == "LD0/GGIO1.Ind2.stVal"
                    : frame.values[0].data_reference.empty() &&
                      frame.values[1].data_reference.empty(),
                "Canonical report matrix DataRef projection mismatch.");
        require(test.reason
                    ? frame.values[0].reason_for_inclusion.has("data-change") &&
                      frame.values[1].reason_for_inclusion.has("integrity")
                    : frame.values[0].reason_for_inclusion.names.empty() &&
                      frame.values[1].reason_for_inclusion.names.empty(),
                "Canonical report matrix ReasonForInclusion projection mismatch.");
    }

    // Reject the legacy false-green layout where a process value precedes DataRef.
    auto legacy = realistic_report();
    std::swap(legacy.items[9U], legacy.items[11U]);
    require_throws([&legacy, &directory] {
        static_cast<void>(MmsReportFrameMapper::map(legacy, directory.members));
    }, "Legacy Value-before-DataRef report layout was silently accepted.");
}

'''
replace_once(
    "tests/test_reporting.cpp",
    "void report_failure_result_is_preserved() {",
    matrix + "void report_failure_result_is_preserved() {",
)
replace_once(
    "tests/test_reporting.cpp",
    "        {\"exact report mapping\", exact_report_mapping_decodes_optional_fields},\n",
    "        {\"exact report mapping\", exact_report_mapping_decodes_optional_fields},\n"
    "        {\"canonical report payload order\", canonical_report_payload_order_matrix},\n",
)

# Lock server-side selective BRCB layout: DataRef* before Value* before Reason*.
helper = r'''[[nodiscard]] bool decode_visible_string(
    const mms::MmsReadAccessResultView& item,
    const std::string_view expected) noexcept {
    if (!item.success) return false;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 10 || tlv.constructed || tlv.value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (tlv.value[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }
    return true;
}

'''
replace_once(
    "embedded/mms_brcb_hard_profile_smoke.cpp",
    "[[nodiscard]] bool expected_entry(\n",
    helper + "[[nodiscard]] bool expected_entry(\n",
)
replace_once(
    "embedded/mms_brcb_hard_profile_smoke.cpp",
    "        !report.try_item(8U, item) || !decode_bit_string(item, 5U, 0xA0U) ||\n"
    "        !report.try_item(11U, item) || !decode_boolean(item, boolean_value) || !boolean_value ||",
    "        !report.try_item(8U, item) || !decode_bit_string(item, 5U, 0xA0U) ||\n"
    "        !report.try_item(9U, item) || !decode_visible_string(item, \"LD0/X1\") ||\n"
    "        !report.try_item(10U, item) || !decode_visible_string(item, \"LD0/X3\") ||\n"
    "        !report.try_item(11U, item) || !decode_boolean(item, boolean_value) || !boolean_value ||",
)

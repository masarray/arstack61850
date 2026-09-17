from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


# IEDScout uses one generic RCB Optional Fields editor. The captured URCB write
# can therefore carry BufferOverflow + EntryID bits (valid for BRCB only) even
# though the effective URCB report must never advertise/encode buffered-only
# metadata. Accept that client request, normalize it to the effective URCB mask,
# and keep reserved/segmentation bits fail-closed.
replace_once(
    "src/mms/static_urcb_runtime.cpp",
    "constexpr std::uint8_t kAllowedOptionalFirst = 0x7CU;\n"
    "constexpr std::uint8_t kAllowedOptionalSecond = 0x80U;\n",
    "constexpr std::uint8_t kEffectiveOptionalFirst = 0x7CU;\n"
    "constexpr std::uint8_t kAcceptedOptionalFirst = 0x7FU;\n"
    "constexpr std::uint8_t kAllowedOptionalSecond = 0x80U;\n")

replace_once(
    "src/mms/static_urcb_runtime.cpp",
    "[[nodiscard]] bool valid_optional_fields(\n"
    "    const std::span<const std::uint8_t> optional_fields) noexcept {\n"
    "    return optional_fields.size() ==\n"
    "               MmsInformationReportSpanCodec::optional_field_bytes &&\n"
    "        (optional_fields[0] & static_cast<std::uint8_t>(~kAllowedOptionalFirst)) == 0U &&\n"
    "        (optional_fields[1] & static_cast<std::uint8_t>(~kAllowedOptionalSecond)) == 0U;\n"
    "}\n",
    "[[nodiscard]] bool valid_optional_fields(\n"
    "    const std::span<const std::uint8_t> optional_fields) noexcept {\n"
    "    return optional_fields.size() ==\n"
    "               MmsInformationReportSpanCodec::optional_field_bytes &&\n"
    "        (optional_fields[0] & static_cast<std::uint8_t>(~kAcceptedOptionalFirst)) == 0U &&\n"
    "        (optional_fields[1] & static_cast<std::uint8_t>(~kAllowedOptionalSecond)) == 0U;\n"
    "}\n\n"
    "[[nodiscard]] std::array<std::uint8_t,\n"
    "    MmsInformationReportSpanCodec::optional_field_bytes> effective_optional_fields(\n"
    "    const std::span<const std::uint8_t> optional_fields) noexcept {\n"
    "    return {\n"
    "        static_cast<std::uint8_t>(optional_fields[0] & kEffectiveOptionalFirst),\n"
    "        static_cast<std::uint8_t>(optional_fields[1] & kAllowedOptionalSecond)};\n"
    "}\n")

replace_once(
    "src/mms/static_urcb_runtime.cpp",
    "        state_ref.optional_fields = definition.optional_fields;\n",
    "        state_ref.optional_fields = effective_optional_fields(definition.optional_fields);\n")

replace_once(
    "src/mms/static_urcb_runtime.cpp",
    "    const std::array<std::uint8_t, MmsInformationReportSpanCodec::optional_field_bytes>\n"
    "        next{optional_fields[0], optional_fields[1]};\n",
    "    const auto next = effective_optional_fields(optional_fields);\n")


# Owning runtime regression: reproduce the exact 0x7B80 IEDScout URCB OptFlds
# value seen on the wire, prove it is accepted as 0x7880 effective URCB fields,
# keep segmentation rejected, then restore the original report fixture.
replace_once(
    "tests/test_mms_server_urcb_report.cpp",
    "    if (!reports.initialize()) {\n"
    "        return 1;\n"
    "    }\n\n"
    "    std::array<std::uint8_t, 2048U> request{};\n",
    "    if (!reports.initialize()) {\n"
    "        return 1;\n"
    "    }\n\n"
    "    constexpr std::array<std::uint8_t, 2U> iedscout_generic_optflds{0x7BU, 0x80U};\n"
    "    constexpr std::array<std::uint8_t, 2U> effective_urcb_optflds{0x78U, 0x80U};\n"
    "    constexpr std::array<std::uint8_t, 2U> unsupported_segmentation{0x78U, 0xC0U};\n"
    "    constexpr std::array<std::uint8_t, 2U> original_optflds{0x7CU, 0x80U};\n"
    "    if (reports.set_optional_fields(0U, iedscout_generic_optflds) !=\n"
    "            mms::MmsStaticUrcbStatus::ok) {\n"
    "        return 44;\n"
    "    }\n"
    "    const auto* compatibility_state = reports.state(0U);\n"
    "    if (compatibility_state == nullptr ||\n"
    "        compatibility_state->optional_fields != effective_urcb_optflds) {\n"
    "        return 45;\n"
    "    }\n"
    "    if (reports.set_optional_fields(0U, unsupported_segmentation) !=\n"
    "            mms::MmsStaticUrcbStatus::invalid_value) {\n"
    "        return 46;\n"
    "    }\n"
    "    if (reports.set_optional_fields(0U, original_optflds) !=\n"
    "            mms::MmsStaticUrcbStatus::ok) {\n"
    "        return 47;\n"
    "    }\n\n"
    "    std::array<std::uint8_t, 2048U> request{};\n")


# Embedded/runtime hard-profile coverage protects the same normalization rule
# independently from the host-side test executable.
replace_once(
    "embedded/mms_urcb_event_hard_profile_smoke.cpp",
    "    if (!runtime.initialize()) return 3;\n"
    "    if (runtime.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok) return 4;\n",
    "    if (!runtime.initialize()) return 3;\n"
    "    constexpr std::array<std::uint8_t, 2U> iedscout_generic_optflds{0x7BU, 0x80U};\n"
    "    constexpr std::array<std::uint8_t, 2U> effective_urcb_optflds{0x78U, 0x80U};\n"
    "    constexpr std::array<std::uint8_t, 2U> unsupported_segmentation{0x78U, 0xC0U};\n"
    "    constexpr std::array<std::uint8_t, 2U> original_optflds{0x5CU, 0x80U};\n"
    "    if (runtime.set_optional_fields(0U, iedscout_generic_optflds) !=\n"
    "            mms::MmsStaticUrcbStatus::ok) return 15;\n"
    "    const auto* compatibility_state = runtime.state(0U);\n"
    "    if (compatibility_state == nullptr ||\n"
    "        compatibility_state->optional_fields != effective_urcb_optflds) return 16;\n"
    "    if (runtime.set_optional_fields(0U, unsupported_segmentation) !=\n"
    "            mms::MmsStaticUrcbStatus::invalid_value) return 17;\n"
    "    if (runtime.set_optional_fields(0U, original_optflds) !=\n"
    "            mms::MmsStaticUrcbStatus::ok) return 18;\n"
    "    if (runtime.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok) return 4;\n")

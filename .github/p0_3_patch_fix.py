from pathlib import Path

p = Path('.github/p0_3_patch.py')
text = p.read_text()

old = '''replace_between(
    "src/mms/connection_runtime.cpp",
    "[[nodiscard]] bool write_outer_capacity(",
    "\\n}\\n\\n} // namespace",
    core_capacity)'''
new = '''replace_between(
    "src/mms/connection_runtime.cpp",
    "[[nodiscard]] bool write_outer_capacity(",
    "\\n\\n} // namespace",
    core_capacity)'''
if old not in text:
    raise SystemExit('write_outer end marker patch point missing')
text = text.replace(old, new, 1)

old = '''replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "    result.owner_size = owner.size();\\n    return result;\\n}",
    "    result.owner_size = owner.size();\\n    result.maximum_tpdu_size_code = 0x07U;\\n    return result;\\n}")'''
new = '''replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "[[nodiscard]] mms::MmsStaticConnectionPolicy connection_policy(\\n"
    "    const std::uint64_t association,\\n"
    "    const std::array<std::uint8_t, 2U>& owner) noexcept {\\n"
    "    mms::MmsStaticConnectionPolicy result;\\n"
    "    result.association_id = association;\\n"
    "    result.owner[0] = owner[0];\\n"
    "    result.owner[1] = owner[1];\\n"
    "    result.owner_size = owner.size();\\n"
    "    return result;\\n}",
    "[[nodiscard]] mms::MmsStaticConnectionPolicy connection_policy(\\n"
    "    const std::uint64_t association,\\n"
    "    const std::array<std::uint8_t, 2U>& owner) noexcept {\\n"
    "    mms::MmsStaticConnectionPolicy result;\\n"
    "    result.association_id = association;\\n"
    "    result.owner[0] = owner[0];\\n"
    "    result.owner[1] = owner[1];\\n"
    "    result.owner_size = owner.size();\\n"
    "    result.maximum_tpdu_size_code = 0x07U;\\n"
    "    return result;\\n}")'''
if old not in text:
    raise SystemExit('connection_policy helper patch point missing')
text = text.replace(old, new, 1)

anchor = '''# URCB server regression: use the IEDScout-like 1024-byte offer in production,
# but force 128 here plus a long RptID so the InformationReport definitely spans
# multiple TPKTs. Reassemble only inside the test decoder.
'''
insert = anchor + '''replace_once(
    "tests/test_mms_server_urcb_report.cpp",
    "#include <array>\\n",
    "#include <algorithm>\\n#include <array>\\n")
replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "#include <array>\\n",
    "#include <algorithm>\\n#include <array>\\n")
'''
if text.count(anchor) != 1:
    raise SystemExit('URCB anchor missing or duplicated')
text = text.replace(anchor, insert, 1)

anchor = '''replace_once(
    "src/mms/static_report_connection.cpp",
    "                    connection.mms_presentation_context_id(),\\n                    encoded.required_bytes,\\n                    final_required)) {",
    "                    connection.mms_presentation_context_id(),\\n                    encoded.required_bytes,\\n                    connection.negotiated_tpdu_size_bytes(),\\n                    final_required)) {")
'''
extra = anchor + '''replace_once(
    "src/mms/static_report_connection.cpp",
    "        if (p_data.status == wire::EncodeStatus::buffer_too_small) {\\n"
    "            std::size_t final_required{};\\n"
    "            if (!add_size(p_data.required_bytes, 7U, final_required)) {\\n"
    "                return make_result(\\n"
    "                    MmsStaticReportConnectionStatus::report_encode_failed,\\n"
    "                    MmsStaticUrcbStatus::report_encode_failed,\\n"
    "                    plan);\\n"
    "            }\\n"
    "            auto result = workspace_capacity(\\n"
    "                plan, MmsStaticUrcbStatus::workspace_too_small, final_required);\\n"
    "            result.required_response_bytes = final_required;\\n"
    "            return result;\\n"
    "        }",
    "        if (p_data.status == wire::EncodeStatus::buffer_too_small) {\\n"
    "            osi::CotpTpktDataStreamPlan stream_plan;\\n"
    "            if (!osi::CotpTpktDataStreamSpanCodec::try_plan(\\n"
    "                    p_data.required_bytes,\\n"
    "                    connection.negotiated_tpdu_size_bytes(),\\n"
    "                    stream_plan)) {\\n"
    "                return make_result(\\n"
    "                    MmsStaticReportConnectionStatus::report_encode_failed,\\n"
    "                    MmsStaticUrcbStatus::report_encode_failed,\\n"
    "                    plan);\\n"
    "            }\\n"
    "            auto result = workspace_capacity(\\n"
    "                plan, MmsStaticUrcbStatus::workspace_too_small,\\n"
    "                p_data.required_bytes);\\n"
    "            result.required_response_bytes = stream_plan.required_bytes;\\n"
    "            return result;\\n"
    "        }")
'''
if text.count(anchor) != 1:
    raise SystemExit('static report p_data anchor missing or duplicated')
text = text.replace(anchor, extra, 1)

p.write_text(text)

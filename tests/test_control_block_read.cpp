// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/control_block_model_evidence.hpp"
#include "ariec61850/mms/control_block_read.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

void inventory_groups_exact_live_name_list_items() {
    using namespace ar::iec61850::mms;
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variables["IEDLD0"] = {
        "LLN0$GO$gcb01$GoEna",
        "LLN0$GO$gcb01$DatSet",
        "LLN0$GO$gcb01$DstAddress$APPID",
        "LLN0$MS$msvcb01$SvEna",
        "LLN0$MS$msvcb01$SvID",
        "LLN0$SP$SGCB$ActSG",
        "LLN0$SP$OtherSetting$setVal",
        "LLN0$LG$lcb01$LogEna",
        "MMXU1$MX$A$phsA$cVal$mag$f"};

    const auto controls = MmsControlBlockInventoryBuilder::build(snapshot);
    CHECK(controls.size() == 4U);

    CHECK(controls[0].kind == MmsControlBlockKind::goose);
    CHECK(controls[0].reference == "IEDLD0/LLN0.GO.gcb01");
    CHECK(controls[0].attributes.size() == 3U);
    CHECK(controls[0].attributes[0].attribute_path == "DatSet");
    CHECK(controls[0].attributes[1].attribute_path == "DstAddress.APPID");
    CHECK(controls[0].attributes[1].variable.item ==
          "LLN0$GO$gcb01$DstAddress$APPID");
    CHECK(controls[0].attributes[2].attribute_path == "GoEna");

    CHECK(controls[1].kind == MmsControlBlockKind::log);
    CHECK(controls[1].reference == "IEDLD0/LLN0.LG.lcb01");

    CHECK(controls[2].kind == MmsControlBlockKind::sampled_value);
    CHECK(controls[2].reference == "IEDLD0/LLN0.MS.msvcb01");
    CHECK(controls[2].attributes.size() == 2U);

    CHECK(controls[3].kind == MmsControlBlockKind::setting_group);
    CHECK(controls[3].reference == "IEDLD0/LLN0.SP.SGCB");
    CHECK(controls[3].attributes.size() == 1U);
    CHECK(controls[3].attributes[0].attribute_path == "ActSG");
}

void inventory_supports_us_sg_and_se_functional_constraints() {
    using namespace ar::iec61850::mms;
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variables["IEDLD1"] = {
        "LLN0$US$usvcb01$SvID",
        "LLN0$SG$SGCB$NumOfSG",
        "LLN0$SE$SGCB$EditSG"};

    const auto controls = MmsControlBlockInventoryBuilder::build(snapshot);
    CHECK(controls.size() == 3U);
    CHECK(controls[0].kind == MmsControlBlockKind::setting_group);
    CHECK(controls[1].kind == MmsControlBlockKind::setting_group);
    CHECK(controls[2].kind == MmsControlBlockKind::sampled_value);
}

void inventory_deduplicates_attribute_paths_case_insensitively() {
    using namespace ar::iec61850::mms;
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variables["IEDLD0"] = {
        "LLN0$GO$gcb01$DatSet",
        "lln0$go$GCB01$datset"};

    const auto controls = MmsControlBlockInventoryBuilder::build(snapshot);
    CHECK(controls.size() == 1U);
    CHECK(controls[0].attributes.size() == 1U);
    CHECK(controls[0].attributes[0].attribute_path == "DatSet");
    CHECK(controls[0].attributes[0].variable.item == "LLN0$GO$gcb01$DatSet");
}

void runtime_projector_promotes_complete_ocr7sr12_sgcb_evidence() {
    using namespace ar::iec61850::mms;

    MmsLiveDiscoveryResult discovery;
    discovery.endpoint = {"192.168.1.10", 102U};
    discovery.names.domain_variables["OCR7SR12PROT"] = {
        "LLN0$SP$SGCB$ActSG",
        "LLN0$SP$SGCB$CnfEdit",
        "LLN0$SP$SGCB$EditSG",
        "LLN0$SP$SGCB$LActTm",
        "LLN0$SP$SGCB$NumOfSG"};
    discovery.control_block_value_reads_requested = true;

    const auto candidates = MmsControlBlockInventoryBuilder::build(discovery.names);
    CHECK(candidates.size() == 1U);
    CHECK(candidates.front().reference == "OCR7SR12PROT/LLN0.SP.SGCB");
    discovery.control_block_value_candidate_count = candidates.size();

    MmsControlBlockReadResult evidence;
    evidence.candidate = candidates.front();
    const Iec61850UtcTime last_activation{
        std::chrono::system_clock::time_point{std::chrono::milliseconds{1'786'100'000'000LL}},
        0U};
    evidence.attributes = {
        {"ActSG", MmsObjectName::domain_specific(
            "OCR7SR12PROT", "LLN0$SP$SGCB$ActSG"),
         MmsDataValue::unsigned_integer(1U), std::nullopt},
        {"CnfEdit", MmsObjectName::domain_specific(
            "OCR7SR12PROT", "LLN0$SP$SGCB$CnfEdit"),
         MmsDataValue::boolean(false), std::nullopt},
        {"EditSG", MmsObjectName::domain_specific(
            "OCR7SR12PROT", "LLN0$SP$SGCB$EditSG"),
         MmsDataValue::unsigned_integer(0U), std::nullopt},
        {"LActTm", MmsObjectName::domain_specific(
            "OCR7SR12PROT", "LLN0$SP$SGCB$LActTm"),
         MmsDataValue::utc_time(last_activation), std::nullopt},
        {"NumOfSG", MmsObjectName::domain_specific(
            "OCR7SR12PROT", "LLN0$SP$SGCB$NumOfSG"),
         MmsDataValue::unsigned_integer(1U), std::nullopt}};
    CHECK(evidence.complete());
    discovery.control_block_reads.push_back(std::move(evidence));

    auto model = MmsLiveModelBuilder::build(discovery);
    CHECK(model.setting_group_controls.size() == 1U);
    CHECK(std::any_of(
        model.warnings.begin(), model.warnings.end(), [](const auto& warning) {
            return warning.code == "CONTROL_BLOCK_VALUE_READ_PENDING";
        }));
    const auto structural_before = model.canonical_fingerprint();

    MmsControlBlockRuntimeProjector::apply(discovery, model);

    CHECK(model.canonical_fingerprint() == structural_before);
    const auto& sgcb = model.setting_group_controls.front();
    CHECK(sgcb.discovery_status == "ValueReadComplete");
    CHECK(sgcb.reference == "OCR7SR12PROT/LLN0.SP.SGCB");
    CHECK(sgcb.message.find("deep read 5/5 attributes") != std::string::npos);
    CHECK(sgcb.message.find("ActSG=1") != std::string::npos);
    CHECK(sgcb.message.find("CnfEdit=false") != std::string::npos);
    CHECK(sgcb.message.find("EditSG=0") != std::string::npos);
    CHECK(sgcb.message.find("LActTm=utcMs=") != std::string::npos);
    CHECK(sgcb.message.find("NumOfSG=1") != std::string::npos);
    CHECK(model.summary.find("CBValueComplete=1") != std::string::npos);
    CHECK(model.summary.find("CBValuePartial=0") != std::string::npos);
    CHECK(std::none_of(
        model.warnings.begin(), model.warnings.end(), [](const auto& warning) {
            return warning.code == "CONTROL_BLOCK_VALUE_READ_PENDING";
        }));
}

void runtime_projector_maps_goose_values_without_inventing_failed_address_data() {
    using namespace ar::iec61850::mms;

    MmsLiveDiscoveryResult discovery;
    discovery.endpoint = {"192.0.2.20", 102U};
    discovery.names.domain_variables["IEDLD0"] = {
        "LLN0$GO$gcb01$DatSet",
        "LLN0$GO$gcb01$GoID",
        "LLN0$GO$gcb01$DstAddress$APPID"};
    discovery.control_block_value_reads_requested = true;

    const auto candidates = MmsControlBlockInventoryBuilder::build(discovery.names);
    CHECK(candidates.size() == 1U);
    discovery.control_block_value_candidate_count = candidates.size();

    MmsControlBlockReadResult evidence;
    evidence.candidate = candidates.front();
    evidence.attributes = {
        {"DatSet", MmsObjectName::domain_specific(
            "IEDLD0", "LLN0$GO$gcb01$DatSet"),
         MmsDataValue::visible_string("IEDLD0/LLN0$DataSetA"), std::nullopt},
        {"DstAddress.APPID", MmsObjectName::domain_specific(
            "IEDLD0", "LLN0$GO$gcb01$DstAddress$APPID"),
         std::nullopt, 3U},
        {"GoID", MmsObjectName::domain_specific(
            "IEDLD0", "LLN0$GO$gcb01$GoID"),
         MmsDataValue::visible_string("GOOSE-1"), std::nullopt}};
    discovery.control_block_reads.push_back(std::move(evidence));

    auto model = MmsLiveModelBuilder::build(discovery);
    MmsControlBlockRuntimeProjector::apply(discovery, model);

    CHECK(model.goose_control_blocks.size() == 1U);
    const auto& goose = model.goose_control_blocks.front();
    CHECK(goose.discovery_status == "ValueReadPartial");
    CHECK(goose.data_set_reference == "IEDLD0/LLN0.DataSetA");
    CHECK(goose.data_set_reference_status == "ValueRead");
    CHECK(goose.control_id == "GOOSE-1");
    CHECK(goose.app_id.empty());
    CHECK(goose.address_status == "MmsValueReadFailed");
    CHECK(std::any_of(
        model.warnings.begin(), model.warnings.end(), [](const auto& warning) {
            return warning.code == "CONTROL_BLOCK_VALUE_READ_PARTIAL";
        }));
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"group exact live control blocks", inventory_groups_exact_live_name_list_items},
        {"support US SG SE control blocks", inventory_supports_us_sg_and_se_functional_constraints},
        {"deduplicate control block attributes", inventory_deduplicates_attribute_paths_case_insensitively},
        {"project complete OCR7SR12 SGCB evidence", runtime_projector_promotes_complete_ocr7sr12_sgcb_evidence},
        {"project partial GOOSE evidence", runtime_projector_maps_goose_values_without_inventing_failed_address_data}};

    std::size_t passed{};
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << passed << '/' << tests.size()
              << " control-block read tests.\n";
    return 0;
}

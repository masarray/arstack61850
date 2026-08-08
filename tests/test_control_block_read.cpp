// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/control_block_read.hpp"

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

void inventory_deduplicates_exact_variables_case_insensitively() {
    using namespace ar::iec61850::mms;
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variables["IEDLD0"] = {
        "LLN0$GO$gcb01$DatSet",
        "lln0$go$GCB01$dataset"};

    const auto controls = MmsControlBlockInventoryBuilder::build(snapshot);
    CHECK(controls.size() == 1U);
    CHECK(controls[0].attributes.size() == 1U);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"group exact live control blocks", inventory_groups_exact_live_name_list_items},
        {"support US SG SE control blocks", inventory_supports_us_sg_and_se_functional_constraints},
        {"deduplicate control block variables", inventory_deduplicates_exact_variables_case_insensitively}};

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

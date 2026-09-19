// SPDX-License-Identifier: GPL-3.0-or-later

#include "../apps/ied_simulator/src/MmsSclClientProjection.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace mms = ar::iec61850::mms;
namespace scl = ar::iec61850::scl;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

[[nodiscard]] scl::SclDocument document() {
    scl::SclDocument value;
    value.ieds.push_back(scl::SclIed{"IED1", "ARStack", "TEST", "1"});
    value.logical_nodes.push_back(
        scl::SclLogicalNode{"IED1", "LD0", "", "LLN0", "", "LLN0"});
    return value;
}

[[nodiscard]] mms::MmsSclAssistedConnectResult matched_snapshot() {
    mms::MmsSclAssistedConnectResult snapshot;
    snapshot.endpoint = {"192.0.2.10", 102U};
    snapshot.ied_name = "IED1";
    snapshot.domains.expected = {"IED1LD0"};
    snapshot.domains.online = {"IED1LD0"};
    snapshot.plan.ied_name = "IED1";
    snapshot.plan.expected_domains = {"IED1LD0"};
    snapshot.associated_after_snapshot = true;
    return snapshot;
}

void matched_projection_remains_exact() {
    const auto model = arstack::iedsim::build_scl_live_model(
        document(), matched_snapshot());
    CHECK(model.identity.ied_name == "IED1");
    CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::exact);
    CHECK(model.summary.find("Trusted SCL [matched]") != std::string::npos);
    CHECK(model.warnings.empty());
}

void degraded_projection_is_explicit_and_preserves_scl() {
    auto snapshot = matched_snapshot();
    snapshot.domains.online.push_back("EXTRA_LD");
    snapshot.domains.extra.push_back("EXTRA_LD");

    const auto model = arstack::iedsim::build_scl_live_model(document(), snapshot);
    CHECK(model.identity.ied_name == "IED1");
    CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::high);
    CHECK(model.logical_devices.size() == 1U);
    CHECK(model.logical_devices.front().mms_domain == "IED1LD0");
    CHECK(model.summary.find("Trusted SCL [degraded]") != std::string::npos);
    CHECK(model.summary.find("extraDomains=1") != std::string::npos);
    CHECK(!model.warnings.empty());
    CHECK(model.warnings.front().code == "SclOnlineIdentityDegraded");
}

void incompatible_projection_fails_closed() {
    auto snapshot = matched_snapshot();
    snapshot.domains.online = {"OTHERLD0"};
    snapshot.domains.missing = {"IED1LD0"};
    snapshot.domains.extra = {"OTHERLD0"};

    bool rejected{};
    try {
        static_cast<void>(
            arstack::iedsim::build_scl_live_model(document(), snapshot));
    } catch (const mms::MmsSclAssistedConnectError& exception) {
        rejected = std::string{exception.what()}.find("incompatible") != std::string::npos;
    }
    CHECK(rejected);
}

} // namespace

int main() {
    try {
        matched_projection_remains_exact();
        degraded_projection_is_explicit_and_preserves_scl();
        incompatible_projection_fails_closed();
        std::cout
            << "SCL_PROJECTION_HEALTH_PASS matched=exact degraded=high "
               "unknown_domain_not_merged=true incompatible=fail_closed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

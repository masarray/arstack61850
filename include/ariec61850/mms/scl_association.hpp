// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/scl/model.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::mms {

struct MmsSclAssociationContext final {
    std::string ied_name;
    std::string access_point_name;
    std::string scl_host;
    std::uint16_t scl_port{102U};
    std::optional<MmsAssociationAddressing> addressing;

    [[nodiscard]] bool selected() const noexcept { return !ied_name.empty(); }
    [[nodiscard]] bool uses_engineering_addressing() const noexcept {
        return addressing.has_value();
    }
};

// Select one SCL ConnectedAP for the requested online endpoint. Exact IP/host
// match wins. If the engineering file describes one IED/one MMS AccessPoint,
// that AccessPoint is also unambiguous even when the operator used an alias or
// hostname. Multiple unmatched AccessPoints fail closed rather than borrowing
// association selectors from an unrelated IED.
[[nodiscard]] inline MmsSclAssociationContext resolve_scl_association_context(
    const scl::SclDocument& document,
    const std::string_view requested_host) {
    MmsSclAssociationContext result;
    if (document.mms_access_points.empty()) return result;

    std::vector<const scl::SclMmsAccessPoint*> candidates;
    candidates.reserve(document.mms_access_points.size());
    for (const auto& access_point : document.mms_access_points) {
        if (!access_point.ied_name.empty()) candidates.push_back(&access_point);
    }
    if (candidates.empty()) return result;

    std::vector<const scl::SclMmsAccessPoint*> host_matches;
    if (!requested_host.empty()) {
        for (const auto* access_point : candidates) {
            if (!access_point->ip_address.empty() &&
                access_point->ip_address == requested_host) {
                host_matches.push_back(access_point);
            }
        }
    }

    const scl::SclMmsAccessPoint* selected{};
    if (host_matches.size() == 1U) {
        selected = host_matches.front();
    } else if (host_matches.size() > 1U) {
        throw std::runtime_error(
            "Trusted SCL contains multiple MMS ConnectedAP entries for endpoint '" +
            std::string{requested_host} + "'. Select an unambiguous engineering endpoint.");
    } else if (candidates.size() == 1U) {
        selected = candidates.front();
    } else if (document.ieds.size() == 1U) {
        const auto& only_ied = document.ieds.front().name;
        std::vector<const scl::SclMmsAccessPoint*> ied_candidates;
        for (const auto* access_point : candidates) {
            if (access_point->ied_name == only_ied) ied_candidates.push_back(access_point);
        }
        if (ied_candidates.size() == 1U) selected = ied_candidates.front();
    }

    if (selected == nullptr) {
        throw std::runtime_error(
            "Trusted SCL MMS ConnectedAP is ambiguous for endpoint '" +
            std::string{requested_host} + "'. No association addressing was guessed.");
    }
    if (!selected->association_parameters_valid) {
        throw std::runtime_error(
            "Trusted SCL MMS association addressing is invalid for " +
            selected->ied_name + "/" + selected->access_point_name + ": " +
            selected->association_error);
    }

    result.ied_name = selected->ied_name;
    result.access_point_name = selected->access_point_name;
    result.scl_host = selected->ip_address;
    result.scl_port = selected->tcp_port;

    if (selected->association_parameters_present) {
        MmsAssociationAddressing addressing;
        addressing.called_ap_title = selected->ap_title;
        addressing.called_ae_qualifier = selected->ae_qualifier;
        addressing.called_p_selector = selected->p_selector;
        addressing.called_s_selector = selected->s_selector;
        addressing.called_t_selector = selected->t_selector;
        result.addressing = std::move(addressing);
    }
    return result;
}

} // namespace ar::iec61850::mms

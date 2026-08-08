// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/tcp_transport.hpp"

#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsLiveDiscoveryError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

struct MmsLiveDiscoveryOptions final {
    std::size_t maximum_pages_per_query{256U};
    std::size_t maximum_domains{4'096U};
    std::size_t maximum_names_per_domain{65'536U};

    bool probe_variable_types{true};
    std::size_t maximum_variable_type_probes{4'096U};

    bool read_data_set_directories{true};
    std::size_t maximum_data_set_directories{4'096U};

    bool probe_report_controls{true};
    std::size_t maximum_report_control_probes{1'024U};
    std::vector<std::string> report_control_attributes{
        "DatSet", "RptID", "ConfRev", "IntgPd", "BufTm", "SqNum",
        "RptEna", "Resv", "ResvTms", "Owner", "EntryID", "TimeOfEntry",
        "TrgOps", "OptFlds"};

    bool continue_on_optional_probe_error{true};
};

struct MmsVariableTypeEvidence final {
    MmsObjectName variable;
    std::optional<MmsVariableAccessAttributesResponse> attributes;
    std::string error;

    [[nodiscard]] bool success() const noexcept { return attributes.has_value(); }
};

struct MmsDataSetDirectoryEvidence final {
    MmsDataSetCandidate candidate;
    std::optional<MmsDataSetDirectoryResponse> directory;
    std::string error;

    [[nodiscard]] bool success() const noexcept { return directory.has_value(); }
};

struct MmsReportControlEvidence final {
    MmsReportControlCandidate candidate;
    std::vector<std::string> requested_attributes;
    std::optional<MmsReportControlState> state;
    std::string error;

    [[nodiscard]] bool success() const noexcept { return state.has_value(); }
};

struct MmsLiveDiscoveryResult final {
    MmsEndpoint endpoint;
    std::string association_profile;
    std::vector<MmsAssociationAttemptEvidence> association_attempts;
    MmsDiscoverySnapshot names;
    MmsReportInventory report_inventory;
    std::vector<MmsVariableTypeEvidence> variable_types;
    std::vector<MmsDataSetDirectoryEvidence> data_set_directories;
    std::vector<MmsReportControlEvidence> report_controls;
    std::vector<std::string> diagnostics;

    [[nodiscard]] std::size_t domain_count() const noexcept;
    [[nodiscard]] std::size_t variable_count() const noexcept;
    [[nodiscard]] std::size_t variable_list_count() const noexcept;
    [[nodiscard]] bool partial() const noexcept { return !diagnostics.empty(); }
    [[nodiscard]] std::string summary() const;
};

// Executes only MMS GetNameList, GetVariableAccessAttributes,
// GetNamedVariableListAttributes, and Read requests. It never sends Write,
// control, file mutation, RCB enable, or dynamic DataSet mutation services.
class MmsLiveDiscoveryClient final {
public:
    explicit MmsLiveDiscoveryClient(MmsAssociationRuntime& association);

    [[nodiscard]] MmsLiveDiscoveryResult discover(
        const MmsLiveDiscoveryOptions& options = {},
        std::stop_token stop_token = {});

private:
    [[nodiscard]] std::vector<std::string> get_name_list(
        MmsGetNameListObjectClass object_class,
        const std::string& domain,
        const MmsLiveDiscoveryOptions& options,
        std::stop_token stop_token);

    MmsAssociationRuntime& association_;
};

// Ready-to-use live session with the built-in non-blocking TCP transport.
class MmsTcpLiveDiscoverySession final {
public:
    explicit MmsTcpLiveDiscoverySession(
        TcpMmsTransportOptions transport_options = {},
        MmsAssociationOptions association_options = {});

    void connect(
        const MmsEndpoint& endpoint,
        std::stop_token stop_token = {});
    [[nodiscard]] MmsLiveDiscoveryResult discover(
        const MmsLiveDiscoveryOptions& options = {},
        std::stop_token stop_token = {});
    void disconnect(std::stop_token stop_token = {}) noexcept;

    [[nodiscard]] bool associated() const noexcept { return association_.associated(); }
    [[nodiscard]] MmsAssociationRuntime& association() noexcept { return association_; }
    [[nodiscard]] const MmsAssociationRuntime& association() const noexcept {
        return association_;
    }

private:
    TcpMmsByteTransport transport_;
    MmsAssociationRuntime association_;
    MmsLiveDiscoveryClient discovery_;
};

} // namespace ar::iec61850::mms

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

enum class MmsStaticDispatchStatus : std::uint8_t {
    response_ready,
    malformed_request,
    unsupported_service,
    unsupported_request,
    object_not_found,
    invalid_object_table,
    workspace_too_small,
    response_buffer_too_small,
    backend_failure,
};

struct MmsStaticDispatchPolicy final {
    // The measured IEDScout server discovery profile emits at most 100
    // identifiers per GetNameList page. Keep the portable default aligned with
    // that profile; tests/embedded users may still choose a smaller bound.
    std::size_t maximum_names_per_response{100U};
    std::size_t maximum_write_variables{1U};
    std::uint32_t missing_object_failure_code{10U};
    std::uint32_t access_denied_failure_code{3U};
    std::uint32_t backend_failure_code{10U};
    // Some live discovery clients still derive the Logical Node root from
    // flattened LN$FC$DO$DA names before requesting the root TypeSpecification.
    // Keep strict IED-simulator root-only discovery as the default, while
    // allowing host interoperability adapters to advertise both forms.
    bool advertise_flattened_child_aliases{};
};

struct MmsStaticDirectoryEntry final {
    std::string_view domain;
    std::string_view item;
};

struct MmsStaticDispatchResult final {
    MmsStaticDispatchStatus status{MmsStaticDispatchStatus::malformed_request};
    MmsWireConfirmedService service{MmsWireConfirmedService::unknown};
    std::uint32_t invoke_id{};
    std::size_t bytes_written{};
    std::size_t required_bytes{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticDispatchStatus::response_ready;
    }
};

class MmsStaticApplicationDispatcher final {
public:
    explicit MmsStaticApplicationDispatcher(
        const MmsStaticObjectTable& objects,
        const MmsStaticDispatchPolicy policy = {}) noexcept
        : objects_{objects},
          policy_{policy},
          model_valid_{data_sets_.valid_against(objects_)} {}

    MmsStaticApplicationDispatcher(
        const MmsStaticObjectTable& objects,
        const MmsStaticDataSetTable& data_sets,
        const MmsStaticDispatchPolicy policy = {}) noexcept
        : objects_{objects},
          data_sets_{data_sets},
          policy_{policy},
          model_valid_{data_sets_.valid_against(objects_)} {}

    // directory must be sorted by (domain,item), unique, and remain alive for
    // the dispatcher lifetime. It is optional so embedded profiles retain the
    // fixed-buffer scan path without host-side heap requirements.
    MmsStaticApplicationDispatcher(
        const MmsStaticObjectTable& objects,
        const MmsStaticDataSetTable& data_sets,
        const std::span<const MmsStaticDirectoryEntry> directory,
        const MmsStaticDispatchPolicy policy = {}) noexcept
        : objects_{objects},
          data_sets_{data_sets},
          directory_{directory},
          policy_{policy},
          model_valid_{data_sets_.valid_against(objects_)} {}

    MmsStaticApplicationDispatcher(
        const MmsStaticObjectTable& objects,
        const std::span<const MmsStaticDirectoryEntry> directory,
        const MmsStaticDispatchPolicy policy = {}) noexcept
        : objects_{objects},
          directory_{directory},
          policy_{policy},
          model_valid_{data_sets_.valid_against(objects_)} {}

    [[nodiscard]] MmsStaticDispatchResult dispatch(
        std::span<const std::uint8_t> mms_request,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace,
        const MmsStaticRequestAccessContext& access = {}) const noexcept;

    [[nodiscard]] MmsStaticDispatchResult dispatch(
        const MmsConfirmedPduView& request,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace,
        const MmsStaticRequestAccessContext& access = {}) const noexcept;

    [[nodiscard]] constexpr const MmsStaticDispatchPolicy& policy() const noexcept {
        return policy_;
    }

    [[nodiscard]] constexpr const MmsStaticDataSetTable& data_sets() const noexcept {
        return data_sets_;
    }

private:
    const MmsStaticObjectTable& objects_;
    MmsStaticDataSetTable data_sets_{};
    std::span<const MmsStaticDirectoryEntry> directory_{};
    MmsStaticDispatchPolicy policy_{};
    // Structural object/DataSet validation is immutable for the dispatcher
    // lifetime. Compute it once at construction so large SCL models do not pay
    // the O(N^2) duplicate scan again for every MMS request.
    bool model_valid_{};
};

} // namespace ar::iec61850::mms

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/acse/association_span.hpp"

#include <cstdint>
#include <span>

namespace ar::iec61850::acse {

// Bounded compatibility decoder for live engineering clients.
//
// The original ARIEC61850 server deliberately used a tolerant AARQ inspector:
// it required a Session Connect carrying Presentation contexts, ACSE AARQ,
// UserInformation and MMS InitiateRequest, but did not reject harmless optional
// ACSE/Presentation decorations it did not need. This helper preserves that
// proven behavior without weakening the strict span codecs used elsewhere.
class AcseAssociationCompat final {
public:
    [[nodiscard]] static bool try_decode_request_view(
        std::span<const std::uint8_t> bytes,
        AssociationRequestView& request) noexcept;
};

} // namespace ar::iec61850::acse

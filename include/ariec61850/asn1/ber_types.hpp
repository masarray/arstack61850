// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ar::iec61850::asn1 {

enum class BerClass : std::uint8_t {
    universal = 0,
    application = 1,
    context_specific = 2,
    private_class = 3
};

} // namespace ar::iec61850::asn1

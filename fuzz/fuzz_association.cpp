// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz_targets.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    ar::iec61850::fuzzing::exercise_association(
        std::span<const std::uint8_t>{data, size});
    return 0;
}

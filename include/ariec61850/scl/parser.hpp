// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/scl/model.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace ar::iec61850::scl {

class SclParser final {
public:
    [[nodiscard]] SclDocument load(const std::filesystem::path& file_path) const;
    [[nodiscard]] SclDocument parse(
        std::string_view xml,
        std::string source_name = {}) const;
};

} // namespace ar::iec61850::scl

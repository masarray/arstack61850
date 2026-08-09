// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_object_table.hpp"

#include <cstddef>
#include <span>

namespace ar::iec61850::mms {

bool MmsStaticObjectTable::try_resolve_write_request(
    const MmsWriteRequestView& request,
    const std::span<const MmsStaticObjectEntry*> resolved,
    std::size_t& resolved_count) const noexcept {
    resolved_count = 0U;
    if (request.variable_count == 0U ||
        request.variable_count != request.data_count ||
        request.variable_count > resolved.size() ||
        request.variable_count > MmsServiceSpanCodec::maximum_variables) {
        return false;
    }
    for (std::size_t index = 0U; index < request.variable_count; ++index) {
        MmsObjectNameView name;
        if (!request.try_variable(index, name)) {
            resolved_count = 0U;
            return false;
        }
        resolved[index] = find(name);
        ++resolved_count;
    }
    return true;
}

} // namespace ar::iec61850::mms

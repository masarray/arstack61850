from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}")
    p.write_text(text.replace(old, new, count))


# Public zero-allocation view. Host runtimes may precompile it once;
# existing embedded callers keep the legacy bounded fallback.
replace(
    'include/ariec61850/mms/static_dispatcher.hpp',
    '#include <span>\n',
    '#include <span>\n#include <string_view>\n')
replace(
    'include/ariec61850/mms/static_dispatcher.hpp',
    'struct MmsStaticDispatchResult final {\n',
    'struct MmsStaticDirectoryEntry final {\n'
    '    std::string_view domain;\n'
    '    std::string_view item;\n'
    '};\n\n'
    'struct MmsStaticDispatchResult final {\n')
replace(
    'include/ariec61850/mms/static_dispatcher.hpp',
    '    constexpr MmsStaticApplicationDispatcher(\n'
    '        const MmsStaticObjectTable& objects,\n'
    '        const MmsStaticDataSetTable& data_sets,\n'
    '        const MmsStaticDispatchPolicy policy = {}) noexcept\n'
    '        : objects_{objects}, data_sets_{data_sets}, policy_{policy} {}\n',
    '    constexpr MmsStaticApplicationDispatcher(\n'
    '        const MmsStaticObjectTable& objects,\n'
    '        const MmsStaticDataSetTable& data_sets,\n'
    '        const MmsStaticDispatchPolicy policy = {}) noexcept\n'
    '        : objects_{objects}, data_sets_{data_sets}, policy_{policy} {}\n\n'
    '    // directory must be sorted by (domain,item), unique, and remain alive for\n'
    '    // the dispatcher lifetime. It is optional so embedded profiles retain the\n'
    '    // fixed-buffer scan path without host-side heap requirements.\n'
    '    constexpr MmsStaticApplicationDispatcher(\n'
    '        const MmsStaticObjectTable& objects,\n'
    '        const MmsStaticDataSetTable& data_sets,\n'
    '        const std::span<const MmsStaticDirectoryEntry> directory,\n'
    '        const MmsStaticDispatchPolicy policy = {}) noexcept\n'
    '        : objects_{objects}, data_sets_{data_sets}, directory_{directory}, policy_{policy} {}\n\n'
    '    constexpr MmsStaticApplicationDispatcher(\n'
    '        const MmsStaticObjectTable& objects,\n'
    '        const std::span<const MmsStaticDirectoryEntry> directory,\n'
    '        const MmsStaticDispatchPolicy policy = {}) noexcept\n'
    '        : objects_{objects}, directory_{directory}, policy_{policy} {}\n')
replace(
    'include/ariec61850/mms/static_dispatcher.hpp',
    '    const MmsStaticObjectTable& objects_;\n'
    '    MmsStaticDataSetTable data_sets_{};\n'
    '    MmsStaticDispatchPolicy policy_{};\n',
    '    const MmsStaticObjectTable& objects_;\n'
    '    MmsStaticDataSetTable data_sets_{};\n'
    '    std::span<const MmsStaticDirectoryEntry> directory_{};\n'
    '    MmsStaticDispatchPolicy policy_{};\n')

fast = '''[[nodiscard]] MmsStaticDispatchResult dispatch_indexed_domain_named_variable_page(
    const std::span<const MmsStaticDirectoryEntry> directory,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const MmsGetNameListRequestView& request,
    const std::span<std::uint8_t> response) noexcept {
    const auto domain = as_text(request.domain_id);
    const auto first = std::lower_bound(
        directory.begin(), directory.end(), domain,
        [](const MmsStaticDirectoryEntry& entry, const std::string_view value) noexcept {
            return entry.domain < value;
        });
    const auto last = std::upper_bound(
        first, directory.end(), domain,
        [](const std::string_view value, const MmsStaticDirectoryEntry& entry) noexcept {
            return value < entry.domain;
        });

    auto cursor = first;
    if (!request.continue_after.empty()) {
        const auto continuation = as_text(request.continue_after);
        const auto found = std::lower_bound(
            first, last, continuation,
            [](const MmsStaticDirectoryEntry& entry, const std::string_view value) noexcept {
                return entry.item < value;
            });
        if (found == last || found->item != continuation) {
            return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
        }
        cursor = found + 1;
    }

    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers> page{};
    std::size_t page_count{};
    while (cursor != last && page_count < policy.maximum_names_per_response) {
        page[page_count++] = cursor->item;
        ++cursor;
    }
    const bool more_follows = cursor != last;
    if (page_count == 0U) {
        const std::span<const std::string_view> empty;
        return make_encoded(
            confirmed,
            MmsServiceSpanCodec::encode_get_name_list_response_into(
                confirmed.invoke_id, empty, false, response));
    }

    auto encoded_count = page_count;
    while (encoded_count > 0U) {
        const auto encoded = MmsServiceSpanCodec::encode_get_name_list_response_into(
            confirmed.invoke_id,
            std::span<const std::string_view>{page}.first(encoded_count),
            more_follows || encoded_count < page_count,
            response);
        if (encoded.success()) return make_encoded(confirmed, encoded);
        if (encoded.status != wire::EncodeStatus::buffer_too_small || encoded_count == 1U) {
            return make_encoded(confirmed, encoded);
        }
        --encoded_count;
    }
    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
}

'''
replace(
    'src/mms/static_dispatcher.cpp',
    '[[nodiscard]] MmsStaticDispatchResult dispatch_domain_named_variable_page(\n',
    fast + '[[nodiscard]] MmsStaticDispatchResult dispatch_domain_named_variable_page(\n')
replace(
    'src/mms/static_dispatcher.cpp',
    '[[nodiscard]] MmsStaticDispatchResult dispatch_get_name_list(\n'
    '    const MmsStaticObjectTable& objects,\n'
    '    const MmsStaticDataSetTable& data_sets,\n'
    '    const MmsStaticDispatchPolicy& policy,\n'
    '    const MmsConfirmedPduView& confirmed,\n'
    '    const std::span<std::uint8_t> response) noexcept {\n',
    '[[nodiscard]] MmsStaticDispatchResult dispatch_get_name_list(\n'
    '    const MmsStaticObjectTable& objects,\n'
    '    const MmsStaticDataSetTable& data_sets,\n'
    '    const std::span<const MmsStaticDirectoryEntry> directory,\n'
    '    const MmsStaticDispatchPolicy& policy,\n'
    '    const MmsConfirmedPduView& confirmed,\n'
    '    const std::span<std::uint8_t> response) noexcept {\n')
replace(
    'src/mms/static_dispatcher.cpp',
    '    if (request.object_class == MmsNameListObjectClass::named_variable &&\n'
    '        request.scope == MmsNameScopeKind::domain_specific) {\n'
    '        return dispatch_domain_named_variable_page(\n'
    '            objects, policy, confirmed, request, response);\n'
    '    }\n',
    '    if (request.object_class == MmsNameListObjectClass::named_variable &&\n'
    '        request.scope == MmsNameScopeKind::domain_specific) {\n'
    '        if (!directory.empty() && policy.advertise_flattened_child_aliases) {\n'
    '            return dispatch_indexed_domain_named_variable_page(\n'
    '                directory, policy, confirmed, request, response);\n'
    '        }\n'
    '        return dispatch_domain_named_variable_page(\n'
    '            objects, policy, confirmed, request, response);\n'
    '    }\n')
replace(
    'src/mms/static_dispatcher.cpp',
    '    case MmsWireConfirmedService::get_name_list:\n'
    '        return dispatch_get_name_list(objects_, data_sets_, policy_, request, response);\n',
    '    case MmsWireConfirmedService::get_name_list:\n'
    '        return dispatch_get_name_list(\n'
    '            objects_, data_sets_, directory_, policy_, request, response);\n')

builder = '''[[nodiscard]] std::vector<mms::MmsStaticDirectoryEntry> build_directory_index(
    const mms::MmsStaticObjectTable& objects) {
    std::vector<mms::MmsStaticDirectoryEntry> directory;
    std::size_t estimated{};
    for (const auto& object : objects.objects()) {
        estimated += 1U + static_cast<std::size_t>(
            std::count(object.item.begin(), object.item.end(), '$'));
    }
    directory.reserve(estimated);
    for (const auto& object : objects.objects()) {
        std::size_t prefix_end = object.item.find('$');
        while (true) {
            const auto prefix = object.item.substr(
                0U,
                prefix_end == std::string_view::npos ? object.item.size() : prefix_end);
            if (!prefix.empty()) directory.push_back({object.domain, prefix});
            if (prefix_end == std::string_view::npos || prefix_end + 1U >= object.item.size()) {
                break;
            }
            prefix_end = object.item.find('$', prefix_end + 1U);
        }
    }
    std::sort(
        directory.begin(), directory.end(),
        [](const mms::MmsStaticDirectoryEntry& left,
           const mms::MmsStaticDirectoryEntry& right) noexcept {
            if (left.domain != right.domain) return left.domain < right.domain;
            return left.item < right.item;
        });
    directory.erase(
        std::unique(
            directory.begin(), directory.end(),
            [](const mms::MmsStaticDirectoryEntry& left,
               const mms::MmsStaticDirectoryEntry& right) noexcept {
                return left.domain == right.domain && left.item == right.item;
            }),
        directory.end());
    return directory;
}

'''
replace('tools/static_ied_server.cpp', 'void serve_connection(\n', builder + 'void serve_connection(\n')
replace(
    'tools/static_ied_server.cpp',
    '    const mms::MmsStaticApplicationDispatcher dispatcher{\n'
    '        *dispatch_objects, data_sets, dispatch_policy};\n',
    '    std::vector<mms::MmsStaticDirectoryEntry> directory_index;\n'
    '    if (dispatch_policy.advertise_flattened_child_aliases) {\n'
    '        directory_index = build_directory_index(*dispatch_objects);\n'
    '    }\n'
    '    const mms::MmsStaticApplicationDispatcher dispatcher{\n'
    '        *dispatch_objects,\n'
    '        data_sets,\n'
    '        std::span<const mms::MmsStaticDirectoryEntry>{directory_index},\n'
    '        dispatch_policy};\n')

replace(
    'embedded/mms_dispatcher_hard_profile_smoke.cpp',
    '    const mms::MmsStaticApplicationDispatcher dual_directory_dispatcher{\n'
    '        hierarchy_table,\n'
    '        dual_directory_policy};\n',
    '    constexpr std::array<mms::MmsStaticDirectoryEntry, 11U> dual_directory_index{{\n'
    '        {"LDH", "GGIO1"},\n'
    '        {"LDH", "GGIO1$ST"},\n'
    '        {"LDH", "GGIO1$ST$Ind1"},\n'
    '        {"LDH", "GGIO1$ST$Ind1$stVal"},\n'
    '        {"LDH", "LLN0"},\n'
    '        {"LDH", "LLN0$ST"},\n'
    '        {"LDH", "LLN0$ST$Mod"},\n'
    '        {"LDH", "LLN0$ST$Mod$stVal"},\n'
    '        {"LDH", "Orphan"},\n'
    '        {"LDH", "Orphan$ST"},\n'
    '        {"LDH", "Orphan$ST$stVal"}}};\n'
    '    const mms::MmsStaticApplicationDispatcher dual_directory_dispatcher{\n'
    '        hierarchy_table,\n'
    '        std::span<const mms::MmsStaticDirectoryEntry>{dual_directory_index},\n'
    '        dual_directory_policy};\n')

print('Phase1B source patch applied')

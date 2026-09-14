#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"{path}: expected exactly one anchor, found {count}: {old[:100]!r}"
        )
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def insert_before(path: str, anchor: str, addition: str) -> None:
    replace_once(path, anchor, addition + anchor)


# 1) Preserve configured SCL instance values on structural leaves.
replace_once(
    "include/ariec61850/scl/model.hpp",
    "    bool is_quality{};\n    bool is_timestamp{};\n\n"
    "    friend bool operator==(const SclDataSetEntry&, const SclDataSetEntry&) = default;\n",
    "    bool is_quality{};\n    bool is_timestamp{};\n\n"
    "    // Instance-level DAI/Val value from SCL when one is explicitly configured.\n"
    "    // This is intentionally separate from runtime state: simulator adapters use it\n"
    "    // to seed configured semantics such as CF$...$ctlModel without inventing defaults.\n"
    "    std::string configured_value;\n\n"
    "    friend bool operator==(const SclDataSetEntry&, const SclDataSetEntry&) = default;\n",
)

replace_once(
    "src/scl/parser_part_03_01.inc",
    "        return {true, resolved_fc, cdc, basic_type, current_type_id, enum_type};\n"
    "    }\n\nprivate:\n",
    '''        return {true, resolved_fc, cdc, basic_type, current_type_id, enum_type};
    }

    // Return an explicitly configured instance value for a structural leaf.
    // The first control slice needs the common DOI/DAI/Val form used for
    // ctlModel. Structured BDA instance values remain runtime/profile work and
    // deliberately do not fall back to a guessed template value.
    [[nodiscard]] std::string configured_value(
        const std::string& ied_name,
        const std::string& ld_inst,
        const std::string& prefix,
        const std::string& ln_class,
        const std::string& ln_inst,
        const std::string& do_name,
        const std::string& da_name) const {
        const auto* logical_node = find_logical_node(
            ied_name, ld_inst, prefix, ln_class, ln_inst);
        if (logical_node == nullptr) return {};

        const auto do_segments = split_name(do_name);
        if (do_segments.empty()) return {};
        const XmlNode* instance = logical_node;
        for (std::size_t index = 0U; index < do_segments.size(); ++index) {
            const auto tag = index == 0U ? std::string_view{"DOI"} : std::string_view{"SDI"};
            const XmlNode* next{};
            for (const auto* candidate : direct_children(*instance, tag)) {
                if (same(attribute(candidate, "name"), do_segments[index])) {
                    next = candidate;
                    break;
                }
            }
            if (next == nullptr) return {};
            instance = next;
        }

        const auto da_segments = split_name(da_name);
        if (da_segments.size() != 1U) return {};
        for (const auto* dai : direct_children(*instance, "DAI")) {
            if (!same(attribute(dai, "name"), da_segments.front())) continue;
            const auto* value = first_child(*dai, "Val");
            return value == nullptr ? std::string{} : trim_copy(value->text);
        }
        return {};
    }

private:
''',
)

replace_once(
    "src/scl/parser_part_05_00.inc",
    "                        is_quality_attribute(leaf.da_name),\n"
    "                        is_timestamp_attribute(leaf.da_name),\n"
    "                    });\n",
    '''                        is_quality_attribute(leaf.da_name),
                        is_timestamp_attribute(leaf.da_name),
                        type_index.configured_value(
                            ied_name,
                            ld_inst,
                            prefix,
                            ln_class,
                            ln_inst,
                            leaf.do_name,
                            leaf.da_name),
                    });
''',
)

# 2) Extend the E2E fixture with one SCL-configured SPC control.
replace_once(
    "tests/fixtures/scl/minimal-station-brcb.scd",
    '          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />\n',
    '''          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />
          <LN lnClass="GGIO" inst="1" lnType="GGIOType">
            <DOI name="SPCSO1">
              <DAI name="ctlModel"><Val>direct-with-normal-security</Val></DAI>
            </DOI>
          </LN>
''',
)
replace_once(
    "tests/fixtures/scl/minimal-station-brcb.scd",
    '    <DOType id="PosType" cdc="DPC">\n'
    '      <DA name="stVal" bType="BOOLEAN" fc="ST" />\n'
    '      <DA name="q" bType="Quality" fc="ST" />\n'
    '      <DA name="t" bType="Timestamp" fc="ST" />\n'
    '    </DOType>\n',
    '''    <DOType id="PosType" cdc="DPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
    </DOType>
    <LNodeType id="GGIOType" lnClass="GGIO">
      <DO name="SPCSO1" type="SPCType" />
    </LNodeType>
    <DOType id="SPCType" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
      <DA name="ctlModel" bType="Enum" type="CtlModelKind" fc="CF" />
    </DOType>
    <EnumType id="CtlModelKind">
      <EnumVal ord="0">status-only</EnumVal>
      <EnumVal ord="1">direct-with-normal-security</EnumVal>
      <EnumVal ord="2">sbo-with-normal-security</EnumVal>
      <EnumVal ord="3">direct-with-enhanced-security</EnumVal>
      <EnumVal ord="4">sbo-with-enhanced-security</EnumVal>
    </EnumType>
''',
)

# 3) Parser regression: prove DAI/Val survives type projection.
insert_before(
    "tests/test_scl.cpp",
    "void parser_detects_duplicate_ieds_and_missing_dataset_references() {\n",
    '''void parser_preserves_configured_control_model_value() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("minimal-station-brcb.scd"));
    const auto configured = std::find_if(
        document.model_entries.begin(),
        document.model_entries.end(),
        [](const SclDataSetEntry& entry) {
            return entry.ln_class == "GGIO" && entry.ln_inst == "1" &&
                entry.do_name == "SPCSO1" && entry.da_name == "ctlModel";
        });
    CHECK(configured != document.model_entries.end());
    CHECK(configured->functional_constraint == "CF");
    CHECK(configured->cdc == "SPC");
    CHECK(configured->basic_type == "Enum");
    CHECK(configured->configured_value == "direct-with-normal-security");
}

''',
)
replace_once(
    "tests/test_scl.cpp",
    '        {"SCL dataset references", dataset_reference_resolver_accepts_canonical_and_local_forms},\n',
    '        {"SCL dataset references", dataset_reference_resolver_accepts_canonical_and_local_forms},\n'
    '        {"SCL configured control model", parser_preserves_configured_control_model_value},\n',
)

# 4) Qt bridge: seed ctlModel from SCL and emit explicit CTL metadata.
replace_once(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    '''QString initialValue(const QString& type, const QString& dataAttribute) {
    if (type == QStringLiteral("Boolean")) return QStringLiteral("false");
    if (type == QStringLiteral("Enumeration")) return QStringLiteral("0");
    if (type == QStringLiteral("Quality")) return QStringLiteral("good");
    if (type == QStringLiteral("Timestamp")) {
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    }
    if (type == QStringLiteral("Number")) return QStringLiteral("0");
    if (dataAttribute.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("on");
    }
    return QStringLiteral("—");
}
''',
    '''std::optional<int> controlModelCode(QString value) {
    value = value.trimmed();
    bool numericOk{};
    const auto numeric = value.toInt(&numericOk);
    if (numericOk && numeric >= 0 && numeric <= 4) return numeric;

    QString token;
    token.reserve(value.size());
    for (const auto character : value.toLower()) {
        if (character.isLetterOrNumber()) token.append(character);
    }
    if (token == QStringLiteral("statusonly")) return 0;
    if (token == QStringLiteral("directwithnormalsecurity") ||
        token == QStringLiteral("directnormal")) return 1;
    if (token == QStringLiteral("sbowithnormalsecurity") ||
        token == QStringLiteral("selectbeforeoperatewithnormalsecurity") ||
        token == QStringLiteral("sbonormal")) return 2;
    if (token == QStringLiteral("directwithenhancedsecurity") ||
        token == QStringLiteral("directenhanced")) return 3;
    if (token == QStringLiteral("sbowithenhancedsecurity") ||
        token == QStringLiteral("selectbeforeoperatewithenhancedsecurity") ||
        token == QStringLiteral("sboenhanced")) return 4;
    return std::nullopt;
}

QString initialValue(
    const ar::iec61850::scl::SclDataSetEntry& entry,
    const QString& type) {
    const auto configured = qstring(entry.configured_value).trimmed();
    const auto dataAttribute = qstring(entry.da_name);
    if (!configured.isEmpty()) {
        if (dataAttribute.compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) == 0) {
            if (const auto model = controlModelCode(configured); model.has_value()) {
                return QString::number(*model);
            }
        }
        return configured;
    }
    if (type == QStringLiteral("Boolean")) return QStringLiteral("false");
    if (type == QStringLiteral("Enumeration")) return QStringLiteral("0");
    if (type == QStringLiteral("Quality")) return QStringLiteral("good");
    if (type == QStringLiteral("Timestamp")) {
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    }
    if (type == QStringLiteral("Number")) return QStringLiteral("0");
    if (dataAttribute.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("on");
    }
    return QStringLiteral("—");
}
''',
)
replace_once(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    '    item.insert(QStringLiteral("value"), initialValue(type, qstring(entry.da_name)));\n',
    '    item.insert(QStringLiteral("value"), initialValue(entry, type));\n',
)
insert_before(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    "    QSet<QString> emittedDataSetMembers;\n",
    '''    // Control service objects are virtual MMS objects. Preserve the configured
    // SCL ctlModel explicitly instead of pretending CO$Oper is a structural DA.
    QSet<QString> emittedControls;
    for (const auto& entry : loaded.document.model_entries) {
        if (qstring(entry.ied_name) != activeIedName ||
            qstring(entry.functional_constraint).compare(QStringLiteral("CF"), Qt::CaseInsensitive) != 0 ||
            qstring(entry.da_name).compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto model = controlModelCode(qstring(entry.configured_value));
        if (!model.has_value()) continue;
        const auto domain = mmsDomainFor(entry);
        const auto logicalNode = qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
        auto dataObject = qstring(entry.do_name);
        dataObject.replace(QLatin1Char('.'), QLatin1Char('$'));
        const auto cdc = qstring(entry.cdc).trimmed().toUpper();
        if (domain.isEmpty() || logicalNode.isEmpty() || dataObject.isEmpty() || cdc.isEmpty()) continue;
        const auto key = domain + QLatin1Char('\n') + logicalNode + QLatin1Char('\n') + dataObject;
        if (emittedControls.contains(key)) continue;
        emittedControls.insert(key);
        manifest += "CTL\t" + manifestField(domain) + "\t" + manifestField(logicalNode) +
            "\t" + manifestField(dataObject) + "\t" + manifestField(cdc) + "\t" +
            QByteArray::number(*model) + "\n";
    }

''',
)

# 5) Server build graph: reuse native Direct-Normal primitive.
replace_once(
    "CMakeLists.txt",
    "    src/mms/static_data_set_table.cpp\n    src/mms/static_dispatcher.cpp\n",
    "    src/mms/static_data_set_table.cpp\n    src/mms/static_direct_control.cpp\n"
    "    src/mms/static_dispatcher.cpp\n",
)
insert_before(
    "CMakeLists.txt",
    "    add_executable(ariec61850_rcb_contention_probe tools/rcb_contention_probe.cpp)\n",
    '''    add_executable(ariec61850_control_interop_probe tools/control_interop_probe.cpp)
    target_link_libraries(ariec61850_control_interop_probe PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_control_interop_probe PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_control_interop_probe)
    ariec61850_apply_sanitizers(ariec61850_control_interop_probe)

''',
)
replace_once(
    "tools/control_interop_probe/CMakeLists.txt",
    '''add_executable(ariec61850_control_interop_probe
    ${ARIEC61850_ROOT}/tools/control_interop_probe.cpp)
target_link_libraries(ariec61850_control_interop_probe PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_control_interop_probe PRIVATE cxx_std_20)
''',
    '''if(NOT TARGET ariec61850_control_interop_probe)
    add_executable(ariec61850_control_interop_probe
        ${ARIEC61850_ROOT}/tools/control_interop_probe.cpp)
    target_link_libraries(ariec61850_control_interop_probe PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_control_interop_probe PRIVATE cxx_std_20)
endif()
''',
)
replace_once(
    "apps/ied_simulator/CMakeLists.txt",
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_data_set_table.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n",
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_data_set_table.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_direct_control.cpp\n"
    "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n",
)
replace_once(
    "apps/ied_simulator/CMakeLists.txt",
    "    ariec61850_mms_urcb_gi_probe\n    ariec61850_mms_brcb_event_probe)\n",
    "    ariec61850_mms_urcb_gi_probe\n    ariec61850_mms_brcb_event_probe\n"
    "    ariec61850_control_interop_probe)\n",
)

# 6) Integrate configured SPC Direct-Normal into the simulator endpoint.
replace_once(
    "tools/static_ied_server.cpp",
    '#include "ariec61850/mms/simulator_manifest_codec.hpp"\n',
    '#include "ariec61850/mms/simulator_manifest_codec.hpp"\n'
    '#include "ariec61850/mms/static_direct_control.hpp"\n',
)
insert_before(
    "tools/static_ied_server.cpp",
    "[[nodiscard]] std::vector<std::uint8_t> encode_ber_length(\n",
    '''[[nodiscard]] wire::EncodeResult read_atomic_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = static_cast<const std::atomic<std::uint8_t>*>(context)->load(
        std::memory_order_relaxed) != 0U;
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool apply_atomic_boolean(void* context, const bool value) noexcept {
    if (context == nullptr) return false;
    static_cast<std::atomic<std::uint8_t>*>(context)->store(
        value ? 1U : 0U, std::memory_order_relaxed);
    return true;
}

''',
)
insert_before(
    "tools/static_ied_server.cpp",
    "struct ConnectionBuffers final {\n",
    '''[[nodiscard]] mms::MmsTypeSpecification control_scalar(
    const mms::MmsTypeKind kind,
    std::string name,
    const std::optional<std::uint32_t> size = std::nullopt) {
    mms::MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    result.size = size;
    return result;
}

[[nodiscard]] mms::MmsTypeSpecification control_structure(
    std::string name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification result;
    result.kind = mms::MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> direct_boolean_oper_type_specification() {
    return mms::MmsServiceCodec::encode_type_specification(control_structure("Oper", {
        control_scalar(mms::MmsTypeKind::boolean, "ctlVal"),
        control_structure("origin", {
            control_scalar(mms::MmsTypeKind::unsigned_integer, "orCat"),
            control_scalar(mms::MmsTypeKind::octet_string, "orIdent", 64U),
        }),
        control_scalar(mms::MmsTypeKind::unsigned_integer, "ctlNum"),
        control_scalar(mms::MmsTypeKind::utc_time, "T"),
        control_scalar(mms::MmsTypeKind::boolean, "Test"),
        control_scalar(mms::MmsTypeKind::bit_string, "Check", 2U),
    }));
}

''',
)
insert_before(
    "tools/static_ied_server.cpp",
    "constexpr std::size_t kMaximumSimulatorBrcbs = 16U;\n",
    '''struct ManifestDirectControlStorage final {
    std::string domain;
    std::string logical_node;
    std::string data_object;
    std::string cdc;
    std::uint8_t control_model{};
    std::string status_item;
    std::string ctl_model_item;
    std::string oper_item;
    std::vector<std::uint8_t> oper_type_specification;
    std::shared_ptr<std::atomic<std::uint8_t>> process_value;
};

constexpr std::size_t kMaximumSimulatorDirectControls = 64U;
''',
)
replace_once(
    "tools/static_ied_server.cpp",
    "    std::vector<ManifestDataSetStorage> data_set_storage;\n",
    "    std::vector<ManifestDirectControlStorage> direct_control_storage;\n"
    "    std::size_t omitted_direct_controls{};\n"
    "    std::vector<ManifestDataSetStorage> data_set_storage;\n",
)
replace_once(
    "tools/static_ied_server.cpp",
    '''    struct ParsedDataSetMember final {
        std::string domain;
        std::string item;
        std::string member_domain;
        std::string member_item;
    };
''',
    '''    struct ParsedControl final {
        std::string domain;
        std::string logical_node;
        std::string data_object;
        std::string cdc;
        std::uint8_t control_model{};
    };
    struct ParsedDataSetMember final {
        std::string domain;
        std::string item;
        std::string member_domain;
        std::string member_item;
    };
''',
)
replace_once(
    "tools/static_ied_server.cpp",
    "    std::vector<std::pair<std::string, std::string>> roots;\n"
    "    std::vector<ParsedObject> parsed_objects;\n",
    "    std::vector<std::pair<std::string, std::string>> roots;\n"
    "    std::vector<ParsedObject> parsed_objects;\n"
    "    std::vector<ParsedControl> parsed_controls;\n",
)
replace_once(
    "tools/static_ied_server.cpp",
    '''        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
''',
    '''        } else if (fields.size() >= 6U && fields[0] == "CTL") {
            ++model.declared_entries;
            if (fields[1].empty() || fields[2].empty() || fields[3].empty() || fields[4].empty()) {
                throw std::runtime_error("Model manifest contains a malformed CTL entry.");
            }
            parsed_controls.push_back({
                fields[1],
                fields[2],
                fields[3],
                fields[4],
                static_cast<std::uint8_t>(parse_u32("CTL ctlModel", fields[5], 4U))});
        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
''',
)
insert_before(
    "tools/static_ied_server.cpp",
    "    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>\n"
    "        grouped_members;\n",
    '''    // Compile virtual service objects from SCL configured ctlModel metadata.
    // Phase one intentionally exposes only SPC Direct-with-normal-security;
    // other configured models remain visible as structural CF data but are not
    // falsely advertised as executable server controls.
    const auto oper_type = direct_boolean_oper_type_specification();
    std::set<std::pair<std::string, std::string>> unique_direct_controls;
    for (const auto& parsed : parsed_controls) {
        if (parsed.control_model != 1U || parsed.cdc != "SPC") {
            ++model.omitted_direct_controls;
            continue;
        }
        if (model.direct_control_storage.size() >= kMaximumSimulatorDirectControls) {
            ++model.omitted_direct_controls;
            continue;
        }
        const auto status_item = parsed.logical_node + "$ST$" + parsed.data_object + "$stVal";
        const auto ctl_model_item = parsed.logical_node + "$CF$" + parsed.data_object + "$ctlModel";
        const auto oper_item = parsed.logical_node + "$CO$" + parsed.data_object + "$Oper";
        if (!unique_direct_controls.emplace(parsed.domain, oper_item).second) continue;
        const auto status = model.value_indices.find(object_key(parsed.domain, status_item));
        const auto ctl_model = model.value_indices.find(object_key(parsed.domain, ctl_model_item));
        if (status == model.value_indices.end() || ctl_model == model.value_indices.end() ||
            model.values[status->second].type.kind != mms::MmsTypeKind::boolean) {
            ++model.omitted_direct_controls;
            continue;
        }
        const auto& encoded_status = model.values[status->second].encoded;
        const auto initial = encoded_status.size() >= 3U && encoded_status[0] == 0x83U &&
            encoded_status.back() != 0U;
        ManifestDirectControlStorage control;
        control.domain = parsed.domain;
        control.logical_node = parsed.logical_node;
        control.data_object = parsed.data_object;
        control.cdc = parsed.cdc;
        control.control_model = parsed.control_model;
        control.status_item = status_item;
        control.ctl_model_item = ctl_model_item;
        control.oper_item = oper_item;
        control.oper_type_specification = oper_type;
        control.process_value = std::make_shared<std::atomic<std::uint8_t>>(initial ? 1U : 0U);
        model.direct_control_storage.push_back(std::move(control));
    }

''',
)
replace_once(
    "tools/static_ied_server.cpp",
    "    auto remaining_object_slots =\n"
    "        mms::MmsStaticObjectTable::maximum_objects - model.objects.size();\n",
    '''    if (model.objects.size() + model.direct_control_storage.size() >
        mms::MmsStaticObjectTable::maximum_objects) {
        throw std::runtime_error("Configured Direct-Normal controls exceed MMS object capacity.");
    }
    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -
        model.objects.size() - model.direct_control_storage.size();
''',
)
replace_once(
    "tools/static_ied_server.cpp",
    '''    std::vector<std::unique_ptr<BrcbAssociationRuntime>> brcb_runtimes;

    mms::MmsStaticDispatchPolicy dispatch_policy;
    dispatch_policy.maximum_write_variables = 1U;
    const mms::MmsStaticObjectTable* dispatch_objects = &object_table;
''',
    '''    std::vector<std::unique_ptr<BrcbAssociationRuntime>> brcb_runtimes;
    std::vector<mms::MmsStaticDirectBooleanControlState> direct_control_states;
    std::vector<mms::MmsStaticDirectBooleanControlBinding> direct_control_bindings;
    std::vector<mms::MmsStaticObjectEntry> direct_control_objects;
    std::unique_ptr<mms::MmsStaticObjectTable> direct_control_table;

    mms::MmsStaticDispatchPolicy dispatch_policy;
    dispatch_policy.maximum_write_variables = 1U;
    const mms::MmsStaticObjectTable* dispatch_objects = &object_table;
    if (manifest_model != nullptr && !manifest_model->direct_control_storage.empty()) {
        direct_control_states.resize(manifest_model->direct_control_storage.size());
        direct_control_bindings.resize(manifest_model->direct_control_storage.size());
        direct_control_objects.assign(object_table.objects().begin(), object_table.objects().end());
        constexpr std::array<std::uint8_t, 2U> unsigned_type{0x86U, 0x00U};

        for (std::size_t index = 0U; index < manifest_model->direct_control_storage.size(); ++index) {
            auto& control = manifest_model->direct_control_storage[index];
            auto& state = direct_control_states[index];
            auto& binding = direct_control_bindings[index];
            state.value = control.process_value->load(std::memory_order_relaxed);
            binding.state = &state;
            binding.apply = apply_atomic_boolean;
            binding.apply_context = control.process_value.get();

            bool status_found{};
            bool ctl_model_found{};
            for (auto& object : direct_control_objects) {
                if (object.domain != control.domain) continue;
                if (object.item == control.status_item) {
                    object.read = read_atomic_boolean;
                    object.context = control.process_value.get();
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    status_found = true;
                } else if (object.item == control.ctl_model_item) {
                    object.type_specification = std::span<const std::uint8_t>{unsigned_type};
                    object.read = mms::mms_static_direct_normal_read_ctl_model;
                    object.context = nullptr;
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    ctl_model_found = true;
                }
            }
            if (!status_found || !ctl_model_found) {
                throw std::runtime_error("Configured Direct-Normal control is missing ST/CF backing objects.");
            }
            direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                control.domain,
                control.oper_item,
                control.oper_type_specification,
                mms::mms_static_control_read_unavailable,
                nullptr,
                false,
                mms::mms_static_direct_boolean_write_oper,
                &binding,
                nullptr});
        }
        direct_control_table = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{direct_control_objects});
        if (!direct_control_table->valid()) {
            throw std::runtime_error("Configured Direct-Normal MMS object table is invalid.");
        }
        dispatch_objects = direct_control_table.get();
        dispatch_policy.advertise_flattened_child_aliases = true;
    }
    const auto* process_objects = dispatch_objects;
''',
)

p = Path("tools/static_ied_server.cpp")
server = p.read_text(encoding="utf-8")
needle = "            object_table,\n            data_sets);"
if server.count(needle) != 2:
    raise SystemExit(
        "static_ied_server.cpp: expected two report runtime object_table anchors, "
        f"found {server.count(needle)}"
    )
server = server.replace(needle, "            *process_objects,\n            data_sets);", 2)
needle = (
    "            object_table.objects(),\n"
    "            std::span<mms::MmsStaticObjectEntry>{urcb_object_storage},"
)
if server.count(needle) != 1:
    raise SystemExit("static_ied_server.cpp: URCB object-bank base anchor mismatch")
server = server.replace(
    needle,
    "            process_objects->objects(),\n"
    "            std::span<mms::MmsStaticObjectEntry>{urcb_object_storage},",
    1,
)
p.write_text(server, encoding="utf-8")

# 7) External wire regression using the existing guarded client.
insert_before(
    "apps/ied_simulator/test_gui_live_value.py",
    "\ndef prove_concurrent_associations(read_probe: str, port: int, item: str) -> float:\n",
    '''
def resolve_control_probe(argument: str) -> str:
    return resolve_named_probe(argument, "ariec61850_control_interop_probe", "control interoperability probe")


def run_direct_normal_control_regression(
    control_probe: str,
    read_probe: str,
    port: int,
) -> str:
    common = [
        control_probe,
        "127.0.0.1",
        str(port),
        "--object",
        "MU01LD0/GGIO1.SPCSO1",
        "--timeout-ms",
        "5000",
    ]
    discovery = subprocess.run(
        common,
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        discovery.returncode != 0
        or "ctlModel=direct-normal" not in discovery.stdout
        or "cdc=SPC" not in discovery.stdout
        or "STATUS_BEFORE false" not in discovery.stdout
        or "status=DISCOVERY_PASS" not in discovery.stdout
    ):
        raise RuntimeError(
            "configured Direct-Normal discovery failed: "
            f"exit={discovery.returncode} stdout={discovery.stdout!r} stderr={discovery.stderr!r}"
        )

    # Prove fail-closed Check handling before the accepted command.
    rejected = subprocess.run(
        common
        + [
            "--action",
            "operate",
            "--value",
            "on",
            "--value-kind",
            "bool",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        rejected.returncode != 4
        or "accepted=false" not in rejected.stdout
        or "mmsFailure=11:object-value-invalid" not in rejected.stdout
        or "STATUS_AFTER false" not in rejected.stdout
    ):
        raise RuntimeError(
            "Direct-Normal fail-closed check-bit regression failed: "
            f"exit={rejected.returncode} stdout={rejected.stdout!r} stderr={rejected.stderr!r}"
        )

    accepted = subprocess.run(
        common
        + [
            "--action",
            "operate",
            "--value",
            "on",
            "--value-kind",
            "bool",
            "--interlock-check",
            "off",
            "--synchro-check",
            "off",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        accepted.returncode != 0
        or "completion=accepted" not in accepted.stdout
        or "accepted=true" not in accepted.stdout
        or "STATUS_AFTER true" not in accepted.stdout
        or "NO_RETRY_EVIDENCE controlWrites=1" not in accepted.stdout
    ):
        raise RuntimeError(
            "Direct-Normal Oper wire regression failed: "
            f"exit={accepted.returncode} stdout={accepted.stdout!r} stderr={accepted.stderr!r}"
        )

    status = run_probe(read_probe, port, "GGIO1$ST$SPCSO1$stVal")
    if status.returncode != 0 or "value=true" not in status.stdout:
        raise RuntimeError(
            "Direct-Normal process status did not persist for a second external association: "
            f"exit={status.returncode} stdout={status.stdout!r} stderr={status.stderr!r}"
        )
    return discovery.stdout.strip() + "\n" + rejected.stdout.strip() + "\n" + accepted.stdout.strip()

''',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    "    brcb_probe = resolve_brcb_probe(args.read_probe)\n",
    "    brcb_probe = resolve_brcb_probe(args.read_probe)\n"
    "    control_probe = resolve_control_probe(args.read_probe)\n",
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '''                brcb_manifest = (
                    "RCB\\tMU01LD0\\tLLN0$BR$BRCB01\\t1\\t"
                    "MU01LD0/LLN0$BR$BRCB01\\tMU01LD0\\tLLN0$dsGO\\t2\\t75\\t1000\\t108\\t121\\t128"
                )
''',
    '''                brcb_manifest = (
                    "RCB\\tMU01LD0\\tLLN0$BR$BRCB01\\t1\\t"
                    "MU01LD0/LLN0$BR$BRCB01\\tMU01LD0\\tLLN0$dsGO\\t2\\t75\\t1000\\t108\\t121\\t128"
                )
                direct_control_manifest = "CTL\\tMU01LD0\\tGGIO1\\tSPCSO1\\tSPC\\t1"
''',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    "                    and brcb_manifest in manifest_text\n",
    "                    and brcb_manifest in manifest_text\n"
    "                    and direct_control_manifest in manifest_text\n"
    '                    and "GGIO1$CF$SPCSO1$ctlModel\\tEnum\\tEnumeration\\t1" in manifest_text\n',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '                    "GUI did not publish revision 2 with edited/full model leaves and URCB/BRCB metadata"\n',
    '                    "GUI did not publish revision 2 with edited/full model leaves, reporting metadata, and configured control metadata"\n',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    "            concurrent_seconds = prove_concurrent_associations(\n",
    "            control_output = run_direct_normal_control_regression(\n"
    "                control_probe,\n                read_probe,\n                port,\n            )\n"
    "            concurrent_seconds = prove_concurrent_associations(\n",
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '                "urcb_gi=pass "\n                "brcb_event=pass "\n',
    '                "control_direct_normal=pass "\n'
    '                "urcb_gi=pass "\n                "brcb_event=pass "\n',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    "            print(urcb_output)\n",
    "            print(control_output)\n            print(urcb_output)\n",
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    '                "--exit-after-ms",\n                "30000",\n',
    '                "--exit-after-ms",\n                "45000",\n',
)
replace_once(
    "apps/ied_simulator/test_gui_live_value.py",
    "            app.wait(timeout=32)\n",
    "            app.wait(timeout=47)\n",
)

# 8) Keep Qt CI path coverage explicit for the external control client.
p = Path(".github/workflows/ied-simulator-qt.yml")
wf = p.read_text(encoding="utf-8")
anchor = "      - 'tools/mms_brcb_event_probe.cpp'\n"
if wf.count(anchor) != 2:
    raise SystemExit(f"Qt workflow expected two BRCB path anchors, found {wf.count(anchor)}")
wf = wf.replace(anchor, anchor + "      - 'tools/control_interop_probe.cpp'\n")
p.write_text(wf, encoding="utf-8")

print("Configured Direct-Normal slice patch applied.")

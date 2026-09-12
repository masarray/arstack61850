#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def insert_before(path: str, anchor: str, addition: str) -> None:
    replace_once(path, anchor, addition + anchor)


# ---------------------------------------------------------------------------
# Static IED server: compile every configured SPC control model into the same
# production static control state machine and emit positive CommandTermination
# for enhanced-security commands.
# ---------------------------------------------------------------------------
server_path = "tools/static_ied_server.cpp"
replace_once(
    server_path,
    '#include "ariec61850/mms/services.hpp"\n#include "ariec61850/mms/simulator_manifest_codec.hpp"\n',
    '#include "ariec61850/mms/services.hpp"\n#include "ariec61850/mms/reporting.hpp"\n#include "ariec61850/mms/simulator_manifest_codec.hpp"\n',
)
replace_once(
    server_path,
    '#include "ariec61850/mms/static_urcb_runtime.hpp"\n\n#include <algorithm>\n',
    '#include "ariec61850/mms/static_urcb_runtime.hpp"\n#include "ariec61850/osi/cotp.hpp"\n#include "ariec61850/osi/tpkt.hpp"\n\n#include <algorithm>\n',
)

replace_once(
    server_path,
    '''[[nodiscard]] std::vector<std::uint8_t> direct_boolean_oper_type_specification() {
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
    '''[[nodiscard]] std::vector<std::uint8_t> boolean_control_type_specification(
    std::string name,
    const bool include_check) {
    std::vector<mms::MmsTypeSpecification> fields;
    fields.reserve(include_check ? 6U : 5U);
    fields.push_back(control_scalar(mms::MmsTypeKind::boolean, "ctlVal"));
    fields.push_back(control_structure("origin", {
        control_scalar(mms::MmsTypeKind::unsigned_integer, "orCat"),
        control_scalar(mms::MmsTypeKind::octet_string, "orIdent", 64U),
    }));
    fields.push_back(control_scalar(mms::MmsTypeKind::unsigned_integer, "ctlNum"));
    fields.push_back(control_scalar(mms::MmsTypeKind::utc_time, "T"));
    fields.push_back(control_scalar(mms::MmsTypeKind::boolean, "Test"));
    if (include_check) {
        fields.push_back(control_scalar(mms::MmsTypeKind::bit_string, "Check", 2U));
    }
    return mms::MmsServiceCodec::encode_type_specification(
        control_structure(std::move(name), std::move(fields)));
}

[[nodiscard]] std::vector<std::uint8_t> direct_boolean_oper_type_specification() {
    return boolean_control_type_specification("Oper", true);
}

[[nodiscard]] std::vector<std::uint8_t> direct_boolean_sbow_type_specification() {
    return boolean_control_type_specification("SBOw", true);
}

[[nodiscard]] std::vector<std::uint8_t> direct_boolean_cancel_type_specification() {
    return boolean_control_type_specification("Cancel", false);
}

[[nodiscard]] std::vector<std::uint8_t> sbo_reference_type_specification() {
    return mms::MmsServiceCodec::encode_type_specification(
        control_scalar(mms::MmsTypeKind::visible_string, "SBO", 129U));
}
''',
)

replace_once(
    server_path,
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
''',
    '''struct ManifestDirectControlStorage final {
    std::string domain;
    std::string logical_node;
    std::string data_object;
    std::string cdc;
    std::uint8_t control_model{};
    std::string status_item;
    std::string ctl_model_item;
    std::string sbo_item;
    std::string sbow_item;
    std::string oper_item;
    std::string cancel_item;
    std::string selection_reference;
    std::vector<std::uint8_t> sbo_type_specification;
    std::vector<std::uint8_t> sbow_type_specification;
    std::vector<std::uint8_t> oper_type_specification;
    std::vector<std::uint8_t> cancel_type_specification;
    std::shared_ptr<mms::MmsStaticDirectBooleanSharedState> shared_state;
    std::size_t service_object_count{};
};
''',
)

old_compile = '''    // Compile virtual service objects from SCL configured ctlModel metadata.
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
'''
new_compile = '''    // Compile configured SPC command Data Objects into virtual IEC 61850
    // control-service objects. Structural ST/CF leaves remain sourced from SCL;
    // CO$SBO/SBOw/Oper/Cancel are service objects owned by the server runtime.
    const auto oper_type = direct_boolean_oper_type_specification();
    const auto sbow_type = direct_boolean_sbow_type_specification();
    const auto cancel_type = direct_boolean_cancel_type_specification();
    const auto sbo_type = sbo_reference_type_specification();
    std::set<std::pair<std::string, std::string>> unique_direct_controls;
    for (const auto& parsed : parsed_controls) {
        if (parsed.control_model == 0U || parsed.control_model > 4U || parsed.cdc != "SPC") {
            ++model.omitted_direct_controls;
            continue;
        }
        if (model.direct_control_storage.size() >= kMaximumSimulatorDirectControls) {
            ++model.omitted_direct_controls;
            continue;
        }
        const auto status_item = parsed.logical_node + "$ST$" + parsed.data_object + "$stVal";
        const auto ctl_model_item = parsed.logical_node + "$CF$" + parsed.data_object + "$ctlModel";
        const auto sbo_item = parsed.logical_node + "$CO$" + parsed.data_object + "$SBO";
        const auto sbow_item = parsed.logical_node + "$CO$" + parsed.data_object + "$SBOw";
        const auto oper_item = parsed.logical_node + "$CO$" + parsed.data_object + "$Oper";
        const auto cancel_item = parsed.logical_node + "$CO$" + parsed.data_object + "$Cancel";
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
        control.sbo_item = sbo_item;
        control.sbow_item = sbow_item;
        control.oper_item = oper_item;
        control.cancel_item = cancel_item;
        control.selection_reference = parsed.domain + "/" + parsed.logical_node + "." + parsed.data_object;
        std::replace(
            control.selection_reference.begin(),
            control.selection_reference.end(),
            '$',
            '.');
        control.sbo_type_specification = sbo_type;
        control.sbow_type_specification = sbow_type;
        control.oper_type_specification = oper_type;
        control.cancel_type_specification = cancel_type;
        control.shared_state = std::make_shared<mms::MmsStaticDirectBooleanSharedState>();
        control.shared_state->value.store(initial ? 1U : 0U, std::memory_order_relaxed);
        control.service_object_count =
            (parsed.control_model == 2U || parsed.control_model == 4U) ? 3U : 1U;
        model.direct_control_storage.push_back(std::move(control));
    }
'''
replace_once(server_path, old_compile, new_compile)

replace_once(
    server_path,
    '''    if (model.objects.size() + model.direct_control_storage.size() >
        mms::MmsStaticObjectTable::maximum_objects) {
        throw std::runtime_error("Configured Direct-Normal controls exceed MMS object capacity.");
    }
    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -
        model.objects.size() - model.direct_control_storage.size();
''',
    '''    std::size_t control_service_objects{};
    for (const auto& control : model.direct_control_storage) {
        if (control.service_object_count >
            mms::MmsStaticObjectTable::maximum_objects - control_service_objects) {
            throw std::runtime_error("Configured controls exceed MMS object capacity.");
        }
        control_service_objects += control.service_object_count;
    }
    if (model.objects.size() > mms::MmsStaticObjectTable::maximum_objects - control_service_objects) {
        throw std::runtime_error("Configured controls exceed MMS object capacity.");
    }
    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -
        model.objects.size() - control_service_objects;
''',
)

old_binding = '''    std::vector<mms::MmsStaticDirectBooleanControlState> direct_control_states;
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
'''
new_binding = '''    std::vector<mms::MmsStaticDirectBooleanControlState> direct_control_states;
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
            state.value = control.shared_state->value.load(std::memory_order_relaxed);
            binding.state = &state;
            binding.shared_state = control.shared_state.get();
            binding.model = static_cast<mms::MmsStaticControlModel>(control.control_model);
            binding.association_id = association_id;
            binding.selection_reference = control.selection_reference;
            binding.sbo_timeout_ms = 10'000U;
            binding.now_ms = report_now_ms;
            binding.now_context = nullptr;

            bool status_found{};
            bool ctl_model_found{};
            for (auto& object : direct_control_objects) {
                if (object.domain != control.domain) continue;
                if (object.item == control.status_item) {
                    object.read = read_atomic_boolean;
                    object.context = &control.shared_state->value;
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    status_found = true;
                } else if (object.item == control.ctl_model_item) {
                    object.type_specification = std::span<const std::uint8_t>{unsigned_type};
                    object.read = mms::mms_static_control_read_ctl_model;
                    object.context = &binding;
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    ctl_model_found = true;
                }
            }
            if (!status_found || !ctl_model_found) {
                throw std::runtime_error("Configured command control is missing ST/CF backing objects.");
            }

            if (control.control_model == 2U) {
                direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                    control.domain,
                    control.sbo_item,
                    control.sbo_type_specification,
                    mms::mms_static_sbo_normal_read,
                    &binding});
            } else if (control.control_model == 4U) {
                direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                    control.domain,
                    control.sbow_item,
                    control.sbow_type_specification,
                    mms::mms_static_control_read_unavailable,
                    nullptr,
                    false,
                    nullptr,
                    &binding,
                    mms::mms_static_boolean_write_sbow_contextual});
            }

            direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                control.domain,
                control.oper_item,
                control.oper_type_specification,
                mms::mms_static_control_read_unavailable,
                nullptr,
                false,
                nullptr,
                &binding,
                mms::mms_static_boolean_write_oper_contextual});

            if (control.control_model == 2U || control.control_model == 4U) {
                direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                    control.domain,
                    control.cancel_item,
                    control.cancel_type_specification,
                    mms::mms_static_control_read_unavailable,
                    nullptr,
                    false,
                    nullptr,
                    &binding,
                    mms::mms_static_boolean_write_cancel_contextual});
            }
        }
        direct_control_table = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{direct_control_objects});
        if (!direct_control_table->valid()) {
            throw std::runtime_error("Configured command-control MMS object table is invalid.");
        }
        dispatch_objects = direct_control_table.get();
        dispatch_policy.advertise_flattened_child_aliases = true;
    }
'''
replace_once(server_path, old_binding, new_binding)

replace_once(
    server_path,
    '''    const auto close_brcbs = [&] {
        const auto now_ms = monotonic_ms();
        for (auto& brcb : brcb_runtimes) {
            if (brcb != nullptr && brcb->control != nullptr) {
                brcb->control->on_association_closed(association_id, now_ms);
            }
        }
    };
''',
    '''    const auto close_brcbs = [&] {
        const auto now_ms = monotonic_ms();
        for (auto& brcb : brcb_runtimes) {
            if (brcb != nullptr && brcb->control != nullptr) {
                brcb->control->on_association_closed(association_id, now_ms);
            }
        }
        for (auto& binding : direct_control_bindings) {
            mms::mms_static_control_on_association_closed(binding);
        }
    };
''',
)

termination_anchor = '''        const auto now_ms = monotonic_ms();
        if (!brcb_runtimes.empty()) {
'''
termination_code = '''        const auto now_ms = monotonic_ms();
        if (manifest_model != nullptr && session.pending_output_bytes() == 0U) {
            for (std::size_t index = 0U; index < direct_control_bindings.size(); ++index) {
                auto& binding = direct_control_bindings[index];
                if (binding.state == nullptr || !binding.state->pending_termination) continue;
                if (index >= manifest_model->direct_control_storage.size()) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=command_termination_error association="
                        << association_id << " message=control-index-mismatch\\n";
                    close_brcbs();
                    return;
                }
                const auto& control = manifest_model->direct_control_storage[index];
                const auto command = binding.state->termination_command;
                try {
                    const auto origin = std::span<const std::uint8_t>{
                        command.origin_identifier.data(), command.origin_identifier_size};
                    auto last_appl_error = mms::MmsDataValue::structure({
                        mms::MmsDataValue::visible_string(control.selection_reference),
                        mms::MmsDataValue::integer(0),
                        mms::MmsDataValue::structure({
                            mms::MmsDataValue::unsigned_integer(command.origin_category),
                            mms::MmsDataValue::octet_string(origin),
                        }),
                        mms::MmsDataValue::unsigned_integer(command.control_number),
                        mms::MmsDataValue::integer(25),
                    });
                    mms::MmsInformationReport report;
                    report.variable_references.push_back(
                        mms::MmsObjectName::domain_specific(control.domain, control.oper_item));
                    report.items.push_back({0U, std::move(last_appl_error), std::nullopt});
                    const auto p_data = mms::MmsInformationReportCodec::encode_p_data(
                        report, runtime.mms_presentation_context_id());
                    const auto cotp = ar::iec61850::osi::CotpFrameCodec::encode_data(p_data);
                    const auto frame = ar::iec61850::osi::TpktFrameCodec::encode(cotp);
                    if (!send_all(socket, frame)) {
                        std::osyncstream{std::cerr}
                            << "IEDSIM_EVENT kind=command_termination_send_error association="
                            << association_id << " object=" << control.selection_reference << '\\n';
                        close_brcbs();
                        return;
                    }
                    total_sent += frame.size();
                    binding.state->pending_termination = false;
                    binding.state->termination_command = {};
                    std::osyncstream{std::cout}
                        << "IEDSIM_EVENT kind=command_termination association="
                        << association_id << " object=" << control.selection_reference
                        << " ctlNum=" << static_cast<unsigned>(command.control_number)
                        << " positive=true bytes=" << frame.size() << '\\n';
                } catch (const std::exception& exception) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=command_termination_error association="
                        << association_id << " object=" << control.selection_reference
                        << " message=" << exception.what() << '\\n';
                    close_brcbs();
                    return;
                }
                break;
            }
        }
        if (!brcb_runtimes.empty()) {
'''
replace_once(server_path, termination_anchor, termination_code)

# ---------------------------------------------------------------------------
# SCL fixture: four configured SPC command Data Objects cover ctlModel 1..4.
# ---------------------------------------------------------------------------
fixture_path = "tests/fixtures/scl/minimal-station-brcb.scd"
replace_once(
    fixture_path,
    '''          <LN lnClass="GGIO" inst="1" lnType="GGIOType">
            <DOI name="SPCSO1">
              <DAI name="ctlModel"><Val>direct-with-normal-security</Val></DAI>
            </DOI>
          </LN>
''',
    '''          <LN lnClass="GGIO" inst="1" lnType="GGIOType">
            <DOI name="SPCSO1">
              <DAI name="ctlModel"><Val>direct-with-normal-security</Val></DAI>
            </DOI>
            <DOI name="SPCSO2">
              <DAI name="ctlModel"><Val>sbo-with-normal-security</Val></DAI>
            </DOI>
            <DOI name="SPCSO3">
              <DAI name="ctlModel"><Val>direct-with-enhanced-security</Val></DAI>
            </DOI>
            <DOI name="SPCSO4">
              <DAI name="ctlModel"><Val>sbo-with-enhanced-security</Val></DAI>
            </DOI>
          </LN>
''',
)
replace_once(
    fixture_path,
    '''    <LNodeType id="GGIOType" lnClass="GGIO">
      <DO name="SPCSO1" type="SPCType" />
    </LNodeType>
''',
    '''    <LNodeType id="GGIOType" lnClass="GGIO">
      <DO name="SPCSO1" type="SPCType" />
      <DO name="SPCSO2" type="SPCType" />
      <DO name="SPCSO3" type="SPCType" />
      <DO name="SPCSO4" type="SPCType" />
    </LNodeType>
''',
)

# SCL regression protects configured ctlModel projection for all four models.
replace_once(
    "tests/test_scl.cpp",
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
    '''void parser_preserves_configured_control_model_value() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("minimal-station-brcb.scd"));
    const std::array expected{
        std::pair<std::string_view, std::string_view>{"SPCSO1", "direct-with-normal-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO2", "sbo-with-normal-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO3", "direct-with-enhanced-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO4", "sbo-with-enhanced-security"},
    };
    for (const auto& [data_object, configured_value] : expected) {
        const auto configured = std::find_if(
            document.model_entries.begin(),
            document.model_entries.end(),
            [&](const SclDataSetEntry& entry) {
                return entry.ln_class == "GGIO" && entry.ln_inst == "1" &&
                    entry.do_name == data_object && entry.da_name == "ctlModel";
            });
        CHECK(configured != document.model_entries.end());
        CHECK(configured->functional_constraint == "CF");
        CHECK(configured->cdc == "SPC");
        CHECK(configured->basic_type == "Enum");
        CHECK(configured->configured_value == configured_value);
    }
}
''',
)
replace_once(
    "tests/test_scl.cpp",
    '#include <algorithm>\n#include <cstdint>\n',
    '#include <algorithm>\n#include <array>\n#include <cstdint>\n',
)
replace_once(
    "tests/test_scl.cpp",
    '#include <stdexcept>\n#include <string>\n',
    '#include <stdexcept>\n#include <string>\n#include <string_view>\n',
)

# ---------------------------------------------------------------------------
# Core state-machine regression for SBO ownership, timeout, enhanced sequence
# correlation and pending CommandTermination.
# ---------------------------------------------------------------------------
direct_test = "tests/test_mms_static_direct_control.cpp"
replace_once(
    direct_test,
    '''[[nodiscard]] std::vector<std::uint8_t> make_oper(
''',
    '''struct TestClock final {
    std::uint64_t now_ms{};
};

[[nodiscard]] std::uint64_t test_now_ms(const void* context) noexcept {
    return context == nullptr ? 0U : static_cast<const TestClock*>(context)->now_ms;
}

[[nodiscard]] MmsStaticRequestAccessContext access_for(const std::uint64_t association_id) {
    static constexpr std::array<std::uint8_t, 1U> owner{0xA5U};
    return {association_id, owner};
}

[[nodiscard]] std::vector<std::uint8_t> make_oper(
''',
)
insert_before(
    direct_test,
    '''void valid_oper_updates_live_state() {
''',
    '''[[nodiscard]] std::vector<std::uint8_t> make_cancel(
    const bool value,
    const std::uint8_t ctl_num,
    const bool test,
    const std::uint8_t origin_category = 2U) {
    constexpr std::array<std::uint8_t, 3U> origin_id{'H', 'M', 'I'};
    auto cancel = MmsDataValue::structure({
        MmsDataValue::boolean(value),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(origin_category),
            MmsDataValue::octet_string(origin_id),
        }),
        MmsDataValue::unsigned_integer(ctl_num),
        MmsDataValue::utc_time(Iec61850UtcTime{}),
        MmsDataValue::boolean(test),
    });
    const auto required = MmsDataSpanCodec::encoded_size(cancel);
    CHECK(required.has_value());
    std::vector<std::uint8_t> bytes(*required);
    const auto encoded = MmsDataSpanCodec::encode_into(cancel, bytes);
    CHECK(encoded.success());
    return bytes;
}

[[nodiscard]] MmsStaticDirectBooleanControlBinding make_binding(
    MmsStaticDirectBooleanControlState& state,
    MmsStaticDirectBooleanSharedState& shared,
    const MmsStaticControlModel model,
    const std::uint64_t association_id,
    TestClock& clock) {
    MmsStaticDirectBooleanControlBinding binding;
    binding.state = &state;
    binding.shared_state = &shared;
    binding.model = model;
    binding.association_id = association_id;
    binding.selection_reference = "LD0/GGIO1.SPCSO1";
    binding.sbo_timeout_ms = 100U;
    binding.now_ms = test_now_ms;
    binding.now_context = &clock;
    return binding;
}

''',
)
insert_before(
    direct_test,
    '''void read_callbacks_match_mms_types() {
''',
    '''void sbo_normal_enforces_owner_cancel_and_timeout() {
    TestClock clock{10U};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state_a{};
    MmsStaticDirectBooleanControlState state_b{};
    auto a = make_binding(state_a, shared, MmsStaticControlModel::sbo_normal, 11U, clock);
    auto b = make_binding(state_b, shared, MmsStaticControlModel::sbo_normal, 22U, clock);
    std::array<std::uint8_t, 64U> selected{};

    const auto first = mms_static_sbo_normal_read(&a, selected);
    CHECK(first.success());
    CHECK(first.bytes_written > 2U);
    CHECK(selected[0] == 0x8AU);
    CHECK(state_a.selected);

    selected.fill(0U);
    const auto contended = mms_static_sbo_normal_read(&b, selected);
    CHECK(contended.success());
    CHECK(contended.bytes_written == 2U);
    CHECK(selected[0] == 0x8AU && selected[1] == 0U);

    const auto denied = mms_static_boolean_write_oper_contextual(
        &b, make_oper(true, 7U, false), access_for(22U));
    CHECK(!denied.success);
    CHECK(denied.failure_code == 2U);

    const auto cancelled = mms_static_boolean_write_cancel_contextual(
        &a, make_cancel(false, 8U, false), access_for(11U));
    CHECK(cancelled.success);
    CHECK(shared.selected_association_id.load() == 0U);

    CHECK(mms_static_sbo_normal_read(&a, selected).success());
    clock.now_ms = 111U;
    selected.fill(0U);
    const auto takeover = mms_static_sbo_normal_read(&b, selected);
    CHECK(takeover.success());
    CHECK(selected[1] != 0U);
    CHECK(shared.selected_association_id.load() == 22U);

    const auto operated = mms_static_boolean_write_oper_contextual(
        &b, make_oper(true, 9U, false), access_for(22U));
    CHECK(operated.success);
    CHECK(shared.value.load() == 1U);
    CHECK(shared.selected_association_id.load() == 0U);
}

void direct_enhanced_queues_one_termination() {
    TestClock clock{};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::direct_enhanced, 31U, clock);

    const auto first = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(true, 12U, false), access_for(31U));
    CHECK(first.success);
    CHECK(state.pending_termination);
    CHECK(state.termination_command.control_number == 12U);
    CHECK(shared.value.load() == 1U);

    const auto second = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(false, 13U, false), access_for(31U));
    CHECK(!second.success);
    CHECK(second.failure_code == 2U);
    CHECK(shared.value.load() == 1U);
}

void sbo_enhanced_requires_exact_selected_sequence() {
    TestClock clock{50U};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::sbo_enhanced, 41U, clock);

    const auto selected = mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 21U, false), access_for(41U));
    CHECK(selected.success);
    CHECK(state.selected && state.selected_with_value);
    CHECK(shared.selected_association_id.load() == 41U);

    const auto mismatch = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(false, 21U, false), access_for(41U));
    CHECK(!mismatch.success);
    CHECK(mismatch.failure_code == 11U);
    CHECK(shared.value.load() == 0U);

    const auto operated = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(true, 21U, false), access_for(41U));
    CHECK(operated.success);
    CHECK(shared.value.load() == 1U);
    CHECK(state.pending_termination);
    CHECK(shared.selected_association_id.load() == 0U);
}

void sbo_enhanced_cancel_and_association_close_release_owner() {
    TestClock clock{};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::sbo_enhanced, 51U, clock);

    CHECK(mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 30U, false), access_for(51U)).success);
    CHECK(mms_static_boolean_write_cancel_contextual(
        &binding, make_cancel(true, 30U, false), access_for(51U)).success);
    CHECK(shared.selected_association_id.load() == 0U);
    CHECK(shared.value.load() == 0U);

    CHECK(mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 31U, false), access_for(51U)).success);
    mms_static_control_on_association_closed(binding);
    CHECK(shared.selected_association_id.load() == 0U);
    CHECK(!state.selected);
}

''',
)
replace_once(
    direct_test,
    '''    read = mms_static_control_read_unavailable(nullptr, bytes);
    CHECK(!read.success());
}
''',
    '''    MmsStaticDirectBooleanControlState configured_state{};
    MmsStaticDirectBooleanControlBinding configured_binding;
    configured_binding.state = &configured_state;
    configured_binding.model = MmsStaticControlModel::sbo_enhanced;
    bytes.fill(0U);
    read = mms_static_control_read_ctl_model(&configured_binding, bytes);
    CHECK(read.success());
    CHECK(bytes[0] == 0x86U && bytes[1] == 0x01U && bytes[2] == 0x04U);

    read = mms_static_control_read_unavailable(nullptr, bytes);
    CHECK(!read.success());
}
''',
)
replace_once(
    direct_test,
    '''        backend_failure_does_not_publish_state();
        read_callbacks_match_mms_types();
''',
    '''        backend_failure_does_not_publish_state();
        sbo_normal_enforces_owner_cancel_and_timeout();
        direct_enhanced_queues_one_termination();
        sbo_enhanced_requires_exact_selected_sequence();
        sbo_enhanced_cancel_and_association_close_release_owner();
        read_callbacks_match_mms_types();
''',
)

# ---------------------------------------------------------------------------
# Qt E2E wire regression: exercise ctlModel 2/3/4 through the same public
# guarded control client used against third-party IEDs.
# ---------------------------------------------------------------------------
gui_test = "apps/ied_simulator/test_gui_live_value.py"
insert_before(
    gui_test,
    '''def prove_concurrent_associations(read_probe: str, port: int, item: str) -> float:
''',
    '''def run_control_action(
    control_probe: str,
    port: int,
    object_reference: str,
    action: str,
    value: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            control_probe,
            "127.0.0.1",
            str(port),
            "--object",
            object_reference,
            "--timeout-ms",
            "5000",
            "--termination-timeout-ms",
            "5000",
            "--action",
            action,
            "--value",
            value,
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
        timeout=10,
        check=False,
        creationflags=creation_flags(),
    )


def run_sbo_enhanced_control_regressions(
    control_probe: str,
    read_probe: str,
    port: int,
) -> str:
    outputs: list[str] = []

    # SBO normal: Select/Cancel must be non-mutating; Select/Operate must mutate.
    sbo_cancel = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-cancel", "on")
    if (
        sbo_cancel.returncode != 0
        or "ctlModel=sbo-normal" not in sbo_cancel.stdout
        or "completion=accepted" not in sbo_cancel.stdout
        or "STATUS_AFTER false" not in sbo_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO2$Cancel" not in sbo_cancel.stdout
    ):
        raise RuntimeError(
            "SBO-normal Select/Cancel wire regression failed: "
            f"exit={sbo_cancel.returncode} stdout={sbo_cancel.stdout!r} stderr={sbo_cancel.stderr!r}"
        )
    outputs.append(sbo_cancel.stdout.strip())

    sbo_oper = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-operate", "on")
    if (
        sbo_oper.returncode != 0
        or "ctlModel=sbo-normal" not in sbo_oper.stdout
        or "completion=accepted" not in sbo_oper.stdout
        or "STATUS_AFTER true" not in sbo_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO2$Oper" not in sbo_oper.stdout
    ):
        raise RuntimeError(
            "SBO-normal Select/Operate wire regression failed: "
            f"exit={sbo_oper.returncode} stdout={sbo_oper.stdout!r} stderr={sbo_oper.stderr!r}"
        )
    outputs.append(sbo_oper.stdout.strip())

    # Direct enhanced: confirmed Write is not completion; positive correlated
    # CommandTermination is mandatory and no automatic retry is allowed.
    direct_enhanced = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO3", "operate", "on")
    if (
        direct_enhanced.returncode != 0
        or "ctlModel=direct-enhanced" not in direct_enhanced.stdout
        or "completion=positive-termination" not in direct_enhanced.stdout
        or "termination=true" not in direct_enhanced.stdout
        or "STATUS_AFTER true" not in direct_enhanced.stdout
        or "NO_RETRY_EVIDENCE controlWrites=1" not in direct_enhanced.stdout
    ):
        raise RuntimeError(
            "Direct-enhanced CommandTermination regression failed: "
            f"exit={direct_enhanced.returncode} stdout={direct_enhanced.stdout!r} stderr={direct_enhanced.stderr!r}"
        )
    outputs.append(direct_enhanced.stdout.strip())

    # SBO enhanced: first prove SBOw/Cancel is non-mutating, then prove exact
    # SBOw -> Oper -> positive CommandTermination sequence.
    enhanced_cancel = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO4", "select-cancel", "on")
    if (
        enhanced_cancel.returncode != 0
        or "ctlModel=sbo-enhanced" not in enhanced_cancel.stdout
        or "STATUS_AFTER false" not in enhanced_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$SBOw" not in enhanced_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$Cancel" not in enhanced_cancel.stdout
    ):
        raise RuntimeError(
            "SBO-enhanced SBOw/Cancel regression failed: "
            f"exit={enhanced_cancel.returncode} stdout={enhanced_cancel.stdout!r} stderr={enhanced_cancel.stderr!r}"
        )
    outputs.append(enhanced_cancel.stdout.strip())

    enhanced_oper = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO4", "select-operate", "on")
    if (
        enhanced_oper.returncode != 0
        or "ctlModel=sbo-enhanced" not in enhanced_oper.stdout
        or "completion=positive-termination" not in enhanced_oper.stdout
        or "termination=true" not in enhanced_oper.stdout
        or "STATUS_AFTER true" not in enhanced_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$SBOw" not in enhanced_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$Oper" not in enhanced_oper.stdout
        or "NO_RETRY_EVIDENCE controlWrites=2" not in enhanced_oper.stdout
    ):
        raise RuntimeError(
            "SBO-enhanced SBOw/Oper/CommandTermination regression failed: "
            f"exit={enhanced_oper.returncode} stdout={enhanced_oper.stdout!r} stderr={enhanced_oper.stderr!r}"
        )
    outputs.append(enhanced_oper.stdout.strip())

    for item in ("SPCSO2", "SPCSO3", "SPCSO4"):
        status = run_probe(read_probe, port, f"GGIO1$ST${item}$stVal")
        if status.returncode != 0 or "value=true" not in status.stdout:
            raise RuntimeError(
                f"{item} status was not persistent across a second association: "
                f"exit={status.returncode} stdout={status.stdout!r} stderr={status.stderr!r}"
            )
    return "\\n".join(outputs)


''',
)
replace_once(
    gui_test,
    '''                direct_control_manifest = "CTL\\tMU01LD0\\tGGIO1\\tSPCSO1\\tSPC\\t1"
                if (
''',
    '''                configured_controls = (
                    "CTL\\tMU01LD0\\tGGIO1\\tSPCSO1\\tSPC\\t1",
                    "CTL\\tMU01LD0\\tGGIO1\\tSPCSO2\\tSPC\\t2",
                    "CTL\\tMU01LD0\\tGGIO1\\tSPCSO3\\tSPC\\t3",
                    "CTL\\tMU01LD0\\tGGIO1\\tSPCSO4\\tSPC\\t4",
                )
                if (
''',
)
replace_once(
    gui_test,
    '''                    and brcb_manifest in manifest_text
                    and direct_control_manifest in manifest_text
                    and "GGIO1$CF$SPCSO1$ctlModel\\tEnum\\tEnumeration\\t1" in manifest_text
''',
    '''                    and brcb_manifest in manifest_text
                    and all(control in manifest_text for control in configured_controls)
                    and "GGIO1$CF$SPCSO1$ctlModel\\tEnum\\tEnumeration\\t1" in manifest_text
                    and "GGIO1$CF$SPCSO2$ctlModel\\tEnum\\tEnumeration\\t2" in manifest_text
                    and "GGIO1$CF$SPCSO3$ctlModel\\tEnum\\tEnumeration\\t3" in manifest_text
                    and "GGIO1$CF$SPCSO4$ctlModel\\tEnum\\tEnumeration\\t4" in manifest_text
''',
)
replace_once(
    gui_test,
    '''            control_output = run_direct_normal_control_regression(
                control_probe,
                read_probe,
                port,
            )
            concurrent_seconds = prove_concurrent_associations(
''',
    '''            control_output = run_direct_normal_control_regression(
                control_probe,
                read_probe,
                port,
            )
            sbo_enhanced_output = run_sbo_enhanced_control_regressions(
                control_probe,
                read_probe,
                port,
            )
            concurrent_seconds = prove_concurrent_associations(
''',
)
replace_once(
    gui_test,
    '''                "control_direct_normal=pass "
                "urcb_gi=pass "
''',
    '''                "control_direct_normal=pass "
                "control_sbo_normal=pass "
                "control_direct_enhanced=pass "
                "control_sbo_enhanced=pass "
                "urcb_gi=pass "
''',
)
replace_once(
    gui_test,
    '''            print(control_output)
            print(urcb_output)
''',
    '''            print(control_output)
            print(sbo_enhanced_output)
            print(urcb_output)
''',
)

print("SBO normal/direct enhanced/SBO enhanced simulator slice patch applied.")

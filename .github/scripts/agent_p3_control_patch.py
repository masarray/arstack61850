from pathlib import Path


def read(path):
    return Path(path).read_text(encoding='utf-8')


def write(path, text):
    Path(path).write_text(text, encoding='utf-8')


def repl(text, old, new, label):
    if old not in text:
        raise RuntimeError(f'anchor missing: {label}')
    if text.count(old) != 1:
        raise RuntimeError(f'anchor not unique ({text.count(old)}): {label}')
    return text.replace(old, new, 1)

# ---- Contextual Read: association-aware SBO normal Select -----------------
p='include/ariec61850/mms/static_object_table.hpp'
s=read(p)
s=repl(s,
'''using MmsStaticContextualWriteCallback = MmsStaticWriteResult (*)(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;
''',
'''using MmsStaticContextualReadCallback = wire::EncodeResult (*)(
    const void* context,
    std::span<std::uint8_t> destination,
    const MmsStaticRequestAccessContext& access) noexcept;

using MmsStaticContextualWriteCallback = MmsStaticWriteResult (*)(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;
''','contextual read typedef')
s=repl(s,
'''    MmsStaticContextualWriteCallback contextual_write{};

    [[nodiscard]] constexpr bool writable() const noexcept {
''',
'''    MmsStaticContextualWriteCallback contextual_write{};

    // Appended for source compatibility. Contextual Read is required by
    // association-owned services such as SBO normal Select (a Read service).
    MmsStaticContextualReadCallback contextual_read{};

    [[nodiscard]] constexpr bool writable() const noexcept {
''','contextual read field')
write(p,s)

p='src/mms/static_object_table.cpp'
s=read(p)
s=repl(s,
'''            !valid_type_specification(object.type_specification) ||
            object.read == nullptr) {
''',
'''            !valid_type_specification(object.type_specification) ||
            (object.read == nullptr && object.contextual_read == nullptr)) {
''','object table contextual validation')
write(p,s)

p='src/mms/static_dispatcher.cpp'
s=read(p)
s=repl(s,
'''    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
''',
'''    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace,
    const MmsStaticRequestAccessContext& access) noexcept {
''','dispatch_read signature')
s=repl(s,
'''        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->read(object->context, remaining);
''',
'''        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->contextual_read != nullptr
            ? object->contextual_read(object->context, remaining, access)
            : object->read(object->context, remaining);
''','dispatch contextual read')
s=repl(s,
'''    case MmsWireConfirmedService::read:
        return dispatch_read(objects_, policy_, request, response, workspace);
''',
'''    case MmsWireConfirmedService::read:
        return dispatch_read(objects_, policy_, request, response, workspace, access);
''','dispatch read call')
write(p,s)

# ---- SCL exact default values + protect standard control CF metadata -------
p='apps/ied_simulator/src/SclMmsMaterializer.hpp'
s=read(p)
s=repl(s,
'''struct DaDecl final {
    QString name;
    QString bType;
    QString type;
    QString fc;
    int count{1};
};
''',
'''struct DaDecl final {
    QString name;
    QString bType;
    QString type;
    QString fc;
    QString defaultValue;
    int count{1};
};
''','DaDecl default')
s=repl(s,
'''    QString currentDaType;

    QXmlStreamReader xml{&input};
''',
'''    QString currentDaType;
    std::optional<int> currentDaIndex;

    QXmlStreamReader xml{&input};
''','current DA index')
s=repl(s,
'''            } else if (name == QStringLiteral("DA") && !currentDoType.isEmpty()) {
                doTypes[currentDoType].dataAttributes.push_back(DaDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("bType")).toString(),
                    a.value(QStringLiteral("type")).toString(),
                    a.value(QStringLiteral("fc")).toString(),
                    elementCount(a.value(QStringLiteral("count")).toString())});
''',
'''            } else if (name == QStringLiteral("DA") && !currentDoType.isEmpty()) {
                auto& attributes = doTypes[currentDoType].dataAttributes;
                attributes.push_back(DaDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("bType")).toString(),
                    a.value(QStringLiteral("type")).toString(),
                    a.value(QStringLiteral("fc")).toString(),
                    {},
                    elementCount(a.value(QStringLiteral("count")).toString())});
                currentDaIndex = static_cast<int>(attributes.size() - 1U);
            } else if (name == QStringLiteral("Val") && !currentDoType.isEmpty() &&
                       currentDaIndex.has_value()) {
                doTypes[currentDoType].dataAttributes[static_cast<std::size_t>(*currentDaIndex)]
                    .defaultValue = xml.readElementText().trimmed();
''','parse template Val')
s=repl(s,
'''            else if (name == QStringLiteral("DOType")) currentDoType.clear();
''',
'''            else if (name == QStringLiteral("DA")) currentDaIndex.reset();
            else if (name == QStringLiteral("DOType")) {
                currentDaIndex.reset();
                currentDoType.clear();
            }
''','reset DA index')
s=repl(s,
'''                             const QString& rawType,
                             const int count) {
''',
'''                             const QString& rawType,
                             const int count,
                             const QString& declaredValue = {}) {
''','addLeaf default arg')
s=repl(s,
'''        value.initialValue = initialValue(normalized, daPath);
''',
'''        value.initialValue = declaredValue.isEmpty()
            ? initialValue(normalized, daPath)
            : declaredValue;
''','apply declared Val')
s=repl(s,
'''        value.mmsWritable = mmsWritableFc(fc) && !value.quality && !value.timestamp;
''',
'''        const auto controlMetadata =
            daPath.compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) == 0 ||
            daPath.compare(QStringLiteral("sboTimeout"), Qt::CaseInsensitive) == 0 ||
            daPath.compare(QStringLiteral("operTimeout"), Qt::CaseInsensitive) == 0;
        value.mmsWritable = mmsWritableFc(fc) && !value.quality && !value.timestamp &&
            !controlMetadata;
''','protect control metadata')
s=repl(s,
'''                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType, da.count);
''',
'''                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType, da.count,
                        da.defaultValue);
''','pass DA Val')
write(p,s)

# ---- Controller manifest CTRL records --------------------------------------
p='apps/ied_simulator/src/IedSimulatorController.cpp'
s=read(p)
anchor='''    if (emittedObjects.isEmpty()) {
'''
control_emit=r'''    // P3: materialize command services from SCL control metadata without
    // making CO a generic writable value. ctlModel/sboTimeout/operTimeout stay
    // regular read-only CF leaves; CTRL only binds the service runtime to the
    // authoritative ST/MX status leaf.
    for (const auto& loaded : documents_) {
        for (const auto& variant : loaded.materializedValues) {
            const auto ctl = variant.toMap();
            if (ctl.value(QStringLiteral("dataAttribute")).toString()
                    .compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) != 0) continue;
            bool modelOk = false;
            const auto model = ctl.value(QStringLiteral("value")).toString().toUInt(&modelOk);
            if (!modelOk || model < 1U || model > 4U) continue;
            const auto domain = ctl.value(QStringLiteral("mmsDomain")).toString();
            const auto ln = ctl.value(QStringLiteral("logicalNode")).toString();
            const auto dataObject = ctl.value(QStringLiteral("dataObject")).toString();
            if (domain.isEmpty() || ln.isEmpty() || dataObject.isEmpty()) continue;

            QString statusDomain;
            QString statusItem;
            quint64 sboTimeout = 10'000U;
            quint64 operTimeout = 1'000U;
            for (const auto& siblingVariant : loaded.materializedValues) {
                const auto sibling = siblingVariant.toMap();
                if (sibling.value(QStringLiteral("mmsDomain")).toString() != domain ||
                    sibling.value(QStringLiteral("logicalNode")).toString() != ln ||
                    sibling.value(QStringLiteral("dataObject")).toString() != dataObject) continue;
                const auto da = sibling.value(QStringLiteral("dataAttribute")).toString();
                const auto fc = sibling.value(QStringLiteral("fc")).toString();
                if (da.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) == 0 &&
                    fc.compare(QStringLiteral("ST"), Qt::CaseInsensitive) == 0) {
                    statusDomain = sibling.value(QStringLiteral("mmsDomain")).toString();
                    statusItem = sibling.value(QStringLiteral("mmsItem")).toString();
                } else if (da.compare(QStringLiteral("sboTimeout"), Qt::CaseInsensitive) == 0) {
                    bool ok = false;
                    const auto parsed = sibling.value(QStringLiteral("value")).toString().toULongLong(&ok);
                    if (ok && parsed > 0U) sboTimeout = parsed;
                } else if (da.compare(QStringLiteral("operTimeout"), Qt::CaseInsensitive) == 0) {
                    bool ok = false;
                    const auto parsed = sibling.value(QStringLiteral("value")).toString().toULongLong(&ok);
                    if (ok && parsed > 0U) operTimeout = parsed;
                }
            }
            if (statusDomain.isEmpty() || statusItem.isEmpty()) continue;
            output.write("CTRL\t");
            output.write(manifestField(domain));
            output.write("\t");
            output.write(manifestField(ln));
            output.write("\t");
            output.write(manifestField(dataObject));
            output.write("\t");
            output.write(QByteArray::number(model));
            output.write("\t");
            output.write(manifestField(statusDomain));
            output.write("\t");
            output.write(manifestField(statusItem));
            output.write("\t");
            output.write(QByteArray::number(sboTimeout));
            output.write("\t");
            output.write(QByteArray::number(operTimeout));
            output.write("\n");
        }
    }

'''
s=repl(s,anchor,control_emit+anchor,'CTRL manifest emission')
write(p,s)

# ---- SCL fixture: all four control models ----------------------------------
p='tests/fixtures/scl/iedsim-full-model.scd'
s=read(p)
s=repl(s,
'''          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />
''',
'''          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />
          <LN lnClass="GGIO" inst="1" lnType="GGIOControlType" />
''','GGIO control LN')
s=repl(s,
'''    <LNodeType id="XCBRType" lnClass="XCBR">
''',
'''    <LNodeType id="GGIOControlType" lnClass="GGIO">
      <DO name="SPCSO1" type="SPCSOType1" />
      <DO name="SPCSO2" type="SPCSOType2" />
      <DO name="SPCSO3" type="SPCSOType3" />
      <DO name="SPCSO4" type="SPCSOType4" />
    </LNodeType>
    <DOType id="SPCSOType1" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>1</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType2" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>2</Val></DA>
      <DA name="sboTimeout" bType="INT32U" fc="CF"><Val>1500</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType3" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>3</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType4" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>4</Val></DA>
      <DA name="sboTimeout" bType="INT32U" fc="CF"><Val>1500</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <LNodeType id="XCBRType" lnClass="XCBR">
''','GGIO templates')
write(p,s)

# ---- Static simulator server control adapter -------------------------------
p='tools/static_ied_server.cpp'
s=read(p)
s=repl(s,
'''#include "ariec61850/mms/static_urcb_runtime.hpp"
#include "ariec61850/mms/data_codec.hpp"
''',
'''#include "ariec61850/mms/static_urcb_runtime.hpp"
#include "ariec61850/mms/static_direct_control.hpp"
#include "ariec61850/control/guarded_control.hpp"
#include "ariec61850/mms/data_codec.hpp"
''','control includes')
s=repl(s,
'''namespace mms = ar::iec61850::mms;
namespace wire = ar::iec61850::wire;
''',
'''namespace mms = ar::iec61850::mms;
namespace control = ar::iec61850::control;
namespace wire = ar::iec61850::wire;
''','control namespace')
s=repl(s,
'''            object.type_specification.empty() || object.read == nullptr ||
''',
'''            object.type_specification.empty() ||
            (object.read == nullptr && object.contextual_read == nullptr) ||
''','host table contextual read')
s=repl(s,
'''struct ManifestReportControl final {
''',
'''struct ManifestControl final {
    std::string domain;
    std::string logical_node;
    std::string data_object;
    std::uint8_t model{};
    std::string status_domain;
    std::string status_item;
    std::uint64_t sbo_timeout_ms{10'000U};
    std::uint64_t operate_timeout_ms{1'000U};
};

struct ManifestReportControl final {
''','ManifestControl')
s=repl(s,
'''    std::vector<ManifestReportControl> report_controls;
''',
'''    std::vector<ManifestReportControl> report_controls;
    std::vector<ManifestControl> controls;
''','model controls')
s=repl(s,
'''    std::vector<ManifestReportControl> parsed_rcbs;
''',
'''    std::vector<ManifestReportControl> parsed_rcbs;
    std::vector<ManifestControl> parsed_controls;
''','parsed controls')
s=repl(s,
'''        } else if (fields.size() >= 11U && fields[0] == "RCB") {
''',
'''        } else if (fields.size() >= 9U && fields[0] == "CTRL") {
            ManifestControl ctl;
            ctl.domain = fields[1];
            ctl.logical_node = fields[2];
            ctl.data_object = fields[3];
            try { ctl.model = static_cast<std::uint8_t>(std::stoul(fields[4])); } catch (...) {}
            ctl.status_domain = fields[5];
            ctl.status_item = fields[6];
            try { ctl.sbo_timeout_ms = std::stoull(fields[7]); } catch (...) {}
            try { ctl.operate_timeout_ms = std::stoull(fields[8]); } catch (...) {}
            if (!ctl.domain.empty() && !ctl.logical_node.empty() && !ctl.data_object.empty() &&
                ctl.model >= 1U && ctl.model <= 4U &&
                !ctl.status_domain.empty() && !ctl.status_item.empty()) {
                parsed_controls.push_back(std::move(ctl));
            }
        } else if (fields.size() >= 11U && fields[0] == "RCB") {
''','parse CTRL')
s=repl(s,
'''    model.report_controls = parsed_rcbs;

    // Manifest v4 is object-driven''',
'''    model.report_controls = parsed_rcbs;
    model.controls = parsed_controls;

    // Manifest v4 is object-driven''','store CTRL')

# Inject control runtime before refresh_manifest_values.
anchor='''[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {
'''
block=r'''
struct HostControl final {
    ManifestControl* manifest{};
    std::size_t status_index{};
    control::ControlObjectReference object{};
    std::unique_ptr<control::GuardedControlPlanner> planner;
    std::string oper_item;
    std::string sbo_item;
    std::string sbow_item;
    std::string cancel_item;
    std::vector<std::uint8_t> oper_type;
    std::vector<std::uint8_t> cancel_type;
    std::vector<std::uint8_t> sbo_type;
    std::vector<std::uint8_t> last_oper;
    std::uint64_t normal_sbo_owner{};
    std::uint64_t normal_sbo_expires{};
    bool termination_pending{};
};

struct HostControlRuntime final {
    ManifestModel* model{};
    std::vector<std::unique_ptr<HostControl>> controls;
};

struct HostControlObjectContext final {
    HostControlRuntime* runtime{};
    HostControl* control{};
    enum class Service : std::uint8_t { oper, sbo, sbow, cancel } service{Service::oper};
};

static std::vector<std::unique_ptr<HostControlObjectContext>> g_control_object_contexts;

[[nodiscard]] bool host_control_authorize(
    void*, control::ControlAction, const control::ControlObjectReference&,
    const control::ControlClientIdentity&, const control::ControlSequenceView&) noexcept {
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> host_boolean_oper_type(const bool cancel) {
    mms::MmsTypeSpecification root;
    root.kind = mms::MmsTypeKind::structure;
    auto field = [](std::string name, mms::MmsTypeKind kind, std::optional<std::uint32_t> size = {}) {
        mms::MmsTypeSpecification value;
        value.name = std::move(name);
        value.kind = kind;
        value.size = size;
        return value;
    };
    root.children.push_back(field("ctlVal", mms::MmsTypeKind::boolean));
    mms::MmsTypeSpecification origin;
    origin.name = "origin";
    origin.kind = mms::MmsTypeKind::structure;
    origin.children.push_back(field("orCat", mms::MmsTypeKind::integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 8U));
    root.children.push_back(std::move(origin));
    root.children.push_back(field("ctlNum", mms::MmsTypeKind::unsigned_integer, 8U));
    root.children.push_back(field("T", mms::MmsTypeKind::utc_time));
    root.children.push_back(field("Test", mms::MmsTypeKind::boolean));
    if (!cancel) root.children.push_back(field("Check", mms::MmsTypeKind::bit_string, 2U));
    return mms::MmsServiceCodec::encode_type_specification(root);
}

[[nodiscard]] std::vector<std::uint8_t> host_visible_string_type() {
    mms::MmsTypeSpecification type;
    type.kind = mms::MmsTypeKind::visible_string;
    type.size = 129U;
    return mms::MmsServiceCodec::encode_type_specification(type);
}

[[nodiscard]] control::ControlModel host_control_model(const std::uint8_t model) noexcept {
    switch (model) {
    case 1U: return control::ControlModel::direct_normal;
    case 2U: return control::ControlModel::select_before_operate_normal;
    case 3U: return control::ControlModel::direct_enhanced;
    case 4U: return control::ControlModel::select_before_operate_enhanced;
    default: return control::ControlModel::unknown;
    }
}

[[nodiscard]] std::uint64_t host_control_now_ms() noexcept {
    return brcb_now_ms(nullptr);
}

[[nodiscard]] control::ControlSequenceView host_control_sequence(
    const mms::MmsStaticDirectBooleanOperate& decoded,
    const std::span<const std::uint8_t> encoded) noexcept {
    control::ControlSequenceView sequence;
    sequence.control_value = encoded;
    sequence.origin_category = static_cast<control::OriginCategory>(decoded.origin_category);
    sequence.control_number = decoded.control_number;
    sequence.timestamp_token = 1U;
    sequence.test = decoded.test;
    sequence.synchro_check = decoded.synchro_check;
    sequence.interlock_check = decoded.interlock_check;
    return sequence;
}

[[nodiscard]] bool apply_host_control(
    HostControlRuntime& runtime,
    HostControl& ctl,
    const mms::MmsStaticDirectBooleanOperate& decoded) {
    if (runtime.model == nullptr || decoded.test) return true;
    return apply_live_data(
        *runtime.model,
        ctl.status_index,
        mms::MmsDataValue::boolean(decoded.control_value),
        decoded.control_value ? "true" : "false",
        "Good",
        "mms-control",
        0U,
        std::nullopt);
}

[[nodiscard]] mms::MmsStaticWriteResult host_control_write(
    void* raw,
    const std::span<const std::uint8_t> encoded,
    const mms::MmsStaticRequestAccessContext& access) noexcept {
    auto* ctx = static_cast<HostControlObjectContext*>(raw);
    if (ctx == nullptr || ctx->runtime == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
    auto& host = *ctx->control;
    const auto now = host_control_now_ms();
    const control::ControlClientIdentity client{access.association_id};

    try {
        if (ctx->service == HostControlObjectContext::Service::cancel) {
            // Cancel's exact shape is validated by the live TypeSpecification on
            // the client. Server-side require one MMS structure before applying
            // ownership semantics; it never mutates the process value.
            const auto values = mms::MmsDataCodec::decode_all(encoded);
            if (values.size() != 1U || values.front().kind() != mms::MmsDataKind::structure) {
                return {false, 3U};
            }
            if (host.manifest->model == 2U) {
                if (host.normal_sbo_owner != access.association_id ||
                    (host.normal_sbo_expires != 0U && now >= host.normal_sbo_expires)) {
                    host.normal_sbo_owner = 0U;
                    host.normal_sbo_expires = 0U;
                    return {false, 3U};
                }
                host.normal_sbo_owner = 0U;
                host.normal_sbo_expires = 0U;
                return {true, 0U};
            }
            const auto decision = host.planner->cancel(client, now);
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }

        mms::MmsStaticDirectBooleanOperate decoded;
        mms::MmsStaticDirectControlPolicy decode_policy;
        decode_policy.allow_test = true;
        decode_policy.allow_synchro_check = true;
        decode_policy.allow_interlock_check = true;
        if (!mms::mms_static_direct_boolean_decode_operate(encoded, decode_policy, decoded)) {
            return {false, 3U};
        }
        const std::array<std::uint8_t, 3U> ctl_value{
            0x83U, 0x01U, decoded.control_value ? 0xFFU : 0x00U};
        const auto sequence = host_control_sequence(decoded, ctl_value);

        if (ctx->service == HostControlObjectContext::Service::sbow) {
            const auto decision = host.planner->select_with_value(client, sequence, now);
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }

        if (host.manifest->model == 2U) {
            if (host.normal_sbo_owner != access.association_id ||
                (host.normal_sbo_expires != 0U && now >= host.normal_sbo_expires)) {
                host.normal_sbo_owner = 0U;
                host.normal_sbo_expires = 0U;
                return {false, 3U};
            }
            host.normal_sbo_owner = 0U;
            host.normal_sbo_expires = 0U;
        } else {
            const auto decision = host.planner->operate(client, sequence, now);
            if (!decision.accepted()) return {false, 3U};
        }

        if (!apply_host_control(*ctx->runtime, host, decoded)) return {false, 10U};
        host.last_oper.assign(encoded.begin(), encoded.end());
        if (host.manifest->model == 3U || host.manifest->model == 4U) {
            host.termination_pending = true;
        }
        std::cout << "IEDSIM_EVENT kind=control_operate association=" << access.association_id
                  << " object=" << host.manifest->domain << '/' << host.manifest->logical_node
                  << '.' << host.manifest->data_object
                  << " model=" << static_cast<unsigned>(host.manifest->model)
                  << " value=" << (decoded.control_value ? "true" : "false")
                  << " test=" << (decoded.test ? "true" : "false") << '\n';
        std::cout.flush();
        return {true, 0U};
    } catch (...) {
        return {false, 10U};
    }
}

[[nodiscard]] wire::EncodeResult host_control_sbo_read(
    const void* raw,
    const std::span<std::uint8_t> destination,
    const mms::MmsStaticRequestAccessContext& access) noexcept {
    auto* ctx = const_cast<HostControlObjectContext*>(
        static_cast<const HostControlObjectContext*>(raw));
    if (ctx == nullptr || ctx->control == nullptr || access.association_id == 0U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    auto& host = *ctx->control;
    const auto now = host_control_now_ms();
    if (host.manifest->model != 2U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (host.normal_sbo_owner != 0U && host.normal_sbo_expires != 0U &&
        now >= host.normal_sbo_expires) {
        host.normal_sbo_owner = 0U;
        host.normal_sbo_expires = 0U;
    }
    if (host.normal_sbo_owner != 0U && host.normal_sbo_owner != access.association_id) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    host.normal_sbo_owner = access.association_id;
    host.normal_sbo_expires = host.manifest->sbo_timeout_ms == 0U
        ? 0U : now + host.manifest->sbo_timeout_ms;
    const auto selected = host.manifest->domain + "/" + host.manifest->logical_node +
        "." + host.manifest->data_object;
    const auto encoded = mms::MmsDataCodec::encode(mms::MmsDataValue::visible_string(selected));
    if (destination.size() < encoded.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, encoded.size()};
    }
    std::copy(encoded.begin(), encoded.end(), destination.begin());
    std::cout << "IEDSIM_EVENT kind=control_select association=" << access.association_id
              << " object=" << selected << " model=2\n";
    std::cout.flush();
    return {wire::EncodeStatus::ok, encoded.size(), encoded.size()};
}

[[nodiscard]] wire::EncodeResult host_control_unreadable(
    const void*, const std::span<std::uint8_t>) noexcept {
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

[[nodiscard]] HostControlRuntime initialize_host_controls(ManifestModel& model) {
    HostControlRuntime runtime;
    runtime.model = &model;
    g_control_object_contexts.clear();
    for (auto& definition : model.controls) {
        const auto status = model.value_indices.find(
            object_key(definition.status_domain, definition.status_item));
        if (status == model.value_indices.end() ||
            model.values[status->second].type.kind != mms::MmsTypeKind::boolean) {
            throw std::runtime_error("Control target is missing or not Boolean: " +
                                     definition.domain + "/" + definition.logical_node + "." +
                                     definition.data_object);
        }
        auto host = std::make_unique<HostControl>();
        host->manifest = &definition;
        host->status_index = status->second;
        const auto reference = definition.domain + "/" + definition.logical_node + "." +
            definition.data_object;
        if (!control::try_parse_control_object_reference(reference, host->object)) {
            throw std::runtime_error("Invalid control object reference: " + reference);
        }
        control::GuardedControlPolicy policy;
        policy.sbo_timeout_ms = definition.sbo_timeout_ms;
        policy.authorize = host_control_authorize;
        host->planner = std::make_unique<control::GuardedControlPlanner>(
            host->object, host_control_model(definition.model), policy);
        host->oper_item = definition.logical_node + "$CO$" + definition.data_object + "$Oper";
        host->sbo_item = definition.logical_node + "$CO$" + definition.data_object + "$SBO";
        host->sbow_item = definition.logical_node + "$CO$" + definition.data_object + "$SBOw";
        host->cancel_item = definition.logical_node + "$CO$" + definition.data_object + "$Cancel";
        host->oper_type = host_boolean_oper_type(false);
        host->cancel_type = host_boolean_oper_type(true);
        host->sbo_type = host_visible_string_type();

        auto add_object = [&](const std::string& item,
                              const std::vector<std::uint8_t>& type,
                              HostControlObjectContext::Service service,
                              const bool contextual_read,
                              const bool writable) {
            auto ctx = std::make_unique<HostControlObjectContext>();
            ctx->runtime = &runtime;
            ctx->control = host.get();
            ctx->service = service;
            mms::MmsStaticObjectEntry entry{
                definition.domain, item, type,
                contextual_read ? nullptr : host_control_unreadable,
                ctx.get()};
            if (contextual_read) entry.contextual_read = host_control_sbo_read;
            if (writable) {
                entry.contextual_write = host_control_write;
                entry.write_context = ctx.get();
            }
            const auto existing = std::find_if(
                model.objects.begin(), model.objects.end(), [&](const auto& candidate) {
                    return candidate.domain == definition.domain && candidate.item == item;
                });
            if (existing == model.objects.end()) model.objects.push_back(entry);
            else *existing = entry;
            g_control_object_contexts.push_back(std::move(ctx));
        };

        add_object(host->oper_item, host->oper_type,
                   HostControlObjectContext::Service::oper, false, true);
        if (definition.model == 2U) {
            add_object(host->sbo_item, host->sbo_type,
                       HostControlObjectContext::Service::sbo, true, false);
            add_object(host->cancel_item, host->cancel_type,
                       HostControlObjectContext::Service::cancel, false, true);
        } else if (definition.model == 4U) {
            add_object(host->sbow_item, host->oper_type,
                       HostControlObjectContext::Service::sbow, false, true);
            add_object(host->cancel_item, host->cancel_type,
                       HostControlObjectContext::Service::cancel, false, true);
        }
        runtime.controls.push_back(std::move(host));
    }
    return runtime;
}

void host_control_association_closed(
    HostControlRuntime* runtime, const std::uint64_t association_id) noexcept {
    if (runtime == nullptr || association_id == 0U) return;
    for (auto& host : runtime->controls) {
        host->planner->on_association_closed(association_id);
        if (host->normal_sbo_owner == association_id) {
            host->normal_sbo_owner = 0U;
            host->normal_sbo_expires = 0U;
        }
        host->termination_pending = false;
    }
}

[[nodiscard]] bool encode_host_control_termination(
    const mms::MmsStaticConnectionRuntime& connection,
    HostControl& host,
    ConnectionBuffers& buffers,
    std::size_t& bytes_written) noexcept {
    bytes_written = 0U;
    if (connection.state() != mms::MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U || host.last_oper.empty()) return false;
    const std::array<std::uint8_t, 2U> opt{{0x04U, 0x00U}}; // dataRef
    const std::array<mms::MmsInformationReportReferenceInput, 1U> refs{{
        {host.manifest->domain, host.oper_item}}};
    const std::array<mms::MmsReadAccessResultInput, 1U> results{{
        {true, host.last_oper, 0U}}};
    mms::MmsInformationReportSnapshotInput report;
    report.report_id = "CommandTermination";
    report.optional_fields = opt;
    report.conf_revision = 1U;
    report.member_references = refs;
    report.member_results = results;
    report.reason_for_inclusion = 0x40U;
    const auto raw = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
        report, buffers.report_response);
    if (!raw.success()) return false;
    const auto p_data = ar::iec61850::osi::PresentationSpanCodec::encode_p_data_into(
        std::span<const std::uint8_t>{buffers.report_response.data(), raw.bytes_written},
        buffers.report_workspace,
        connection.mms_presentation_context_id(),
        true);
    if (!p_data.success()) return false;
    const auto cotp = ar::iec61850::osi::CotpSpanCodec::encode_data_into(
        std::span<const std::uint8_t>{buffers.report_workspace.data(), p_data.bytes_written},
        buffers.report_response);
    if (!cotp.success()) return false;
    const auto tpkt = ar::iec61850::osi::TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{buffers.report_response.data(), cotp.bytes_written},
        buffers.report_workspace);
    if (!tpkt.success()) return false;
    std::copy_n(buffers.report_workspace.begin(), tpkt.bytes_written,
                buffers.report_response.begin());
    bytes_written = tpkt.bytes_written;
    return true;
}

[[nodiscard]] bool poll_host_control_terminations(
    HostControlRuntime& controls,
    const mms::MmsStaticConnectionRuntime& connection,
    const embedded::TcpByteStream& stream,
    ConnectionBuffers& buffers,
    const std::uint64_t association_id,
    std::size_t& total_sent) {
    for (auto& host : controls.controls) {
        if (!host->termination_pending) continue;
        std::size_t bytes{};
        if (!encode_host_control_termination(connection, *host, buffers, bytes)) return false;
        if (!send_complete_report_frame(
                stream,
                std::span<const std::uint8_t>{buffers.report_response.data(), bytes},
                total_sent)) return false;
        const auto decision = host->planner->command_termination(
            control::ControlError::no_error, control::AddCause::none);
        if (decision.status != control::GuardedControlStatus::positive_termination) return false;
        host->termination_pending = false;
        std::cout << "IEDSIM_EVENT kind=control_termination association=" << association_id
                  << " object=" << host->manifest->domain << '/' << host->manifest->logical_node
                  << '.' << host->manifest->data_object
                  << " positive=true bytes=" << bytes << '\n';
        std::cout.flush();
        return true;
    }
    return true;
}

'''
s=repl(s,anchor,block+anchor,'host control runtime block')

# serve_connection parameter + termination send + cleanup
s=repl(s,
'''    HostBrcbReporting* const brcb_reporting,
    const std::uint64_t association_id,
''',
'''    HostBrcbReporting* const brcb_reporting,
    HostControlRuntime* const controls,
    const std::uint64_t association_id,
''','serve control param')
s=repl(s,
'''        if (brcb_reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&
            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&
            !poll_host_brcb_reports(
''',
'''        if (controls != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&
            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&
            !poll_host_control_terminations(
                *controls, runtime, stream, buffers, association_id, total_sent)) {
            host_control_association_closed(controls, association_id);
            reset_host_urcb_connection(reporting);
            runtime.close_transport();
            return;
        }
        if (brcb_reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&
            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&
            !poll_host_brcb_reports(
''','poll control termination')
s=repl(s,
'''        if (result.terminal()) {
            reset_host_urcb_connection(reporting);
''',
'''        if (result.terminal()) {
            host_control_association_closed(controls, association_id);
            reset_host_urcb_connection(reporting);
''','terminal control cleanup')
s=repl(s,
'''    reset_host_urcb_connection(reporting);
    runtime.close_transport();
}
''',
'''    host_control_association_closed(controls, association_id);
    reset_host_urcb_connection(reporting);
    runtime.close_transport();
}
''','final control cleanup')

# initialize controls after reporting before object table snapshot
s=repl(s,
'''        auto brcb_reporting = initialize_host_brcb_reporting(manifest_model);
        g_active_brcb_reporting = brcb_reporting.controls.empty() ? nullptr : &brcb_reporting;

        std::array<mms::MmsStaticObjectEntry, 13U> objects{};
''',
'''        auto brcb_reporting = initialize_host_brcb_reporting(manifest_model);
        g_active_brcb_reporting = brcb_reporting.controls.empty() ? nullptr : &brcb_reporting;
        auto control_runtime = initialize_host_controls(manifest_model);

        std::array<mms::MmsStaticObjectEntry, 13U> objects{};
''','initialize control runtime')
s=repl(s,
'''                      << " runtime=static-urcb+brcb-core" << '\\n';
''',
'''                      << " runtime=static-urcb+brcb-core" << '\\n';
            std::cout << "IEDSIM_EVENT kind=control_ready objects="
                      << control_runtime.controls.size()
                      << " runtime=static-direct+guarded-control" << '\\n';
''','control ready event')
s=repl(s,
'''                brcb_reporting.controls.empty() ? nullptr : &brcb_reporting,
                static_cast<std::uint64_t>(connection_count),
''',
'''                brcb_reporting.controls.empty() ? nullptr : &brcb_reporting,
                control_runtime.controls.empty() ? nullptr : &control_runtime,
                static_cast<std::uint64_t>(connection_count),
''','serve controls arg')
write(p,s)

# ---- CMake: make static direct control part of all simulator server cores --
for p in ['CMakeLists.txt','apps/ied_simulator/CMakeLists.txt','tools/static_ied_server_iedsim/CMakeLists.txt']:
    s=read(p)
    if 'src/mms/static_direct_control.cpp' in s or 'static_direct_control.cpp"' in s:
        continue
    if p == 'CMakeLists.txt':
        old='''    src/mms/static_dispatcher.cpp\n    src/mms/connection_runtime.cpp\n'''
        new='''    src/mms/static_dispatcher.cpp\n    src/mms/static_direct_control.cpp\n    src/mms/connection_runtime.cpp\n'''
    else:
        old='''    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/connection_runtime_iedsim.cpp\n''' if p.startswith('apps/') else '''    "${ARSTACK_ROOT}/src/mms/static_dispatcher.cpp"\n    "${ARSTACK_ROOT}/src/mms/connection_runtime_iedsim.cpp"\n'''
        new='''    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_direct_control.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/connection_runtime_iedsim.cpp\n''' if p.startswith('apps/') else '''    "${ARSTACK_ROOT}/src/mms/static_dispatcher.cpp"\n    "${ARSTACK_ROOT}/src/mms/static_direct_control.cpp"\n    "${ARSTACK_ROOT}/src/mms/connection_runtime_iedsim.cpp"\n'''
    s=repl(s,old,new,f'static direct source {p}')
    write(p,s)

print('P3 patch applied')

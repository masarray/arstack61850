// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsLiveTreeModel.hpp"
#include "MmsEngineeringEvidence.hpp"
#include "MmsSclClientProjection.hpp"
#include "BrowserReportMemberProjection.hpp"
#include "BrowserBoundedMmsPreview.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <chrono>

#include <iostream>
#include <string>

namespace {
ar::iec61850::mms::MmsLiveModelDocument makeLargeModel() {
    using namespace ar::iec61850::mms;
    MmsLiveModelDocument document;
    document.endpoint.host = "192.0.2.200";
    document.endpoint.port = 102;
    document.identity.ied_name = "P4_LARGE_IED";

    constexpr int ldCount = 5;
    constexpr int lnPerLd = 20;
    constexpr int doPerLn = 100;
    constexpr int daPerDo = 5;

    for (int ldIndex = 0; ldIndex < ldCount; ++ldIndex) {
        MmsLiveLogicalDevice ld;
        ld.mms_domain = "LD" + std::to_string(ldIndex);
        ld.instance = "LD" + std::to_string(ldIndex);

        for (int lnIndex = 0; lnIndex < lnPerLd; ++lnIndex) {
            MmsLiveLogicalNode ln;
            ln.name = "LLN" + std::to_string(lnIndex);

            for (int doIndex = 0; doIndex < doPerLn; ++doIndex) {
                MmsLiveDataObject object;
                object.name = "DO" + std::to_string(doIndex);
                object.reference = ld.mms_domain + "/" + ln.name + "." + object.name;

                for (int daIndex = 0; daIndex < daPerDo; ++daIndex) {
                    MmsLiveDataAttribute attribute;
                    attribute.attribute_path = object.name + ".DA" + std::to_string(daIndex);
                    attribute.object_reference = object.reference;
                    attribute.functional_constraint =
                        (ldIndex == 0 && lnIndex == 0 && doIndex == 0 && daIndex == 0)
                            ? "CO"
                            : (daIndex == 0 ? "ST" : "MX");
                    attribute.mms_item_name =
                        ln.name + "$" + attribute.functional_constraint + "$" +
                        object.name + "$DA" + std::to_string(daIndex);
                    attribute.mms_reference = ld.mms_domain + "/" + attribute.mms_item_name;
                    attribute.mms_type = "integer";
                    attribute.scl_basic_type = "INT32";
                    attribute.type_discovery_status = "Exact";
                    object.attributes.push_back(std::move(attribute));
                }
                ln.data_objects.push_back(std::move(object));
            }
            ld.logical_nodes.push_back(std::move(ln));
        }
        document.logical_devices.push_back(std::move(ld));
    }
    return document;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    MmsLiveTreeModel model;
    const auto document = makeLargeModel();

    QElapsedTimer applyTimer;
    applyTimer.start();
    model.applyDocument(document);
    const auto applyMs = applyTimer.elapsed();

    constexpr int expectedAttributes = 5 * 20 * 100 * 5;
    const int expectedMinimumNodes = expectedAttributes + 5 * 20 * 100 + 5 * 20 + 5 + 1;
    if (model.totalNodeCount() < expectedMinimumNodes) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL node_count=" << model.totalNodeCount() << "\n";
        return 2;
    }

    // Root and LDs are open, but LNs remain collapsed. Large online models must
    // not eagerly instantiate every DO/DA row into the visible projection.
    if (model.visibleNodeCount() > 256) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL eager_visible=" << model.visibleNodeCount() << "\n";
        return 3;
    }

    if (!model.selectRoot() || model.selectedChildCount() != 5 ||
        model.selectedChildren().size() != 5 || !model.selectChild(4) ||
        model.presentationNode().value(QStringLiteral("kind")).toString() != QStringLiteral("LD") ||
        model.selectedChildCount() != 20 || !model.selectChild(19) ||
        model.presentationNode().value(QStringLiteral("kind")).toString() != QStringLiteral("LN") ||
        model.selectedChildCount() != 100 || !model.selectChild(99) ||
        model.presentationNode().value(QStringLiteral("kind")).toString() != QStringLiteral("DO") ||
        model.selectedChildCount() != 5) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_drilldown_projection\n";
        return 12;
    }

    // Inline projection remains bounded and does not change canonical selection root.
    auto* detail = qobject_cast<MmsLiveDetailModel*>(model.detailModel());
    if (!detail || detail->rootKind() != QStringLiteral("DO") || detail->rowCount() != 5) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL inline_root\n";
        return 13;
    }
    model.selectRoot();
    if (detail->rowCount() != 5) return 14;
    detail->toggle(4);
    if (detail->rowCount() != 25 ||
        detail->data(detail->index(5), MmsLiveDetailModel::DepthRole).toInt() != 1) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL inline_expansion\n";
        return 15;
    }
    detail->toggle(4);
    if (detail->rowCount() != 5) return 16;
    detail->select(0);
    if (detail->rootKind() != QStringLiteral("IED") ||
        model.selectedNode().value(QStringLiteral("kind")).toString() != QStringLiteral("LD")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL selection_context\n";
        return 17;
    }

    QElapsedTimer filterTimer;
    filterTimer.start();
    model.setFilterText(QStringLiteral("LD4/LLN19.DO99"));
    const auto filterMs = filterTimer.elapsed();
    if (model.visibleNodeCount() <= 0 || model.visibleNodeCount() > 16) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL filtered_visible=" << model.visibleNodeCount() << "\n";
        return 4;
    }

    model.setFilterText(QString{});
    QElapsedTimer selectTimer;
    selectTimer.start();
    if (!model.selectMmsItem(
            QStringLiteral("LD4"),
            QStringLiteral("LLN19$MX$DO99$DA4"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL select_item\n";
        return 5;
    }
    const auto selectMs = selectTimer.elapsed();
    if (model.selectedRow() < 0 || model.visibleNodeCount() > 512) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL selected_projection="
                  << model.visibleNodeCount() << "\n";
        return 6;
    }

    if (!model.selectMmsItem(
            QStringLiteral("LD0"),
            QStringLiteral("LLN0$CO$DO0$DA0"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL control_candidate_select\n";
        return 9;
    }
    const auto controlNode = model.selectedNode();
    if (controlNode.value(QStringLiteral("controlCandidate")).toBool() != true ||
        controlNode.value(QStringLiteral("objectReference")).toString() !=
            QStringLiteral("LD0/LLN0.DO0")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL canonical_control_root\n";
        return 10;
    }

    const auto monitoredNode =
        model.nodeForReference(QStringLiteral("LD4/LLN19$MX$DO99$DA4"));
    if (monitoredNode.value(QStringLiteral("reference")).toString() !=
            QStringLiteral("LD4/LLN19$MX$DO99$DA4") ||
        monitoredNode.value(QStringLiteral("readable")).toBool() != true) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL canonical_reference_lookup\n";
        return 7;
    }

    // FC-qualified members use canonical reference indices rather than O(N)
    // linear scans per visible DataSet row. Expanding a DO is read-only.
    const auto doNode = model.nodeForReference(QStringLiteral("LD4/LLN19.DO99  [MX]"));
    const auto boundedChildren = model.dataSetChildrenForReference(
        QStringLiteral("LD4/LLN19.DO99"), QStringLiteral("MX"), 2);
    const auto missingFc = model.dataSetChildrenForReference(
        QStringLiteral("LD4/LLN19.DO99"), QStringLiteral("CF"), 64);
    const auto exactDa = model.nodeForReference(
        QStringLiteral("LD4/LLN19$MX$DO99$DA4  [MX]"));
    const auto iecDa = model.nodeForReference(
        QStringLiteral("LD4/LLN19.DO99.DA4  [MX]"));
    if (doNode.value(QStringLiteral("kind")).toString() != QStringLiteral("DO") ||
        boundedChildren.size() != 2 ||
        boundedChildren.constFirst().toMap()
            .value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("MX") ||
        !missingFc.isEmpty() ||
        exactDa.value(QStringLiteral("reference")).toString() !=
            QStringLiteral("LD4/LLN19$MX$DO99$DA4") ||
        iecDa.value(QStringLiteral("reference")).toString() !=
            QStringLiteral("LD4/LLN19$MX$DO99$DA4")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL dataset_reference_projection\n";
        return 18;
    }

    const QString liveKey = QStringLiteral("LD4") + QChar(0x1f) +
                            QStringLiteral("LLN19$MX$DO99$DA4");
    // ARSAS parity: discovered/readable is not the same as measured. An
    // empty-string MMS sample is still a real received value, not "Not read".
    if (model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4"))
            .value(QStringLiteral("hasReceivedValue")).toBool()) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL unread_sample_false_live\n";
        return 30;
    }
    model.applyReadValue(liveKey, QString{});
    if (!model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4"))
             .value(QStringLiteral("hasReceivedValue")).toBool()) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL empty_sample_received\n";
        return 31;
    }
    model.applyReadValue(liveKey, QStringLiteral("23"));
    if (model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4  [MX]"))
             .value(QStringLiteral("value")).toString() != QStringLiteral("23")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL dataset_alias_value\n";
        return 19;
    }

    QStringList monitoredReferences;
    for (int doIndex = 0; doIndex < 100; ++doIndex) {
        monitoredReferences.push_back(
            QStringLiteral("LD4/LLN19$MX$DO%1$DA4").arg(doIndex));
    }
    const auto boundedTargets =
        model.readTargetsForReferences(monitoredReferences, 64);
    if (boundedTargets.size() != 64) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL monitored_target_bound="
                  << boundedTargets.size() << "\n";
        return 8;
    }

    // Global Data must read true MMS targets even when a DataSet supplies the
    // IEC FCDA spelling rather than the MMS $FC$ spelling. Unknown FCs fail
    // closed. Duplicate aliases must not cause extra requests.
    const QStringList iecWatched{
        QStringLiteral("LD4/LLN19.DO99.DA4  [MX]"),
        QStringLiteral("LD4/LLN19$MX$DO99$DA4  [MX]"),
        QStringLiteral("LD4/LLN19.DO99.DA3  [MX]")};
    const auto readIec = model.readTargetsForReferences(iecWatched, 64);
    const auto readDo = model.readTargetsForReferences(
        {QStringLiteral("LD4/LLN19.DO99  [MX]")}, 64);
    const auto readInvalidFc = model.readTargetsForReferences(
        {QStringLiteral("LD4/LLN19.DO99.DA4  [ST]")}, 64);
    const auto readMmsFcd = model.readTargetsForReferences(
        {QStringLiteral("LD4/LLN19$MX$DO99")}, 64);
    const auto rejectMismatchedFcd = model.readTargetsForReferences(
        {QStringLiteral("LD4/LLN19$MX$DO99  [ST]")}, 64);
    if (readIec.size() != 2 || readDo.size() != 4 ||
        readMmsFcd.size() != 4 || !rejectMismatchedFcd.isEmpty() ||
        !readInvalidFc.isEmpty() ||
        readIec.constFirst().item != QStringLiteral("LLN19$MX$DO99$DA4") ||
        model.readTargetsForReferences(iecWatched, 1).size() != 1) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL canonical_global_watch_read\n";
        return 20;
    }

    // Decoding is based on actual typed MMS payload, never on displayed hex.
    // Quality: validity=invalid, Overflow=true, Source=substituted,
    // Test=true, OperatorBlocked=true. The unused bits must be exactly three.
    const std::array<std::uint8_t, 2> bits{0x60U, 0x38U};
    const auto qualityEvidence = arstack::iedsim::decodeEngineeringEvidence(
        QStringLiteral("LLN19$MX$DO99$q"),
        ar::iec61850::mms::MmsDataValue::bit_string(3U, bits));
    const auto qualityRows = qualityEvidence.value(QStringLiteral("rows")).toList();
    // RCB member references can use IEC LN.DO.q rather than MMS $FC$DO$q.
    // Both must use exactly the same typed decoder and return equal results.
    const auto iecQualityEvidence = arstack::iedsim::decodeEngineeringEvidence(
        QStringLiteral("LD4/LLN19.DO99.q"),
        ar::iec61850::mms::MmsDataValue::bit_string(3U, bits));
    if (iecQualityEvidence != qualityEvidence) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL report_iec_quality_alias\n";
        return 26;
    }
    const auto malformedQuality = arstack::iedsim::decodeEngineeringEvidence(
        QStringLiteral("LLN19$MX$DO99$q"),
        ar::iec61850::mms::MmsDataValue::bit_string(2U, bits));
    const auto wrongKind = arstack::iedsim::decodeEngineeringEvidence(
        QStringLiteral("LLN19$MX$DO99$q"),
        ar::iec61850::mms::MmsDataValue::boolean(true));
    if (qualityEvidence.value(QStringLiteral("kind")).toString() != QStringLiteral("quality") ||
        qualityRows.size() != 12 ||
        qualityRows.at(0).toMap().value(QStringLiteral("value")).toString() != QStringLiteral("invalid") ||
        !qualityRows.at(1).toMap().value(QStringLiteral("value")).toBool() ||
        qualityRows.at(9).toMap().value(QStringLiteral("value")).toString() != QStringLiteral("substituted") ||
        !qualityRows.at(10).toMap().value(QStringLiteral("value")).toBool() ||
        !qualityRows.at(11).toMap().value(QStringLiteral("value")).toBool() ||
        !malformedQuality.isEmpty() || !wrongKind.isEmpty()) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_quality_evidence\n";
        return 22;
    }

    const ar::iec61850::mms::Iec61850UtcTime utc{
        std::chrono::system_clock::time_point{std::chrono::seconds{1720000000}},
        0xBFU};  // leap known, clock not synchronized, accuracy unspecified
    const auto timeEvidence = arstack::iedsim::decodeEngineeringEvidence(
        QStringLiteral("LLN19$MX$DO99$t"),
        ar::iec61850::mms::MmsDataValue::utc_time(utc));
    const auto timeRows = timeEvidence.value(QStringLiteral("rows")).toList();
    if (timeEvidence.value(QStringLiteral("kind")).toString() != QStringLiteral("timestamp") ||
        timeRows.size() != 5 ||
        !timeRows.at(1).toMap().value(QStringLiteral("value")).toBool() ||
        timeRows.at(2).toMap().value(QStringLiteral("value")).toBool() ||
        !timeRows.at(3).toMap().value(QStringLiteral("value")).toBool() ||
        timeRows.at(4).toMap().value(QStringLiteral("value")).toString() != QStringLiteral("unspecified") ||
        !timeEvidence.value(QStringLiteral("summary")).toString().endsWith(QStringLiteral("UTC"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_timestamp_evidence\n";
        return 23;
    }

    // Model evidence is replaced by each new sample and never retained
    // after an untyped refresh. Reuse an existing canonical MMS leaf.
    model.applyReadValue(liveKey, QStringLiteral("typed"), qualityEvidence);
    if (model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4"))
            .value(QStringLiteral("engineeringEvidence")).toMap() != qualityEvidence) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL canonical_evidence_path\n";
        return 24;
    }
    model.applyReadValue(liveKey, QStringLiteral("typed"));
    if (!model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4"))
             .value(QStringLiteral("engineeringEvidence")).toMap().isEmpty()) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL stale_evidence\n";
        return 25;
    }

    // A successful SCL-assisted initial MMS Read already contains typed q/t;
    // never infer from display strings or fabricated post-connect values.
    {
        namespace mms = ar::iec61850::mms;
        namespace scl = ar::iec61850::scl;
        scl::SclDocument engineering;
        engineering.model_entries.reserve(5);
        const auto entry = [](const std::string& object, const std::string& attribute) {
            scl::SclDataSetEntry value;
            value.ied_name = "IED_TYPED";
            value.ld_inst = "LD0";
            value.ln_class = "XCBR";
            value.ln_inst = "1";
            value.do_name = object;
            value.da_name = attribute;
            value.functional_constraint = "ST";
            return value;
        };
        engineering.model_entries.push_back(entry("Pos", "q"));
        engineering.model_entries.push_back(entry("Pos", "t"));
        engineering.model_entries.push_back(entry("BadPos", "q"));
        engineering.model_entries.push_back(entry("WrongPos", "q"));
        engineering.model_entries.push_back(entry("Pos", "stVal"));

        mms::MmsSclAssistedConnectResult snapshot;
        snapshot.ied_name = "IED_TYPED";
        mms::MmsInitialFcBatchResult batch;
        mms::MmsInitialFcRootResult root;
        root.root_value = mms::MmsDataValue::boolean(true);
        const auto push = [&root](const std::size_t index, mms::MmsDataValue value) {
            mms::MmsSclMappedLeaf mapped;
            mapped.model_entry_index = index;
            mapped.value = std::move(value);
            root.mapped_leaves.push_back(std::move(mapped));
        };
        push(0U, mms::MmsDataValue::bit_string(3U, bits));
        push(1U, mms::MmsDataValue::utc_time(utc));
        push(2U, mms::MmsDataValue::bit_string(2U, bits));
        push(3U, mms::MmsDataValue::boolean(true));
        push(4U, mms::MmsDataValue::boolean(false));
        batch.roots.push_back(std::move(root));
        snapshot.batches.push_back(std::move(batch));
        snapshot.mapped_leaf_count = 5U;
        const auto mapped = arstack::iedsim::collect_scl_snapshot_values(engineering, snapshot);
        const auto mappedEvidence = [](const arstack::iedsim::SclSnapshotValue& value) {
            return value.typedSample
                ? arstack::iedsim::decodeEngineeringEvidence(
                    QString::fromStdString(value.item), *value.typedSample)
                : QVariantMap{};
        };
        if (mapped.size() != 5U ||
            !mapped.at(0).typedSample || !mapped.at(1).typedSample ||
            mappedEvidence(mapped.at(0)) != qualityEvidence ||
            mappedEvidence(mapped.at(1)) != timeEvidence ||
            !mappedEvidence(mapped.at(2)).isEmpty() ||
            !mappedEvidence(mapped.at(3)).isEmpty() ||
            mapped.at(4).typedSample ||
            mapped.at(0).item != "XCBR1$ST$Pos$q" ||
            mapped.at(1).item != "XCBR1$ST$Pos$t") {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL trusted_scl_initial_typed_evidence\n";
            return 27;
        }
        // Bound typed initial-snapshot copies and decoded GUI maps at 96,
        // independently of the number of successfully mapped values.
        mms::MmsSclAssistedConnectResult largeSnapshot;
        largeSnapshot.ied_name = "IED_TYPED";
        mms::MmsInitialFcBatchResult largeBatch;
        mms::MmsInitialFcRootResult largeRoot;
        largeRoot.root_value = mms::MmsDataValue::boolean(true);
        const auto qEntry = engineering.model_entries.at(0);
        engineering.model_entries.resize(150, qEntry);
        for (std::size_t i = 0; i < 150U; ++i) {
            mms::MmsSclMappedLeaf leaf;
            leaf.model_entry_index = i;
            leaf.value = mms::MmsDataValue::bit_string(3U, bits);
            largeRoot.mapped_leaves.push_back(std::move(leaf));
        }
        largeBatch.roots.push_back(std::move(largeRoot));
        largeSnapshot.batches.push_back(std::move(largeBatch));
        largeSnapshot.mapped_leaf_count = 150U;
        const auto bounded = arstack::iedsim::collect_scl_snapshot_values(
            engineering, largeSnapshot);
        std::size_t typedVisible{};
        std::size_t eligibleOrdinal{};
        bool ordinalMismatch{};
        for (const auto& item : bounded) {
            const bool eligible = arstack::iedsim::isSclTypedEvidenceLeaf(item.item);
            const bool expectedTyped = eligible &&
                eligibleOrdinal < arstack::iedsim::kMaximumSclInitialTypedEvidence;
            if (static_cast<bool>(item.typedSample) != expectedTyped)
                ordinalMismatch = true;
            if (item.typedSample) ++typedVisible;
            if (eligible) ++eligibleOrdinal;
        }
        // Scalar leaves do not consume the q/t budget; assert on candidate
        // ordinal, not an assumed fixed position in a mixed FC-root batch.
        if (bounded.size() != 150U ||
            eligibleOrdinal != 149U ||
            typedVisible != arstack::iedsim::kMaximumSclInitialTypedEvidence ||
            ordinalMismatch || bounded.back().display.empty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL initial_qt_evidence_budget\n";
            return 29;
        }

        // A failed FC-root Read must never populate GUI evidence.
        snapshot.batches.front().roots.front().access_failure_code = 1U;
        if (!arstack::iedsim::collect_scl_snapshot_values(engineering, snapshot).empty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL failed_root_evidence\n";
            return 28;
        }
    }

    // Project 300 decoded report members but allocate only 256 GUI rows.
    // A shared one-item evidence budget prevents a q/t storm from allocating
    // a detailed map per member or per retained report frame.
    std::vector<ar::iec61850::mms::MmsReportValue> largeFrame(300);
    for (std::size_t index = 0; index < largeFrame.size(); ++index) {
        largeFrame[index].data_set_index = index;
        largeFrame[index].data_reference = "LD4/LLN19.DO99.stVal";
        largeFrame[index].value = ar::iec61850::mms::MmsDataValue::boolean(true);
    }
    largeFrame[0].data_reference = "LD4/LLN19.DO99.q";
    largeFrame[0].value = ar::iec61850::mms::MmsDataValue::bit_string(3U, bits);
    largeFrame[1].data_reference = "LD4/LLN19.DO99.t";
    largeFrame[1].value = ar::iec61850::mms::MmsDataValue::utc_time(utc);
    largeFrame[2].value.reset();   // An unsuccessful member has no typed evidence
    largeFrame[3].data_reference = "LD4/LLN19.DO99.q";
    largeFrame[3].value = ar::iec61850::mms::MmsDataValue::boolean(false); // Wrong kind
    largeFrame[4].data_reference = std::string(2048, 'X'); // Bounded preview
    int typedBudget = 1;
    const auto projected = arstack::iedsim::projectBrowserReportMembers(
        largeFrame, typedBudget);
    if (projected.values.size() != 256 ||
        projected.evidence.size() != 1 ||
        projected.evidence.constFirst().toMap().value(QStringLiteral("index")).toInt() != 0 ||
        typedBudget != 0 ||
        !projected.rowsTruncated || !projected.textTruncated ||
        !projected.evidenceTruncated ||
        !projected.values.at(2).contains(QStringLiteral("<failed>")) ||
        projected.values.at(4).size() > 1100) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_report_projection\n";
        return 27;
    }
    int smallBudget = 2;
    const auto smallProjection = arstack::iedsim::projectBrowserReportMembers(
        largeFrame, smallBudget, 3);
    if (smallProjection.values.size() != 3 ||
        smallProjection.evidence.size() != 2 ||
        smallBudget != 0 || !smallProjection.rowsTruncated ||
        smallProjection.evidenceTruncated) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_report_small_window\n";
        return 28;
    }

    // P0: transport order becomes IEC attribute identity only after an exact
    // LN-root GVAA TypeSpecification matches every nested kind/cardinality.
    // No ARIEC61850 renderer child-count guessing and no fake q/t.
    {
        namespace mms = ar::iec61850::mms;
        const auto typed = [](mms::MmsTypeKind kind, std::string name,
                              std::vector<mms::MmsTypeSpecification> children = {}) {
            mms::MmsTypeSpecification node;
            node.kind = kind;
            node.name = std::move(name);
            node.children = std::move(children);
            return node;
        };
        mms::MmsVariableTypeEvidence evidence;
        evidence.variable = mms::MmsObjectName::domain_specific("LD4", "XCBR1");
        evidence.attributes.emplace();
        evidence.attributes->type = typed(mms::MmsTypeKind::structure, "XCBR1", {
            typed(mms::MmsTypeKind::structure, "ST", {
                typed(mms::MmsTypeKind::structure, "Pos", {
                    typed(mms::MmsTypeKind::bit_string, "stVal"),
                    typed(mms::MmsTypeKind::bit_string, "q"),
                    typed(mms::MmsTypeKind::utc_time, "t")
                })
            })
        });
        const auto typeIndex = arstack::iedsim::verified_report_detail::indexTypes(
            {evidence});
        mms::MmsReportValue reportMember;
        reportMember.member.emplace();
        reportMember.member->object_name =
            mms::MmsObjectName::domain_specific("LD4", "XCBR1$ST$Pos");
        reportMember.member->functional_constraint = "ST";
        reportMember.data_reference = "LD4/XCBR1.Pos";
        const std::array<std::uint8_t, 1U> dbposBytes{0x80U};
        reportMember.value = mms::MmsDataValue::structure({
            mms::MmsDataValue::bit_string(6U, dbposBytes),
            mms::MmsDataValue::bit_string(3U, bits),
            mms::MmsDataValue::utc_time(utc)
        });
        auto reportTypes = arstack::iedsim::verified_report_detail::project(
            reportMember, typeIndex);
        int semanticBudget = 4;
        const auto semantic = arstack::iedsim::projectBrowserReportMembers(
            {reportMember}, semanticBudget, 8, true, &typeIndex);
        const auto details = semantic.details.constFirst().toMap();
        if (reportTypes.size() != 3 || semantic.details.size() != 1 ||
            !details.value(QStringLiteral("typed")).toBool() ||
            !semantic.values.constFirst().contains(QStringLiteral("verified IEC")) ||
            reportTypes.at(0).toMap().value(QStringLiteral("label")).toString() !=
                QStringLiteral("stVal") ||
            reportTypes.at(1).toMap().value(QStringLiteral("label")).toString() !=
                QStringLiteral("q") ||
            reportTypes.at(1).toMap().value(QStringLiteral("engineeringEvidence"))
                .toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("quality") ||
            reportTypes.at(2).toMap().value(QStringLiteral("engineeringEvidence"))
                .toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("timestamp")) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL verified_semantic_report_type\n";
            return 91;
        }
        // Wrong cardinality, data kind, device/domain and duplicate GVAA
        // authority all fail closed without promoting Field N to semantic DA.
        auto mismatch = reportMember;
        mismatch.value = mms::MmsDataValue::structure({
            mms::MmsDataValue::boolean(true),
            mms::MmsDataValue::bit_string(3U, bits)
        });
        auto stale = reportMember;
        stale.member->object_name =
            mms::MmsObjectName::domain_specific("LD_WRONG", "XCBR1$ST$Pos");
        const auto ambiguous = arstack::iedsim::verified_report_detail::indexTypes(
            {evidence, evidence});
        if (!arstack::iedsim::verified_report_detail::project(mismatch, typeIndex).isEmpty() ||
            !arstack::iedsim::verified_report_detail::project(stale, typeIndex).isEmpty() ||
            !arstack::iedsim::verified_report_detail::project(reportMember, ambiguous).isEmpty() ||
            !arstack::iedsim::verified_report_detail::project(
                reportMember, arstack::iedsim::verified_report_detail::WireTypeIndex{}).isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL semantic_report_fallback\n";
            return 92;
        }
        int fallbackBudget = 4;
        const auto fallback = arstack::iedsim::projectBrowserReportMembers(
            {mismatch}, fallbackBudget, 8, true, &typeIndex);
        if (fallback.details.size() != 1 ||
            fallback.details.constFirst().toMap().value(QStringLiteral("typed")).toBool() ||
            fallback.values.constFirst().contains(QStringLiteral("verified IEC"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL semantic_mismatch_claim\n";
            return 93;
        }
    }

    using ar::iec61850::mms::MmsDataCodec;
    using ar::iec61850::mms::MmsDataValue;
    // M2F: the GUI must not allocate multi-megabyte intermediary strings
    // for a tiny report preview. Primitive display remains codec-identical.
    const std::string hugeText(2'000'000, 'V');
    const auto hugeVisible = MmsDataValue::visible_string(hugeText);
    const auto hugePreview = arstack::iedsim::previewMmsValue(hugeVisible, 1024);
    const std::vector<std::uint8_t> hugeRawBytes(1'000'000, 0xABU);
    const auto hugeBytes = MmsDataValue::octet_string(hugeRawBytes);
    const auto hexPreview = arstack::iedsim::previewMmsValue(hugeBytes, 1024);
    std::vector<MmsDataValue> children;
    children.reserve(4000);
    for (int index = 0; index < 4000; ++index)
        children.push_back(MmsDataValue::visible_string(std::string(300, 'C')));
    const auto nested = MmsDataValue::array(std::move(children));
    const auto nestedPreview = arstack::iedsim::previewMmsValue(nested, 1024);
    if (hugePreview.text.size() != 1024 || !hugePreview.truncated ||
        hugePreview.text.front() != 'V' || hugePreview.text.back() != 'V' ||
        hexPreview.text.size() != 1024 || !hexPreview.truncated ||
        hexPreview.text.substr(0, 4) != "ABAB" ||
        nestedPreview.text.size() != 1024 || !nestedPreview.truncated ||
        nestedPreview.text.front() != '[') {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_value_serialization\n";
        return 29;
    }
    // Exact semantics for complete output, including nested structures,
    // raw-byte uppercase hex, bool and UTC types.
    const std::vector<MmsDataValue> samples{
        MmsDataValue::boolean(true),
        MmsDataValue::integer(-42),
        MmsDataValue::floating_point(3.125),
        MmsDataValue::bit_string(3U, bits),
        MmsDataValue::utc_time(utc),
        MmsDataValue::structure({MmsDataValue::boolean(false),
                                 MmsDataValue::visible_string("ABC")})};
    for (const auto& sample : samples) {
        const auto preview = arstack::iedsim::previewMmsValue(sample, 1024);
        if (preview.truncated ||
            preview.text != MmsDataCodec::to_display_string(sample)) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_value_parity\n";
            return 30;
        }
    }
    const auto zeroBudget = arstack::iedsim::previewMmsValue(hugeVisible, 0);
    if (!zeroBudget.text.empty() || !zeroBudget.truncated) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_value_zero_budget\n";
        return 31;
    }
    // A bounded MMSString must not end on an incomplete UTF-8 character.
    const std::string utf8Boundary = std::string(1023, 'A') + "\xE2\x82\xAC";
    const auto utf8Preview = arstack::iedsim::previewMmsValue(
        MmsDataValue::mms_string(utf8Boundary), 1024);
    if (!utf8Preview.truncated || utf8Preview.text.size() != 1023 ||
        utf8Preview.text.back() != 'A') {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_value_utf8_boundary\n";
        return 33;
    }
    largeFrame[5].value = hugeVisible;
    int hugeBudget = 4;
    const auto boundedFrame = arstack::iedsim::projectBrowserReportMembers(
        largeFrame, hugeBudget, 6);
    if (boundedFrame.values.size() != 6 || !boundedFrame.textTruncated ||
        boundedFrame.values.at(5).size() > 1100) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_value_report_integration\n";
        return 32;
    }

    using ar::iec61850::mms::MmsReportValue;
    // M2G: bounded value formatting alone is insufficient if an IED returns
    // oversized IEC member references or Reason-for-Inclusion names.
    std::vector<MmsReportValue> giantMetadata(4);
    giantMetadata[0].data_reference = std::string(1'500'000, 'R');
    giantMetadata[0].value = MmsDataValue::boolean(true);
    giantMetadata[1].data_reference = "LD4/LLN19.DO99.stVal";
    giantMetadata[1].value = MmsDataValue::boolean(false);
    giantMetadata[1].reason_for_inclusion.names.push_back(std::string(1'500'000, 'N'));
    giantMetadata[2].data_reference = std::string(1'500'000, 'R') + ".q";
    giantMetadata[2].value = MmsDataValue::bit_string(3U, bits);
    giantMetadata[3].data_reference = "LD4/LLN19.DO99.stVal";
    giantMetadata[3].value = MmsDataValue::boolean(true);
    giantMetadata[3].reason_for_inclusion.names.assign(1500, "data-change");
    int metadataBudget = 8;
    const auto safeMetadata = arstack::iedsim::projectBrowserReportMembers(
        giantMetadata, metadataBudget);
    if (safeMetadata.values.size() != 4 || !safeMetadata.textTruncated ||
        !safeMetadata.evidenceTruncated || !safeMetadata.evidence.isEmpty() ||
        metadataBudget != 8 ||
        safeMetadata.values.at(0).size() > 1024 ||
        safeMetadata.values.at(1).size() > 1024 ||
        safeMetadata.values.at(2).size() > 1024 ||
        safeMetadata.values.at(3).size() > 1024 ||
        !safeMetadata.values.at(0).contains(QStringLiteral("preview truncated")) ||
        !safeMetadata.values.at(1).contains(QStringLiteral("preview truncated")) ||
        !safeMetadata.values.at(3).contains(QStringLiteral("preview truncated"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL bounded_report_metadata\n";
        return 34;
    }
    // Normal IEC references remain exact so q/t evidence can still be
    // associated with the original received member ordinal.
    std::vector<MmsReportValue> normalMetadata(1);
    normalMetadata[0].data_reference = "LD4/LLN19.DO99.q";
    normalMetadata[0].value = MmsDataValue::bit_string(3U, bits);
    const auto normalProjected = arstack::iedsim::projectBrowserReportMembers(
        normalMetadata, metadataBudget);
    if (normalProjected.evidence.size() != 1 ||
        normalProjected.evidence.constFirst().toMap()
            .value(QStringLiteral("index")).toInt() != 0 ||
        normalProjected.textTruncated) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL report_metadata_provenance\n";
        return 35;
    }


    // M2L: prove scalar-vs-vector output comes from exact CDC + MX + received
    // typed numeric DAs, never guessing an angle or engineering unit.
    {
        using namespace ar::iec61850::mms;
        MmsLiveModelDocument engineeringModel;
        engineeringModel.identity.ied_name = "M2L_TYPED";
        MmsLiveLogicalDevice device;
        device.mms_domain = "LD_M2L";
        device.instance = "LD_M2L";
        MmsLiveLogicalNode ln;
        ln.name = "MMXU1";
        ln.logical_node_class = "MMXU";
        const auto addObject = [&](const std::string& name, const std::string& cdc,
                                   MmsLiveModelConfidence confidence)
            -> MmsLiveDataObject& {
            ln.data_objects.emplace_back();
            auto& object = ln.data_objects.back();
            object.name = name;
            object.reference = "LD_M2L/MMXU1." + name;
            object.inferred_cdc = cdc;
            object.confidence = confidence;
            return object;
        };
        const auto addAttribute = [](MmsLiveDataObject& object,
                                     const std::string& path,
                                     const std::string& fc,
                                     const std::string& type) {
            MmsLiveDataAttribute attribute;
            attribute.attribute_path = object.name + "." + path;
            attribute.object_reference = object.reference;
            attribute.functional_constraint = fc;
            std::string itemPath = path;
            std::replace(itemPath.begin(), itemPath.end(), '.', '$');
            attribute.mms_item_name = "MMXU1$" + fc + "$" + object.name + "$" + itemPath;
            attribute.mms_reference = "LD_M2L/" + attribute.mms_item_name;
            attribute.mms_type = type;
            attribute.type_discovery_status = "Exact";
            object.attributes.push_back(std::move(attribute));
        };
        auto& phasor = addObject("PhV", "CMV", MmsLiveModelConfidence::exact);
        addAttribute(phasor, "cVal.mag.f", "MX", "floating-point");
        addAttribute(phasor, "cVal.ang.f", "MX", "floating-point");
        addAttribute(phasor, "q", "ST", "bit-string"); // wrong FC must never leak
        addAttribute(phasor, "q", "MX", "bit-string");
        addAttribute(phasor, "t", "MX", "utc-time");
        auto& integerVector = addObject("PhVInteger", "CMV", MmsLiveModelConfidence::exact);
        addAttribute(integerVector, "cVal.mag.i", "MX", "integer");
        addAttribute(integerVector, "cVal.ang.i", "MX", "integer");
        auto& integerSav = addObject("SavInteger", "SAV", MmsLiveModelConfidence::exact);
        addAttribute(integerSav, "instMag.i", "MX", "integer");
        for (const std::string name : {"Hz", "TotW"}) {
            auto& scalar = addObject(name, "MV", MmsLiveModelConfidence::exact);
            addAttribute(scalar, "mag.f", "MX", "floating-point");
            addAttribute(scalar, "ang.f", "MX", "floating-point"); // spurious, not CMV
        }
        auto& status = addObject("Health", "SPS", MmsLiveModelConfidence::exact);
        addAttribute(status, "stVal", "ST", "boolean");
        auto& uncertain = addObject("EstimatedPhV", "CMV", MmsLiveModelConfidence::low);
        addAttribute(uncertain, "cVal.mag.f", "MX", "floating-point");
        addAttribute(uncertain, "cVal.ang.f", "MX", "floating-point");
        device.logical_nodes.push_back(std::move(ln));
        engineeringModel.logical_devices.push_back(std::move(device));
        MmsLiveTreeModel measurementModel;
        measurementModel.applyDocument(engineeringModel);
        const auto ref = [](const QString& name) {
            return QStringLiteral("LD_M2L/MMXU1.") + name;
        };
        const auto received = [&measurementModel](const QString& item,
                                                   const QString& display) {
            measurementModel.applyReadValue(
                QStringLiteral("LD_M2L") + QChar(0x1f) + item, display);
        };
        if (!measurementModel.nodeForReference(ref(QStringLiteral("PhV")))
                 .value(QStringLiteral("measurement")).toMap().isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_unread_vector\n";
            return 36;
        }
        received(QStringLiteral("MMXU1$MX$PhV$cVal$mag$f"), QStringLiteral("115.0"));
        const auto partial = measurementModel.nodeForReference(ref(QStringLiteral("PhV")))
            .value(QStringLiteral("measurement")).toMap();
        if (partial.value(QStringLiteral("kind")).toString() != QStringLiteral("vector") ||
            partial.value(QStringLiteral("magnitude")).toString() != QStringLiteral("115.0") ||
            partial.contains(QStringLiteral("angle")) ||
            partial.contains(QStringLiteral("unit"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_partial_phasor\n";
            return 37;
        }
        received(QStringLiteral("MMXU1$MX$PhV$cVal$ang$f"), QStringLiteral("not-number"));
        if (measurementModel.nodeForReference(ref(QStringLiteral("PhV")))
                .value(QStringLiteral("measurement")).toMap().contains(QStringLiteral("angle"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_malformed_angle\n";
            return 38;
        }
        received(QStringLiteral("MMXU1$MX$PhV$cVal$ang$f"), QStringLiteral("30.0"));
        const auto vector = measurementModel.nodeForReference(ref(QStringLiteral("PhV")))
            .value(QStringLiteral("measurement")).toMap();
        if (vector.value(QStringLiteral("angle")).toString() != QStringLiteral("30.0") ||
            !vector.value(QStringLiteral("angleSourceReference")).toString()
                     .contains(QStringLiteral("cVal$ang$f"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_valid_phasor\n";
            return 39;
        }
        const auto selectedRef = ref(QStringLiteral("PhV.cVal.mag.f"));
        received(QStringLiteral("MMXU1$ST$PhV$q"), QStringLiteral("ST_Q"));
        if (!measurementModel.nodeForReference(selectedRef)
                 .value(QStringLiteral("quality")).toString().isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_cross_fc_quality\n";
            return 40;
        }
        received(QStringLiteral("MMXU1$MX$PhV$q"), QStringLiteral("MX_Q"));
        received(QStringLiteral("MMXU1$MX$PhV$t"), QStringLiteral("IED_UTC"));
        const auto observed = measurementModel.nodeForReference(selectedRef);
        if (observed.value(QStringLiteral("quality")).toString() != QStringLiteral("MX_Q") ||
            observed.value(QStringLiteral("timestamp")).toString() != QStringLiteral("IED_UTC") ||
            observed.value(QStringLiteral("measurement")).toMap() != vector) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_companion_provenance\n";
            return 41;
        }
        for (const QString name : {QStringLiteral("Hz"), QStringLiteral("TotW")}) {
            const auto item = QStringLiteral("MMXU1$MX$") + name;
            received(item + QStringLiteral("$mag$f"), QStringLiteral("50.25"));
            received(item + QStringLiteral("$ang$f"), QStringLiteral("90"));
            const auto scalar = measurementModel.nodeForReference(ref(name))
                .value(QStringLiteral("measurement")).toMap();
            if (scalar.value(QStringLiteral("kind")).toString() != QStringLiteral("scalar") ||
                scalar.value(QStringLiteral("magnitude")).toString() != QStringLiteral("50.25") ||
                scalar.contains(QStringLiteral("angle")) || scalar.contains(QStringLiteral("unit"))) {
                std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_scalar_false_angle\n";
                return 42;
            }
        }
        received(QStringLiteral("MMXU1$MX$PhVInteger$cVal$mag$i"), QStringLiteral("127"));
        received(QStringLiteral("MMXU1$MX$PhVInteger$cVal$ang$i"), QStringLiteral("-15"));
        const auto integerPhasor = measurementModel.nodeForReference(
            ref(QStringLiteral("PhVInteger")))
                .value(QStringLiteral("measurement")).toMap();
        if (integerPhasor.value(QStringLiteral("kind")).toString() != QStringLiteral("vector") ||
            integerPhasor.value(QStringLiteral("magnitude")).toString() != QStringLiteral("127") ||
            integerPhasor.value(QStringLiteral("angle")).toString() != QStringLiteral("-15")) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_integer_analogue_vector\n";
            return 48;
        }
        received(QStringLiteral("MMXU1$MX$SavInteger$instMag$i"), QStringLiteral("42"));
        const auto integerScalar = measurementModel.nodeForReference(
            ref(QStringLiteral("SavInteger")))
                .value(QStringLiteral("measurement")).toMap();
        if (integerScalar.value(QStringLiteral("kind")).toString() != QStringLiteral("scalar") ||
            integerScalar.value(QStringLiteral("magnitude")).toString() != QStringLiteral("42") ||
            integerScalar.contains(QStringLiteral("angle")) ||
            integerScalar.contains(QStringLiteral("unit"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_integer_analogue_scalar\n";
            return 49;
        }
        received(QStringLiteral("MMXU1$ST$Health$stVal"), QStringLiteral("true"));
        const auto sourcedStatus = measurementModel.nodeForReference(
            ref(QStringLiteral("Health")));
        if (sourcedStatus.value(QStringLiteral("value")).toString() != QStringLiteral("true") ||
            !sourcedStatus.value(QStringLiteral("hasReceivedValue")).toBool() ||
            !sourcedStatus.value(QStringLiteral("valueSourceReference")).toString()
                 .contains(QStringLiteral("Health$stVal"))) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_do_value_source\n";
            return 46;
        }
        // A successful empty-string sample stays received and source-backed,
        // not a fabricated NOT READ or a stale previous nonempty value.
        received(QStringLiteral("MMXU1$ST$Health$stVal"), QString{});
        const auto receivedEmptyStatus = measurementModel.nodeForReference(
            ref(QStringLiteral("Health")));
        if (!receivedEmptyStatus.value(QStringLiteral("hasReceivedValue")).toBool() ||
            !receivedEmptyStatus.value(QStringLiteral("value")).toString().isEmpty() ||
            receivedEmptyStatus.value(QStringLiteral("valueSourceReference")).toString().isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_do_received_empty\n";
            return 47;
        }
        if (!measurementModel.nodeForReference(ref(QStringLiteral("Health")))
                .value(QStringLiteral("measurement")).toMap().isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_boolean_false_magnitude\n";
            return 43;
        }
        received(QStringLiteral("MMXU1$MX$EstimatedPhV$cVal$mag$f"), QStringLiteral("100"));
        received(QStringLiteral("MMXU1$MX$EstimatedPhV$cVal$ang$f"), QStringLiteral("20"));
        if (!measurementModel.nodeForReference(ref(QStringLiteral("EstimatedPhV")))
                .value(QStringLiteral("measurement")).toMap().isEmpty()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_uncertain_cdc\n";
            return 44;
        }
        measurementModel.applyDocument(engineeringModel);
        if (!measurementModel.nodeForReference(ref(QStringLiteral("PhV")))
                .value(QStringLiteral("measurement")).toMap().isEmpty() ||
            measurementModel.nodeForReference(selectedRef)
                .value(QStringLiteral("hasReceivedValue")).toBool()) {
            std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL m2l_source_replacement_stale\n";
            return 45;
        }
    }

    const bool passes = applyMs <= 5000 && filterMs <= 1500 && selectMs <= 500;
    std::cout << "IED_BROWSER_LARGE_MODEL_" << (passes ? "PASS" : "FAIL")
              << " nodes=" << model.totalNodeCount()
              << " visible=" << model.visibleNodeCount()
              << " apply_ms=" << applyMs
              << " filter_ms=" << filterMs
              << " select_ms=" << selectMs
              << " eager_expansion=false drilldown=canonical inline_projection=bounded dataset_lookup=indexed\n";
    return passes ? 0 : 11;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsLiveTreeModel.hpp"
#include "MmsSemanticValue.hpp"
#include <array>
#include <chrono>

#include <QCoreApplication>
#include <QElapsedTimer>

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

    // Typed IEC 61850 Quality: exactly 13 bits. 01=invalid;
    // substituted/test/operatorBlocked are independently asserted.
    const std::array<std::uint8_t, 2> qualityBytes{0x40U, 0x38U};
    const auto quality = arstack::iedsim::decodeMmsSemanticAttribute(
        ar::iec61850::mms::MmsDataValue::bit_string(3U, qualityBytes),
        QStringLiteral("LLN0$ST$Pos$q"));
    const auto malformed = arstack::iedsim::decodeMmsSemanticAttribute(
        ar::iec61850::mms::MmsDataValue::bit_string(0U, qualityBytes),
        QStringLiteral("LLN0$ST$Pos$q"));
    const auto wrongLeaf = arstack::iedsim::decodeMmsSemanticAttribute(
        ar::iec61850::mms::MmsDataValue::bit_string(3U, qualityBytes),
        QStringLiteral("LLN0$ST$Pos$stVal"));
    const QVariantList qualityFields = quality.value(QStringLiteral("fields")).toList();
    if (quality.value(QStringLiteral("summary")).toString() != QStringLiteral("invalid") ||
        qualityFields.size() != 12 ||
        qualityFields.at(9).toMap().value(QStringLiteral("value")).toString() !=
            QStringLiteral("substituted") ||
        !qualityFields.at(10).toMap().value(QStringLiteral("value")).toBool() ||
        !qualityFields.at(11).toMap().value(QStringLiteral("value")).toBool() ||
        !malformed.isEmpty() || !wrongLeaf.isEmpty()) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_quality_semantics\n";
        return 24;
    }
    const ar::iec61850::mms::Iec61850UtcTime utc{
        std::chrono::system_clock::time_point{std::chrono::seconds{1'730'000'000}},
        0xA5U};
    const auto time = arstack::iedsim::decodeMmsSemanticAttribute(
        ar::iec61850::mms::MmsDataValue::utc_time(utc),
        QStringLiteral("LLN0$ST$Pos$t"));
    const auto timeFields = time.value(QStringLiteral("fields")).toList();
    if (timeFields.size() != 5 ||
        !timeFields.at(1).toMap().value(QStringLiteral("value")).toBool() ||
        timeFields.at(2).toMap().value(QStringLiteral("value")).toBool() ||
        !timeFields.at(3).toMap().value(QStringLiteral("value")).toBool() ||
        timeFields.at(4).toMap().value(QStringLiteral("value")).toString() != QStringLiteral("5") ||
        !time.value(QStringLiteral("summary")).toString().contains(QStringLiteral("UTC"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_time_quality\n";
        return 25;
    }

    constexpr int expectedAttributes = 5 * 20 * 100 * 5;
    const int expectedMinimumNodes = expectedAttributes + 5 * 20 * 100 + 5 * 20 + 5 + 1;
    if (model.totalNodeCount() < expectedMinimumNodes) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL node_count=" << model.totalNodeCount() << "\n";
        return 2;
    }

    // Metadata is initially unknown and must not be fabricated.
    const auto beforeQuality = model.nodeForReference(QStringLiteral("LD4/LLN19.DO99.DA4 [MX]"));
    if (beforeQuality.contains(QStringLiteral("qualityDetails")) ||
        beforeQuality.contains(QStringLiteral("timestampDetails"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL invented_quality\n";
        return 26;
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

    // Sparse canonical metadata must be shared by all siblings and cleared
    // when a new IED document replaces the previous one.
    ar::iec61850::mms::MmsLiveModelDocument small;
    ar::iec61850::mms::MmsLiveLogicalDevice sd;
    sd.mms_domain = "LD0"; sd.instance = "LD0";
    ar::iec61850::mms::MmsLiveLogicalNode sn;
    sn.name = "LLN0";
    ar::iec61850::mms::MmsLiveDataObject so;
    so.name = "Pos"; so.reference = "LD0/LLN0.Pos";
    for (const auto& leaf : {"stVal", "q", "t"}) {
        ar::iec61850::mms::MmsLiveDataAttribute da;
        da.attribute_path = std::string("Pos.") + leaf;
        da.object_reference = so.reference;
        da.functional_constraint = "ST";
        da.mms_item_name = std::string("LLN0$ST$Pos$") + leaf;
        da.mms_reference = std::string("LD0/") + da.mms_item_name;
        da.mms_type = std::string(leaf) == "t" ? "utc-time"
                     : std::string(leaf) == "q" ? "bit-string" : "integer";
        da.type_discovery_status = "Exact";
        so.attributes.push_back(std::move(da));
    }
    sn.data_objects.push_back(std::move(so));
    sd.logical_nodes.push_back(std::move(sn));
    small.logical_devices.push_back(std::move(sd));
    MmsLiveTreeModel typedTree;
    typedTree.applyDocument(small);
    const QString keyBase = QStringLiteral("LD0") + QChar(0x1f) + QStringLiteral("LLN0$ST$Pos$");
    typedTree.applyReadValue(keyBase + QStringLiteral("q"), QStringLiteral("034038"), quality);
    typedTree.applyReadValue(keyBase + QStringLiteral("t"), QStringLiteral("UTC time"), time);
    const auto sibling = typedTree.nodeForReference(QStringLiteral("LD0/LLN0.Pos.stVal [ST]"));
    const auto typedQ = sibling.value(QStringLiteral("qualityDetails")).toMap();
    const auto typedT = sibling.value(QStringLiteral("timestampDetails")).toMap();
    if (typedQ.value(QStringLiteral("summary")).toString() != QStringLiteral("invalid") ||
        typedT.value(QStringLiteral("kind")).toString() != QStringLiteral("timestamp")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_canonical_siblings\n";
        return 27;
    }
    typedTree.applyDocument(small);
    if (typedTree.nodeForReference(QStringLiteral("LD0/LLN0.Pos.q [ST]"))
            .contains(QStringLiteral("qualityDetails"))) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL typed_stale_document\n";
        return 28;
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

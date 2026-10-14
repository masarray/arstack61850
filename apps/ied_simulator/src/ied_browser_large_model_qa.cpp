// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsLiveTreeModel.hpp"

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
    if (doNode.value(QStringLiteral("kind")).toString() != QStringLiteral("DO") ||
        boundedChildren.size() != 2 ||
        boundedChildren.constFirst().toMap()
            .value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("MX") ||
        !missingFc.isEmpty() ||
        exactDa.value(QStringLiteral("reference")).toString() !=
            QStringLiteral("LD4/LLN19$MX$DO99$DA4")) {
        std::cerr << "IED_BROWSER_LARGE_MODEL_FAIL dataset_reference_projection\n";
        return 18;
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "SclWorkspaceController.hpp"

#include "ariec61850/scl/exporter.hpp"
#include "ariec61850/scl/parser.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>
#include <string_view>

namespace scl = ar::iec61850::scl;

namespace {
bool waitFor(const std::function<bool()>& predicate, const int timeoutMs = 8'000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool containsAll(const QByteArray& bytes) {
    constexpr std::string_view required[] = {
        "<DataTypeTemplates>",
        "<LNodeType",
        "<DOType",
        "<DAType",
        "<DataSet name=\"dsGO\"",
        "<GSEControl",
        "<SampledValueControl",
        "<ReportControl",
        "<ConnectedAP",
        "direct-with-normal-security",
    };
    for (const auto token : required) {
        if (!bytes.contains(QByteArray{token.data(), static_cast<qsizetype>(token.size())})) return false;
    }
    return true;
}

std::optional<scl::SclDocument> parseFile(const QString& path) {
    try {
        scl::SclParser parser;
        return parser.load(path.toStdString());
    } catch (...) {
        return std::nullopt;
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: ied_scl_workspace_qa <fixture.scd>\n";
        return 2;
    }

    const QString fixture = QString::fromLocal8Bit(argv[1]);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::cerr << "Temporary directory creation failed.\n";
        return 3;
    }

    SclWorkspaceController workspace;
    if (!workspace.openFile(QUrl::fromLocalFile(fixture)) ||
        !waitFor([&] { return !workspace.busy(); })) {
        std::cerr << "SCL workspace open did not complete.\n";
        return 4;
    }
    if (!workspace.loaded() || !workspace.lastError().isEmpty()) {
        std::cerr << "SCL workspace open failed: " << workspace.lastError().toStdString() << '\n';
        return 5;
    }
    if (workspace.editionText() != QStringLiteral("Edition 2") ||
        workspace.namespaceUri().isEmpty() || workspace.iedCount() < 1 ||
        workspace.logicalNodeCount() < 1 || workspace.modelLeafCount() < 1 ||
        !workspace.exactSourceSaveSupported() || !workspace.reconstructionSupported() ||
        !workspace.editionConversionSupported() || !workspace.profileConversionSupported()) {
        std::cerr << "SCL workspace capability projection is incomplete.\n";
        return 6;
    }

    const auto gooseProjection = workspace.gooseStreams();
    if (workspace.gooseCount() < 1 || gooseProjection.size() != workspace.gooseCount()) {
        std::cerr << "Configured GOOSE workspace projection is incomplete.\n";
        return 27;
    }
    const auto firstGoose = gooseProjection.constFirst().toMap();
    if (firstGoose.value(QStringLiteral("reference")).toString().isEmpty() ||
        firstGoose.value(QStringLiteral("dataSet")).toString().isEmpty() ||
        firstGoose.value(QStringLiteral("members")).toStringList().isEmpty()) {
        std::cerr << "Configured GOOSE workspace projection lost identity or DataSet membership.\n";
        return 28;
    }

    const QString exact = temp.filePath(QStringLiteral("exact.scd"));
    if (!workspace.saveAs(QUrl::fromLocalFile(exact), QStringLiteral("preserve")) ||
        !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified() ||
        workspace.lastExportMode() != QStringLiteral("Exact source")) {
        std::cerr << "Exact source-preserving Save As failed: "
                  << workspace.lastError().toStdString() << '\n';
        return 7;
    }
    const auto sourceBytes = readBytes(fixture);
    if (sourceBytes.isEmpty() || readBytes(exact) != sourceBytes) {
        std::cerr << "Exact Save As changed source bytes.\n";
        return 8;
    }

    const QString canonical1 = temp.filePath(QStringLiteral("canonical-1.scd"));
    if (!workspace.exportCanonical(QUrl::fromLocalFile(canonical1), QStringLiteral("preserve")) ||
        !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified()) {
        std::cerr << "Canonical SCD export failed: " << workspace.lastError().toStdString() << '\n';
        return 9;
    }
    const auto canonicalBytes = readBytes(canonical1);
    if (canonicalBytes.isEmpty() || !containsAll(canonicalBytes)) {
        std::cerr << "Canonical SCD is missing required DTT/reference/configured-value surface.\n";
        return 10;
    }
    const auto canonicalDoc = parseFile(canonical1);
    if (!canonicalDoc || canonicalDoc->edition != scl::SclEdition::edition2 ||
        canonicalDoc->ieds.size() != 1U || canonicalDoc->logical_nodes.empty() ||
        canonicalDoc->model_entries.size() != static_cast<std::size_t>(workspace.modelLeafCount()) ||
        canonicalDoc->data_sets.size() != static_cast<std::size_t>(workspace.dataSetCount()) ||
        canonicalDoc->report_controls.size() != static_cast<std::size_t>(workspace.reportCount()) ||
        canonicalDoc->goose_streams.size() != static_cast<std::size_t>(workspace.gooseCount()) ||
        canonicalDoc->sampled_values_streams.size() != static_cast<std::size_t>(workspace.smvCount())) {
        std::cerr << "Canonical SCD reparsed with incomplete modeled semantics.\n";
        return 11;
    }

    const QString canonical2 = temp.filePath(QStringLiteral("canonical-2.scd"));
    if (!workspace.exportCanonical(QUrl::fromLocalFile(canonical2), QStringLiteral("ed2")) ||
        !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified() ||
        readBytes(canonical2) != canonicalBytes) {
        std::cerr << "Canonical output is not deterministic.\n";
        return 12;
    }

    const QString ed1 = temp.filePath(QStringLiteral("converted-ed1.scd"));
    if (!workspace.exportCanonical(QUrl::fromLocalFile(ed1), QStringLiteral("ed1")) ||
        !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified()) {
        std::cerr << "Ed2 -> Ed1 canonical conversion failed: " << workspace.lastError().toStdString() << '\n';
        return 13;
    }
    const auto ed1Doc = parseFile(ed1);
    if (!ed1Doc || ed1Doc->edition != scl::SclEdition::edition1) {
        std::cerr << "Ed1 canonical output did not reparse as Edition 1.\n";
        return 14;
    }

    const QString ed21 = temp.filePath(QStringLiteral("converted-ed21.scd"));
    if (!workspace.exportCanonical(QUrl::fromLocalFile(ed21), QStringLiteral("ed2.1")) ||
        !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified()) {
        std::cerr << "Ed2 -> Ed2.1 canonical conversion failed: " << workspace.lastError().toStdString() << '\n';
        return 15;
    }
    const auto ed21Doc = parseFile(ed21);
    if (!ed21Doc || ed21Doc->edition != scl::SclEdition::edition21) {
        std::cerr << "Ed2.1 canonical output did not reparse as Edition 2.1.\n";
        return 16;
    }

    for (const auto& extension : {QStringLiteral("icd"), QStringLiteral("cid")}) {
        const QString path = temp.filePath(QStringLiteral("converted.") + extension);
        if (!workspace.exportCanonical(QUrl::fromLocalFile(path), QStringLiteral("preserve")) ||
            !waitFor([&] { return !workspace.busy(); }) || !workspace.lastExportVerified() ||
            !parseFile(path)) {
            std::cerr << "Canonical profile export failed for ." << extension.toStdString() << '\n';
            return 17;
        }
    }

    const QString unsupported = temp.filePath(QStringLiteral("rejected.iid"));
    if (workspace.exportCanonical(QUrl::fromLocalFile(unsupported), QStringLiteral("preserve")) ||
        workspace.lastError().isEmpty() || QFile::exists(unsupported)) {
        std::cerr << "Unsupported canonical profile did not fail closed.\n";
        return 18;
    }

    const QString exactEd1 = temp.filePath(QStringLiteral("exact-ed1.scd"));
    if (workspace.saveAs(QUrl::fromLocalFile(exactEd1), QStringLiteral("ed1")) ||
        workspace.lastError().isEmpty() || QFile::exists(exactEd1)) {
        std::cerr << "Exact cross-edition relabel did not fail closed.\n";
        return 19;
    }
    const QString exactCid = temp.filePath(QStringLiteral("exact-relabel.cid"));
    if (workspace.saveAs(QUrl::fromLocalFile(exactCid), QStringLiteral("preserve")) ||
        workspace.lastError().isEmpty() || QFile::exists(exactCid)) {
        std::cerr << "Exact profile relabel did not fail closed.\n";
        return 20;
    }

    scl::SclParser parser;
    const auto sourceDocument = parser.load(fixture.toStdString());
    scl::SclCanonicalExportOptions options{sourceDocument.edition, scl::SclExportProfile::icd};

    auto multiIed = sourceDocument;
    multiIed.ieds.push_back(sourceDocument.ieds.front());
    if (scl::SclExporter::validate(multiIed, options).success) {
        std::cerr << "Multi-IED ICD export was not rejected.\n";
        return 21;
    }

    auto unknownEnum = sourceDocument;
    if (unknownEnum.model_entries.empty()) return 22;
    unknownEnum.model_entries.front().basic_type = "Enum";
    unknownEnum.model_entries.front().da_name = "vendorMode";
    unknownEnum.model_entries.front().type_id = "VendorModeKind";
    unknownEnum.model_entries.front().enum_type = "VendorModeKind";
    options.profile = scl::SclExportProfile::scd;
    if (scl::SclExporter::validate(unknownEnum, options).success) {
        std::cerr << "Unknown EnumType ordinal domain was not rejected.\n";
        return 23;
    }

    auto nestedSdo = sourceDocument;
    nestedSdo.model_entries.front().do_name = "Parent.Child";
    if (scl::SclExporter::validate(nestedSdo, options).success) {
        std::cerr << "Nested-SDO ambiguity was not rejected.\n";
        return 24;
    }

    const QString malformed = temp.filePath(QStringLiteral("malformed.scd"));
    {
        QFile file(malformed);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write("<not-scl/>") < 0) {
            std::cerr << "Could not create malformed SCL fixture.\n";
            return 25;
        }
    }
    SclWorkspaceController negative;
    if (!negative.openFile(QUrl::fromLocalFile(malformed)) ||
        !waitFor([&] { return !negative.busy(); }) || negative.loaded() ||
        negative.lastError().isEmpty()) {
        std::cerr << "Malformed SCL did not fail closed.\n";
        return 26;
    }

    std::cout << "SCL_WORKSPACE_PASS"
              << " edition_source=ed2"
              << " exact_preserve=pass"
              << " canonical=pass"
              << " dtt=pass"
              << " references=pass"
              << " configured_values=pass"
              << " deterministic=pass"
              << " ed1=pass"
              << " ed2=pass"
              << " ed21=pass"
              << " scd=pass"
              << " icd=pass"
              << " cid=pass"
              << " semantic_roundtrip=pass"
              << " goose_browser_projection=pass"
              << " ieds=" << workspace.iedCount()
              << " lns=" << workspace.logicalNodeCount()
              << " leaves=" << workspace.modelLeafCount() << '\n';
    std::cout << "SCL_WORKSPACE_NEGATIVE_PASS"
              << " unsupported_profile=rejected"
              << " exact_relabel=rejected"
              << " multi_ied_icd=rejected"
              << " unknown_enum=rejected"
              << " nested_sdo_ambiguity=rejected"
              << " malformed=rejected"
              << " vendor_lossless_claimed=false\n";
    return 0;
}

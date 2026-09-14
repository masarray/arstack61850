// SPDX-License-Identifier: GPL-3.0-or-later
#include "SclWorkspaceController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>

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
        !workspace.exactSourceSaveSupported() || workspace.reconstructionSupported() ||
        workspace.editionConversionSupported()) {
        std::cerr << "SCL workspace capability projection is unsafe or incomplete.\n";
        return 6;
    }

    const QString firstCopy = temp.filePath(QStringLiteral("verified-1.scd"));
    if (!workspace.saveAs(QUrl::fromLocalFile(firstCopy), QStringLiteral("preserve")) ||
        !waitFor([&] { return !workspace.busy(); }) ||
        !workspace.lastExportVerified()) {
        std::cerr << "Verified source-preserving Save As failed: "
                  << workspace.lastError().toStdString() << '\n';
        return 7;
    }
    const auto sourceBytes = readBytes(fixture);
    const auto firstBytes = readBytes(firstCopy);
    if (sourceBytes.isEmpty() || firstBytes != sourceBytes) {
        std::cerr << "Source-preserving Save As changed source bytes.\n";
        return 8;
    }

    try {
        ar::iec61850::scl::SclParser parser;
        const auto source = parser.load(fixture.toStdString());
        const auto saved = parser.load(firstCopy.toStdString());
        auto left = source;
        auto right = saved;
        left.source_name.clear();
        right.source_name.clear();
        if (!(left == right)) {
            std::cerr << "Reparsed Save As changed modeled SCL semantics.\n";
            return 9;
        }
    } catch (const std::exception& exception) {
        std::cerr << "Round-trip parse failed: " << exception.what() << '\n';
        return 10;
    }

    const QString secondCopy = temp.filePath(QStringLiteral("verified-2.scd"));
    if (!workspace.saveAs(QUrl::fromLocalFile(secondCopy), QStringLiteral("ed2")) ||
        !waitFor([&] { return !workspace.busy(); }) ||
        !workspace.lastExportVerified() || readBytes(secondCopy) != firstBytes) {
        std::cerr << "Deterministic second Save As failed.\n";
        return 11;
    }

    const QString rejectedEdition = temp.filePath(QStringLiteral("rejected-edition.scd"));
    if (workspace.saveAs(QUrl::fromLocalFile(rejectedEdition), QStringLiteral("ed1")) ||
        workspace.lastError().isEmpty() || QFile::exists(rejectedEdition)) {
        std::cerr << "Cross-edition conversion was not rejected fail-closed.\n";
        return 12;
    }

    const QString rejectedProfile = temp.filePath(QStringLiteral("relabel.icd"));
    if (workspace.saveAs(QUrl::fromLocalFile(rejectedProfile), QStringLiteral("preserve")) ||
        workspace.lastError().isEmpty() || QFile::exists(rejectedProfile)) {
        std::cerr << "Profile relabel was not rejected fail-closed.\n";
        return 13;
    }

    const QString malformed = temp.filePath(QStringLiteral("malformed.scd"));
    {
        QFile file(malformed);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write("<not-scl/>") < 0) {
            std::cerr << "Could not create malformed SCL fixture.\n";
            return 14;
        }
    }
    SclWorkspaceController negative;
    if (!negative.openFile(QUrl::fromLocalFile(malformed)) ||
        !waitFor([&] { return !negative.busy(); }) || negative.loaded() ||
        negative.lastError().isEmpty()) {
        std::cerr << "Malformed SCL did not fail closed.\n";
        return 15;
    }

    std::cout << "SCL_WORKSPACE_FOUNDATION_PASS"
              << " edition=ed2"
              << " namespace=explicit"
              << " exact_copy=pass"
              << " reparse=pass"
              << " semantic_roundtrip=pass"
              << " deterministic=pass"
              << " source_format=scd"
              << " ieds=" << workspace.iedCount()
              << " lns=" << workspace.logicalNodeCount()
              << " leaves=" << workspace.modelLeafCount() << '\n';
    std::cout << "SCL_WORKSPACE_NEGATIVE_PASS"
              << " cross_edition=rejected"
              << " profile_relabel=rejected"
              << " malformed=rejected"
              << " reconstruction_claimed=false"
              << " conversion_claimed=false\n";
    return 0;
}

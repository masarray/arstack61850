// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4MultiSvUploadTransaction.hpp"
#include "SclProfileModel.hpp"

#include <QCoreApplication>
#include <QUrl>
#include <filesystem>
#include <stdexcept>

namespace {
void check(bool good, const char* what) {
    if (!good) throw std::runtime_error{what};
}
QByteArray fixtureEnvelope() {
    SclProfileModel scl;
    const auto path = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/m4-three-9-2le-reference.scd";
    check(scl.loadFile(QUrl::fromLocalFile(QString::fromStdString(path.string()))),
          "real SCD load");
    check(scl.confirmCounterModulus(4000), "counter policy");
    for (int i = 0; i < 3; ++i) {
        scl.selectStream(i);
        check(scl.selectProfileFamily(QStringLiteral("9-2le")), "explicit profile family");
    }
    check(scl.m4DraftSupported(), "real ARM3 compile");
    return scl.m4ConfigurationBinary();
}
const QString kBoot = QStringLiteral("ABCDEF0123456789");
void ack(M4MultiSvUploadTransaction& x, const QString& line) {
    const auto before = x.stage();
    const bool consumed = x.receive(line, kBoot);
    if (!consumed || x.stage() == M4MultiSvUploadTransaction::Stage::failed) {
        throw std::runtime_error{
            QStringLiteral("ACK rejected stage=%1 line=%2 error=%3")
                .arg(static_cast<int>(before)).arg(line, x.error()).toStdString()};
    }
}
QString status(bool staging, unsigned tx, unsigned last) {
    return QStringLiteral("MULTISV STATUS staging=%1 validated=0 transaction=%2 "
        "received=0 expected=0 last=%3 configured=0 generation=0 hash=0000000000000000 "
        "bytes=0 output=OPT_IN_THREE_SV_TX_PREVIEW")
        .arg(staging ? 1 : 0).arg(tx).arg(last);
}
QString stream(unsigned slot) {
    return QStringLiteral("MULTISV STREAM slot=%1 svID=MU01_SV%1 APPID=0x4000 "
        "MAC=01:0C:CD:04:00:0%1 VLAN=1/0/4 rate=4000 wrap=4000 noASDU=1 confRev=1 output=INACTIVE")
        .arg(slot);
}
void advanceThroughChunks(M4MultiSvUploadTransaction& x) {
    qsizetype lastReceived = 0;
    unsigned chunks = 0;
    while (true) {
        const QString cmd = x.takeCommand();
        if (cmd.startsWith(QStringLiteral("MULTISV VALIDATE"))) {
            check(chunks > 3 && lastReceived > 48, "bounded chunks were not transmitted");
            ack(x, QStringLiteral("MULTISV VALIDATE transaction=%1 accepted count=3 rate=4000")
                       .arg(x.transaction()));
            break;
        }
        check(cmd.startsWith(QStringLiteral("MULTISV CHUNK ")), "sequential CHUNK expected");
        const auto columns = cmd.split(QLatin1Char(' '));
        check(columns.size() == 5 && columns[3].toLongLong() == lastReceived,
              "contiguous non-overlap offset");
        check(columns[4].size() >= 2 && columns[4].size() <= 96 &&
              columns[4].size() % 2 == 0, "48B UART limit");
        lastReceived += columns[4].size() / 2;
        ack(x, QStringLiteral("MULTISV CHUNK transaction=%1 received=%2")
                   .arg(x.transaction()).arg(lastReceived));
        ++chunks;
        check(chunks < 100, "transaction failed to terminate");
    }
}
void happyPath(const QByteArray& image) {
    M4MultiSvUploadTransaction x;
    check(x.start(image, kBoot), "begin validated transaction");
    check(x.takeCommand() == QStringLiteral("MULTISV STATUS"), "preflight status first");
    ack(x, status(false, 0, 8));
    check(x.takeCommand() == QStringLiteral("MULTISV BEGIN 9 ") +
          QString::number(image.size()), "monotonic BEGIN and bounded size");
    // A stale BEGIN ACK for a different transaction is a failure in a separate test.
    ack(x, QStringLiteral("MULTISV BEGIN transaction=9 bytes=%1 accepted")
               .arg(image.size()));
    advanceThroughChunks(x);
    check(x.takeCommand() == QStringLiteral("MULTISV COMMIT 9"), "validated commit");
    ack(x, QStringLiteral("MULTISV COMMITTED transaction=9 generation=17 "
        "hash=%1 bytes=%2 output=CONFIG_COMMITTED_TX_REQUIRES_SELECT_AND_VERIFIED_START")
            .arg(x.identityHash()).arg(image.size()));
    check(x.takeCommand() == QStringLiteral("MULTISV SHOW"), "fresh readback required");
    ack(x, QStringLiteral("MULTISV STATUS staging=0 validated=0 transaction=0 "
        "received=0 expected=0 last=9 configured=1 generation=17 hash=%1 bytes=%2 "
        "output=OPT_IN_THREE_SV_TX_PREVIEW").arg(x.identityHash()).arg(image.size()));
    check(x.takeCommand().isEmpty(), "one STATUS alone cannot authorize SELECT");
    // SHOW also emits an informational TXMODE line; it is NOT a SELECT ACK.
    check(!x.receive(QStringLiteral("MULTISV TXMODE selected=0 configured=1 "
        "(START uses selected roster; STOP ends TX)"), kBoot),
        "informational SHOW TXMODE must not advance the transaction");
    for (unsigned slot = 1; slot <= 3; ++slot) {
        ack(x, stream(slot));
        if (slot < 3) check(x.takeCommand().isEmpty(), "all three identities required");
    }
    check(x.takeCommand() == QStringLiteral("MULTISV SELECT"), "select only after roster verification");
    check(!x.verified(), "cannot arm before SELECT acknowledgement");
    ack(x, QStringLiteral("MULTISV TXMODE selected=1; next verified START uses THREE_SV"));
    check(x.verified() && x.generation() == 17 && x.takeCommand().isEmpty(),
          "three verified stream identities and selected firmware roster");
}
void negativeCases(const QByteArray& image) {
    M4MultiSvUploadTransaction x;
    auto corrupt = image;
    corrupt[corrupt.size()-1] = static_cast<char>(corrupt.back() ^ 1);
    check(!x.start(corrupt, kBoot), "reject corrupt ARM3 CRC before any serial TX");
    check(x.start(image, kBoot), "reset after invalid source");
    check(x.takeCommand() == QStringLiteral("MULTISV STATUS"), "preflight");
    ack(x, status(true, 11, 11));
    check(x.takeCommand() == QStringLiteral("MULTISV ABORT 11"), "abort prior staging");
    ack(x, QStringLiteral("MULTISV ABORT transaction=11 accepted"));
    check(x.takeCommand().startsWith(QStringLiteral("MULTISV BEGIN 12 ")),
          "fresh transaction after stale abort");
    check(x.receive(QStringLiteral("MULTISV BEGIN transaction=11 bytes=100 accepted"), kBoot),
          "mismatched BEGIN consumed");
    check(x.stage() == M4MultiSvUploadTransaction::Stage::failed,
          "mismatched transaction must fail closed");

    check(x.start(image, kBoot), "start after rejected transaction");
    static_cast<void>(x.takeCommand());
    ack(x, status(false, 0, 22));
    static_cast<void>(x.takeCommand());
    ack(x, QStringLiteral("MULTISV BEGIN transaction=23 bytes=%1 accepted")
        .arg(image.size()));
    static_cast<void>(x.takeCommand());
    check(x.receive(QStringLiteral("MULTISV CHUNK transaction=23 received=47"), kBoot),
          "bad offset ACK processed");
    check(x.stage() == M4MultiSvUploadTransaction::Stage::failed,
          "wrong byte range never commits");

    check(x.start(image, kBoot), "reboot guard new transaction");
    static_cast<void>(x.takeCommand());
    check(x.receive(status(false, 0, 0), QStringLiteral("DIFFERENTBOOT")), "boot change");
    check(x.stage() == M4MultiSvUploadTransaction::Stage::failed,
          "boot change invalidates the transaction");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto image = fixtureEnvelope();
        happyPath(image);
        negativeCases(image);
        qInfo("PASS: ACK-driven M4 48B chunks, BEGIN/VALIDATE/COMMIT/SHOW/SELECT, "
              "full hash and three-MU readback, stale transaction and boot-fault guards");
        return 0;
    } catch (const std::exception& e) {
        qCritical("FAIL M4 uploader: %s", e.what());
        return 1;
    }
}

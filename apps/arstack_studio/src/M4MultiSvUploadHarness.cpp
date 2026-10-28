// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/live_signal_transaction.hpp"
#include "M4MultiSvUploadTransaction.hpp"
#include "M4ThreeMuScenarioApply.hpp"
#include "M4ThreeMuBoardReadback.hpp"
#include "SclProfileModel.hpp"

#include <QCoreApplication>
#include <QUrl>
#include <filesystem>
#include <array>
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
void reconnectRosterSclMatch(const QByteArray& image) {
    M4MultiSvUploadTransaction tx;
    check(tx.start(image, kBoot), "real SCD ARM3 must decode");
    const QString hash = tx.identityHash();
    QStringList rows{stream(1), stream(2), stream(3)};
    const auto matches = [&image, &hash](const QStringList& wire) {
        return M4MultiSvUploadTransaction::matchesBoardRoster(image, hash, wire);
    };
    check(matches(rows), "exact three firmware identities match original SCD");
    const auto rejectChange = [&matches, &rows](const QString& original,
                                                  const QString& changed) {
        auto modified = rows;
        modified[1].replace(original, changed);
        check(!matches(modified), "changed firmware roster field must fail closed");
    };
    rejectChange(QStringLiteral("svID=MU01_SV2"), QStringLiteral("svID=WRONG_MU"));
    rejectChange(QStringLiteral("APPID=0x4000"), QStringLiteral("APPID=0x4001"));
    rejectChange(QStringLiteral("MAC=01:0C:CD:04:00:02"),
                 QStringLiteral("MAC=01:0C:CD:04:00:09"));
    rejectChange(QStringLiteral("VLAN=1/0/4"), QStringLiteral("VLAN=1/23/4"));
    rejectChange(QStringLiteral("rate=4000"), QStringLiteral("rate=3999"));
    rejectChange(QStringLiteral("wrap=4000"), QStringLiteral("wrap=8192"));
    rejectChange(QStringLiteral("confRev=1"), QStringLiteral("confRev=2"));
    auto duplicate = rows;
    duplicate[1] = duplicate[0];
    check(!matches(duplicate), "duplicate MU cannot satisfy 3/3 roster");
    check(!M4MultiSvUploadTransaction::matchesBoardRoster(
        image, QStringLiteral("FFFFFFFFFFFFFFFF"), rows),
        "different ARM3 whole-envelope hash cannot match");
    check(!M4MultiSvUploadTransaction::matchesBoardRoster(
        image, hash, {rows[0], rows[1]}),
        "incomplete firmware roster cannot match SCD");
    auto corrupt = image;
    corrupt[corrupt.size()-1] = static_cast<char>(corrupt.back() ^ 1);
    check(!M4MultiSvUploadTransaction::matchesBoardRoster(corrupt, hash, rows),
        "CRC-corrupt expected SCD binary cannot match board");
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

QVariantMap nominalMu(double hz) {
    QVariantList current, voltage;
    for (int i=0; i<4; ++i) {
        const double phase = (i == 1 ? -120.0 : i == 2 ? 120.0 : 0.0);
        current.push_back(QVariant{QVariantList{1.0 + 0.1 * i, phase, true, qulonglong{0}}});
        voltage.push_back(QVariant{QVariantList{57.74, phase, true, qulonglong{0}}});
    }
    return QVariantMap{
        {QStringLiteral("hz"), hz},
        {QStringLiteral("currentScale"), 100000.0},
        {QStringLiteral("voltageScale"), 100.0},
        {QStringLiteral("current"), current},
        {QStringLiteral("voltage"), voltage},
        {QStringLiteral("ct"), QVariantMap{
            {QStringLiteral("enabled"), false},
            {QStringLiteral("dc"), 0.0},
            {QStringLiteral("harmonic"), 0.0},
            {QStringLiteral("order"), 2},
            {QStringLiteral("clip"), 100.0}}}
    };
}
QStringList applyStep(M4ThreeMuScenarioApply& x, int slot) {
    const auto batch = x.takeCommands();
    check(batch.size() == 12, "fixed 12-command staged MU transaction");
    const QString lead = QStringLiteral("SVSTREAM %1 CFG ").arg(slot);
    check(batch.first().startsWith(lead + QStringLiteral("BEGIN ")) &&
          batch.at(1).startsWith(lead + QStringLiteral("FREQ ")) &&
          batch.at(10).startsWith(lead + QStringLiteral("SHAPE ")) &&
          batch.last().startsWith(lead + QStringLiteral("COMMIT ")),
          "bounded slot-qualified staged CFG");
    check(x.takeCommands().isEmpty(), "no double-dispatch before COMMIT");
    return batch;
}
void testThreeMuScenarioApply() {
    QVariantList drafts{nominalMu(50.0), nominalMu(60.0), nominalMu(0.0)};
    M4ThreeMuScenarioApply tx;
    auto invalid = drafts;
    auto third = invalid.at(2).toMap();
    third[QStringLiteral("current")] = QVariantList{};
    invalid[2] = third;
    check(!tx.start(invalid, kBoot), "validate ALL 3 before any serial dispatch");
    check(tx.takeCommands().isEmpty(), "invalid MU3 must not stage MU1");
    check(tx.start(drafts, kBoot), "three valid isolated MU snapshots");
    for (int slot=1; slot<=3; ++slot) {
        const auto batch = applyStep(tx, slot);
        const QString token = batch.first().section(QLatin1Char(' '), 4, 4);
        const QString hash = batch.last().section(QLatin1Char(' '), 5, 5);
        check(!tx.receive(QStringLiteral("SVSTREAM CFG STAGED slot=%1 verb=FREQ").arg(slot), kBoot),
              "generic staged line does not bypass COMMIT");
        const QString committed = QStringLiteral(
            "SVSTREAM CFG COMMITTED slot=%1 token=%2 hash=%3 generation=%4")
            .arg(slot).arg(token).arg(hash).arg(slot + 5);
        check(tx.receive(committed, kBoot), "exact MU COMMIT");
        const auto readback = tx.takeCommands();
        check(readback.size() == 1 &&
              readback.first() == QStringLiteral("SVSTREAM %1 CFG READBACK %2").arg(slot).arg(token),
              "independent firmware READBACK after COMMIT");
        check(!tx.receive(QStringLiteral("SVSTREAM CFG STAGED slot=%1 verb=SET").arg(slot), kBoot),
              "readback cannot be satisfied by SET ACK");
        check(tx.receive(QStringLiteral("SVSTREAM CFG READBACK slot=%1 token=%2 hash=%3 generation=%4")
              .arg(slot).arg(token).arg(hash).arg(slot + 5), kBoot),
              "exact slot-bound readback");
        check(tx.verifiedSlots() == slot, "one verified MU per complete bank");
    }
    check(tx.verified() && tx.generation(0) == 6 &&
          tx.generation(1) == 7 && tx.generation(2) == 8,
          "three independently committed MCU generations");
    M4ThreeMuScenarioApply rejected;
    check(rejected.start(drafts, kBoot), "negative fixture started");
    const auto first = applyStep(rejected, 1);
    const auto token = first.first().section(QLatin1Char(' '), 4, 4);
    check(rejected.receive(QStringLiteral(
          "SVSTREAM CFG COMMITTED slot=1 token=%1 hash=DEADBEEF generation=6").arg(token), kBoot),
          "mismatched commit classified");
    check(!rejected.verified() &&
          rejected.stage() == M4ThreeMuScenarioApply::Stage::failed,
          "wrong hash cannot advance MU2");
    M4ThreeMuScenarioApply reboot;
    check(reboot.start(drafts, kBoot), "boot test staged");
    static_cast<void>(applyStep(reboot, 1));
    check(reboot.receive(QStringLiteral("SVSTREAM CFG COMMITTED slot=1"),
          QStringLiteral("0000000000000000")) &&
          reboot.stage() == M4ThreeMuScenarioApply::Stage::failed,
          "firmware reboot invalidates all MU proof");
}

QStringList snapRows(int slot) {
    using namespace ar::iec61850::sampled_values;
    SvLiveSignalState state{};
    state.generation=static_cast<std::uint64_t>(slot+3);
    state.frequency_millihz=static_cast<std::uint32_t>(slot*50000);
    state.current_shape={slot==2,0,static_cast<std::uint32_t>(slot*10),2,1000};
    static constexpr std::array<const char*,8> names{"IA","IB","IC","IN","UA","UB","UC","UN"};
    for(int i=0;i<8;++i) state.channels[static_cast<std::size_t>(i)]={
        slot*1000+i, i*3000, static_cast<std::uint32_t>(i*128), i%2==0};
    const QString hash=QString::number(sv_scenario_hash(state),16)
        .rightJustified(16,QLatin1Char('0')).toUpper();
    QStringList lines;
    lines << QStringLiteral(
        "I (1234) ar_smv_multi: SVSTREAM SNAP BEGIN slot=%1 generation=%2 hash=%3 hz_millihz=%4 tx_enabled=%5 ct_enabled=%6 ct_dc=0 ct_harmonic=%7 ct_order=2 ct_clip=1000")
        .arg(slot).arg(static_cast<qulonglong>(state.generation)).arg(hash).arg(state.frequency_millihz)
        .arg(slot==1?1:0).arg(slot==2?1:0).arg(slot*10);
    for(int i=0;i<8;++i) {
        const auto& ch=state.channels[static_cast<std::size_t>(i)];
        lines << QStringLiteral(
            "SVSTREAM SNAP ROW slot=%1 generation=%2 channel=%3 rms=%4 phase=%5 quality=%6 enabled=%7")
            .arg(slot).arg(static_cast<qulonglong>(state.generation))
            .arg(QString::fromLatin1(names[static_cast<std::size_t>(i)]))
            .arg(ch.rms_counts).arg(ch.phase_millidegrees).arg(ch.quality)
            .arg(ch.enabled?1:0);
    }
    lines << QStringLiteral("SVSTREAM SNAP END slot=%1 generation=%2 hash=%3")
        .arg(slot).arg(static_cast<qulonglong>(state.generation)).arg(hash);
    return lines;
}
void testBoardSnapshots() {
    M4ThreeMuBoardReadback reader;
    check(reader.start(kBoot),"read-only 3-slot inspection start");
    for(int slot=1;slot<=3;++slot) {
        check(reader.takeCommand()==QStringLiteral("SVSTREAM %1 SHOW").arg(slot),
              "exact sequential read-only SHOW");
        const auto lines=snapRows(slot);
        for(const auto& line:lines)
            check(reader.receive(line,kBoot),"valid atomic bank record accepted");
        check(reader.verifiedSlots()==slot,"only complete hash-valid slot advances");
    }
    const auto results=reader.snapshots();
    check(reader.verified() && results.size()==3,"three authoritative board banks");
    check(results[0].toMap().value(QStringLiteral("txEnabled")).toBool() &&
          results[1].toMap().value(QStringLiteral("ct")).toMap()
              .value(QStringLiteral("enabled")).toBool() &&
          results[2].toMap().value(QStringLiteral("channels")).toList().size()==8,
          "read-only board TX/CT/8-channel quality exposed, no GUI draft writes");
    M4ThreeMuBoardReadback duplicate;
    check(duplicate.start(kBoot) && !duplicate.takeCommand().isEmpty(),"duplicate fixture start");
    auto dup=snapRows(1);
    check(duplicate.receive(dup[0],kBoot),"begin accepted");
    check(duplicate.receive(dup[1],kBoot),"row accepted");
    check(duplicate.receive(dup[1],kBoot) &&
          duplicate.stage()==M4ThreeMuBoardReadback::Stage::failed,
          "duplicate row fails closed");
    M4ThreeMuBoardReadback missing;
    check(missing.start(kBoot) && !missing.takeCommand().isEmpty(),"missing fixture start");
    auto partial=snapRows(1);
    partial.removeAt(5);
    for(const auto& line:partial) missing.receive(line,kBoot);
    check(missing.stage()==M4ThreeMuBoardReadback::Stage::failed,
          "missing channel cannot be a coherent bank");
    M4ThreeMuBoardReadback badHash;
    check(badHash.start(kBoot) && !badHash.takeCommand().isEmpty(),"hash fixture start");
    auto altered=snapRows(1);
    altered[3].replace(QStringLiteral("quality=256"),QStringLiteral("quality=257"));
    for(const auto& line:altered) badHash.receive(line,kBoot);
    check(badHash.stage()==M4ThreeMuBoardReadback::Stage::failed,
          "tampered quality invalidates canonical board hash");
    M4ThreeMuBoardReadback reboot;
    check(reboot.start(kBoot) && !reboot.takeCommand().isEmpty(),"boot fixture start");
    check(reboot.receive(snapRows(1)[0],QStringLiteral("0011223344556677")) &&
          reboot.stage()==M4ThreeMuBoardReadback::Stage::failed,
          "new boot never adopts earlier bank rows");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto image = fixtureEnvelope();
        happyPath(image);
        reconnectRosterSclMatch(image);
        negativeCases(image);
        testThreeMuScenarioApply();
        testBoardSnapshots();
        qInfo("PASS: exact 3-MU SCD/board roster matcher plus complete 3-MU atomic firmware snapshot hash/CT/quality/reboot, scenario preflight/COMMIT/READBACK, and ACK-driven M4 48B chunks, BEGIN/VALIDATE/COMMIT/SHOW/SELECT, "
              "full hash and three-MU readback, stale transaction and boot-fault guards");
        return 0;
    } catch (const std::exception& e) {
        qCritical("FAIL M4 uploader: %s", e.what());
        return 1;
    }
}

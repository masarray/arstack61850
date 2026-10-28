// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4MultiSvUploadTransaction.hpp"
#include "ariec61850/sampled_values/profile_identity.hpp"

#include <algorithm>
#include <limits>
#include <QStringList>
#include <utility>
#include <span>

using namespace ar::iec61850::sampled_values;

bool M4MultiSvUploadTransaction::start(
        const QByteArray& arm3, const QString& bootId) {
    if (active() || bootId.trimmed().isEmpty() || arm3.isEmpty() ||
        arm3.size() >= 4096) return false;
    Profiles decoded{};
    const auto source = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(arm3.constData()),
        static_cast<std::size_t>(arm3.size())};
    const auto result = M4ThreeSvBinaryCodec::decode(source, decoded);
    if (!result.success() || result.bytes != source.size()) return false;
    record_ = arm3;
    profiles_ = decoded;
    bootId_ = bootId;
    pendingCommand_ = QStringLiteral("MULTISV STATUS");
    error_.clear();
    stage_ = Stage::status;
    identityHash_ = sv_binary_profile_identity_hash(source);
    generation_ = 0;
    transaction_ = 0;
    observedLast_ = 0;
    offset_ = 0;
    expectedReceived_ = 0;
    rowsVerified_ = 0;
    statusVerified_ = false;
    return true;
}

QString M4MultiSvUploadTransaction::takeCommand() {
    if (!active()) return {};
    QString out = std::move(pendingCommand_);
    pendingCommand_.clear();
    return out;
}

void M4MultiSvUploadTransaction::fail(const QString& reason) {
    if (!active()) return;
    error_ = reason;
    pendingCommand_.clear();
    stage_ = Stage::failed;
}

void M4MultiSvUploadTransaction::cancel(const QString& reason) {
    fail(reason);
}

QString M4MultiSvUploadTransaction::identityHash() const {
    return QString::number(static_cast<qulonglong>(identityHash_), 16)
        .rightJustified(16, QLatin1Char('0')).toUpper();
}

bool M4MultiSvUploadTransaction::matchesBoardRoster(
    const QByteArray& expectedArm3, const QString& boardHash,
    const QStringList& firmwareStreamLines) {
    if (expectedArm3.isEmpty() || expectedArm3.size() >= 4096 ||
        firmwareStreamLines.size() != 3 || boardHash.size() != 16) return false;
    const auto source = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(expectedArm3.constData()),
        static_cast<std::size_t>(expectedArm3.size())};
    const QString canonicalHash = QString::number(
        static_cast<qulonglong>(sv_binary_profile_identity_hash(source)), 16)
            .rightJustified(16, QLatin1Char('0')).toUpper();
    if (canonicalHash != boardHash.toUpper()) return false;
    M4MultiSvUploadTransaction checker;
    const auto decoded = M4ThreeSvBinaryCodec::decode(source, checker.profiles_);
    if (!decoded.success() || decoded.bytes != source.size()) return false;
    for (const QString& line : firmwareStreamLines) {
        if (!line.startsWith(QStringLiteral("MULTISV STREAM ")) ||
            !checker.validateStream(line)) return false;
    }
    return checker.rowsVerified_ == 0x07U;
}

QString M4MultiSvUploadTransaction::value(
    const QString& line, const char* key) {
    const QString prefix = QString::fromLatin1(key) + QLatin1Char('=');
    const auto offset = line.indexOf(prefix, 0, Qt::CaseSensitive);
    if (offset < 0 || (offset > 0 && !line.at(offset - 1).isSpace())) return {};
    const qsizetype start = offset + prefix.size();
    qsizetype end = line.indexOf(QLatin1Char(' '), start);
    if (end < 0) end = line.size();
    return line.mid(start, end - start).trimmed();
}

bool M4MultiSvUploadTransaction::uintField(
    const QString& line, const char* key, std::uint64_t& output, const int base) {
    bool ok = false;
    const auto parsed = value(line, key).toULongLong(&ok, base);
    if (ok) output = parsed;
    return ok;
}

QString M4MultiSvUploadTransaction::mac(
    const std::array<std::uint8_t, 6>& bytes) {
    QString result;
    result.reserve(17);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) result.append(QLatin1Char(':'));
        result.append(QStringLiteral("%1")
            .arg(static_cast<unsigned>(bytes[i]), 2, 16, QLatin1Char('0')).toUpper());
    }
    return result;
}

void M4MultiSvUploadTransaction::issueBegin(const std::uint32_t last) {
    if (last == std::numeric_limits<std::uint32_t>::max()) {
        fail(QStringLiteral("MULTISV transaction IDs exhausted; board reboot required."));
        return;
    }
    transaction_ = last + 1U;
    stage_ = Stage::begin;
    pendingCommand_ = QStringLiteral("MULTISV BEGIN %1 %2")
        .arg(transaction_).arg(record_.size());
}

void M4MultiSvUploadTransaction::issueChunkOrValidate() {
    if (offset_ == record_.size()) {
        stage_ = Stage::validate;
        pendingCommand_ = QStringLiteral("MULTISV VALIDATE %1").arg(transaction_);
        return;
    }
    constexpr qsizetype kChunkMax = 48;
    const qsizetype count = std::min(kChunkMax, record_.size() - offset_);
    const auto hex = record_.mid(offset_, count).toHex().toUpper();
    expectedReceived_ = offset_ + count;
    stage_ = Stage::chunk;
    pendingCommand_ = QStringLiteral("MULTISV CHUNK %1 %2 %3")
        .arg(transaction_).arg(offset_).arg(QString::fromLatin1(hex));
}

// This is canonical committed firmware SHOW: not a preview of GUI labels.
bool M4MultiSvUploadTransaction::validateStream(const QString& line) {
    std::uint64_t slot{}, appId{}, rate{}, wrap{}, asdu{}, rev{};
    if (!uintField(line, "slot", slot) || slot < 1 || slot > 3 ||
        !uintField(line, "APPID", appId, 16) ||
        !uintField(line, "rate", rate) ||
        !uintField(line, "wrap", wrap) ||
        !uintField(line, "noASDU", asdu) ||
        !uintField(line, "confRev", rev)) return false;
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << (slot - 1U));
    if ((rowsVerified_ & bit) != 0U) return false;
    const auto& profile = profiles_[static_cast<std::size_t>(slot - 1U)];
    const auto id = QString::fromUtf8(profile.sv_id.data(), profile.sv_id_length);
    const QStringList vlan = value(line, "VLAN").split(QLatin1Char('/'));
    if (vlan.size() != 3) return false;
    bool presentOk{}, vidOk{}, pcpOk{};
    const auto present = vlan[0].toUInt(&presentOk);
    const auto vid = vlan[1].toUInt(&vidOk);
    const auto pcp = vlan[2].toUInt(&pcpOk);
    if (!presentOk || !vidOk || !pcpOk ||
        value(line, "svID") != id ||
        value(line, "MAC").compare(mac(profile.destination_mac),
                                   Qt::CaseInsensitive) != 0 ||
        appId != profile.app_id || rate != profile.frame_rate_hz ||
        wrap != profile.sample_counter_modulus ||
        asdu != profile.no_asdu ||
        rev != profile.configuration_revision ||
        present != static_cast<unsigned>(profile.vlan_present) ||
        vid != profile.vlan_id || pcp != profile.vlan_priority) return false;
    rowsVerified_ = static_cast<std::uint8_t>(rowsVerified_ | bit);
    return true;
}

bool M4MultiSvUploadTransaction::receive(
    const QString& rawLine, const QString& currentBootId) {
    if (!active()) return false;
    if (currentBootId.compare(bootId_, Qt::CaseInsensitive) != 0) {
        fail(QStringLiteral("Firmware reboot/change detected during MULTISV transaction."));
        return true;
    }
    // The host must send the queued command before accepting an ACK. Never
    // advance on a replay or unsolicited response from an earlier operation.
    if (!pendingCommand_.isEmpty()) return false;
    const auto begin = rawLine.indexOf(QStringLiteral("MULTISV "));
    if (begin < 0) return false;
    const QString line = rawLine.mid(begin).trimmed();
    if (line.contains(QStringLiteral("rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive)) {
        fail(QStringLiteral("Firmware rejected MULTISV transaction: ") + line.left(180));
        return true;
    }
    const auto txMatches = [this, &line]() {
        std::uint64_t tx{};
        return uintField(line, "transaction", tx) && tx == transaction_;
    };
    std::uint64_t n{}, extra{}, hash{};
    switch (stage_) {
    case Stage::status:
        if (!line.startsWith(QStringLiteral("MULTISV STATUS"))) return false;
        if (!uintField(line, "staging", n) || n > 1U ||
            !uintField(line, "transaction", extra) ||
            extra > std::numeric_limits<std::uint32_t>::max() ||
            !uintField(line, "last", hash) ||
            hash > std::numeric_limits<std::uint32_t>::max()) {
            fail(QStringLiteral("Invalid firmware MULTISV STATUS."));
            return true;
        }
        observedLast_ = static_cast<std::uint32_t>(std::max(extra, hash));
        if (n == 1U) {
            if (extra == 0U) {
                fail(QStringLiteral("Firmware reports staging without transaction ID."));
                return true;
            }
            transaction_ = static_cast<std::uint32_t>(extra);
            stage_ = Stage::abort_stale;
            pendingCommand_ = QStringLiteral("MULTISV ABORT %1").arg(transaction_);
        } else {
            issueBegin(observedLast_);
        }
        return true;
    case Stage::abort_stale:
        if (!line.startsWith(QStringLiteral("MULTISV ABORT"))) return false;
        if (!txMatches() || !line.contains(QStringLiteral("accepted"))) {
            fail(QStringLiteral("Stale upload ABORT was not acknowledged."));
        } else {
            issueBegin(observedLast_);
        }
        return true;
    case Stage::begin:
        if (!line.startsWith(QStringLiteral("MULTISV BEGIN"))) return false;
        if (!txMatches() || !uintField(line, "bytes", n) ||
            n != static_cast<std::uint64_t>(record_.size()) ||
            !line.contains(QStringLiteral("accepted"))) {
            fail(QStringLiteral("Firmware BEGIN mismatch."));
        } else {
            offset_ = 0;
            issueChunkOrValidate();
        }
        return true;
    case Stage::chunk:
        if (!line.startsWith(QStringLiteral("MULTISV CHUNK"))) return false;
        if (!txMatches() || !uintField(line, "received", n) ||
            n != static_cast<std::uint64_t>(expectedReceived_)) {
            fail(QStringLiteral("Firmware CHUNK offset mismatch."));
        } else {
            offset_ = expectedReceived_;
            issueChunkOrValidate();
        }
        return true;
    case Stage::validate:
        if (!line.startsWith(QStringLiteral("MULTISV VALIDATE"))) return false;
        if (!txMatches() || !line.contains(QStringLiteral("accepted count=3 rate=4000"))) {
            fail(QStringLiteral("Firmware rejected complete 3-MU ARM3 validation."));
        } else {
            stage_ = Stage::commit;
            pendingCommand_ = QStringLiteral("MULTISV COMMIT %1").arg(transaction_);
        }
        return true;
    case Stage::commit:
        if (!line.startsWith(QStringLiteral("MULTISV COMMITTED"))) return false;
        if (!txMatches() || !uintField(line, "generation", n) || n == 0 ||
            !uintField(line, "hash", hash, 16) || hash != identityHash_ ||
            !uintField(line, "bytes", extra) ||
            extra != static_cast<std::uint64_t>(record_.size())) {
            fail(QStringLiteral("Firmware COMMIT hash/generation/size mismatch."));
        } else {
            generation_ = n;
            rowsVerified_ = 0;
            statusVerified_ = false;
            stage_ = Stage::readback;
            pendingCommand_ = QStringLiteral("MULTISV SHOW");
        }
        return true;
    case Stage::readback:
        if (line.startsWith(QStringLiteral("MULTISV STATUS"))) {
            if (!uintField(line, "configured", n) || n != 1U ||
                !uintField(line, "generation", extra) || extra != generation_ ||
                !uintField(line, "hash", hash, 16) || hash != identityHash_ ||
                !uintField(line, "bytes", n) ||
                n != static_cast<std::uint64_t>(record_.size())) {
                fail(QStringLiteral("Firmware SHOW roster hash/generation mismatch."));
            } else statusVerified_ = true;
            return true;
        }
        if (!line.startsWith(QStringLiteral("MULTISV STREAM"))) return false;
        if (!statusVerified_ || !validateStream(line)) {
            fail(QStringLiteral("Firmware SHOW contains wrong or repeated MU identity."));
            return true;
        }
        if (rowsVerified_ == 0x07U) {
            stage_ = Stage::select;
            pendingCommand_ = QStringLiteral("MULTISV SELECT");
        }
        return true;
    case Stage::select:
        if (!line.startsWith(QStringLiteral("MULTISV TXMODE"))) return false;
        if (line.contains(QStringLiteral("selected=1;")) &&
            line.contains(QStringLiteral("THREE_SV"))) {
            stage_ = Stage::verified;
        } else fail(QStringLiteral("Firmware did not confirm THREE_SV selection."));
        return true;
    default:
        return false;
    }
}

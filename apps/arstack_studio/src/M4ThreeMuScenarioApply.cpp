// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4ThreeMuScenarioApply.hpp"
#include "ariec61850/sampled_values/live_signal_transaction.hpp"
#include <QMetaType>
#include <QRandomGenerator>
#include <cmath>
#include <limits>
#include <utility>

using namespace ar::iec61850::sampled_values;

namespace {
constexpr std::array<const char*, 8> kIds{"IA","IB","IC","IN","UA","UB","UC","UN"};
QString hexHash(std::uint64_t hash) {
    return QString::number(static_cast<qulonglong>(hash), 16)
        .rightJustified(16, QLatin1Char('0')).toUpper();
}
bool finitePositive(double n) { return std::isfinite(n) && n > 0.0; }
}

bool M4ThreeMuScenarioApply::buildSlot(
        const QVariantMap& draft, int slot, Slot& out, QString& error) {
    const auto invalid = [&error](const QString& why) {
        error = why; return false;
    };
    bool hzOk = false, iOk = false, uOk = false;
    const double hz = draft.value(QStringLiteral("hz")).toDouble(&hzOk);
    const double ci = draft.value(QStringLiteral("currentScale")).toDouble(&iOk);
    const double cu = draft.value(QStringLiteral("voltageScale")).toDouble(&uOk);
    const auto current = draft.value(QStringLiteral("current")).toList();
    const auto voltage = draft.value(QStringLiteral("voltage")).toList();
    if (!hzOk || !std::isfinite(hz) || (hz != 0.0 && hz < 1.0) ||
        hz < 0.0 || hz > 1000.0 || !iOk || !uOk ||
        !finitePositive(ci) || !finitePositive(cu) ||
        current.size() != 4 || voltage.size() != 4)
        return invalid(QStringLiteral("Invalid frequency, scale or 4I+4V layout."));
    SvLiveSignalState normalized{};
    normalized.frequency_millihz = static_cast<std::uint32_t>(std::llround(hz * 1000.0));
    const QString prefix = QStringLiteral("SVSTREAM %1 CFG ").arg(slot);
    QStringList batch;
    batch.reserve(12);
    batch << prefix + QStringLiteral("FREQ %1").arg(normalized.frequency_millihz);
    for (int i = 0; i < 8; ++i) {
        const QVariantList row = (i < 4 ? current.at(i) : voltage.at(i-4)).toList();
        if (row.size() != 4 || row.at(2).metaType().id() != QMetaType::Bool)
            return invalid(QStringLiteral("Invalid channel shape or enabled type."));
        bool magOk{}, phaseOk{}, qOk{};
        const double magnitude = row[0].toDouble(&magOk);
        const double phase = row[1].toDouble(&phaseOk);
        const auto quality = row[3].toULongLong(&qOk);
        const double scaled = magnitude * (i < 4 ? ci : cu);
        const double angle = phase * 1000.0;
        if (!magOk || !phaseOk || !qOk ||
            !std::isfinite(scaled) || !std::isfinite(angle) ||
            (hz > 0.0 && magnitude < 0.0) ||
            scaled <= static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
            angle < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            angle > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
            quality > std::numeric_limits<std::uint32_t>::max())
            return invalid(QStringLiteral("Channel wire data out of bounds."));
        auto& ch = normalized.channels[static_cast<std::size_t>(i)];
        ch.rms_counts = static_cast<std::int32_t>(std::llround(scaled));
        ch.phase_millidegrees = static_cast<std::int32_t>(std::llround(angle));
        ch.quality = static_cast<std::uint32_t>(quality);
        ch.enabled = row[2].toBool();
        batch << prefix + QStringLiteral("SET %1 %2 %3 %4 %5")
            .arg(QString::fromLatin1(kIds[static_cast<std::size_t>(i)]))
            .arg(ch.rms_counts).arg(ch.phase_millidegrees)
            .arg(ch.quality).arg(ch.enabled ? 1 : 0);
    }
    const auto shape = draft.value(QStringLiteral("ct")).toMap();
    bool dcOk{}, harmonicOk{}, clipOk{}, orderOk{};
    const double dc = shape.value(QStringLiteral("dc")).toDouble(&dcOk);
    const double harmonic = shape.value(QStringLiteral("harmonic")).toDouble(&harmonicOk);
    const double clip = shape.value(QStringLiteral("clip")).toDouble(&clipOk);
    const auto order = shape.value(QStringLiteral("order")).toInt(&orderOk);
    const bool enabled = shape.value(QStringLiteral("enabled")).toBool();
    if (!dcOk || !harmonicOk || !clipOk || !orderOk ||
        !std::isfinite(dc) || !std::isfinite(harmonic) || !std::isfinite(clip) ||
        dc < -300 || dc > 300 || harmonic < 0 || harmonic > 300 ||
        clip < 1 || clip > 1000 || order < 2 || order > 63 ||
        (enabled && hz == 0.0))
        return invalid(QStringLiteral("Invalid CT saturation settings."));
    normalized.current_shape.enabled = enabled;
    normalized.current_shape.dc_offset_permille = static_cast<std::int32_t>(std::llround(dc*10.0));
    normalized.current_shape.harmonic_permille = static_cast<std::uint32_t>(std::llround(harmonic*10.0));
    normalized.current_shape.clip_permille = static_cast<std::uint32_t>(std::llround(clip*10.0));
    normalized.current_shape.harmonic_order = static_cast<std::uint8_t>(order);
    if (!SvLiveSignalBank::validate(normalized))
        return invalid(QStringLiteral("Firmware canonical state validation failed."));
    out.token = QRandomGenerator::global()->generate64();
    if (out.token == 0U) out.token = 1U;
    out.hash = sv_scenario_hash(normalized);
    batch.prepend(prefix + QStringLiteral("BEGIN %1").arg(out.token));
    batch << prefix + QStringLiteral("SHAPE %1 %2 %3 %4 %5")
        .arg(enabled ? 1 : 0).arg(normalized.current_shape.dc_offset_permille)
        .arg(normalized.current_shape.harmonic_permille)
        .arg(order).arg(normalized.current_shape.clip_permille);
    batch << prefix + QStringLiteral("COMMIT %1 %2")
        .arg(out.token).arg(hexHash(out.hash));
    out.batch = std::move(batch);
    return true;
}

bool M4ThreeMuScenarioApply::start(const QVariantList& scenarios, const QString& bootId) {
    if (active() || scenarios.size() != 3 || bootId.size() != 16) return false;
    std::array<Slot,3> staged{};
    for (int i=0; i<3; ++i) {
        const auto draft = scenarios.at(i).toMap();
        QString error;
        if (!buildSlot(draft, i+1, staged[static_cast<std::size_t>(i)], error)) {
            error_ = QStringLiteral("MU%1 rejected before serial transfer: %2").arg(i+1).arg(error);
            stage_ = Stage::failed;
            return false;
        }
    }
    slots_ = std::move(staged);
    boot_ = bootId;
    slot_ = 0;
    verifiedSlots_ = 0;
    error_.clear();
    stage_ = Stage::send_batch;
    return true;
}
QStringList M4ThreeMuScenarioApply::takeCommands() {
    if (stage_ == Stage::send_batch) {
        stage_ = Stage::wait_commit;
        return slots_[static_cast<std::size_t>(slot_)].batch;
    }
    if (stage_ == Stage::send_readback) {
        stage_ = Stage::wait_readback;
        return {QStringLiteral("SVSTREAM %1 CFG READBACK %2")
            .arg(slot_+1).arg(slots_[static_cast<std::size_t>(slot_)].token)};
    }
    return {};
}
QString M4ThreeMuScenarioApply::field(const QString& s, const char* key) {
    const QString term = QString::fromLatin1(key) + QLatin1Char('=');
    const auto i = s.indexOf(term);
    if (i < 0 || (i > 0 && !s.at(i-1).isSpace())) return {};
    const auto begin = i + term.size();
    auto end = s.indexOf(QLatin1Char(' '), begin);
    if (end < 0) end = s.size();
    return s.mid(begin, end-begin).trimmed();
}
void M4ThreeMuScenarioApply::fail(const QString& why) {
    error_ = why;
    stage_ = Stage::failed;
}
void M4ThreeMuScenarioApply::cancel(const QString& why) {
    if (active()) fail(why);
}
bool M4ThreeMuScenarioApply::receive(const QString& raw, const QString& currentBootId) {
    if (!active()) return false;
    if (currentBootId != boot_) {
        fail(QStringLiteral("Boot epoch changed during 3-MU scenario apply."));
        return true;
    }
    const auto offset = raw.indexOf(QStringLiteral("SVSTREAM "));
    if (offset < 0) return false;
    const QString line = raw.mid(offset).trimmed();
    if (line.contains(QStringLiteral("rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("REJECT"), Qt::CaseInsensitive)) {
        fail(QStringLiteral("Firmware rejected MU%1 transaction: %2").arg(slot_+1).arg(line.left(180)));
        return true;
    }
    const bool commit = stage_ == Stage::wait_commit &&
        line.startsWith(QStringLiteral("SVSTREAM CFG COMMITTED "));
    const bool readback = stage_ == Stage::wait_readback &&
        line.startsWith(QStringLiteral("SVSTREAM CFG READBACK "));
    if (!commit && !readback) return false;
    bool slotOk{}, tokenOk{}, hashOk{}, genOk{};
    const unsigned slotValue = field(line, "slot").toUInt(&slotOk);
    const auto token = field(line, "token").toULongLong(&tokenOk);
    const auto hash = field(line, "hash").toULongLong(&hashOk, 16);
    const auto generation = field(line, "generation").toULongLong(&genOk);
    auto& current = slots_[static_cast<std::size_t>(slot_)];
    if (!slotOk || slotValue != static_cast<unsigned>(slot_+1) ||
        !tokenOk || token != current.token || !hashOk || hash != current.hash ||
        !genOk || generation == 0 ||
        (readback && generation != current.generation)) {
        fail(QStringLiteral("MU%1 firmware token/hash/generation mismatch.").arg(slot_+1));
        return true;
    }
    if (commit) {
        current.generation = generation;
        stage_ = Stage::send_readback;
    } else {
        ++verifiedSlots_;
        if (++slot_ == 3) stage_ = Stage::verified;
        else stage_ = Stage::send_batch;
    }
    return true;
}

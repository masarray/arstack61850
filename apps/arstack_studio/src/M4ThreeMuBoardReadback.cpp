// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4ThreeMuBoardReadback.hpp"
#include "ariec61850/sampled_values/live_signal_transaction.hpp"
#include <limits>

using namespace ar::iec61850::sampled_values;
namespace {
constexpr std::array<const char*,8> ids{"IA","IB","IC","IN","UA","UB","UC","UN"};
bool unsignedValue(const QString& line, const char* name, std::uint64_t& v) {
    const QString key = QString::fromLatin1(name) + QLatin1Char('=');
    auto at = line.indexOf(key);
    if (at < 0 || (at && !line.at(at-1).isSpace())) return false;
    at += key.size();
    auto end = line.indexOf(QLatin1Char(' '), at);
    if (end < 0) end = line.size();
    const auto text = line.mid(at, end-at);
    if (text.isEmpty() || text.startsWith(QLatin1Char('-'))) return false;
    bool ok{};
    const auto n = text.toULongLong(&ok, 10);
    if (ok) v = n;
    return ok;
}
bool signedValue(const QString& line, const char* name, std::int64_t& v) {
    const QString key = QString::fromLatin1(name) + QLatin1Char('=');
    auto at = line.indexOf(key);
    if (at < 0 || (at && !line.at(at-1).isSpace())) return false;
    at += key.size();
    auto end = line.indexOf(QLatin1Char(' '), at);
    if (end < 0) end = line.size();
    bool ok{};
    const auto n = line.mid(at,end-at).toLongLong(&ok,10);
    if (ok) v = n;
    return ok;
}
bool hashValue(const QString& line, std::uint64_t& v) {
    const QString key = QStringLiteral("hash=");
    auto at = line.indexOf(key);
    if (at < 0 || (at && !line.at(at-1).isSpace())) return false;
    at += key.size();
    const auto end = line.indexOf(QLatin1Char(' '),at);
    const QString raw = line.mid(at,end<0?-1:end-at);
    if (raw.size()!=16) return false;
    for(const auto ch:raw)
        if (!ch.isDigit() && !(ch.toUpper()>=QLatin1Char('A') &&
                               ch.toUpper()<=QLatin1Char('F'))) return false;
    bool ok{};
    const auto n=raw.toULongLong(&ok,16);
    if(ok) v=n;
    return ok;
}
bool boundedUnsigned(const QString& line,const char* key,std::uint32_t& out,std::uint32_t hi) {
    std::uint64_t n{};
    if (!unsignedValue(line,key,n) || n>hi) return false;
    out=static_cast<std::uint32_t>(n); return true;
}
bool boundedSigned(const QString& line,const char* key,std::int32_t& out,
                   std::int32_t lo,std::int32_t hi) {
    std::int64_t n{};
    if(!signedValue(line,key,n) || n<lo || n>hi) return false;
    out=static_cast<std::int32_t>(n); return true;
}
QString hashText(std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value),16)
        .rightJustified(16,QLatin1Char('0')).toUpper();
}
}
QString M4ThreeMuBoardReadback::field(const QString& line, const char* key) {
    const auto token = QString::fromLatin1(key) + QLatin1Char('=');
    auto at = line.indexOf(token);
    if(at<0 || (at && !line.at(at-1).isSpace())) return {};
    at += token.size();
    auto end=line.indexOf(QLatin1Char(' '),at);
    if(end<0) end=line.size();
    return line.mid(at,end-at);
}
bool M4ThreeMuBoardReadback::start(const QString& bootId) {
    if(active() || bootId.size()!=16) return false;
    boot_=bootId;
    verified_={};
    staging_={};
    slot_=0;
    rows_=0;
    error_.clear();
    stage_=Stage::send;
    return true;
}
QString M4ThreeMuBoardReadback::takeCommand() {
    if(stage_!=Stage::send) return {};
    stage_=Stage::waiting_begin;
    return QStringLiteral("SVSTREAM %1 SHOW").arg(slot_+1);
}
void M4ThreeMuBoardReadback::fail(const QString& message) {
    error_=message;
    verified_={}; staging_={};
    rows_=0; stage_=Stage::failed;
}
void M4ThreeMuBoardReadback::cancel(const QString& why) {
    if(active() || verified()) fail(why);
}
bool M4ThreeMuBoardReadback::parseBegin(const QString& line) {
    std::uint64_t slot{},gen{},tx{},ct{};
    std::uint32_t hz{},harm{},order{},clip{};
    std::int32_t dc{};
    std::uint64_t hash{};
    if(!unsignedValue(line,"slot",slot) || slot!=static_cast<unsigned>(slot_+1) ||
       !unsignedValue(line,"generation",gen) ||
       !hashValue(line,hash) ||
       !boundedUnsigned(line,"hz_millihz",hz,1000000) ||
       !unsignedValue(line,"tx_enabled",tx) || tx>1 ||
       !unsignedValue(line,"ct_enabled",ct) || ct>1 ||
       !boundedSigned(line,"ct_dc",dc,-3000,3000) ||
       !boundedUnsigned(line,"ct_harmonic",harm,3000) ||
       !boundedUnsigned(line,"ct_order",order,63) || order<2 ||
       !boundedUnsigned(line,"ct_clip",clip,10000) || clip<10)
        return false;
    staging_={};
    staging_.hash=hash;
    staging_.txEnabled=tx==1;
    auto& state=staging_.state;
    state.generation=gen;
    state.frequency_millihz=hz;
    state.current_shape={ct==1,dc,harm,static_cast<std::uint8_t>(order),clip};
    rows_=0;stage_=Stage::waiting_rows;
    return true;
}
bool M4ThreeMuBoardReadback::parseRow(const QString& line) {
    std::uint64_t slot{},gen{},quality{},enabled{};
    std::int32_t rms{},phase{};
    if(!unsignedValue(line,"slot",slot) || slot!=static_cast<unsigned>(slot_+1) ||
       !unsignedValue(line,"generation",gen) || gen!=staging_.state.generation ||
       !boundedSigned(line,"rms",rms,std::numeric_limits<std::int32_t>::min()+1,
                     std::numeric_limits<std::int32_t>::max()) ||
       !boundedSigned(line,"phase",phase,-360000000,360000000) ||
       !unsignedValue(line,"quality",quality) ||
       quality>std::numeric_limits<std::uint32_t>::max() ||
       !unsignedValue(line,"enabled",enabled) || enabled>1) return false;
    const auto name=field(line,"channel");
    int index=-1;
    for(int i=0;i<8;++i) if(name==QString::fromLatin1(ids[static_cast<std::size_t>(i)])) {
        index=i; break;
    }
    if(index<0 || (rows_&(1U<<index))) return false;
    rows_|=1U<<index;
    staging_.state.channels[static_cast<std::size_t>(index)]={
        rms,phase,static_cast<std::uint32_t>(quality),enabled==1};
    return true;
}
bool M4ThreeMuBoardReadback::parseEnd(const QString& line) {
    std::uint64_t slot{},gen{},hash{};
    if(!unsignedValue(line,"slot",slot) || slot!=static_cast<unsigned>(slot_+1) ||
       !unsignedValue(line,"generation",gen) || gen!=staging_.state.generation ||
       !hashValue(line,hash) || hash!=staging_.hash || rows_!=0xFFU ||
       !SvLiveSignalBank::validate(staging_.state) ||
       sv_scenario_hash(staging_.state)!=hash) return false;
    verified_[static_cast<std::size_t>(slot_)]=staging_;
    staging_={};
    rows_=0;
    ++slot_;
    stage_=slot_==3?Stage::verified:Stage::send;
    return true;
}
bool M4ThreeMuBoardReadback::receive(const QString& raw,const QString& currentBoot) {
    if(!active()) return false;
    if(boot_!=currentBoot) {fail(QStringLiteral("M4 snapshot boot epoch changed."));return true;}
    const auto offset=raw.indexOf(QStringLiteral("SVSTREAM SNAP "));
    if(offset<0) {
        if(raw.contains(QStringLiteral("SVSTREAM rejected"),Qt::CaseInsensitive)) {
            fail(QStringLiteral("Firmware rejected M4 snapshot request."));return true;
        }
        return false;
    }
    if(stage_==Stage::send) return false; // Never accept an unsolicited/queued replay
    const auto line=raw.mid(offset).trimmed();
    bool ok=false;
    if(stage_==Stage::waiting_begin && line.startsWith(QStringLiteral("SVSTREAM SNAP BEGIN ")))
        ok=parseBegin(line);
    else if(stage_==Stage::waiting_rows && line.startsWith(QStringLiteral("SVSTREAM SNAP ROW ")))
        ok=parseRow(line);
    else if(stage_==Stage::waiting_rows && line.startsWith(QStringLiteral("SVSTREAM SNAP END ")))
        ok=parseEnd(line);
    else {
        // Out-of-order END/ROW/BEGIN is not a valid coherent bank.
        fail(QStringLiteral("Unexpected M4 bank snapshot record/order."));return true;
    }
    if(!ok) fail(QStringLiteral("M4 bank snapshot mismatch, missing row or hash invalid."));
    return true;
}
QVariantList M4ThreeMuBoardReadback::snapshots() const {
    if(!verified()) return {};
    QVariantList out;
    for(int i=0;i<3;++i) {
        const auto& bank=verified_[static_cast<std::size_t>(i)];
        const auto& state=bank.state;
        QVariantList channels;
        for(int c=0;c<8;++c) {
            const auto& ch=state.channels[static_cast<std::size_t>(c)];
            channels.push_back(QVariantMap{
                {QStringLiteral("id"),QString::fromLatin1(ids[static_cast<std::size_t>(c)])},
                {QStringLiteral("counts"),ch.rms_counts},
                {QStringLiteral("phaseMdeg"),ch.phase_millidegrees},
                {QStringLiteral("quality"),static_cast<qulonglong>(ch.quality)},
                {QStringLiteral("enabled"),ch.enabled}});
        }
        out.push_back(QVariantMap{
            {QStringLiteral("slot"),i+1},
            {QStringLiteral("generation"),QString::number(static_cast<qulonglong>(state.generation))},
            {QStringLiteral("hash"),hashText(bank.hash)},
            {QStringLiteral("frequencyMilliHz"),state.frequency_millihz},
            {QStringLiteral("txEnabled"),bank.txEnabled},
            {QStringLiteral("ct"),QVariantMap{
                {QStringLiteral("enabled"),state.current_shape.enabled},
                {QStringLiteral("dcPermille"),state.current_shape.dc_offset_permille},
                {QStringLiteral("harmonicPermille"),state.current_shape.harmonic_permille},
                {QStringLiteral("order"),static_cast<int>(state.current_shape.harmonic_order)},
                {QStringLiteral("clipPermille"),state.current_shape.clip_permille}}},
            {QStringLiteral("channels"),channels}});
    }
    return out;
}

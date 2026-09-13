// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedCommissioningModel.hpp"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString qstringRuntime(const std::string& value) {
    return QString::fromStdString(value);
}

QString logicalNodeRuntime(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstringRuntime(entry.prefix) + qstringRuntime(entry.ln_class) + qstringRuntime(entry.ln_inst);
}

bool textEquals(const QString& left, const QString& right) {
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool optionContains(const QStringList& options, const QString& value) {
    return std::any_of(options.cbegin(), options.cend(), [&value](const QString& option) {
        return option.compare(value, Qt::CaseInsensitive) == 0;
    });
}

QString normalizedDataObject(QString value) {
    value.replace(QLatin1Char('$'), QLatin1Char('.'));
    return value;
}
} // namespace

QString IedCommissioningModel::iedSessionKey(const int iedIndex) const {
    if (backend_ == nullptr || iedIndex < 0 || iedIndex >= backend_->ieds_.size()) return {};
    return backend_->ieds_.at(iedIndex).toMap().value(QStringLiteral("sessionKey")).toString();
}

std::optional<IedCommissioningModel::PointBinding>
IedCommissioningModel::pointBindingForMember(const int row) const {
    if (backend_ == nullptr || backend_->selectedIedIndex_ < 0 ||
        selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) {
        return std::nullopt;
    }
    const auto* entries = entriesFor(handles_.at(selectedHandleIndex_));
    if (entries == nullptr || row < 0 || row >= static_cast<int>(entries->size())) {
        return std::nullopt;
    }
    const auto& entry = entries->at(static_cast<std::size_t>(row));
    const int iedIndex = backend_->selectedIedIndex_;
    const auto ied = backend_->ieds_.at(iedIndex).toMap();
    const auto iedName = ied.value(QStringLiteral("name")).toString();
    if (iedName.isEmpty()) return std::nullopt;

    const auto bindAbsolute = [this, iedIndex](const int absoluteIndex)
        -> std::optional<PointBinding> {
        if (absoluteIndex < 0) return std::nullopt;
        const int sourceIndex = backend_->selectedPointIndices_.indexOf(absoluteIndex);
        if (sourceIndex < 0) return std::nullopt;
        const auto* point = backend_->pointStore_.at(absoluteIndex);
        if (point == nullptr) return std::nullopt;
        return PointBinding{
            iedIndex,
            sourceIndex,
            absoluteIndex,
            iedSessionKey(iedIndex),
            point->reference};
    };

    const auto exactReference = qstringRuntime(entry.signal_reference);
    if (!exactReference.isEmpty()) {
        if (auto exact = bindAbsolute(backend_->pointStore_.indexOf(iedName, exactReference));
            exact.has_value()) {
            return exact;
        }
    }

    const auto expectedDomain = qstringRuntime(entry.ied_name) + qstringRuntime(entry.ld_inst);
    const auto expectedNode = logicalNodeRuntime(entry);
    const auto expectedObject = normalizedDataObject(qstringRuntime(entry.do_name));
    const auto expectedAttribute = normalizedDataObject(qstringRuntime(entry.da_name));
    const auto expectedFc = qstringRuntime(entry.functional_constraint);

    for (int sourceIndex = 0; sourceIndex < backend_->selectedPointIndices_.size(); ++sourceIndex) {
        const int absoluteIndex = backend_->selectedPointIndices_.at(sourceIndex);
        const auto* point = backend_->pointStore_.at(absoluteIndex);
        if (point == nullptr || point->iedName != iedName) continue;
        if (!expectedDomain.isEmpty() && point->mmsDomain != expectedDomain) continue;
        if (!expectedNode.isEmpty() && point->logicalNode != expectedNode) continue;
        if (!expectedFc.isEmpty() &&
            point->functionalConstraint.compare(expectedFc, Qt::CaseInsensitive) != 0) continue;
        if (!expectedObject.isEmpty() &&
            normalizedDataObject(point->dataObject) != expectedObject) continue;
        if (!expectedAttribute.isEmpty() &&
            normalizedDataObject(point->dataAttribute) != expectedAttribute) continue;
        return PointBinding{
            iedIndex,
            sourceIndex,
            absoluteIndex,
            iedSessionKey(iedIndex),
            point->reference};
    }
    return std::nullopt;
}

std::optional<IedCommissioningModel::PointBinding>
IedCommissioningModel::controlStatusBinding() const {
    if (backend_ == nullptr || backend_->selectedIedIndex_ < 0 ||
        selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) {
        return std::nullopt;
    }
    const auto& handle = handles_.at(selectedHandleIndex_);
    if (handle.kind != Kind::control || handle.documentIndex < 0 ||
        handle.documentIndex >= static_cast<int>(backend_->documents_.size())) {
        return std::nullopt;
    }
    const auto& document = backend_->documents_[static_cast<std::size_t>(handle.documentIndex)].document;
    if (handle.sourceIndex < 0 ||
        handle.sourceIndex >= static_cast<int>(document.model_entries.size())) {
        return std::nullopt;
    }
    const auto& entry = document.model_entries[static_cast<std::size_t>(handle.sourceIndex)];
    const auto iedName = qstringRuntime(entry.ied_name);
    const auto expectedDomain = iedName + qstringRuntime(entry.ld_inst);
    const auto expectedNode = logicalNodeRuntime(entry);
    const auto expectedObject = normalizedDataObject(qstringRuntime(entry.do_name));
    const int iedIndex = backend_->selectedIedIndex_;

    for (int sourceIndex = 0; sourceIndex < backend_->selectedPointIndices_.size(); ++sourceIndex) {
        const int absoluteIndex = backend_->selectedPointIndices_.at(sourceIndex);
        const auto* point = backend_->pointStore_.at(absoluteIndex);
        if (point == nullptr || point->iedName != iedName || point->mmsDomain != expectedDomain ||
            point->logicalNode != expectedNode ||
            normalizedDataObject(point->dataObject) != expectedObject ||
            point->functionalConstraint.compare(QStringLiteral("ST"), Qt::CaseInsensitive) != 0 ||
            point->dataAttribute.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        return PointBinding{
            iedIndex,
            sourceIndex,
            absoluteIndex,
            iedSessionKey(iedIndex),
            point->reference};
    }
    return std::nullopt;
}

bool IedCommissioningModel::drivablePoint(const IedPointStore::PointRecord& point) const {
    if (!point.writable) return false;
    const auto fc = point.functionalConstraint.trimmed().toUpper();
    if (fc != QStringLiteral("ST") && fc != QStringLiteral("MX")) return false;
    if (point.dataAttribute.compare(QStringLiteral("q"), Qt::CaseInsensitive) == 0 ||
        point.dataAttribute.compare(QStringLiteral("t"), Qt::CaseInsensitive) == 0 ||
        point.type.contains(QStringLiteral("Quality"), Qt::CaseInsensitive) ||
        point.type.contains(QStringLiteral("Timestamp"), Qt::CaseInsensitive)) {
        return false;
    }
    if ((optionContains(point.options, QStringLiteral("false")) &&
         optionContains(point.options, QStringLiteral("true"))) ||
        (optionContains(point.options, QStringLiteral("off")) &&
         optionContains(point.options, QStringLiteral("on"))) ||
        point.type.contains(QStringLiteral("Boolean"), Qt::CaseInsensitive)) {
        return true;
    }
    bool numeric{};
    static_cast<void>(point.value.toDouble(&numeric));
    return numeric;
}

QString IedCommissioningModel::normalizedBehaviorMode(const QString& mode) const {
    QString token;
    token.reserve(mode.size());
    for (const auto character : mode.toLower()) {
        if (character.isLetterOrNumber()) token.append(character);
    }
    if (token == QStringLiteral("pulse")) return QStringLiteral("Pulse");
    if (token == QStringLiteral("toggle")) return QStringLiteral("Toggle");
    if (token == QStringLiteral("ramp") || token == QStringLiteral("ramp1")) {
        return QStringLiteral("Ramp");
    }
    return {};
}

QString IedCommissioningModel::nextBehaviorValue(
    const IedPointStore::PointRecord& point,
    const BehaviorSlot& slot,
    bool* const ok) const {
    if (ok != nullptr) *ok = false;
    const auto current = point.value.trimmed();

    if (optionContains(point.options, QStringLiteral("false")) &&
        optionContains(point.options, QStringLiteral("true"))) {
        if (slot.mode == QStringLiteral("Ramp")) return {};
        const bool active = textEquals(current, QStringLiteral("true")) || current == QStringLiteral("1");
        if (ok != nullptr) *ok = true;
        return active ? QStringLiteral("false") : QStringLiteral("true");
    }
    if (optionContains(point.options, QStringLiteral("off")) &&
        optionContains(point.options, QStringLiteral("on"))) {
        if (slot.mode == QStringLiteral("Ramp")) return {};
        if (ok != nullptr) *ok = true;
        return textEquals(current, QStringLiteral("on"))
            ? QStringLiteral("off") : QStringLiteral("on");
    }
    if (point.type.contains(QStringLiteral("Boolean"), Qt::CaseInsensitive)) {
        if (slot.mode == QStringLiteral("Ramp")) return {};
        const bool active = textEquals(current, QStringLiteral("true")) || current == QStringLiteral("1");
        if (ok != nullptr) *ok = true;
        return active ? QStringLiteral("false") : QStringLiteral("true");
    }

    bool currentOk{};
    const double currentNumber = current.toDouble(&currentOk);
    if (!currentOk || !std::isfinite(currentNumber)) return {};

    double next = currentNumber + slot.step;
    if (slot.mode == QStringLiteral("Toggle")) {
        bool originalOk{};
        const double original = slot.originalValue.toDouble(&originalOk);
        if (!originalOk || !std::isfinite(original)) return {};
        const double epsilon = std::max(1.0, std::abs(original)) * 1e-9;
        next = std::abs(currentNumber - original) <= epsilon ? original + slot.step : original;
    }
    if (!std::isfinite(next) || std::abs(next) > 1.0e12) return {};

    const bool floating = point.type.contains(QStringLiteral("Float"), Qt::CaseInsensitive) ||
        point.rawType.contains(QStringLiteral("FLOAT"), Qt::CaseInsensitive);
    if (ok != nullptr) *ok = true;
    return floating
        ? QString::number(next, 'g', 12)
        : QString::number(static_cast<qlonglong>(std::llround(next)));
}

int IedCommissioningModel::behaviorIndexFor(const PointBinding& binding) const noexcept {
    for (int index = 0; index < behaviors_.size(); ++index) {
        const auto& slot = behaviors_.at(index);
        if (slot.iedIndex == binding.iedIndex &&
            slot.pointStoreIndex == binding.pointStoreIndex &&
            slot.sessionKey == binding.sessionKey) {
            return index;
        }
    }
    return -1;
}

bool IedCommissioningModel::applyBehaviorValue(
    BehaviorSlot& slot,
    const QString& value,
    const bool restoreOriginal) {
    if (backend_ == nullptr || slot.iedIndex < 0 || slot.pointStoreIndex < 0 ||
        iedSessionKey(slot.iedIndex) != slot.sessionKey) {
        return false;
    }
    auto* runtime = backend_->runtimeAt(slot.iedIndex);
    auto* point = backend_->pointStore_.atMutable(slot.pointStoreIndex);
    if (runtime == nullptr || runtime->state != IedFleetController::RuntimeState::running ||
        point == nullptr || !point->writable) {
        return false;
    }

    const auto before = *point;
    point->value = value;
    if (restoreOriginal) {
        point->quality = slot.originalQuality;
        point->origin = slot.originalOrigin;
        point->changed = slot.originalChanged;
        point->updated = slot.originalUpdated;
    } else {
        point->origin = QStringLiteral("Commissioning %1").arg(slot.mode);
        point->changed = true;
        point->updated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    }

    if (!backend_->enqueueLiveUpdate(slot.iedIndex, *point)) {
        *point = before;
        ++behaviorRejectedCount_;
        if (!slot.rejectionReported) {
            slot.rejectionReported = true;
            backend_->appendActivity(
                QStringLiteral("Commissioning"),
                QStringLiteral("Behavior update for %1 was deferred because the bounded live channel rejected it.")
                    .arg(slot.reference),
                QStringLiteral("Warning"),
                point->iedName);
        }
        return false;
    }

    slot.rejectionReported = false;
    slot.lastAppliedValue = value;
    ++slot.ticks;
    ++behaviorTickCount_;
    if (slot.iedIndex == backend_->selectedIedIndex_) {
        emit backend_->valuesChanged();
        emit backend_->selectionChanged();
    }
    return true;
}

void IedCommissioningModel::ensureBehaviorTimer() {
    if (!behaviorClock_.isValid()) behaviorClock_.start();
    if (behaviorTimer_ != nullptr) return;
    behaviorTimer_ = new QTimer{this};
    behaviorTimer_->setTimerType(Qt::CoarseTimer);
    behaviorTimer_->setInterval(kBehaviorMinimumIntervalMilliseconds);
    connect(behaviorTimer_, &QTimer::timeout, this, &IedCommissioningModel::advanceBehaviors);
}

void IedCommissioningModel::pruneBehaviors() {
    if (behaviors_.isEmpty()) return;
    bool changed{};
    for (int index = behaviors_.size() - 1; index >= 0; --index) {
        const auto& slot = behaviors_.at(index);
        const auto* runtime = backend_ == nullptr ? nullptr : backend_->runtimeAt(slot.iedIndex);
        const auto* point = backend_ == nullptr ? nullptr : backend_->pointStore_.at(slot.pointStoreIndex);
        if (backend_ != nullptr && point != nullptr && runtime != nullptr &&
            runtime->state == IedFleetController::RuntimeState::running &&
            iedSessionKey(slot.iedIndex) == slot.sessionKey) {
            continue;
        }
        behaviors_.removeAt(index);
        changed = true;
    }
    if (behaviors_.isEmpty() && behaviorTimer_ != nullptr) behaviorTimer_->stop();
    if (changed) emit behaviorChanged();
}

void IedCommissioningModel::advanceBehaviors() {
    if (behaviors_.isEmpty()) {
        if (behaviorTimer_ != nullptr) behaviorTimer_->stop();
        return;
    }
    if (!behaviorClock_.isValid()) behaviorClock_.start();
    const qint64 now = behaviorClock_.elapsed();
    bool changed{};

    for (int index = behaviors_.size() - 1; index >= 0; --index) {
        auto& slot = behaviors_[index];
        if (backend_ == nullptr || iedSessionKey(slot.iedIndex) != slot.sessionKey) {
            behaviors_.removeAt(index);
            changed = true;
            continue;
        }
        auto* runtime = backend_->runtimeAt(slot.iedIndex);
        auto* point = backend_->pointStore_.atMutable(slot.pointStoreIndex);
        if (runtime == nullptr || runtime->state != IedFleetController::RuntimeState::running ||
            point == nullptr) {
            behaviors_.removeAt(index);
            changed = true;
            continue;
        }
        if (point->value != slot.lastAppliedValue) {
            backend_->appendActivity(
                QStringLiteral("Commissioning"),
                QStringLiteral("Behavior on %1 stopped because another edit changed the owned value.")
                    .arg(slot.reference),
                QStringLiteral("Warning"),
                point->iedName);
            behaviors_.removeAt(index);
            changed = true;
            continue;
        }
        if (now < slot.nextDueMilliseconds) continue;

        if (slot.pulseRestorePending) {
            if (applyBehaviorValue(slot, slot.originalValue, true)) {
                backend_->appendActivity(
                    QStringLiteral("Commissioning"),
                    QStringLiteral("Pulse on %1 restored its original value.").arg(slot.reference),
                    QStringLiteral("Success"),
                    point->iedName);
                behaviors_.removeAt(index);
                changed = true;
            } else {
                slot.nextDueMilliseconds = now + slot.intervalMilliseconds;
                changed = true;
            }
            continue;
        }

        bool nextOk{};
        const auto next = nextBehaviorValue(*point, slot, &nextOk);
        if (!nextOk || !applyBehaviorValue(slot, next, false)) {
            slot.nextDueMilliseconds = now + slot.intervalMilliseconds;
            changed = true;
            continue;
        }
        // Do not catch up missed intervals in a burst. One scheduler turn emits
        // at most one update per bounded slot, then advances from 'now'.
        slot.nextDueMilliseconds = now + slot.intervalMilliseconds;
        changed = true;
    }

    if (behaviors_.isEmpty() && behaviorTimer_ != nullptr) behaviorTimer_->stop();
    if (changed) emit behaviorChanged();
}

int IedCommissioningModel::firstDrivableMember() const {
    for (int row = 0; row < memberCount(); ++row) {
        if (canDriveMember(row)) return row;
    }
    return -1;
}

bool IedCommissioningModel::canDriveMember(const int row) const {
    const auto binding = pointBindingForMember(row);
    if (!binding.has_value() || backend_ == nullptr) return false;
    const auto* point = backend_->pointStore_.at(binding->pointStoreIndex);
    return point != nullptr && drivablePoint(*point);
}

bool IedCommissioningModel::focusMemberValue(const int row) {
    const auto binding = pointBindingForMember(row);
    if (!binding.has_value() || backend_ == nullptr ||
        binding->iedIndex != backend_->selectedIedIndex_) {
        return false;
    }
    backend_->selectValue(binding->sourceIndex);
    return backend_->selectedValueIndex_ == binding->sourceIndex;
}

bool IedCommissioningModel::focusControlStatus() {
    const auto binding = controlStatusBinding();
    if (!binding.has_value() || backend_ == nullptr ||
        binding->iedIndex != backend_->selectedIedIndex_) {
        return false;
    }
    backend_->selectValue(binding->sourceIndex);
    return backend_->selectedValueIndex_ == binding->sourceIndex;
}

QVariantMap IedCommissioningModel::memberBehavior(const int row) const {
    QVariantMap result;
    const auto binding = pointBindingForMember(row);
    if (!binding.has_value()) {
        result.insert(QStringLiteral("active"), false);
        return result;
    }
    const int index = behaviorIndexFor(*binding);
    if (index < 0) {
        result.insert(QStringLiteral("active"), false);
        return result;
    }
    const auto& slot = behaviors_.at(index);
    result.insert(QStringLiteral("active"), true);
    result.insert(QStringLiteral("mode"), slot.mode);
    result.insert(QStringLiteral("intervalMs"), slot.intervalMilliseconds);
    result.insert(QStringLiteral("step"), slot.step);
    result.insert(QStringLiteral("ticks"), static_cast<qulonglong>(slot.ticks));
    result.insert(QStringLiteral("reference"), slot.reference);
    result.insert(QStringLiteral("originalValue"), slot.originalValue);
    result.insert(QStringLiteral("lastValue"), slot.lastAppliedValue);
    return result;
}

bool IedCommissioningModel::startMemberBehavior(
    const int row,
    const QString& requestedMode,
    const int intervalMilliseconds,
    const double step) {
    const auto mode = normalizedBehaviorMode(requestedMode);
    if (backend_ == nullptr || mode.isEmpty() ||
        intervalMilliseconds < kBehaviorMinimumIntervalMilliseconds ||
        intervalMilliseconds > kBehaviorMaximumIntervalMilliseconds ||
        !std::isfinite(step) || step == 0.0 || std::abs(step) > 1.0e9) {
        return false;
    }
    const auto binding = pointBindingForMember(row);
    if (!binding.has_value()) return false;
    auto* runtime = backend_->runtimeAt(binding->iedIndex);
    auto* point = backend_->pointStore_.atMutable(binding->pointStoreIndex);
    if (runtime == nullptr || runtime->state != IedFleetController::RuntimeState::running ||
        point == nullptr || !drivablePoint(*point) || behaviorIndexFor(*binding) >= 0 ||
        behaviors_.size() >= kBehaviorCapacity) {
        return false;
    }

    BehaviorSlot slot;
    slot.iedIndex = binding->iedIndex;
    slot.pointStoreIndex = binding->pointStoreIndex;
    slot.sessionKey = binding->sessionKey;
    slot.reference = binding->reference;
    slot.mode = mode;
    slot.originalValue = point->value;
    slot.originalQuality = point->quality;
    slot.originalOrigin = point->origin;
    slot.originalUpdated = point->updated;
    slot.originalChanged = point->changed;
    slot.lastAppliedValue = point->value;
    slot.step = step;
    slot.intervalMilliseconds = intervalMilliseconds;
    slot.pulseRestorePending = mode == QStringLiteral("Pulse");

    bool nextOk{};
    const auto next = nextBehaviorValue(*point, slot, &nextOk);
    if (!nextOk) return false;

    ensureBehaviorTimer();
    if (!applyBehaviorValue(slot, next, false)) return false;
    slot.nextDueMilliseconds = behaviorClock_.elapsed() + intervalMilliseconds;
    behaviors_.push_back(std::move(slot));
    if (!behaviorTimer_->isActive()) behaviorTimer_->start();

    const auto& stored = behaviors_.constLast();
    backend_->appendActivity(
        QStringLiteral("Commissioning"),
        stored.mode == QStringLiteral("Pulse")
            ? QStringLiteral("Pulsed %1; restore is scheduled in %2 ms.")
                  .arg(stored.reference)
                  .arg(stored.intervalMilliseconds)
            : QStringLiteral("Started %1 behavior on %2 every %3 ms (%4/%5 slots active).")
                  .arg(stored.mode, stored.reference)
                  .arg(stored.intervalMilliseconds)
                  .arg(behaviors_.size())
                  .arg(kBehaviorCapacity),
        QStringLiteral("Success"),
        point->iedName);
    emit behaviorChanged();
    return true;
}

bool IedCommissioningModel::pulseSelectedService(const int intervalMilliseconds) {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return false;
    const auto kind = handles_.at(selectedHandleIndex_).kind;
    if (kind == Kind::control) return false;
    const int row = firstDrivableMember();
    return row >= 0 && startMemberBehavior(
        row, QStringLiteral("Pulse"), intervalMilliseconds, 1.0);
}

bool IedCommissioningModel::stopMemberBehavior(const int row, const bool restore) {
    const auto binding = pointBindingForMember(row);
    if (!binding.has_value()) return false;
    const int index = behaviorIndexFor(*binding);
    if (index < 0) return false;

    auto& slot = behaviors_[index];
    auto* point = backend_ == nullptr ? nullptr : backend_->pointStore_.atMutable(slot.pointStoreIndex);
    const bool externallyOverridden = point != nullptr && point->value != slot.lastAppliedValue;
    bool restored = !restore || externallyOverridden;
    if (restore && !externallyOverridden) {
        restored = applyBehaviorValue(slot, slot.originalValue, true);
    }
    const auto reference = slot.reference;
    const auto iedName = point == nullptr ? QString{} : point->iedName;
    behaviors_.removeAt(index);
    if (behaviors_.isEmpty() && behaviorTimer_ != nullptr) behaviorTimer_->stop();
    if (backend_ != nullptr) {
        backend_->appendActivity(
            QStringLiteral("Commissioning"),
            restored
                ? QStringLiteral("Stopped behavior on %1%2.")
                      .arg(reference, restore && !externallyOverridden
                              ? QStringLiteral(" and restored the original value") : QString{})
                : QStringLiteral("Stopped behavior on %1; the runtime was unavailable for restore.")
                      .arg(reference),
            restored ? QStringLiteral("Info") : QStringLiteral("Warning"),
            iedName);
    }
    emit behaviorChanged();
    return restored;
}

void IedCommissioningModel::stopAllBehaviors(const bool restore) {
    if (behaviors_.isEmpty()) return;
    for (int index = behaviors_.size() - 1; index >= 0; --index) {
        auto& slot = behaviors_[index];
        auto* point = backend_ == nullptr ? nullptr : backend_->pointStore_.atMutable(slot.pointStoreIndex);
        const bool externallyOverridden = point != nullptr && point->value != slot.lastAppliedValue;
        if (restore && !externallyOverridden) {
            static_cast<void>(applyBehaviorValue(slot, slot.originalValue, true));
        }
    }
    behaviors_.clear();
    if (behaviorTimer_ != nullptr) behaviorTimer_->stop();
    if (backend_ != nullptr) {
        backend_->appendActivity(
            QStringLiteral("Commissioning"),
            restore
                ? QStringLiteral("Stopped all commissioning behaviors; owned values were restored where possible.")
                : QStringLiteral("Stopped all commissioning behaviors."),
            QStringLiteral("Info"));
    }
    emit behaviorChanged();
}

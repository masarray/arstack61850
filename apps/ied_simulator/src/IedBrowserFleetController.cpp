// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedBrowserFleetController.hpp"

#include <algorithm>
#include <utility>

struct IedBrowserFleetController::Entry final {
    explicit Entry(const quint64 identifier)
        : id(identifier),
          context(std::make_unique<IedEngineeringContextController>()),
          client(std::make_unique<MmsClientController>()),
          reports(std::make_unique<MmsReportController>()),
          utilities(std::make_unique<MmsFileSettingsController>()),
          controls(std::make_unique<MmsControlController>()),
          engineering(std::make_unique<SclWorkspaceController>()),
          session(std::make_unique<IedBrowserSessionController>()) {
        session->setClient(client.get());
        session->setReports(reports.get());
        session->setUtilities(utilities.get());
        session->setControls(controls.get());
        session->setEngineeringContext(context.get());
        engineering->setEngineeringContext(context.get());
    }

    quint64 id{};
    std::unique_ptr<IedEngineeringContextController> context;
    std::unique_ptr<MmsClientController> client;
    std::unique_ptr<MmsReportController> reports;
    std::unique_ptr<MmsFileSettingsController> utilities;
    std::unique_ptr<MmsControlController> controls;
    std::unique_ptr<SclWorkspaceController> engineering;
    // Destructed first: it holds only observing pointers to the above services.
    std::unique_ptr<IedBrowserSessionController> session;
};

IedBrowserFleetController::IedBrowserFleetController(QObject* parent)
    : QAbstractListModel(parent) {
    entries_.push_back(std::make_unique<Entry>(nextSlotId_++));
    activeIndex_ = 0;
    connectEntry(entries_.front().get());
}

IedBrowserFleetController::~IedBrowserFleetController() = default;

int IedBrowserFleetController::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : workspaceCount();
}

QHash<int, QByteArray> IedBrowserFleetController::roleNames() const {
    return {
        {SlotIdRole, "slotId"},
        {LabelRole, "label"},
        {EndpointRole, "endpoint"},
        {OnlineRole, "online"},
        {BusyRole, "busy"},
        {LoadedRole, "loaded"},
        {AuthorityRole, "authority"},
    };
}

QVariant IedBrowserFleetController::data(const QModelIndex& index, const int role) const {
    const auto* entry = entryAt(index.row());
    if (!entry || !index.isValid() || index.column() != 0) return {};
    const auto* context = entry->context.get();
    const auto* session = entry->session.get();
    switch (role) {
    case SlotIdRole: return static_cast<qulonglong>(entry->id);
    case LabelRole:
        if (!context->iedName().isEmpty()) return context->iedName();
        if (context->selectionRequired()) return QStringLiteral("Select IED · %1").arg(entry->id);
        return QStringLiteral("IED %1").arg(entry->id);
    case EndpointRole: return session->endpoint();
    case OnlineRole: return session->connected();
    case BusyRole: return session->busy() || entry->engineering->busy();
    case LoadedRole: return context->loaded();
    case AuthorityRole: return context->authorityKey();
    default: return {};
    }
}

IedBrowserFleetController::Entry* IedBrowserFleetController::entryAt(const int index) const noexcept {
    return index >= 0 && index < workspaceCount()
        ? entries_[static_cast<std::size_t>(index)].get() : nullptr;
}
IedBrowserFleetController::Entry* IedBrowserFleetController::activeEntry() const noexcept {
    return entryAt(activeIndex_);
}
IedEngineeringContextController* IedBrowserFleetController::contextAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->context.get() : nullptr;
}
IedBrowserSessionController* IedBrowserFleetController::sessionAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->session.get() : nullptr;
}
MmsClientController* IedBrowserFleetController::clientAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->client.get() : nullptr;
}
MmsReportController* IedBrowserFleetController::reportsAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->reports.get() : nullptr;
}
MmsControlController* IedBrowserFleetController::controlsAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->controls.get() : nullptr;
}
MmsFileSettingsController* IedBrowserFleetController::utilitiesAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->utilities.get() : nullptr;
}
SclWorkspaceController* IedBrowserFleetController::engineeringAt(const int index) const {
    auto* entry = entryAt(index);
    return entry ? entry->engineering.get() : nullptr;
}
IedEngineeringContextController* IedBrowserFleetController::activeContext() const {
    return contextAt(activeIndex_);
}
IedBrowserSessionController* IedBrowserFleetController::activeSession() const {
    return sessionAt(activeIndex_);
}
MmsClientController* IedBrowserFleetController::activeClient() const {
    return clientAt(activeIndex_);
}
MmsReportController* IedBrowserFleetController::activeReports() const {
    return reportsAt(activeIndex_);
}
MmsControlController* IedBrowserFleetController::activeControls() const {
    return controlsAt(activeIndex_);
}
MmsFileSettingsController* IedBrowserFleetController::activeUtilities() const {
    return utilitiesAt(activeIndex_);
}
SclWorkspaceController* IedBrowserFleetController::activeEngineering() const {
    return engineeringAt(activeIndex_);
}

void IedBrowserFleetController::connectEntry(Entry* entry) {
    const auto id = entry->id;
    const auto notify = [this, id] {
        const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& item) {
            return item->id == id;
        });
        if (found == entries_.end()) return;
        const auto row = static_cast<int>(std::distance(entries_.begin(), found));
        emit dataChanged(index(row), index(row));
    };
    connect(entry->context.get(), &IedEngineeringContextController::contextChanged, this, notify);
    connect(entry->context.get(), &IedEngineeringContextController::runtimeChanged, this, notify);
    connect(entry->session.get(), &IedBrowserSessionController::stateChanged, this, notify);
    connect(entry->session.get(), &IedBrowserSessionController::configurationChanged, this, notify);
    connect(entry->engineering.get(), &SclWorkspaceController::stateChanged, this, notify);
}

bool IedBrowserFleetController::fail(const QString& message) {
    lastError_ = message;
    emit stateChanged();
    return false;
}
void IedBrowserFleetController::clearError() {
    if (lastError_.isEmpty()) return;
    lastError_.clear();
    emit stateChanged();
}

bool IedBrowserFleetController::switchTo(const int index) {
    if (!entryAt(index)) return fail(QStringLiteral("The requested IED workspace does not exist."));
    clearError();
    if (activeIndex_ == index) return true;
    activeIndex_ = index;
    emit activeChanged();
    return true;
}

bool IedBrowserFleetController::newWorkspace() {
    if (workspaceCount() >= maximumWorkspaces) {
        return fail(QStringLiteral("Maximum of %1 concurrent IED workspaces reached.")
                        .arg(maximumWorkspaces));
    }
    const int next = workspaceCount();
    beginInsertRows({}, next, next);
    entries_.push_back(std::make_unique<Entry>(nextSlotId_++));
    endInsertRows();
    connectEntry(entries_.back().get());
    clearError();
    emit workspacesChanged();
    activeIndex_ = next;
    emit activeChanged();
    return true;
}

bool IedBrowserFleetController::prepareForNewSource() {
    const auto* entry = activeEntry();
    if (!entry) return fail(QStringLiteral("No active IED workspace."));
    if (!entry->context->hasSource() && !entry->session->connected() &&
        !entry->session->busy() && !entry->engineering->busy()) {
        clearError();
        return true;
    }
    return newWorkspace();
}

bool IedBrowserFleetController::openSclIedInNewWorkspace(const QString& iedName) {
    const auto* original = activeEntry();
    if (!original || original->context->authorityKey() != QStringLiteral("scl") ||
        !original->context->sourceSclDocument()) {
        return fail(QStringLiteral("Open an SCL engineering source before adding another IED."));
    }
    const auto name = iedName.trimmed();
    if (name.isEmpty() || !original->context->candidateIeds().contains(name)) {
        return fail(QStringLiteral("Selected IED is not present in the active SCL source."));
    }
    const auto sourcePath = original->context->sourcePath();
    for (int row = 0; row < workspaceCount(); ++row) {
        const auto* existing = contextAt(row);
        if (existing->authorityKey() == QStringLiteral("scl") &&
            existing->sourcePath() == sourcePath && existing->iedName() == name) {
            return switchTo(row);
        }
    }
    const auto* source = original->context->sourceSclDocument();
    if (!newWorkspace()) return false;
    if (activeContext()->publishSclDocument(*source, sourcePath, name)) {
        // Each cloned context retains the exact file provenance for its own
        // independent SCL-assisted association, without re-reading the file.
        activeSession()->setTrustedSclPath(sourcePath);
        // Preserve exact source bytes when this SCL came from a parsed file.
        // No file reload is needed and the selected IED is not republished.
        static_cast<void>(activeEngineering()->adoptSourceFrom(*original->engineering));
        return true;
    }
    const auto error = activeContext()->lastError();
    const auto createdIndex = activeIndex_;
    // Failed source creation is discarded; the original slot remains intact.
    static_cast<void>(closeWorkspace(createdIndex));
    return fail(error.isEmpty() ? QStringLiteral("Cannot activate requested SCL IED.") : error);
}

bool IedBrowserFleetController::closeWorkspace(const int index) {
    auto* entry = entryAt(index);
    if (!entry) return fail(QStringLiteral("The requested IED workspace does not exist."));
    if (workspaceCount() <= 1) {
        return fail(QStringLiteral("Keep at least one IED workspace open."));
    }
    if (entry->session->connected() || entry->session->busy() ||
        entry->reports->connected() || entry->reports->busy() ||
        entry->reports->cleanupRequired() ||
        entry->utilities->connected() || entry->utilities->busy() ||
        entry->controls->connected() || entry->controls->busy() ||
        entry->engineering->busy()) {
        return fail(QStringLiteral(
            "Disconnect this IED and finish pending service operations before closing its workspace."));
    }
    beginRemoveRows({}, index, index);
    auto retired = std::move(entries_[static_cast<std::size_t>(index)]);
    entries_.erase(entries_.begin() + index);
    endRemoveRows();
    clearError();
    emit workspacesChanged();
    if (activeIndex_ > index) --activeIndex_;
    else if (activeIndex_ == index) activeIndex_ = std::min(index, workspaceCount() - 1);
    emit activeChanged();
    // Retire services only AFTER Qt views process the row removal and all
    // active QObject bindings have been retargeted away from the removed IED.
    retired.reset();
    return true;
}

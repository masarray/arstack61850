// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedCommissioningModel.hpp"

#include <QSet>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}

QString logicalNodeName(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
}

QString objectReference(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstring(entry.ied_name) + qstring(entry.ld_inst) + QLatin1Char('/') +
        logicalNodeName(entry) + QLatin1Char('.') + qstring(entry.do_name);
}

QString mmsItem(const ar::iec61850::scl::SclDataSetEntry& entry) {
    auto dataObject = qstring(entry.do_name);
    dataObject.replace(QLatin1Char('.'), QLatin1Char('$'));
    auto dataAttribute = qstring(entry.da_name);
    dataAttribute.replace(QLatin1Char('.'), QLatin1Char('$'));
    QStringList parts{
        logicalNodeName(entry),
        qstring(entry.functional_constraint),
        dataObject};
    if (!dataAttribute.isEmpty()) parts.push_back(dataAttribute);
    parts.removeAll(QString{});
    return parts.join(QLatin1Char('$'));
}

QString reportBindingStatus(const ar::iec61850::scl::SclDataSetBindingStatus status) {
    using Status = ar::iec61850::scl::SclDataSetBindingStatus;
    switch (status) {
    case Status::resolved: return QStringLiteral("Bound");
    case Status::resolved_empty: return QStringLiteral("Empty DataSet");
    case Status::unresolved: return QStringLiteral("Unresolved");
    case Status::not_specified: return QStringLiteral("No DataSet");
    }
    return QStringLiteral("Unknown");
}

QString normalizedKind(QString value) {
    value = value.trimmed();
    if (value.compare(QStringLiteral("DataSet"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("DataSet");
    }
    if (value.compare(QStringLiteral("Report"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Report");
    }
    if (value.compare(QStringLiteral("GOOSE"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("GOOSE");
    }
    if (value.compare(QStringLiteral("Control"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Control");
    }
    return QStringLiteral("All");
}

int boundedSize(const std::size_t size) {
    return static_cast<int>(std::min<std::size_t>(
        size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}
} // namespace

IedCommissioningModel::IedCommissioningModel(QObject* parent)
    : QObject(parent) {}

void IedCommissioningModel::setBackend(IedFleetController* backend) {
    if (backend_ == backend) return;
    if (backend_ != nullptr) disconnect(backend_, nullptr, this, nullptr);
    backend_ = backend;
    if (backend_ != nullptr) {
        connect(backend_, &IedFleetController::modelChanged, this, &IedCommissioningModel::rebuild);
        connect(
            backend_,
            &IedFleetController::selectionChanged,
            this,
            &IedCommissioningModel::synchronize);
    }
    synchronizedSessionKey_.clear();
    synchronizedDocumentCount_ = -1;
    rebuild();
    emit backendChanged();
}

void IedCommissioningModel::setKindFilter(const QString& filter) {
    const auto normalized = normalizedKind(filter);
    if (kindFilter_ == normalized) return;
    kindFilter_ = normalized;
    applyFilter();
    emit filterChanged();
}

void IedCommissioningModel::setFilterText(const QString& filter) {
    const auto normalized = filter.trimmed();
    if (filterText_ == normalized) return;
    filterText_ = normalized;
    applyFilter();
    emit filterChanged();
}

void IedCommissioningModel::synchronize() {
    QString sessionKey;
    if (backend_ != nullptr && backend_->selectedIedIndex_ >= 0 &&
        backend_->selectedIedIndex_ < backend_->ieds_.size()) {
        sessionKey = backend_->ieds_.at(backend_->selectedIedIndex_)
                         .toMap()
                         .value(QStringLiteral("sessionKey"))
                         .toString();
    }
    const auto documentCount = backend_ == nullptr
        ? qsizetype{0}
        : static_cast<qsizetype>(backend_->documents_.size());
    if (sessionKey == synchronizedSessionKey_ && documentCount == synchronizedDocumentCount_) return;
    rebuild();
}

void IedCommissioningModel::rebuild() {
    const QString previousIdentity =
        selectedHandleIndex_ >= 0 && selectedHandleIndex_ < handles_.size()
        ? handles_.at(selectedHandleIndex_).identity
        : QString{};

    handles_.clear();
    visibleHandles_.clear();
    selectedHandleIndex_ = -1;
    dataSetCount_ = 0;
    reportCount_ = 0;
    gooseCount_ = 0;
    controlCount_ = 0;
    synchronizedSessionKey_.clear();
    synchronizedDocumentCount_ = backend_ == nullptr
        ? qsizetype{0}
        : static_cast<qsizetype>(backend_->documents_.size());

    if (backend_ == nullptr || backend_->selectedIedIndex_ < 0 ||
        backend_->selectedIedIndex_ >= backend_->ieds_.size()) {
        ++revision_;
        emit modelChanged();
        emit selectionChanged();
        return;
    }

    const auto ied = backend_->ieds_.at(backend_->selectedIedIndex_).toMap();
    const auto iedName = ied.value(QStringLiteral("name")).toString();
    const int documentIndex = ied.value(QStringLiteral("documentIndex"), -1).toInt();
    synchronizedSessionKey_ = ied.value(QStringLiteral("sessionKey")).toString();
    if (iedName.isEmpty() || documentIndex < 0 ||
        documentIndex >= static_cast<int>(backend_->documents_.size())) {
        ++revision_;
        emit modelChanged();
        emit selectionChanged();
        return;
    }

    const auto& document = backend_->documents_[static_cast<std::size_t>(documentIndex)].document;
    const auto append = [this, documentIndex](
                            const Kind kind,
                            const int sourceIndex,
                            QString identity) {
        Handle handle;
        handle.kind = kind;
        handle.documentIndex = documentIndex;
        handle.sourceIndex = sourceIndex;
        handle.identity = kindName(kind) + QLatin1Char('\x1f') + std::move(identity);
        handles_.push_back(std::move(handle));
    };

    for (int index = 0; index < boundedSize(document.data_sets.size()); ++index) {
        const auto& dataSet = document.data_sets[static_cast<std::size_t>(index)];
        if (qstring(dataSet.ied_name) != iedName) continue;
        auto identity = qstring(dataSet.reference);
        if (identity.isEmpty()) identity = qstring(dataSet.name);
        append(Kind::dataSet, index, identity);
        ++dataSetCount_;
    }
    for (int index = 0; index < boundedSize(document.report_controls.size()); ++index) {
        const auto& report = document.report_controls[static_cast<std::size_t>(index)];
        if (qstring(report.ied_name) != iedName) continue;
        auto identity = qstring(report.control_block_reference);
        if (identity.isEmpty()) identity = qstring(report.name);
        append(Kind::report, index, identity);
        ++reportCount_;
    }
    for (int index = 0; index < boundedSize(document.goose_streams.size()); ++index) {
        const auto& goose = document.goose_streams[static_cast<std::size_t>(index)];
        if (qstring(goose.ied_name) != iedName) continue;
        auto identity = qstring(goose.control_block_reference);
        if (identity.isEmpty()) identity = qstring(goose.control_name);
        append(Kind::goose, index, identity);
        ++gooseCount_;
    }

    QSet<QString> controls;
    for (int index = 0; index < boundedSize(document.model_entries.size()); ++index) {
        const auto& entry = document.model_entries[static_cast<std::size_t>(index)];
        if (qstring(entry.ied_name) != iedName ||
            qstring(entry.functional_constraint).compare(QStringLiteral("CF"), Qt::CaseInsensitive) != 0 ||
            qstring(entry.da_name).compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) != 0 ||
            qstring(entry.configured_value).trimmed().isEmpty()) {
            continue;
        }
        const auto identity = objectReference(entry);
        if (identity.isEmpty() || controls.contains(identity)) continue;
        controls.insert(identity);
        append(Kind::control, index, identity);
        ++controlCount_;
    }

    if (!previousIdentity.isEmpty()) {
        for (int index = 0; index < handles_.size(); ++index) {
            if (handles_.at(index).identity == previousIdentity) {
                selectedHandleIndex_ = index;
                break;
            }
        }
    }
    applyFilter(false);
}

void IedCommissioningModel::applyFilter(const bool preserveSelection) {
    visibleHandles_.clear();
    visibleHandles_.reserve(handles_.size());
    for (int index = 0; index < handles_.size(); ++index) {
        const auto& handle = handles_.at(index);
        if (kindFilter_ != QStringLiteral("All") && kindName(handle.kind) != kindFilter_) continue;
        if (!matches(handle)) continue;
        visibleHandles_.push_back(index);
    }

    if (!preserveSelection || !visibleHandles_.contains(selectedHandleIndex_)) {
        selectedHandleIndex_ = visibleHandles_.isEmpty() ? -1 : visibleHandles_.constFirst();
    }
    ++revision_;
    emit modelChanged();
    emit selectionChanged();
}

QString IedCommissioningModel::kindName(const Kind kind) {
    switch (kind) {
    case Kind::dataSet: return QStringLiteral("DataSet");
    case Kind::report: return QStringLiteral("Report");
    case Kind::goose: return QStringLiteral("GOOSE");
    case Kind::control: return QStringLiteral("Control");
    }
    return QStringLiteral("Unknown");
}

QVariantMap IedCommissioningModel::mapForHandle(
    const Handle& handle,
    const int catalogIndex) const {
    QVariantMap item;
    if (backend_ == nullptr || handle.documentIndex < 0 ||
        handle.documentIndex >= static_cast<int>(backend_->documents_.size())) {
        return item;
    }
    const auto& document =
        backend_->documents_[static_cast<std::size_t>(handle.documentIndex)].document;
    item.insert(QStringLiteral("catalogIndex"), catalogIndex);
    item.insert(QStringLiteral("kind"), kindName(handle.kind));
    item.insert(QStringLiteral("selected"), catalogIndex == selectedHandleIndex_);

    switch (handle.kind) {
    case Kind::dataSet: {
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.data_sets.size())) return {};
        const auto& dataSet = document.data_sets[static_cast<std::size_t>(handle.sourceIndex)];
        item.insert(QStringLiteral("name"), qstring(dataSet.name));
        item.insert(QStringLiteral("reference"), qstring(dataSet.reference));
        item.insert(QStringLiteral("dataSetReference"), qstring(dataSet.reference));
        item.insert(QStringLiteral("memberCount"), boundedSize(dataSet.entries.size()));
        item.insert(QStringLiteral("status"), QStringLiteral("Configured"));
        item.insert(QStringLiteral("logicalNode"), qstring(dataSet.logical_node_path));
        item.insert(QStringLiteral("logicalDevice"), qstring(dataSet.ld_inst));
        item.insert(
            QStringLiteral("summary"),
            QStringLiteral("%1 canonical FCDA member%2")
                .arg(static_cast<qulonglong>(dataSet.entries.size()))
                .arg(dataSet.entries.size() == 1 ? QString{} : QStringLiteral("s")));
        break;
    }
    case Kind::report: {
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.report_controls.size())) return {};
        const auto& report = document.report_controls[static_cast<std::size_t>(handle.sourceIndex)];
        item.insert(QStringLiteral("name"), qstring(report.name));
        item.insert(QStringLiteral("reference"), qstring(report.control_block_reference));
        item.insert(QStringLiteral("dataSetReference"), qstring(report.data_set_reference));
        item.insert(QStringLiteral("memberCount"), boundedSize(report.entries.size()));
        item.insert(QStringLiteral("status"), reportBindingStatus(report.data_set_binding_status));
        item.insert(QStringLiteral("buffered"), report.buffered);
        item.insert(QStringLiteral("reportId"), qstring(report.report_id));
        item.insert(QStringLiteral("confRev"), static_cast<qulonglong>(report.configuration_revision));
        item.insert(QStringLiteral("bufferTimeMs"), static_cast<qulonglong>(report.buffer_time_milliseconds));
        item.insert(QStringLiteral("integrityMs"), static_cast<qulonglong>(report.integrity_period_milliseconds));
        item.insert(QStringLiteral("logicalNode"), qstring(report.logical_node_path));
        item.insert(
            QStringLiteral("summary"),
            QStringLiteral("%1 · ConfRev %2 · %3 member%4")
                .arg(report.buffered ? QStringLiteral("BRCB") : QStringLiteral("URCB"))
                .arg(static_cast<qulonglong>(report.configuration_revision))
                .arg(static_cast<qulonglong>(report.entries.size()))
                .arg(report.entries.size() == 1 ? QString{} : QStringLiteral("s")));
        break;
    }
    case Kind::goose: {
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.goose_streams.size())) return {};
        const auto& goose = document.goose_streams[static_cast<std::size_t>(handle.sourceIndex)];
        const bool addressed = goose.address.app_id.has_value() && goose.address.destination_mac.has_value();
        item.insert(QStringLiteral("name"), qstring(goose.control_name));
        item.insert(QStringLiteral("reference"), qstring(goose.control_block_reference));
        item.insert(QStringLiteral("dataSetReference"), qstring(goose.data_set_reference));
        item.insert(QStringLiteral("memberCount"), boundedSize(goose.entries.size()));
        item.insert(
            QStringLiteral("status"),
            goose.entries.empty()
                ? QStringLiteral("No members")
                : addressed ? QStringLiteral("Bound + addressed") : QStringLiteral("Bound"));
        item.insert(QStringLiteral("goId"), qstring(goose.go_id));
        item.insert(QStringLiteral("confRev"), static_cast<qulonglong>(goose.configuration_revision));
        item.insert(QStringLiteral("minTimeMs"), static_cast<qulonglong>(goose.min_time_milliseconds));
        item.insert(QStringLiteral("maxTimeMs"), static_cast<qulonglong>(goose.max_time_milliseconds));
        item.insert(QStringLiteral("appId"), qstring(goose.address.app_id_text));
        item.insert(QStringLiteral("mac"), qstring(goose.address.destination_mac_text));
        item.insert(
            QStringLiteral("vlanId"),
            goose.address.vlan_id.has_value()
                ? QVariant{static_cast<int>(*goose.address.vlan_id)} : QVariant{});
        item.insert(
            QStringLiteral("vlanPriority"),
            goose.address.vlan_priority.has_value()
                ? QVariant{static_cast<int>(*goose.address.vlan_priority)} : QVariant{});
        item.insert(
            QStringLiteral("summary"),
            QStringLiteral("APPID %1 · %2 member%3")
                .arg(qstring(goose.address.app_id_text).isEmpty()
                         ? QStringLiteral("—") : qstring(goose.address.app_id_text))
                .arg(static_cast<qulonglong>(goose.entries.size()))
                .arg(goose.entries.size() == 1 ? QString{} : QStringLiteral("s")));
        break;
    }
    case Kind::control: {
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.model_entries.size())) return {};
        const auto& entry = document.model_entries[static_cast<std::size_t>(handle.sourceIndex)];
        item.insert(QStringLiteral("name"), qstring(entry.do_name));
        item.insert(QStringLiteral("reference"), objectReference(entry));
        item.insert(QStringLiteral("dataSetReference"), QString{});
        item.insert(QStringLiteral("memberCount"), 0);
        item.insert(QStringLiteral("status"), QStringLiteral("Configured"));
        item.insert(QStringLiteral("controlModel"), qstring(entry.configured_value));
        item.insert(QStringLiteral("cdc"), qstring(entry.cdc));
        item.insert(QStringLiteral("logicalNode"), logicalNodeName(entry));
        item.insert(QStringLiteral("logicalDevice"), qstring(entry.ld_inst));
        item.insert(
            QStringLiteral("summary"),
            QStringLiteral("%1 · %2")
                .arg(qstring(entry.cdc), qstring(entry.configured_value)));
        break;
    }
    }
    return item;
}

QVariantMap IedCommissioningModel::item(const int visibleRow) const {
    if (visibleRow < 0 || visibleRow >= visibleHandles_.size()) return {};
    const int catalogIndex = visibleHandles_.at(visibleRow);
    if (catalogIndex < 0 || catalogIndex >= handles_.size()) return {};
    return mapForHandle(handles_.at(catalogIndex), catalogIndex);
}

QVariantMap IedCommissioningModel::selectedItem() const {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return {};
    return mapForHandle(handles_.at(selectedHandleIndex_), selectedHandleIndex_);
}

const std::vector<ar::iec61850::scl::SclDataSetEntry>* IedCommissioningModel::entriesFor(
    const Handle& handle) const noexcept {
    if (backend_ == nullptr || handle.documentIndex < 0 ||
        handle.documentIndex >= static_cast<int>(backend_->documents_.size())) return nullptr;
    const auto& document =
        backend_->documents_[static_cast<std::size_t>(handle.documentIndex)].document;
    switch (handle.kind) {
    case Kind::dataSet:
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.data_sets.size())) return nullptr;
        return &document.data_sets[static_cast<std::size_t>(handle.sourceIndex)].entries;
    case Kind::report:
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.report_controls.size())) return nullptr;
        return &document.report_controls[static_cast<std::size_t>(handle.sourceIndex)].entries;
    case Kind::goose:
        if (handle.sourceIndex < 0 ||
            handle.sourceIndex >= static_cast<int>(document.goose_streams.size())) return nullptr;
        return &document.goose_streams[static_cast<std::size_t>(handle.sourceIndex)].entries;
    case Kind::control:
        return nullptr;
    }
    return nullptr;
}

int IedCommissioningModel::memberCount() const noexcept {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return 0;
    const auto* entries = entriesFor(handles_.at(selectedHandleIndex_));
    return entries == nullptr ? 0 : boundedSize(entries->size());
}

QVariantMap IedCommissioningModel::member(const int row) const {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return {};
    const auto* entries = entriesFor(handles_.at(selectedHandleIndex_));
    if (entries == nullptr || row < 0 || row >= boundedSize(entries->size())) return {};
    const auto& entry = entries->at(static_cast<std::size_t>(row));
    QVariantMap item;
    item.insert(QStringLiteral("index"), row);
    item.insert(QStringLiteral("reference"), qstring(entry.signal_reference));
    item.insert(QStringLiteral("logicalDevice"), qstring(entry.ld_inst));
    item.insert(QStringLiteral("logicalNode"), logicalNodeName(entry));
    item.insert(QStringLiteral("dataObject"), qstring(entry.do_name));
    item.insert(QStringLiteral("dataAttribute"), qstring(entry.da_name));
    item.insert(QStringLiteral("fc"), qstring(entry.functional_constraint));
    item.insert(QStringLiteral("cdc"), qstring(entry.cdc));
    item.insert(QStringLiteral("type"), qstring(entry.basic_type));
    item.insert(QStringLiteral("configuredValue"), qstring(entry.configured_value));
    item.insert(QStringLiteral("quality"), entry.is_quality);
    item.insert(QStringLiteral("timestamp"), entry.is_timestamp);
    item.insert(QStringLiteral("mmsDomain"), qstring(entry.ied_name) + qstring(entry.ld_inst));
    item.insert(QStringLiteral("mmsItem"), mmsItem(entry));
    return item;
}

void IedCommissioningModel::select(const int visibleRow) {
    if (visibleRow < 0 || visibleRow >= visibleHandles_.size()) return;
    const int catalogIndex = visibleHandles_.at(visibleRow);
    if (selectedHandleIndex_ == catalogIndex) return;
    selectedHandleIndex_ = catalogIndex;
    ++revision_;
    emit modelChanged();
    emit selectionChanged();
}

QString IedCommissioningModel::dataSetReferenceFor(const Handle& handle) const {
    const auto item = mapForHandle(handle, -1);
    return item.value(QStringLiteral("dataSetReference")).toString();
}

bool IedCommissioningModel::focusBoundDataSet() {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return false;
    const auto& selected = handles_.at(selectedHandleIndex_);
    if (selected.kind != Kind::report && selected.kind != Kind::goose) return false;
    const auto target = dataSetReferenceFor(selected);
    if (target.isEmpty()) return false;

    int targetIndex = -1;
    for (int index = 0; index < handles_.size(); ++index) {
        const auto& candidate = handles_.at(index);
        if (candidate.kind != Kind::dataSet) continue;
        if (dataSetReferenceFor(candidate) == target) {
            targetIndex = index;
            break;
        }
    }
    if (targetIndex < 0) return false;

    const bool filterStateChanged = kindFilter_ != QStringLiteral("DataSet") || !filterText_.isEmpty();
    kindFilter_ = QStringLiteral("DataSet");
    filterText_.clear();
    selectedHandleIndex_ = targetIndex;
    applyFilter(true);
    if (filterStateChanged) emit filterChanged();
    return true;
}

bool IedCommissioningModel::matches(const Handle& handle) const {
    if (filterText_.isEmpty()) return true;
    const auto item = mapForHandle(handle, -1);
    QString haystack;
    static const QStringList keys{
        QStringLiteral("kind"),
        QStringLiteral("name"),
        QStringLiteral("reference"),
        QStringLiteral("dataSetReference"),
        QStringLiteral("status"),
        QStringLiteral("summary"),
        QStringLiteral("reportId"),
        QStringLiteral("goId"),
        QStringLiteral("appId"),
        QStringLiteral("mac"),
        QStringLiteral("controlModel"),
        QStringLiteral("cdc")};
    for (const auto& key : keys) {
        haystack += item.value(key).toString();
        haystack += QLatin1Char('\n');
    }
    return haystack.contains(filterText_, Qt::CaseInsensitive);
}

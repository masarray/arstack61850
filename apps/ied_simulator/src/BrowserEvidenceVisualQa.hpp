// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Opt-in CI-only rendering. Typed QA fixtures enter the existing canonical
// MmsLiveTreeModel; neither protocol state nor normal UI startup is modified.
#include "IedBrowserFleetController.hpp"
#include "MmsReportController.hpp"
#include "MmsEngineeringEvidence.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace arstack::iedsim {
namespace detail {

class BrowserEvidenceVisualQa final
    : public std::enable_shared_from_this<BrowserEvidenceVisualQa> {
public:
    BrowserEvidenceVisualQa(QCoreApplication& app, QObject* root,
                            IedBrowserFleetController* fleet, QString directory)
        : app_(app), root_(root), fleet_(fleet), directory_(std::move(directory)) {}
    void start() { schedule(100); }

private:
    struct Attribute {
        QString reference;
        QString domain;
        QString item;
        [[nodiscard]] bool valid() const {
            return !reference.isEmpty() && !domain.isEmpty() && !item.isEmpty();
        }
        [[nodiscard]] QString key() const { return domain + QChar(0x1f) + item; }
    };

    void fail(const char* reason) {
        qCritical().noquote() << "BROWSER_EVIDENCE_VISUAL_FAIL" << reason;
        app_.exit(72);
    }

    void schedule(const int ms) {
        const auto self = shared_from_this();
        QTimer::singleShot(ms, root_, [self] { self->advance(); });
    }

    [[nodiscard]] static Attribute attribute(const QVariantMap& node) {
        return {node.value(QStringLiteral("reference")).toString(),
                node.value(QStringLiteral("mmsDomain")).toString(),
                node.value(QStringLiteral("mmsItem")).toString()};
    }

    [[nodiscard]] bool prepare() {
        context_ = fleet_ ? fleet_->activeContext() : nullptr;
        reports_ = fleet_ ? fleet_->activeReports() : nullptr;
        model_ = context_ ? context_->treeModel() : nullptr;
        if (!context_ || !reports_ || !model_ || !context_->loaded() ||
            context_->selectionRequired() || model_->totalNodeCount() <= 0) return false;

        // Find both typed DAs in an actual canonical DataSet. No guessed
        // index, synthesized reference, or second semantic inventory.
        for (int index = 0; index < reports_->dataSets().size(); ++index) {
            if (!reports_->selectDataSet(index)) continue;
            Attribute q, t;
            for (const auto& member : reports_->selectedDataSetMembers()) {
                const auto node = model_->nodeForReference(member);
                if (!node.value(QStringLiteral("readable")).toBool()) continue;
                const auto entry = attribute(node);
                if (entry.item.endsWith(QStringLiteral("$q"))) q = entry;
                else if (entry.item.endsWith(QStringLiteral("$t"))) t = entry;
            }
            if (q.valid() && t.valid()) {
                dataSetIndex_ = index;
                quality_ = q;
                timestamp_ = t;
                break;
            }
        }
        if (dataSetIndex_ < 0) return false;

        auto* host = root_->findChild<QQuickItem*>(QStringLiteral("perIedBrowserHost"));
        if (!host) return false;
        for (auto* item : host->childItems()) {
            if (item && item->objectName() == QStringLiteral("iedBrowserView_0")) {
                browser_ = item;
                break;
            }
        }
        if (!browser_ || !browser_->isVisible()) return false;
        modelPane_ = browser_->findChild<QQuickItem*>(QStringLiteral("browserDataModelPane"));
        dataSetPane_ = browser_->findChild<QQuickItem*>(QStringLiteral("browserDataSetPane"));
        modelEvidence_ = modelPane_ ? modelPane_->findChild<QQuickItem*>(
            QStringLiteral("browserDataModelTypedEvidence")) : nullptr;
        dataSetEvidence_ = dataSetPane_ ? dataSetPane_->findChild<QQuickItem*>(
            QStringLiteral("browserDataSetTypedEvidence")) : nullptr;
        window_ = qobject_cast<QQuickWindow*>(root_);
        if (!modelPane_ || !dataSetPane_ || !modelEvidence_ || !dataSetEvidence_ ||
            !window_ || window_->width() < 800 || window_->height() < 500) return false;

        const std::array<std::uint8_t, 2> bits{0x60U, 0x38U};
        const auto qSample = ar::iec61850::mms::MmsDataValue::bit_string(3U, bits);
        const ar::iec61850::mms::Iec61850UtcTime utc{
            std::chrono::system_clock::time_point{std::chrono::seconds{1720000000}}, 0xBFU};
        const auto tSample = ar::iec61850::mms::MmsDataValue::utc_time(utc);
        const auto qEvidence = decodeEngineeringEvidence(quality_.item, qSample);
        const auto tEvidence = decodeEngineeringEvidence(timestamp_.item, tSample);
        if (qEvidence.value(QStringLiteral("kind")).toString() != QStringLiteral("quality") ||
            tEvidence.value(QStringLiteral("kind")).toString() != QStringLiteral("timestamp"))
            return false;
        model_->applyReadValue(quality_.key(), QStringLiteral("QA typed Quality"), qEvidence);
        model_->applyReadValue(timestamp_.key(), QStringLiteral("QA typed UTC"), tEvidence);
        return QDir().mkpath(directory_);
    }

    [[nodiscard]] bool selectModel(const Attribute& entry) {
        browser_->setProperty("activeSection", 0);
        browser_->setProperty("activeSectionTitle", QStringLiteral("Data Model"));
        if (!model_->selectMmsItem(entry.domain, entry.item)) return false;
        auto* detail = qobject_cast<MmsLiveDetailModel*>(model_->detailModel());
        if (!detail) return false;
        detail->selectParent();  // Preserve the DO/DA tree beside inspector.
        for (int row = 0; row < detail->rowCount(); ++row) {
            if (detail->data(detail->index(row), MmsLiveDetailModel::ReferenceRole)
                    .toString() != entry.reference) continue;
            detail->select(row);  // Same selection path as an inline user click.
            return detail->rootKind() == QStringLiteral("DO") &&
                model_->selectedNode().value(QStringLiteral("reference")).toString()
                    == entry.reference;
        }
        return false;
    }

    [[nodiscard]] bool selectDataSet(const Attribute& entry) {
        browser_->setProperty("activeSection", 1);
        browser_->setProperty("activeSectionTitle", QStringLiteral("DataSets"));
        if (!reports_->selectDataSet(dataSetIndex_)) return false;
        QVariant accepted;
        const bool invoked = QMetaObject::invokeMethod(
            dataSetPane_, "selectEvidenceAttribute", Q_RETURN_ARG(QVariant, accepted),
            Q_ARG(QVariant, QVariant(entry.domain)),
            Q_ARG(QVariant, QVariant(entry.item)),
            Q_ARG(QVariant, QVariant(entry.reference)));
        return invoked && accepted.toBool() &&
            model_->selectedNode().value(QStringLiteral("reference")).toString()
                == entry.reference;
    }

    [[nodiscard]] bool visibleEvidence(QQuickItem* component, const QString& kind) const {
        return component && component->isVisible() &&
            component->property("hasEvidence").toBool() &&
            component->property("engineeringEvidence").toMap()
                .value(QStringLiteral("kind")).toString() == kind;
    }

    void capture(const char* path, QQuickItem* component, const QString& kind) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!visibleEvidence(component, kind)) {
            fail("typed_details_not_visible");
            return;
        }
        const QImage image = window_->grabWindow();
        if (image.isNull() || image.width() < 800 || image.height() < 500 ||
            !image.save(QDir(directory_).filePath(QString::fromLatin1(path)))) {
            fail("rendered_screenshot_invalid");
            return;
        }
        if (!previous_.isNull() && image == previous_) {
            fail("different_selected_values_produced_identical_screenshot");
            return;
        }
        previous_ = image;
        ++phase_;
        schedule(0);
    }

    void advance() {
        if (phase_ == 0) {
            auto* context = fleet_ ? fleet_->activeContext() : nullptr;
            if (!context || !context->loaded()) {
                if (++readinessChecks_ > 40) { fail("source_load_timeout"); return; }
                schedule(100);  // QA-only bounded readiness; never a runtime poll.
                return;
            }
            if (!prepare() || !selectModel(quality_)) {
                fail("quality_model_setup"); return;
            }
            phase_ = 1;
            schedule(150);
        } else if (phase_ == 1) {
            capture("data-model-quality.png", modelEvidence_, QStringLiteral("quality"));
        } else if (phase_ == 2) {
            if (!selectModel(timestamp_)) { fail("timestamp_model_selection"); return; }
            phase_ = 3; schedule(150);
        } else if (phase_ == 3) {
            capture("data-model-timestamp.png", modelEvidence_, QStringLiteral("timestamp"));
        } else if (phase_ == 4) {
            if (!selectDataSet(quality_)) { fail("quality_dataset_selection"); return; }
            phase_ = 5; schedule(150);
        } else if (phase_ == 5) {
            capture("datasets-quality.png", dataSetEvidence_, QStringLiteral("quality"));
        } else if (phase_ == 6) {
            if (!selectDataSet(timestamp_)) { fail("timestamp_dataset_selection"); return; }
            phase_ = 7; schedule(150);
        } else if (phase_ == 7) {
            capture("datasets-timestamp.png", dataSetEvidence_, QStringLiteral("timestamp"));
        } else if (phase_ == 8) {
            const std::array<std::uint8_t, 2> bits{0x60U, 0x38U};
            const auto malformedEvidence = decodeEngineeringEvidence(
                quality_.item, ar::iec61850::mms::MmsDataValue::bit_string(2U, bits));
            if (!malformedEvidence.isEmpty()) { fail("fabricated_malformed_evidence"); return; }
            model_->applyReadValue(quality_.key(), QStringLiteral("QA invalid quality"),
                                   malformedEvidence);
            if (!selectDataSet(quality_) ||
                visibleEvidence(dataSetEvidence_, QStringLiteral("quality")) ||
                !model_->selectedNode().value(QStringLiteral("engineeringEvidence"))
                    .toMap().isEmpty()) {
                fail("stale_quality_evidence"); return;
            }
            if (!reports_->selectDataSet(-1) ||
                dataSetPane_->property("showSelectedEvidence").toBool()) {
                fail("stale_dataset_selection"); return;
            }
            qInfo().noquote()
                << "BROWSER_EVIDENCE_VISUAL_PASS screenshots=4"
                << "data_model=q,t datasets=q,t malformed=unknown"
                << "stale_selection=cleared polling=false synthetic_qa=true";
            app_.exit(0);
        } else {
            fail("invalid_qa_phase");
        }
    }

    QCoreApplication& app_;
    QObject* root_{};
    IedBrowserFleetController* fleet_{};
    QString directory_;
    IedEngineeringContextController* context_{};
    MmsReportController* reports_{};
    MmsLiveTreeModel* model_{};
    QQuickWindow* window_{};
    QQuickItem* browser_{};
    QQuickItem* modelPane_{};
    QQuickItem* dataSetPane_{};
    QQuickItem* modelEvidence_{};
    QQuickItem* dataSetEvidence_{};
    Attribute quality_;
    Attribute timestamp_;
    QImage previous_;
    int dataSetIndex_{-1};
    int readinessChecks_{};
    int phase_{};
};

} // namespace detail

inline void runBrowserEvidenceVisualQa(
    QCoreApplication& app, QObject* root, IedBrowserFleetController* fleet,
    const QString& directory) {
    std::make_shared<detail::BrowserEvidenceVisualQa>(
        app, root, fleet, directory)->start();
}
} // namespace arstack::iedsim

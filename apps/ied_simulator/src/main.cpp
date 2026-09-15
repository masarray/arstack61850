// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace {
bool configureEndpoint(QObject* backend, const QString& specification, const int defaultPort) {
    const auto equals = specification.indexOf(QLatin1Char('='));
    if (equals <= 0 || equals >= specification.size() - 1) return false;

    bool indexOk{};
    const auto index = specification.left(equals).toInt(&indexOk);
    if (!indexOk || index < 0) return false;

    auto endpoint = specification.mid(equals + 1).trimmed();
    if (endpoint.isEmpty()) return false;

    int port = defaultPort;
    const auto colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool portOk{};
        const auto requestedPort = endpoint.mid(colon + 1).toInt(&portOk);
        if (!portOk || requestedPort < 1 || requestedPort > 65'535) return false;
        port = requestedPort;
        endpoint = endpoint.left(colon);
    }
    if (endpoint.isEmpty()) return false;

    bool configured{};
    const bool invoked = QMetaObject::invokeMethod(
        backend,
        "configureIedEndpoint",
        Q_RETURN_ARG(bool, configured),
        Q_ARG(int, index),
        Q_ARG(QString, endpoint),
        Q_ARG(int, port));
    return invoked && configured;
}
} // namespace

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack IED Lab"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ARStack IEC 61850 multi-IED simulation lab"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption sclOption{
        QStringLiteral("scl"),
        QStringLiteral("Import an engineering file before showing the window."),
        QStringLiteral("path")};
    const QCommandLineOption qaAsyncImportOption{
        QStringLiteral("qa-async-import"),
        QStringLiteral("QA: import one engineering file through the interactive bounded worker and exit."),
        QStringLiteral("path")};
    const QCommandLineOption qaAsyncImportRepeatOption{
        QStringLiteral("qa-async-import-repeat"),
        QStringLiteral("QA: repeat the bounded async import in one process for lifecycle/leak soak."),
        QStringLiteral("count"),
        QStringLiteral("1")};
    const QCommandLineOption qaRuntimeCyclesOption{
        QStringLiteral("qa-runtime-cycles"),
        QStringLiteral("QA: repeatedly start and stop the selected MMS runtime in one GUI process."),
        QStringLiteral("count")};
    const QCommandLineOption runtimeOption{
        QStringLiteral("runtime"),
        QStringLiteral("Start the selected MMS runtime after importing the model.")};
    const QCommandLineOption iedEndpointOption{
        QStringLiteral("ied-endpoint"),
        QStringLiteral(
            "Assign a fleet endpoint as INDEX=IPv4[:PORT]. Repeat for multiple IEDs."),
        QStringLiteral("assignment")};
    const QCommandLineOption startIedOption{
        QStringLiteral("start-ied"),
        QStringLiteral("Start one IED index after import. Repeat to start a fleet subset."),
        QStringLiteral("index")};
    const QCommandLineOption screenshotOption{
        QStringLiteral("screenshot"),
        QStringLiteral("Capture the rendered window and exit."),
        QStringLiteral("path")};
    const QCommandLineOption smokeOption{
        QStringLiteral("smoke-test"),
        QStringLiteral("Load the QML scene, wait briefly, and exit.")};
    const QCommandLineOption portOption{
        QStringLiteral("port"),
        QStringLiteral("Override the default MMS listen port."),
        QStringLiteral("number")};
    const QCommandLineOption setFirstValueOption{
        QStringLiteral("set-first-value"),
        QStringLiteral("QA: apply a value to the first runtime point after start."),
        QStringLiteral("value")};
    const QCommandLineOption qaLiveBurstOption{
        QStringLiteral("qa-live-burst"),
        QStringLiteral("QA: enqueue a bounded burst through the live runtime data plane."),
        QStringLiteral("count")};
    const QCommandLineOption exitAfterOption{
        QStringLiteral("exit-after-ms"),
        QStringLiteral("QA: exit after the specified runtime duration."),
        QStringLiteral("milliseconds")};
    parser.addOptions({
        sclOption,
        qaAsyncImportOption,
        qaAsyncImportRepeatOption,
        qaRuntimeCyclesOption,
        runtimeOption,
        iedEndpointOption,
        startIedOption,
        screenshotOption,
        smokeOption,
        portOption,
        setFirstValueOption,
        qaLiveBurstOption,
        exitAfterOption});
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("ARStack.IedSimulator", "Main");

    if (!engine.rootObjects().isEmpty()) {
        auto* const rootObject = engine.rootObjects().constFirst();
        auto* const backend = rootObject->findChild<QObject*>(QStringLiteral("simulatorBackend"));
        int defaultPort = 102;
        if (backend != nullptr && parser.isSet(portOption)) {
            bool valid{};
            const auto port = parser.value(portOption).toInt(&valid);
            if (valid && port >= 1 && port <= 65'535) {
                defaultPort = port;
                backend->setProperty("port", port);
            }
        }
        if (backend != nullptr && parser.isSet(sclOption)) {
            QMetaObject::invokeMethod(
                backend,
                "loadFile",
                Q_ARG(QUrl, QUrl::fromLocalFile(parser.value(sclOption))));
        }
        if (backend != nullptr && parser.isSet(qaAsyncImportOption)) {
            bool repeatOk{};
            const auto parsedRepeat = parser.value(qaAsyncImportRepeatOption).toInt(&repeatOk);
            const int repeatCount = repeatOk ? std::clamp(parsedRepeat, 1, 100) : 1;
            const auto importUrl = QUrl::fromLocalFile(parser.value(qaAsyncImportOption));
            auto iteration = std::make_shared<int>(0);

            bool accepted{};
            const bool invoked = QMetaObject::invokeMethod(
                backend,
                "loadFileAsync",
                Q_RETURN_ARG(bool, accepted),
                Q_ARG(QUrl, importUrl));
            if (!invoked || !accepted) {
                qWarning().noquote() << "Async import request was rejected.";
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(5); });
            } else {
                auto* const importTimer = new QTimer{backend};
                importTimer->setInterval(25);
                QObject::connect(
                    importTimer,
                    &QTimer::timeout,
                    backend,
                    [&app, backend, importTimer, importUrl, repeatCount, iteration] {
                        if (backend->property("importing").toBool()) return;
                        if (!backend->property("imported").toBool()) {
                            importTimer->stop();
                            importTimer->deleteLater();
                            qWarning().noquote()
                                << "Async import failed:"
                                << backend->property("fatalError").toString();
                            app.exit(6);
                            return;
                        }

                        ++(*iteration);
                        qInfo().noquote()
                            << "ASYNC_IMPORT_ITERATION"
                            << *iteration
                            << backend->property("sourceName").toString();
                        if (*iteration < repeatCount) {
                            bool nextAccepted{};
                            const bool nextInvoked = QMetaObject::invokeMethod(
                                backend,
                                "loadFileAsync",
                                Q_RETURN_ARG(bool, nextAccepted),
                                Q_ARG(QUrl, importUrl));
                            if (nextInvoked && nextAccepted) return;

                            importTimer->stop();
                            importTimer->deleteLater();
                            qWarning().noquote() << "Repeated async import request was rejected.";
                            app.exit(5);
                            return;
                        }

                        importTimer->stop();
                        importTimer->deleteLater();
                        qInfo().noquote()
                            << "ASYNC_IMPORT_OK"
                            << backend->property("sourceName").toString()
                            << "iterations=" << *iteration;
                        app.exit(0);
                    });
                importTimer->start();
                QTimer::singleShot(60'000, backend, [&app, importTimer] {
                    if (!importTimer->isActive()) return;
                    importTimer->stop();
                    importTimer->deleteLater();
                    qWarning().noquote() << "Async import timed out.";
                    app.exit(7);
                });
            }
        }

        bool fleetConfigurationValid = true;
        if (backend != nullptr) {
            for (const auto& specification : parser.values(iedEndpointOption)) {
                if (configureEndpoint(backend, specification, defaultPort)) continue;
                fleetConfigurationValid = false;
                qWarning().noquote() << "Invalid or rejected --ied-endpoint:" << specification;
            }
        }

        bool qaRuntimeCycleScheduled = false;
        if (backend != nullptr && fleetConfigurationValid && parser.isSet(qaRuntimeCyclesOption)) {
            bool countOk{};
            const auto requested = parser.value(qaRuntimeCyclesOption).toInt(&countOk);
            const int cycleCount = countOk ? std::clamp(requested, 1, 100) : 0;
            if (cycleCount <= 0 || !backend->property("imported").toBool()) {
                fleetConfigurationValid = false;
                qWarning().noquote()
                    << "--qa-runtime-cycles requires a positive count and a successfully loaded --scl model.";
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(8); });
            } else {
                qaRuntimeCycleScheduled = true;
                auto cycle = std::make_shared<int>(0);
                auto phase = std::make_shared<int>(0);
                auto* const cycleTimer = new QTimer{backend};
                cycleTimer->setInterval(25);
                QObject::connect(
                    cycleTimer,
                    &QTimer::timeout,
                    backend,
                    [&app, backend, cycleTimer, cycleCount, cycle, phase] {
                        if (*phase == 0) {
                            bool started{};
                            const bool invoked = QMetaObject::invokeMethod(
                                backend,
                                "startSimulation",
                                Q_RETURN_ARG(bool, started));
                            if (!invoked || !started) {
                                cycleTimer->stop();
                                cycleTimer->deleteLater();
                                qWarning().noquote()
                                    << "Runtime lifecycle soak could not start cycle"
                                    << (*cycle + 1);
                                app.exit(9);
                                return;
                            }
                            *phase = 1;
                            return;
                        }

                        if (*phase == 1) {
                            if (!backend->property("running").toBool()) return;
                            qInfo().noquote() << "RUNTIME_CYCLE_STARTED" << (*cycle + 1);

                            // Hold the second restarted process beyond the 900 ms
                            // delayed-kill grace period. A stale timer from the
                            // first stop must not be able to kill this generation.
                            if (*cycle == 1) {
                                *phase = 3;
                                QTimer::singleShot(1'100, backend, [phase] { *phase = 4; });
                                return;
                            }

                            QMetaObject::invokeMethod(backend, "stopSimulation");
                            *phase = 2;
                            return;
                        }

                        if (*phase == 3) {
                            if (backend->property("running").toBool()) return;
                            cycleTimer->stop();
                            cycleTimer->deleteLater();
                            qWarning().noquote()
                                << "Runtime died inside the delayed-kill restart guard window.";
                            app.exit(11);
                            return;
                        }

                        if (*phase == 4) {
                            if (!backend->property("running").toBool()) {
                                cycleTimer->stop();
                                cycleTimer->deleteLater();
                                qWarning().noquote()
                                    << "Runtime was not alive after the delayed-kill restart guard window.";
                                app.exit(11);
                                return;
                            }
                            qInfo().noquote() << "RUNTIME_RESTART_GUARD_PASS cycle=" << (*cycle + 1);
                            QMetaObject::invokeMethod(backend, "stopSimulation");
                            *phase = 2;
                            return;
                        }

                        if (backend->property("anyRunning").toBool()) return;
                        ++(*cycle);
                        qInfo().noquote() << "RUNTIME_CYCLE_FINISHED" << *cycle;
                        if (*cycle >= cycleCount) {
                            cycleTimer->stop();
                            cycleTimer->deleteLater();
                            qInfo().noquote() << "RUNTIME_CYCLE_SOAK_PASS cycles=" << *cycle;
                            app.exit(0);
                            return;
                        }
                        *phase = 0;
                    });
                cycleTimer->start();
                QTimer::singleShot(45'000, backend, [&app, cycleTimer] {
                    if (!cycleTimer->isActive()) return;
                    cycleTimer->stop();
                    cycleTimer->deleteLater();
                    qWarning().noquote() << "Runtime lifecycle soak timed out.";
                    app.exit(10);
                });
            }
        }

        if (!fleetConfigurationValid) {
            QTimer::singleShot(0, &app, [] { QCoreApplication::exit(2); });
        } else if (backend != nullptr && !qaRuntimeCycleScheduled) {
            if (parser.isSet(runtimeOption)) {
                QTimer::singleShot(150, backend, [backend] {
                    QMetaObject::invokeMethod(backend, "startSimulation");
                });
            }
            const auto startIndices = parser.values(startIedOption);
            if (!startIndices.isEmpty()) {
                QTimer::singleShot(150, backend, [backend, startIndices] {
                    for (const auto& text : startIndices) {
                        bool valid{};
                        const auto index = text.toInt(&valid);
                        if (!valid || index < 0) {
                            qWarning().noquote() << "Invalid --start-ied index:" << text;
                            continue;
                        }
                        QMetaObject::invokeMethod(backend, "startIed", Q_ARG(int, index));
                    }
                });
            }
        }

        if (backend != nullptr && parser.isSet(setFirstValueOption)) {
            const auto value = parser.value(setFirstValueOption);
            auto* const applyTimer = new QTimer{backend};
            applyTimer->setInterval(100);
            QObject::connect(applyTimer, &QTimer::timeout, backend, [backend, applyTimer, value] {
                if (!backend->property("running").toBool()) return;
                QMetaObject::invokeMethod(backend, "selectValue", Q_ARG(int, 0));
                QMetaObject::invokeMethod(
                    backend,
                    "applySelectedValue",
                    Q_ARG(QString, value),
                    Q_ARG(QString, QStringLiteral("Good")),
                    Q_ARG(QString, QStringLiteral("Simulator QA")));
                applyTimer->stop();
                applyTimer->deleteLater();
            });
            applyTimer->start();
        }
        if (backend != nullptr && parser.isSet(qaLiveBurstOption)) {
            bool countOk{};
            const auto requested = parser.value(qaLiveBurstOption).toInt(&countOk);
            const int count = countOk ? std::clamp(requested, 1, 100'000) : 0;
            if (count <= 0) {
                qWarning().noquote() << "--qa-live-burst requires a positive count.";
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(12); });
            } else {
                auto* const liveTimer = new QTimer{backend};
                liveTimer->setInterval(25);
                QObject::connect(liveTimer, &QTimer::timeout, backend, [backend, liveTimer, count] {
                    if (!backend->property("running").toBool()) return;
                    int accepted{};
                    const bool invoked = QMetaObject::invokeMethod(
                        backend,
                        "qaBurstLiveValues",
                        Q_RETURN_ARG(int, accepted),
                        Q_ARG(int, count));
                    liveTimer->stop();
                    liveTimer->deleteLater();
                    if (!invoked || accepted != count) {
                        qWarning().noquote()
                            << "Live burst was not fully accepted" << accepted << "of" << count;
                    }
                });
                liveTimer->start();
            }
        }
        if (parser.isSet(screenshotOption)) {
            const auto outputPath = parser.value(screenshotOption);
            QTimer::singleShot(1600, &app, [&app, rootObject, outputPath] {
                if (auto* const window = qobject_cast<QQuickWindow*>(rootObject)) {
                    const auto image = window->grabWindow();
                    app.exit(image.save(outputPath) ? 0 : 3);
                    return;
                }
                app.exit(4);
            });
        } else if (parser.isSet(smokeOption)) {
            QTimer::singleShot(1200, &app, &QCoreApplication::quit);
        }
        if (parser.isSet(exitAfterOption)) {
            bool valid{};
            const auto milliseconds = parser.value(exitAfterOption).toInt(&valid);
            if (valid && milliseconds > 0) {
                QTimer::singleShot(milliseconds, &app, &QCoreApplication::quit);
            }
        }
    }

    return app.exec();
}

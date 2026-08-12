// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedSimulatorController.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack IED Simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ARStack IEC 61850 IED Simulator"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption sclOption{
        QStringLiteral("scl"),
        QStringLiteral("Import an engineering file before showing the window."),
        QStringLiteral("path")};
    const QCommandLineOption runtimeOption{
        QStringLiteral("runtime"),
        QStringLiteral("Start the MMS runtime after importing the model.")};
    const QCommandLineOption screenshotOption{
        QStringLiteral("screenshot"),
        QStringLiteral("Capture the rendered window and exit."),
        QStringLiteral("path")};
    const QCommandLineOption smokeOption{
        QStringLiteral("smoke-test"),
        QStringLiteral("Load the QML scene, wait briefly, and exit.")};
    const QCommandLineOption portOption{
        QStringLiteral("port"),
        QStringLiteral("Override the MMS listen port."),
        QStringLiteral("number")};
    const QCommandLineOption setFirstValueOption{
        QStringLiteral("set-first-value"),
        QStringLiteral("QA: apply a value to the first runtime point after start."),
        QStringLiteral("value")};
    const QCommandLineOption undoAfterOption{
        QStringLiteral("undo-after-ms"),
        QStringLiteral("QA: invoke live-value undo after the specified delay."),
        QStringLiteral("milliseconds")};
    const QCommandLineOption stateDumpOption{
        QStringLiteral("state-dump"),
        QStringLiteral("Write the final Qt live-state mirror to JSON on exit."),
        QStringLiteral("path")};
    const QCommandLineOption exitAfterOption{
        QStringLiteral("exit-after-ms"),
        QStringLiteral("QA: exit after the specified runtime duration."),
        QStringLiteral("milliseconds")};
    parser.addOptions({
        sclOption,
        runtimeOption,
        screenshotOption,
        smokeOption,
        portOption,
        setFirstValueOption,
        undoAfterOption,
        stateDumpOption,
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
        if (backend != nullptr && parser.isSet(portOption)) {
            bool valid{};
            const auto port = parser.value(portOption).toInt(&valid);
            if (valid && port >= 1 && port <= 65'535) backend->setProperty("port", port);
        }
        if (backend != nullptr && parser.isSet(sclOption)) {
            QMetaObject::invokeMethod(
                backend,
                "loadFile",
                Q_ARG(QUrl, QUrl::fromLocalFile(parser.value(sclOption))));
        }
        if (backend != nullptr && parser.isSet(runtimeOption)) {
            QTimer::singleShot(150, backend, [backend] {
                QMetaObject::invokeMethod(backend, "startSimulation");
            });
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
        if (backend != nullptr && parser.isSet(undoAfterOption)) {
            bool valid{};
            const auto milliseconds = parser.value(undoAfterOption).toInt(&valid);
            if (valid && milliseconds > 0) {
                QTimer::singleShot(milliseconds, backend, [backend] {
                    QMetaObject::invokeMethod(backend, "undoLastChange");
                });
            }
        }
        if (backend != nullptr && parser.isSet(stateDumpOption)) {
            const auto stateDumpPath = parser.value(stateDumpOption);
            QObject::connect(
                &app,
                &QCoreApplication::aboutToQuit,
                backend,
                [backend, stateDumpPath] {
                    QMetaObject::invokeMethod(
                        backend,
                        "writeLiveStateSnapshot",
                        Q_ARG(QString, stateDumpPath));
                });
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

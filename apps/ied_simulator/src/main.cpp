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
    parser.addOptions({sclOption, runtimeOption, screenshotOption, smokeOption});
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
    }

    return app.exec();
}

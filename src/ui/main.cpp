#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "ui/backend/KhronicleApiClient.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    khronicle::KhronicleApiClient apiClient;
    engine.rootContext()->setContextProperty(QStringLiteral("khronicleApi"),
                                             &apiClient);
    const QUrl url = QUrl::fromLocalFile(
        QStringLiteral(KHRONICLE_QML_DIR "/Main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(EXIT_FAILURE);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QGuiApplication::setOrganizationName(QStringLiteral("dhtdiag"));
    QGuiApplication::setApplicationName(QStringLiteral("DHT Diagnostics"));
    QGuiApplication::setApplicationVersion(QStringLiteral(DHTDIAG_VERSION));

    // Basic is the least opinionated base to put our own theme on, and
    // pinning it keeps Windows and Linux identical. Everything visual comes
    // from Theme.qml.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("DhtDiag", "Main");

    return app.exec();
}

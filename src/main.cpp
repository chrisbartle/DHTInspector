#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    // No state between runs: use only the QML that qmlcachegen compiled into
    // the executable, and never read or write .qmlc cache files. Qt's own
    // Basic style controls ship as source in this static Qt, so they are
    // compiled in memory at each launch instead of being cached on disk.
    // (QML_DISABLE_DISK_CACHE would be wrong: it also discards the
    // ahead-of-time compiled units.) Must be set before the engine starts.
    qputenv("QML_DISK_CACHE", "aot");

    QGuiApplication app(argc, argv);

    QGuiApplication::setOrganizationName(QStringLiteral("DHTInspector"));
    QGuiApplication::setApplicationName(QStringLiteral("DHT Inspector"));
    QGuiApplication::setApplicationVersion(QStringLiteral(DHTINSPECTOR_VERSION));

    // Basic is the least opinionated base to put our own theme on, and
    // pinning it keeps Windows and Linux identical. Everything visual comes
    // from Theme.qml.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("DHTInspector", "Main");

    return app.exec();
}

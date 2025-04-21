#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "VTKRendererItem.h"

int main(int argc, char *argv[])
{
    VTKRendererItem::setGraphicsApi();

    QGuiApplication app(argc, argv);

    qmlRegisterType<VTKRendererItem>("VTKRenderer", 1, 0, "VTKRendererItem");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("VTKQMLExtension", "Main");

    return app.exec();
}

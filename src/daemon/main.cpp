#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "Khronicle daemon starting...";

    return app.exec();
}

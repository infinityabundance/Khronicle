#include <QCoreApplication>

#include "report/ReportCli.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    khronicle::ReportCli cli;
    return cli.run(argc, argv);
}

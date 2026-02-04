#include <QCoreApplication>

#include "report/ReportCli.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // CLI entry point: delegate to ReportCli for argument parsing and output.
    khronicle::ReportCli cli;
    return cli.run(argc, argv);
}

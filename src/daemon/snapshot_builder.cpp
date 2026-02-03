#include "daemon/snapshot_builder.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <QDateTime>
#include <QProcess>
#include <QStringList>

#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

QString runCommand(const QString &program, const QStringList &arguments,
                   int *exitCode)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted()) {
        if (exitCode) {
            *exitCode = -1;
        }
        return {};
    }

    process.closeWriteChannel();

    if (!process.waitForFinished()) {
        if (exitCode) {
            *exitCode = -1;
        }
        return {};
    }

    if (exitCode) {
        *exitCode = process.exitCode();
    }

    if (process.exitStatus() != QProcess::NormalExit) {
        return {};
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

} // namespace

SystemSnapshot buildCurrentSnapshot()
{
    SystemSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();

    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             snapshot.timestamp.time_since_epoch())
                             .count();
    snapshot.id = "snapshot-" + std::to_string(epochMs);

    snapshot.gpuDriver = nlohmann::json::object();
    snapshot.firmwareVersions = nlohmann::json::object();
    snapshot.keyPackages = nlohmann::json::object();

    int unameExit = 0;
    QString kernel = runCommand(QStringLiteral("uname"), {QStringLiteral("-r")},
                                &unameExit);
    if (unameExit == 0) {
        snapshot.kernelVersion = kernel.toStdString();
    } else {
        // Fallback when uname fails; keep kernelVersion empty.
        snapshot.kernelVersion.clear();
    }

    const std::vector<QString> packages = {
        QStringLiteral("linux"),
        QStringLiteral("linux-cachyos"),
        QStringLiteral("linux-zen"),
        QStringLiteral("linux-lts"),
        QStringLiteral("mesa"),
        QStringLiteral("mesa-git"),
        QStringLiteral("nvidia"),
        QStringLiteral("nvidia-dkms"),
        QStringLiteral("nvidia-utils"),
        QStringLiteral("vulkan-radeon"),
        QStringLiteral("vulkan-intel"),
        QStringLiteral("vulkan-nouveau"),
        QStringLiteral("xf86-video-amdgpu"),
        QStringLiteral("xf86-video-intel"),
        QStringLiteral("linux-firmware"),
        QStringLiteral("amd-ucode"),
        QStringLiteral("intel-ucode"),
    };

    for (const QString &pkg : packages) {
        int exitCode = 0;
        QString output = runCommand(QStringLiteral("pacman"),
                                    {QStringLiteral("-Q"), pkg}, &exitCode);
        if (exitCode != 0 || output.isEmpty()) {
            continue;
        }

        const QStringList tokens = output.split(QChar(' '), Qt::SkipEmptyParts);
        if (tokens.size() < 2) {
            continue;
        }

        snapshot.keyPackages[pkg.toStdString()] = tokens[1].toStdString();
    }

    return snapshot;
}

} // namespace khronicle

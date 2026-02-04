#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>

#include "daemon/khronicle_store.hpp"

class HostIdentityTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testHostIdentityPersistence();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void HostIdentityTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void HostIdentityTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path HostIdentityTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void HostIdentityTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void HostIdentityTests::testHostIdentityPersistence()
{
    resetDb();

    std::string hostIdA;
    {
        khronicle::KhronicleStore store;
        hostIdA = store.getHostIdentity().hostId;
        QVERIFY(!hostIdA.empty());
    }

    {
        khronicle::KhronicleStore store;
        const std::string hostIdB = store.getHostIdentity().hostId;
        QCOMPARE(QString::fromStdString(hostIdA), QString::fromStdString(hostIdB));
    }
}

QTEST_MAIN(HostIdentityTests)
#include "test_host_identity.moc"

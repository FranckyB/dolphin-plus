#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class DolphinPlusCoexistenceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void independentInstances();
    void cleanupTestCase();

private:
    bool start(QProcess &process, const QString &executable, const QStringList &arguments);
    bool isRegistered(const QString &service) const;
    QStringList plusServices() const;
    bool isUrlOpen(QDBusInterface &window, const QString &path) const;
    QByteArray contents(const QString &path) const;

    QTemporaryDir m_directory;
    QProcessEnvironment m_environment;
    QProcess m_plus;
    QProcess m_stock;
    QProcess m_reopen;
};

void DolphinPlusCoexistenceTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
    QVERIFY(QDBusConnection::sessionBus().isConnected());
    m_environment = QProcessEnvironment::systemEnvironment();
    for (const QString &directory : {QStringLiteral("home"),
                                     QStringLiteral("config"),
                                     QStringLiteral("data"),
                                     QStringLiteral("cache"),
                                     QStringLiteral("state"),
                                     QStringLiteral("one"),
                                     QStringLiteral("two"),
                                     QStringLiteral("stock")}) {
        QVERIFY(QDir().mkpath(m_directory.filePath(directory)));
    }
    m_environment.insert(QStringLiteral("HOME"), m_directory.filePath(QStringLiteral("home")));
    m_environment.insert(QStringLiteral("XDG_CONFIG_HOME"), m_directory.filePath(QStringLiteral("config")));
    m_environment.insert(QStringLiteral("XDG_DATA_HOME"), m_directory.filePath(QStringLiteral("data")));
    m_environment.insert(QStringLiteral("XDG_CACHE_HOME"), m_directory.filePath(QStringLiteral("cache")));
    m_environment.insert(QStringLiteral("XDG_STATE_HOME"), m_directory.filePath(QStringLiteral("state")));
    m_environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_environment.insert(QStringLiteral("QT_ACCESSIBILITY"), QStringLiteral("0"));
    m_environment.remove(QStringLiteral("SESSION_MANAGER"));
    m_environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));

    QFile plusConfig(m_directory.filePath(QStringLiteral("config/dolphinplusrc")));
    QVERIFY(plusConfig.open(QIODevice::WriteOnly));
    QVERIFY(plusConfig.write("[General]\nRememberOpenedTabs=true\nOpenExternallyCalledFolderInNewTab=true\n") > 0);
    QFile stockConfig(m_directory.filePath(QStringLiteral("config/dolphinrc")));
    QVERIFY(stockConfig.open(QIODevice::WriteOnly));
    QVERIFY(stockConfig.write("[General]\nRememberOpenedTabs=false\nOpenExternallyCalledFolderInNewTab=false\n") > 0);
}

bool DolphinPlusCoexistenceTest::start(QProcess &process, const QString &executable, const QStringList &arguments)
{
    process.setProcessEnvironment(m_environment);
    process.setWorkingDirectory(m_directory.filePath(QStringLiteral("home")));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, arguments);
    return process.waitForStarted();
}

bool DolphinPlusCoexistenceTest::isRegistered(const QString &service) const
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(service).value();
}

QStringList DolphinPlusCoexistenceTest::plusServices() const
{
    QStringList services;
    const auto registered = QDBusConnection::sessionBus().interface()->registeredServiceNames().value();
    for (const QString &service : registered) {
        if (service.startsWith(QStringLiteral("local.dolphinplus-"))) {
            services.append(service);
        }
    }
    return services;
}

bool DolphinPlusCoexistenceTest::isUrlOpen(QDBusInterface &window, const QString &path) const
{
    const QDBusReply<bool> reply = window.call(QStringLiteral("isUrlOpen"), QUrl::fromLocalFile(path).toString());
    return reply.isValid() && reply.value();
}

QByteArray DolphinPlusCoexistenceTest::contents(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void DolphinPlusCoexistenceTest::independentInstances()
{
    const QString plusExecutable = qEnvironmentVariable("DOLPHINPLUS_TEST_EXECUTABLE", qEnvironmentVariable("DOLPHINPLUS_EXECUTABLE"));
    const QString stockExecutable = qEnvironmentVariable("STOCK_DOLPHIN_EXECUTABLE");
    const QString firstFolder = m_directory.filePath(QStringLiteral("one"));
    const QString secondFolder = m_directory.filePath(QStringLiteral("two"));
    const QString stockFolder = m_directory.filePath(QStringLiteral("stock"));

    QVERIFY(start(m_plus, plusExecutable, {QStringLiteral("--new-window"), firstFolder}));
    const QString plusService = QStringLiteral("local.dolphinplus-%1").arg(m_plus.processId());
    QTRY_VERIFY_WITH_TIMEOUT(isRegistered(plusService), 10000);
    QVERIFY(!isRegistered(QStringLiteral("org.freedesktop.FileManager1")));

    QDBusInterface plusWindow(plusService, QStringLiteral("/dolphinplus/Dolphin_1"), QStringLiteral("org.kde.dolphin.MainWindow"));
    QVERIFY2(plusWindow.isValid(), qPrintable(plusWindow.lastError().message()));
    QTRY_VERIFY(isUrlOpen(plusWindow, firstFolder));

    QVERIFY(start(m_stock, stockExecutable, {QStringLiteral("--new-window"), stockFolder}));
    const QString stockService = QStringLiteral("org.kde.dolphin-%1").arg(m_stock.processId());
    QTRY_VERIFY_WITH_TIMEOUT(isRegistered(stockService), 10000);
    QDBusInterface stockWindow(stockService, QStringLiteral("/dolphin/Dolphin_1"), QStringLiteral("org.kde.dolphin.MainWindow"));
    QVERIFY(stockWindow.isValid());
    QTRY_VERIFY(isUrlOpen(stockWindow, stockFolder));
    QDBusReply<QString> owner = QDBusConnection::sessionBus().interface()->serviceOwner(QStringLiteral("org.freedesktop.FileManager1"));
    QCOMPARE(owner.value(), QDBusConnection::sessionBus().interface()->serviceOwner(stockService).value());

    QVERIFY(start(m_reopen, plusExecutable, {secondFolder}));
    QVERIFY(m_reopen.waitForFinished(10000));
    QCOMPARE(m_reopen.exitCode(), 0);
    QTRY_VERIFY(isUrlOpen(plusWindow, secondFolder));
    QVERIFY(isUrlOpen(plusWindow, firstFolder));
    QVERIFY(!isUrlOpen(stockWindow, firstFolder));
    QVERIFY(!isUrlOpen(stockWindow, secondFolder));
    QVERIFY(!isUrlOpen(plusWindow, stockFolder));

    QDBusInterface plusActions(plusService, QStringLiteral("/dolphinplus/Dolphin_1"), QStringLiteral("org.kde.KMainWindow"));
    const auto newWindowReply = plusActions.call(QStringLiteral("activateAction"), QStringLiteral("file_new"));
    QVERIFY(newWindowReply.type() != QDBusMessage::ErrorMessage);
    QTRY_COMPARE_WITH_TIMEOUT(plusServices().size(), 2, 10000);
    QStringList otherServices = plusServices();
    otherServices.removeAll(plusService);
    QCOMPARE(otherServices.size(), 1);
    QDBusInterface otherWindow(otherServices.first(), QStringLiteral("/dolphinplus/Dolphin_1"), QStringLiteral("org.kde.dolphin.MainWindow"));
    QVERIFY(otherWindow.isValid());
    otherWindow.call(QStringLiteral("quit"));
    QTRY_VERIFY(!isRegistered(otherServices.first()));
    QVERIFY(isRegistered(stockService));

    stockWindow.call(QStringLiteral("quit"));
    QVERIFY(m_stock.waitForFinished(10000));
    QCOMPARE(m_stock.exitCode(), 0);
    QTRY_VERIFY(!isRegistered(QStringLiteral("org.freedesktop.FileManager1")));
    QVERIFY(isRegistered(plusService));
    const QString stockConfigPath = m_directory.filePath(QStringLiteral("config/dolphinrc"));
    const QByteArray stockConfig = contents(stockConfigPath);
    QVERIFY(!stockConfig.isEmpty());

    plusWindow.call(QStringLiteral("quit"));
    QVERIFY(m_plus.waitForFinished(10000));
    QCOMPARE(m_plus.exitCode(), 0);
    QCOMPARE(contents(stockConfigPath), stockConfig);
    QVERIFY(!contents(m_directory.filePath(QStringLiteral("config/dolphinplusrc"))).isEmpty());
    const QDir sessionDirectory(m_directory.filePath(QStringLiteral("config/session")));
    QVERIFY(!sessionDirectory.entryList({QStringLiteral("dolphinplus*")}, QDir::Files).isEmpty());
    QVERIFY(sessionDirectory.entryList({QStringLiteral("dolphin_*")}, QDir::Files).isEmpty());
}

void DolphinPlusCoexistenceTest::cleanupTestCase()
{
    for (const QString &service : plusServices()) {
        QDBusInterface window(service, QStringLiteral("/dolphinplus/Dolphin_1"), QStringLiteral("org.kde.dolphin.MainWindow"));
        window.call(QStringLiteral("quit"));
    }
    for (QProcess *process : {&m_reopen, &m_stock, &m_plus}) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished();
        }
        if (QTest::currentTestFailed()) {
            qWarning().noquote() << process->readAll();
        }
    }
}

QTEST_GUILESS_MAIN(DolphinPlusCoexistenceTest)

#include "dolphinpluscoexistencetest.moc"
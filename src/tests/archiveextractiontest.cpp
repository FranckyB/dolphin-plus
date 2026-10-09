#include "archiveextractionjob.h"

#include <KFileItem>
#include <KFileItemListProperties>
#include <QAction>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class FileManagerProbe : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    int requests = 0;
public Q_SLOTS:
    void ShowItems(const QStringList &, const QString &)
    {
        ++requests;
    }
};

class ArchiveExtractionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compressionDoesNotOpenWindow()
    {
        FileManagerProbe probe;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.FileManager1")));
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &probe, QDBusConnection::ExportAllSlots));
        QTemporaryDir directory;
        QFile source(directory.filePath(QStringLiteral("payload.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("contents"), 8);
        source.close();
        QMenu parent;
        const KFileItem item(QUrl::fromLocalFile(source.fileName()));
        const auto actions = ArchiveExtractionJob::compressionActions(KFileItemListProperties(KFileItemList{item}), &parent);
        QCOMPARE(actions.size(), 1);
        auto *action = parent.findChild<QAction *>(QStringLiteral("dolphinplus_compress_zip"));
        QVERIFY(action);
        action->trigger();
        QPointer<QProcess> process = qApp->findChild<QProcess *>(QStringLiteral("dolphinplus_compression_process"));
        QVERIFY(process);
        QTRY_VERIFY_WITH_TIMEOUT(!process || process->state() == QProcess::NotRunning, 15000);
        QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.zip")}, QDir::Files).size(), 1);
        QCOMPARE(probe.requests, 0);
        bus.unregisterObject(QStringLiteral("/org/freedesktop/FileManager1"));
        bus.unregisterService(QStringLiteral("org.freedesktop.FileManager1"));
    }

    void layouts_data()
    {
        QTest::addColumn<QStringList>("files");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("collision");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("single-file") << QStringList{QStringLiteral("item.txt")} << QStringLiteral("item.txt") << false << false;
        QTest::newRow("single-folder-no-directory-entry")
            << QStringList{QStringLiteral("folder/item.txt"), QStringLiteral("folder/other.txt")} << QStringLiteral("folder") << false << false;
        QTest::newRow("multiple-items") << QStringList{QStringLiteral("item.txt"), QStringLiteral("other.txt")} << QStringLiteral("name.with.dots") << false
                                        << false;
        QTest::newRow("hidden-file-counts") << QStringList{QStringLiteral("folder/item.txt"), QStringLiteral(".hidden")} << QStringLiteral("name.with.dots")
                                            << false << false;
        QTest::newRow("single-hidden-file") << QStringList{QStringLiteral(".hidden")} << QStringLiteral(".hidden") << false << false;
        QTest::newRow("file-collision") << QStringList{QStringLiteral("item.txt")} << QStringLiteral("item.txt") << true << false;
        QTest::newRow("folder-collision") << QStringList{QStringLiteral("folder/item.txt")} << QStringLiteral("folder") << true << false;
        QTest::newRow("wrapper-collision") << QStringList{QStringLiteral("item.txt"), QStringLiteral("other.txt")} << QStringLiteral("name.with.dots") << true
                                           << false;
        QTest::newRow("cancel") << QStringList{QStringLiteral("item.txt")} << QStringLiteral("item.txt") << false << true;
        QTest::newRow("empty") << QStringList{} << QString() << false << false;
    }

    void layouts()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, expected);
        QFETCH(bool, collision);
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("input")));
        for (const QString &name : files) {
            const QString path = directory.filePath(QStringLiteral("input/") + name);
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("contents"), 8);
        }
        const QString archive = directory.filePath(QStringLiteral("name.with.dots.tar.gz"));
        QStringList arguments{QStringLiteral("-czf"), archive, QStringLiteral("-C"), directory.filePath(QStringLiteral("input"))};
        if (files.isEmpty()) {
            QFile list(directory.filePath(QStringLiteral("empty-list")));
            QVERIFY(list.open(QIODevice::WriteOnly));
            list.close();
            arguments << QStringLiteral("-T") << list.fileName();
        } else {
            arguments << files;
        }
        QProcess process;
        process.start(QStandardPaths::findExecutable(QStringLiteral("bsdtar")), arguments);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const QString destination = directory.filePath(expected);
        if (collision) {
            if (expected == QLatin1String("item.txt")) {
                QFile existing(destination);
                QVERIFY(existing.open(QIODevice::WriteOnly));
                QCOMPARE(existing.write("existing"), 8);
            } else {
                QVERIFY(QDir().mkpath(destination));
            }
        }
        ArchiveExtractionJob job({QUrl::fromLocalFile(archive)});
        job.setAutoDelete(false);
        if (cancel) {
            connect(&job, &KJob::description, &job, [&job]() {
                job.kill(KJob::EmitResult);
            });
        }
        QSignalSpy finished(&job, &KJob::result);
        job.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        if (collision || cancel || files.isEmpty()) {
            QVERIFY(job.error());
            QVERIFY(QFile::exists(archive));
            if (collision && expected == QLatin1String("item.txt")) {
                QFile existing(destination);
                QVERIFY(existing.open(QIODevice::ReadOnly));
                QCOMPARE(existing.readAll(), QByteArray("existing"));
            } else if (collision) {
                QVERIFY(QDir(destination).isEmpty());
            } else if (cancel) {
                QVERIFY(!QFile::exists(destination));
            }
            if (!cancel) {
                QVERIFY(job.errorText().contains(QStringLiteral(".dolphinplus-extract-")));
            }
        } else {
            QVERIFY2(!job.error(), qPrintable(job.errorString()));
            QVERIFY(!QFile::exists(archive));
            QVERIFY(QFile::exists(destination));
            for (const QString &name : files) {
                const QString path = expected == QLatin1String("name.with.dots") ? destination + QLatin1Char('/') + name : directory.filePath(name);
                QFile result(path);
                QVERIFY2(result.open(QIODevice::ReadOnly), qPrintable(path));
                QCOMPARE(result.readAll(), QByteArray("contents"));
            }
            QVERIFY(QDir(directory.path()).entryList({QStringLiteral(".dolphinplus-extract-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
        }
    }

    void corruptArchive()
    {
        QTemporaryDir directory;
        QFile archive(directory.filePath(QStringLiteral("corrupt.zip")));
        QVERIFY(archive.open(QIODevice::WriteOnly));
        QCOMPARE(archive.write("invalid archive"), 15);
        archive.close();
        ArchiveExtractionJob job({QUrl::fromLocalFile(archive.fileName())});
        job.setAutoDelete(false);
        QSignalSpy finished(&job, &KJob::result);
        job.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(job.error());
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), QByteArray("invalid archive"));
    }

    void menuAction()
    {
        QTemporaryDir directory;
        QFile source(directory.filePath(QStringLiteral("payload.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("contents"), 8);
        source.close();
        const QString archive = directory.filePath(QStringLiteral("sample.zip"));
        QProcess encoder;
        encoder.start(QStandardPaths::findExecutable(QStringLiteral("bsdtar")),
                      {QStringLiteral("-cf"), archive, QStringLiteral("--format=zip"), QStringLiteral("-C"), directory.path(), QStringLiteral("payload.txt")});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        QVERIFY(source.remove());
        KFileItem item(QUrl::fromLocalFile(archive), QStringLiteral("application/zip"), 0);
        QMenu parent;
        const auto actions = ArchiveExtractionJob::actions(KFileItemListProperties(KFileItemList{item}), &parent);
        QCOMPARE(actions.size(), 1);
        QVERIFY(actions.first()->menu());
        QCOMPARE(actions.first()->menu()->actions().size(), 3);
        QCOMPARE(actions.first()->menu()->actions().at(1)->objectName(), QStringLiteral("dolphinplus_extract_and_trash"));
        actions.first()->menu()->actions().at(1)->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(archive), 10000);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("contents"));
    }

    void rejectedArchives_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("path-traversal") << QStringLiteral("traversal");
        QTest::newRow("symlink-escape") << QStringLiteral("symlink");
        QTest::newRow("encrypted") << QStringLiteral("encrypted");
    }

    void rejectedArchives()
    {
        QFETCH(QString, kind);
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("input")));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("outside")));
        QFile source(directory.filePath(QStringLiteral("input/payload.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("contents"), 8);
        source.close();
        const QString archive = directory.filePath(QStringLiteral("sample.zip"));
        QStringList arguments{QStringLiteral("-cf"), archive, QStringLiteral("-C"), directory.filePath(QStringLiteral("input"))};
        if (kind == QLatin1String("encrypted")) {
            arguments << QStringLiteral("--format=zip") << QStringLiteral("--options=zip:encryption=aes256") << QStringLiteral("--passphrase")
                      << QStringLiteral("test-fixture-password");
        } else if (kind == QLatin1String("traversal")) {
            arguments << QStringLiteral("-s") << QStringLiteral("|^payload[.]txt$|../escaped.txt|");
        } else {
            QVERIFY(QFile::link(directory.filePath(QStringLiteral("outside")), directory.filePath(QStringLiteral("input/link"))));
            arguments << QStringLiteral("-s") << QStringLiteral("|^payload[.]txt$|link/escaped.txt|") << QStringLiteral("link");
        }
        arguments << QStringLiteral("payload.txt");
        QProcess encoder;
        encoder.start(QStandardPaths::findExecutable(QStringLiteral("bsdtar")), arguments);
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        ArchiveExtractionJob job({QUrl::fromLocalFile(archive)});
        job.setAutoDelete(false);
        QSignalSpy finished(&job, &KJob::result);
        job.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(job.error());
        QVERIFY(QFile::exists(archive));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("escaped.txt"))));
        QVERIFY(QDir(directory.filePath(QStringLiteral("outside"))).isEmpty());
    }

    void backend()
    {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
        QVERIFY2(!program.isEmpty(), "bsdtar is required");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("input/archive")));
        QFile source(directory.filePath(QStringLiteral("input/archive/file.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("contents"), 8);
        source.close();
        QProcess process;
        process.start(program,
                      {QStringLiteral("-cf"),
                       directory.filePath(QStringLiteral("archive.zip")),
                       QStringLiteral("--format=zip"),
                       QStringLiteral("-C"),
                       directory.filePath(QStringLiteral("input")),
                       QStringLiteral("archive")});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("output")));
        process.start(program,
                      {QStringLiteral("-xf"),
                       directory.filePath(QStringLiteral("archive.zip")),
                       QStringLiteral("-C"),
                       directory.filePath(QStringLiteral("output")),
                       QStringLiteral("--no-same-owner"),
                       QStringLiteral("--no-same-permissions")});
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QDir(directory.filePath(QStringLiteral("output"))).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("archive")});
        process.start(program, {QStringLiteral("-xf"), source.fileName(), QStringLiteral("-C"), directory.filePath(QStringLiteral("output"))});
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(10000));
        QVERIFY(process.exitCode() != 0);

        ArchiveExtractionJob job({QUrl::fromLocalFile(directory.filePath(QStringLiteral("archive.zip")))});
        job.setAutoDelete(false);
        QSignalSpy finished(&job, &KJob::result);
        job.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(job.error(), 0);
        QVERIFY(QFile::exists(directory.filePath(QStringLiteral("archive/file.txt"))));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("archive/archive/file.txt"))));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("archive.zip"))));
    }
};

QTEST_MAIN(ArchiveExtractionTest)
#include "archiveextractiontest.moc"
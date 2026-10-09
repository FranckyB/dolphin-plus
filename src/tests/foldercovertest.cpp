#include "kitemviews/foldercover.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kfileitemmodelrolesupdater.h"
#include "settings/interface/previewssettingspage.h"

#include <KConfig>
#include <KConfigGroup>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class FolderCoverTest : public QObject
{
    Q_OBJECT

    int m_previewJobsCreated = 0;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            ++m_previewJobsCreated;
        }
        return QObject::eventFilter(watched, event);
    }

private Q_SLOTS:
    void cacheInvalidation()
    {
        QTemporaryDir directory;
        const QString preferred = directory.filePath(QStringLiteral("fanart.png"));
        QImage source(80, 60, QImage::Format_RGB32);
        source.fill(Qt::green);
        QVERIFY(source.save(directory.filePath(QStringLiteral("landscape.png"))));
        source.fill(Qt::blue);
        QVERIFY(source.save(preferred));
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto url = QUrl::fromLocalFile(directory.path());
        QCOMPARE(FolderCover::generate(url, {}, QSize(256, 256), cancel).pixelColor(128, 128), QColor(Qt::blue));
        source.fill(Qt::red);
        QVERIFY(source.save(preferred));
        QFile file(preferred);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2), QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(FolderCover::generate(url, {}, QSize(256, 256), cancel).pixelColor(128, 128), QColor(Qt::red));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("not an image"), 12);
        file.close();
        QCOMPARE(FolderCover::generate(url, {}, QSize(256, 256), cancel).pixelColor(128, 128), QColor(Qt::green));
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden), QStringList({QStringLiteral("fanart.png"), QStringLiteral("landscape.png")}));
    }

    void videoFallback()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
            QSKIP("ffmpeg and ffprobe are required for the video fixture");
        }
        QTemporaryDir directory;
        QProcess encoder;
        encoder.start(ffmpeg,
                      {QStringLiteral("-nostdin"),
                       QStringLiteral("-v"),
                       QStringLiteral("error"),
                       QStringLiteral("-f"),
                       QStringLiteral("lavfi"),
                       QStringLiteral("-i"),
                       QStringLiteral("color=c=red:s=160x90:r=10:d=1.5"),
                       QStringLiteral("-f"),
                       QStringLiteral("lavfi"),
                       QStringLiteral("-i"),
                       QStringLiteral("color=c=blue:s=160x90:r=10:d=2.5"),
                       QStringLiteral("-filter_complex"),
                       QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0[out]"),
                       QStringLiteral("-map"),
                       QStringLiteral("[out]"),
                       QStringLiteral("-c:v"),
                       QStringLiteral("ffv1"),
                       directory.filePath(QStringLiteral("sample.mkv"))});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        FolderCover::Settings settings;
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto url = QUrl::fromLocalFile(directory.path());
        const auto cover = FolderCover::generate(url, settings, QSize(256, 256), cancel);
        QVERIFY(!cover.isNull());
        const QColor center = cover.pixelColor(128, 128);
        QVERIFY(center.blue() > 240 && center.red() < 15);
        settings.videoFallback = false;
        QVERIFY(FolderCover::generate(url, settings, QSize(256, 256), cancel).isNull());
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(directory.filePath(QStringLiteral("poster.png"))));
        settings.videoFallback = true;
        QCOMPARE(FolderCover::generate(url, settings, QSize(256, 256), cancel).pixelColor(128, 128), QColor(Qt::green));
    }

    void preferences()
    {
        const auto previous = FolderCover::loadSettings();
        const auto restore = qScopeGuard([previous]() {
            FolderCover::saveSettings(previous);
        });
        FolderCover::saveSettings({});
        FolderCoversSettingsPage page;
        page.resize(760, 520);
        page.show();
        auto *radius = page.findChild<QSpinBox *>(QStringLiteral("folder_cover_radius"));
        auto *position = page.findChild<QSpinBox *>(QStringLiteral("folder_cover_x"));
        auto *width = page.findChild<QSpinBox *>(QStringLiteral("folder_cover_width"));
        auto *preview = page.findChild<QLabel *>(QStringLiteral("folder_cover_preview"));
        QVERIFY(radius && position && width && preview);
        QCOMPARE(radius->value(), 18);
        QVERIFY(!preview->pixmap().isNull());
        radius->setValue(0);
        QCOMPARE(FolderCover::loadSettings().radius, 18);
        page.applySettings();
        QCOMPARE(FolderCover::loadSettings().radius, 0);
        position->setValue(240);
        QCOMPARE(width->maximum(), 16);
        page.restoreDefaults();
        QCOMPARE(radius->value(), 18);
        QCOMPARE(width->value(), 225);
        QCOMPARE(FolderCover::loadSettings().radius, 0);
        page.applySettings();
        QCOMPARE(FolderCover::loadSettings().radius, 18);
        const QString screenshots = qEnvironmentVariable("DOLPHINPLUS_TEST_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            QVERIFY(page.grab().save(screenshots + QStringLiteral("/folder-cover-preferences.png")));
        }
    }

    void asynchronousPreview()
    {
        const auto previous = FolderCover::loadSettings();
        const auto restore = qScopeGuard([previous]() {
            FolderCover::saveSettings(previous);
        });
        FolderCover::Settings settings;
        FolderCover::saveSettings(settings);
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("child")));
        QImage source(80, 60, QImage::Format_RGB32);
        source.fill(Qt::red);
        QVERIFY(source.save(directory.filePath(QStringLiteral("child/fanart.png"))));
        KFileItemModel model;
        KFileItemModelRolesUpdater updater(&model);
        updater.setIconSize(QSize(256, 256));
        updater.setPreviewsShown(true);
        updater.setVisibleIndexRange(0, 1);
        model.loadDirectory(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        QTRY_VERIFY(!model.data(0).value("iconPixmap").value<QPixmap>().isNull());
        QCOMPARE(model.data(0).value("iconPixmap").value<QPixmap>().toImage().pixelColor(128, 128), QColor(Qt::red));
        settings.mode = FolderCover::Mode::Disabled;
        FolderCover::saveSettings(settings);
        updater.setEnabledPlugins(updater.enabledPlugins());
        QTRY_VERIFY(model.data(0).value("iconPixmap").value<QPixmap>().isNull());
        settings.mode = FolderCover::Mode::SingleCover;
        FolderCover::saveSettings(settings);
        updater.setEnabledPlugins(updater.enabledPlugins());
        updater.setPreviewsShown(false);
        QTRY_VERIFY(model.data(0).value("iconPixmap").value<QPixmap>().isNull());
    }

    void existingImageIcon_data()
    {
        QTest::addColumn<bool>("absolutePath");
        QTest::addColumn<bool>("respectCustomIcons");
        QTest::newRow("relative-keep-icons") << false << true;
        QTest::newRow("relative-replace-themed-icons") << false << false;
        QTest::newRow("absolute-keep-icons") << true << true;
        QTest::newRow("absolute-replace-themed-icons") << true << false;
    }

    void existingImageIcon()
    {
        QFETCH(bool, absolutePath);
        QFETCH(bool, respectCustomIcons);
        const auto previous = FolderCover::loadSettings();
        const auto restore = qScopeGuard([previous]() {
            FolderCover::saveSettings(previous);
        });
        FolderCover::Settings settings;
        settings.respectCustomIcons = respectCustomIcons;
        FolderCover::saveSettings(settings);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("child")));
        const QString child = directory.filePath(QStringLiteral("child"));
        const QString artwork = child + QStringLiteral("/.folder.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(artwork));
        image.fill(Qt::red);
        QVERIFY(image.save(child + QStringLiteral("/fanart.png")));
        KConfig metadata(child + QStringLiteral("/.directory"), KConfig::SimpleConfig);
        metadata.group(QStringLiteral("Desktop Entry")).writeEntry("Icon", absolutePath ? artwork : QStringLiteral("./.folder.png"));
        metadata.sync();
        const auto url = QUrl::fromLocalFile(child);
        QVERIFY(FolderCover::usesCustomIcon(url, settings));
        QVERIFY(FolderCover::generate(url, settings, QSize(256, 256), std::make_shared<std::atomic_bool>(false)).isNull());
        KFileItemModel model;
        KFileItemModelRolesUpdater updater(&model);
        m_previewJobsCreated = 0;
        updater.installEventFilter(this);
        updater.setIconSize(QSize(256, 256));
        updater.setPreviewsShown(true);
        updater.setVisibleIndexRange(0, 1);
        model.loadDirectory(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        QTRY_VERIFY(m_previewJobsCreated > 0);
        QTRY_VERIFY(updater.findChildren<QFutureWatcherBase *>().isEmpty());
        QTRY_COMPARE(QDir::cleanPath(model.data(0).value("iconName").toString()), artwork);
        QVERIFY(model.data(0).value("iconPixmap").value<QPixmap>().isNull());
        QCOMPARE(QImage(model.data(0).value("iconName").toString()).pixelColor(40, 30), QColor(Qt::blue));
        QVERIFY(QFile::remove(artwork));
        QCOMPARE(FolderCover::usesCustomIcon(url, settings), respectCustomIcons);
    }

    void largeCustomIconDirectory_data()
    {
        QTest::addColumn<bool>("scroll");
        QTest::newRow("initial-viewport") << false;
        QTest::newRow("scroll-during-resolution") << true;
    }

    void largeCustomIconDirectory()
    {
        QFETCH(bool, scroll);
        const auto previous = FolderCover::loadSettings();
        const auto restore = qScopeGuard([previous]() {
            FolderCover::saveSettings(previous);
        });
        FolderCover::saveSettings({});
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString artwork = directory.filePath(QStringLiteral(".folder.png"));
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(artwork));
        for (int index = 0; index < 513; ++index) {
            const QString name = QStringLiteral("folder-%1").arg(index, 3, 10, QLatin1Char('0'));
            QVERIFY(QDir(directory.path()).mkdir(name));
            KConfig metadata(directory.filePath(name + QStringLiteral("/.directory")), KConfig::SimpleConfig);
            metadata.group(QStringLiteral("Desktop Entry")).writeEntry("Icon", artwork);
            QVERIFY(metadata.sync());
        }

        KFileItemModel model;
        KFileItemModelRolesUpdater updater(&model);
        updater.setPaused(true);
        updater.setIconSize(QSize(256, 256));
        updater.setPreviewsShown(true);
        updater.setMaximumVisibleItems(12);
        model.loadDirectory(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 513);
        updater.setVisibleIndexRange(250, 12);
        QVERIFY(!model.data(0).contains("supportsSequencing"));

        int firstResolvedIndex = -1;
        connect(&model, &KFileItemModel::itemsChanged, &model, [&]() {
            if (firstResolvedIndex >= 0) {
                return;
            }
            for (int index = 0; index < model.count(); ++index) {
                if (model.data(index).contains("supportsSequencing")) {
                    firstResolvedIndex = index;
                    break;
                }
            }
        });
        updater.setPaused(false);
        QCOMPARE(firstResolvedIndex, -1);
        QVERIFY(!model.data(0).contains("supportsSequencing"));
        if (scroll) {
            updater.setVisibleIndexRange(500, 12);
        }
        const int expectedIndex = scroll ? 500 : 250;
        QTRY_COMPARE(firstResolvedIndex, expectedIndex);
        QCOMPARE(QDir::cleanPath(model.data(expectedIndex).value("iconName").toString()), artwork);
        QVERIFY(model.data(expectedIndex).value("iconPixmap").value<QPixmap>().isNull());
        updater.setPaused(true);
    }

    void generation()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(directory.filePath(QStringLiteral("a.png"))));
        image.fill(Qt::green);
        QVERIFY(image.save(directory.filePath(QStringLiteral("landscape.png"))));
        FolderCover::Settings settings;
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto url = QUrl::fromLocalFile(directory.path());
        auto cover = FolderCover::generate(url, settings, QSize(256, 256), cancel);
        QVERIFY(!cover.isNull());
        QCOMPARE(cover.pixelColor(128, 128), QColor(Qt::green));
        image.fill(Qt::blue);
        QVERIFY(image.save(directory.filePath(QStringLiteral("fanart.png"))));
        cover = FolderCover::generate(url, settings, QSize(256, 256), cancel);
        QCOMPARE(cover.pixelColor(128, 128), QColor(Qt::blue));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral(".folder.png"))));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral(".directory"))));
        settings.contentRect = QRect(0, 0, 50, 50);
        cover = FolderCover::generate(url, settings, QSize(256, 256), cancel);
        QCOMPARE(cover.pixelColor(25, 25), QColor(Qt::blue));
        QVERIFY(cover.pixelColor(128, 128) != QColor(Qt::blue));
        settings = {};
        KConfig metadata(directory.filePath(QStringLiteral(".directory")), KConfig::SimpleConfig);
        metadata.group(QStringLiteral("Desktop Entry")).writeEntry("Icon", "folder-red");
        metadata.sync();
        QVERIFY(FolderCover::generate(url, settings, QSize(256, 256), cancel).isNull());
        settings.respectCustomIcons = false;
        QVERIFY(!FolderCover::generate(url, settings, QSize(256, 256), cancel).isNull());
        cancel->store(true);
        QVERIFY(FolderCover::generate(url, settings, QSize(256, 256), cancel).isNull());
        cancel->store(false);
        QVERIFY(FolderCover::generate(QUrl(QStringLiteral("sftp://example.test/folder")), settings, QSize(256, 256), cancel).isNull());
        QTemporaryDir parent;
        QVERIFY(QDir(parent.path()).mkdir(QStringLiteral("child")));
        QVERIFY(image.save(parent.filePath(QStringLiteral("child/poster.png"))));
        const auto parentUrl = QUrl::fromLocalFile(parent.path());
        settings.subfolderDepth = 0;
        QVERIFY(FolderCover::generate(parentUrl, settings, QSize(256, 256), cancel).isNull());
        settings.subfolderDepth = 1;
        QCOMPARE(FolderCover::generate(parentUrl, settings, QSize(256, 256), cancel).pixelColor(128, 128), QColor(Qt::blue));
    }

    void composition()
    {
        QImage folder(256, 256, QImage::Format_ARGB32_Premultiplied);
        folder.fill(Qt::transparent);
        QImage source(600, 200, QImage::Format_RGB32);
        source.fill(Qt::red);
        for (int column = 200; column < 400; ++column) {
            for (int row = 0; row < source.height(); ++row) {
                source.setPixelColor(column, row, Qt::green);
            }
        }
        FolderCover::Settings settings;
        const auto cover = FolderCover::compose(folder, source, settings, QSize(256, 256));
        QCOMPARE(cover.size(), QSize(256, 256));
        QCOMPARE(cover.pixelColor(128, 128), QColor(Qt::green));
        QCOMPARE(cover.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(cover.pixelColor(15, 67).alpha(), 0);
        QCOMPARE(cover.pixelColor(15, 128), QColor(Qt::red));
        const auto larger = FolderCover::compose(folder, source, settings, QSize(512, 512));
        QCOMPARE(larger.pixelColor(256, 256), QColor(Qt::green));
        QCOMPARE(larger.pixelColor(30, 134).alpha(), 0);
        settings.radius = 0;
        const auto square = FolderCover::compose(folder, source, settings, QSize(256, 256));
        QCOMPARE(square.pixelColor(15, 67), QColor(Qt::red));
        settings.contentRect = QRect(300, 300, 20, 20);
        QVERIFY(FolderCover::compose(folder, source, settings, QSize(256, 256)).isNull());
        QVERIFY(FolderCover::compose(folder, {}, settings, QSize(256, 256)).isNull());
    }
};

QTEST_MAIN(FolderCoverTest)

#include "foldercovertest.moc"
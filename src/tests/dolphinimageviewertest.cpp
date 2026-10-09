#include "dolphinimageviewer.h"
#include "dolphin_generalsettings.h"
#include "settings/viewer/viewersettingspage.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KIO/ListJob>
#include <KParts/ReadOnlyPart>
#include <KSharedConfig>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWheelEvent>

class DolphinImageViewerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (!DolphinImageViewer::supportsMimeType(QStringLiteral("image/png"))) {
            QSKIP("Gwenview's image KPart is not installed.");
        }
    }

    void init()
    {
        GeneralSettings::setImageViewerOpenFullscreen(false);
        GeneralSettings::setImageViewerEnlargeSmallerImages(false);
        GeneralSettings::self()->save();
        KConfigGroup shortcuts(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("ImageViewer Shortcuts"));
        shortcuts.deleteGroup();
        shortcuts.sync();
    }

    void testDisplayAndReturn()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QUrl first = QUrl::fromLocalFile(directory.filePath(QStringLiteral("first.png")));
        const QUrl second = QUrl::fromLocalFile(directory.filePath(QStringLiteral("second.png")));
        QVERIFY(image.save(first.toLocalFile()));
        image.fill(Qt::green);
        QVERIFY(image.save(second.toLocalFile()));

        DolphinImageViewer viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY2(viewer.errorString().isEmpty(), qPrintable(viewer.errorString()));
        QVERIFY(DolphinImageViewer::supportsMimeType(QStringLiteral("image/png")));
        QVERIFY(!DolphinImageViewer::supportsMimeType(QStringLiteral("text/plain")));
        auto *part = viewer.findChild<KParts::ReadOnlyPart *>();
        QVERIFY(part);
        QSignalSpy loaded(part, QOverload<>::of(&KParts::ReadOnlyPart::completed));
        QSignalSpy returned(&viewer, &DolphinImageViewer::returnToFileRequested);
        QVERIFY(viewer.setImages({second, first}, second));
        viewer.show();
        QTRY_VERIFY(!loaded.isEmpty());
        QCOMPARE(viewer.currentUrl(), second);
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::green));
        const QString screenshotDirectory = qEnvironmentVariable("DOLPHINPLUS_VIEWER_SCREENSHOT_DIR");
        for (const QSize &size : {QSize(1280, 800), QSize(480, 720)}) {
            viewer.resize(size);
            QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::green));
            for (const QPoint &point :
                 {QPoint(10, 10), QPoint(size.width() - 10, 10), QPoint(10, size.height() - 10), QPoint(size.width() - 10, size.height() - 10)}) {
                QTRY_COMPARE(viewer.grab().toImage().pixelColor(point), QColor(Qt::black));
            }
            if (!screenshotDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(screenshotDirectory));
                QVERIFY(viewer.grab().save(screenshotDirectory + QStringLiteral("/viewer-%1x%2.png").arg(size.width()).arg(size.height())));
            }
        }

        auto *zoomIn = part->actionCollection()->action(QStringLiteral("view_zoom_in"));
        auto *actualSize = viewer.actionCollection()->action(QStringLiteral("viewer_actual_size"));
        auto *fit = viewer.actionCollection()->action(QStringLiteral("viewer_fit"));
        QVERIFY(zoomIn);
        QVERIFY(actualSize);
        QVERIFY(fit);
        QSignalSpy zoomed(zoomIn, &QAction::triggered);
        QSignalSpy actualSizeTriggered(actualSize, &QAction::triggered);
        QSignalSpy fitTriggered(fit, &QAction::triggered);
        QTest::keyClick(part->widget(), Qt::Key_1);
        QCOMPARE(actualSizeTriggered.count(), 1);
        QTest::keyClick(part->widget(), Qt::Key_0);
        QCOMPARE(fitTriggered.count(), 1);
        QWheelEvent zoomWheel(QPointF(40, 30), QPointF(40, 30), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(part->widget(), &zoomWheel);
        QCOMPARE(zoomed.count(), 1);
        QCOMPARE(viewer.currentUrl(), second);

        QWheelEvent halfStep(QPointF(40, 30), QPointF(40, 30), QPoint(), QPoint(0, -60), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(part->widget(), &halfStep);
        QCOMPARE(viewer.currentUrl(), second);
        QApplication::sendEvent(part->widget(), &halfStep);
        QCOMPARE(viewer.currentUrl(), first);
        QTest::keyClick(part->widget(), Qt::Key_Left);
        QCOMPARE(viewer.currentUrl(), second);
        QTest::keyClick(part->widget(), Qt::Key_Left);
        QCOMPARE(viewer.currentUrl(), second);
        QTest::keyClick(part->widget(), Qt::Key_Right);
        QCOMPARE(viewer.currentUrl(), first);
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::red));
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(QPoint(10, 10)), QColor(Qt::black));
        QTest::keyClick(part->widget(), Qt::Key_Right);
        QCOMPARE(viewer.currentUrl(), first);
        QTest::keyClick(part->widget(), Qt::Key_F11);
        QVERIFY(viewer.isFullScreen());
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(QPoint(10, 10)), QColor(Qt::black));
        QTest::keyClick(part->widget(), Qt::Key_F11);
        QVERIFY(!viewer.isFullScreen());
        QTest::keyClick(part->widget(), Qt::Key_Return);
        QCOMPARE(returned.count(), 1);
        QCOMPARE(returned.first().first().toUrl(), first);
        QVERIFY(!viewer.isVisible());
    }

    void testFirstAndLastImage()
    {
        QTemporaryDir directory;
        const QUrl first = QUrl::fromLocalFile(directory.filePath(QStringLiteral("first.png")));
        const QUrl last = QUrl::fromLocalFile(directory.filePath(QStringLiteral("last.png")));
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(first.toLocalFile()));
        QVERIFY(image.save(last.toLocalFile()));
        DolphinImageViewer viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY(viewer.setImages({first, last}, first));
        viewer.showViewer();
        QTRY_VERIFY(viewer.isActiveWindow());
        QTest::keyClick(&viewer, Qt::Key_End);
        QCOMPARE(viewer.currentUrl(), last);
        QTest::keyClick(&viewer, Qt::Key_Home);
        QCOMPARE(viewer.currentUrl(), first);
        QCOMPARE(viewer.actionCollection()->action(QStringLiteral("viewer_first"))->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_Home)});
        QCOMPARE(viewer.actionCollection()->action(QStringLiteral("viewer_last"))->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_End)});
        ViewerSettingsPage preferences;
        auto *actions = preferences.findChild<KActionCollection *>();
        actions->action(QStringLiteral("viewer_first"))->setShortcuts({QKeySequence(Qt::Key_F)});
        actions->action(QStringLiteral("viewer_last"))->setShortcuts({QKeySequence(Qt::Key_L)});
        preferences.applySettings();
        QTest::keyClick(&viewer, Qt::Key_End);
        QCOMPARE(viewer.currentUrl(), first);
        QTest::keyClick(&viewer, Qt::Key_L);
        QCOMPARE(viewer.currentUrl(), last);
        QTest::keyClick(&viewer, Qt::Key_Home);
        QCOMPARE(viewer.currentUrl(), last);
        QTest::keyClick(&viewer, Qt::Key_F);
        QCOMPARE(viewer.currentUrl(), first);
    }

    void testSiblingImageNavigation_data()
    {
        QTest::addColumn<bool>("useSymlink");
        QTest::newRow("directories") << false;
        QTest::newRow("symlink-to-external-directory") << true;
    }

    void testSiblingImageNavigation()
    {
        QFETCH(bool, useSymlink);
        QTemporaryDir directory;
        QTemporaryDir external;
        QDir root(directory.path());
        for (const QString &name :
             {QStringLiteral("Folder 1"), QStringLiteral("folder 2"), QStringLiteral("Folder 3"), QStringLiteral("Folder 10 #%"), QStringLiteral(".hidden")}) {
            if (useSymlink && name == QLatin1String("Folder 10 #%")) {
                QVERIFY(QFile::link(external.path(), directory.filePath(name)));
            } else {
                QVERIFY(root.mkdir(name));
            }
        }
        QVERIFY(root.mkpath(QStringLiteral("Folder 3/nested")));
        QFile textFile(directory.filePath(QStringLiteral("Folder 3/notes.txt")));
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        QCOMPARE(textFile.write("not an image"), 12);
        textFile.close();
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        const auto imageUrl = [&directory, &image](const QString &path) {
            const QString filename = directory.filePath(path);
            return image.save(filename) ? QUrl::fromLocalFile(filename) : QUrl();
        };
        const QUrl first = imageUrl(QStringLiteral("Folder 1/image.png"));
        const QUrl middleFirst = imageUrl(QStringLiteral("folder 2/image 2.png"));
        const QUrl middleLast = imageUrl(QStringLiteral("folder 2/image 10.png"));
        const QUrl last = imageUrl(QStringLiteral("Folder 10 #%/image.png"));
        QVERIFY(!imageUrl(QStringLiteral(".hidden/image.png")).isEmpty());
        QVERIFY(!imageUrl(QStringLiteral("Folder 3/.hidden.png")).isEmpty());
        QVERIFY(!imageUrl(QStringLiteral("Folder 3/nested/image.png")).isEmpty());
        QVERIFY(!first.isEmpty());
        QVERIFY(!middleFirst.isEmpty());
        QVERIFY(!middleLast.isEmpty());
        QVERIFY(!last.isEmpty());
        DolphinImageViewer viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY(viewer.setImages({first}, first, first.adjusted(QUrl::RemoveFilename)));
        auto *next = viewer.actionCollection()->action(QStringLiteral("viewer_next"));
        auto *previous = viewer.actionCollection()->action(QStringLiteral("viewer_previous"));
        auto *nextFolder = viewer.actionCollection()->action(QStringLiteral("viewer_next_sibling"));
        auto *previousFolder = viewer.actionCollection()->action(QStringLiteral("viewer_previous_sibling"));
        QVERIFY(nextFolder->shortcuts().isEmpty());
        QVERIFY(previousFolder->shortcuts().isEmpty());
        next->trigger();
        QVERIFY(!next->isEnabled());
        QVERIFY(!nextFolder->isEnabled());
        next->trigger();
        QTRY_COMPARE(viewer.currentUrl(), middleFirst);
        next->trigger();
        QCOMPARE(viewer.currentUrl(), middleLast);
        next->trigger();
        QTRY_COMPARE(viewer.currentUrl(), last);
        previous->trigger();
        QTRY_COMPARE(viewer.currentUrl(), middleLast);
        previousFolder->trigger();
        QTRY_COMPARE(viewer.currentUrl(), first);
        nextFolder->trigger();
        QTRY_COMPARE(viewer.currentUrl(), middleFirst);
        nextFolder->trigger();
        QTRY_COMPARE(viewer.currentUrl(), last);
        next->trigger();
        QTRY_VERIFY(next->isEnabled());
        QCOMPARE(viewer.currentUrl(), last);
        ViewerSettingsPage preferences;
        auto *actions = preferences.findChild<KActionCollection *>();
        actions->action(QStringLiteral("viewer_previous_sibling"))->setShortcuts({QKeySequence(Qt::Key_P)});
        actions->action(QStringLiteral("viewer_next_sibling"))->setShortcuts({QKeySequence(Qt::Key_N)});
        preferences.applySettings();
        ViewerSettingsPage reopened;
        QCOMPARE(reopened.findChild<KActionCollection *>()->action(QStringLiteral("viewer_previous_sibling"))->shortcuts(),
                 QList<QKeySequence>{QKeySequence(Qt::Key_P)});
        QCOMPARE(reopened.findChild<KActionCollection *>()->action(QStringLiteral("viewer_next_sibling"))->shortcuts(),
                 QList<QKeySequence>{QKeySequence(Qt::Key_N)});
        viewer.showViewer();
        QTRY_VERIFY(viewer.isActiveWindow());
        QTest::keyClick(&viewer, Qt::Key_P);
        QTRY_COMPARE(viewer.currentUrl(), middleFirst);
        QTest::keyClick(&viewer, Qt::Key_N);
        QTRY_COMPARE(viewer.currentUrl(), last);
        QTest::keyClick(&viewer, Qt::Key_P);
        QTRY_COMPARE(viewer.currentUrl(), middleFirst);
        previous->trigger();
        QTRY_COMPARE(viewer.currentUrl(), first);
        previous->trigger();
        QTRY_VERIFY(previous->isEnabled());
        QCOMPARE(viewer.currentUrl(), first);
        next->trigger();
        viewer.actionCollection()->action(QStringLiteral("viewer_first"))->trigger();
        QTRY_VERIFY(viewer.findChildren<KIO::ListJob *>().isEmpty());
        QCOMPARE(viewer.currentUrl(), first);
        next->trigger();
        QVERIFY(viewer.setImages({last}, last, last.adjusted(QUrl::RemoveFilename)));
        QTRY_VERIFY(viewer.findChildren<KIO::ListJob *>().isEmpty());
        QCOMPARE(viewer.currentUrl(), last);
        QVERIFY(viewer.setImages({first}, first, first.adjusted(QUrl::RemoveFilename)));
        next->trigger();
        QSignalSpy returned(&viewer, &DolphinImageViewer::returnToFileRequested);
        viewer.close();
        QTRY_VERIFY(viewer.findChildren<KIO::ListJob *>().isEmpty());
        QCOMPARE(returned.count(), 1);
        QCOMPARE(returned.first().at(0).toUrl(), first);
        QCOMPARE(returned.first().at(1).toUrl(), first.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash));
    }

    void testZoomModesAreExclusive()
    {
        QTemporaryDir directory;
        const QUrl imageUrl = QUrl::fromLocalFile(directory.filePath(QStringLiteral("small.png")));
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(imageUrl.toLocalFile()));
        DolphinImageViewer viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY(viewer.setImages({imageUrl}, imageUrl));
        viewer.show();
        auto *part = viewer.findChild<KParts::ReadOnlyPart *>();
        QVERIFY(part);
        auto *actualSize = viewer.actionCollection()->action(QStringLiteral("viewer_actual_size"));
        auto *fit = viewer.actionCollection()->action(QStringLiteral("viewer_fit"));
        QVERIFY(actualSize);
        QVERIFY(fit);
        QTRY_VERIFY(actualSize->isEnabled());
        actualSize->trigger();
        QTRY_VERIFY(actualSize->isChecked());
        fit->trigger();
        QTRY_VERIFY(fit->isChecked());
        QVERIFY(!actualSize->isChecked());
        fit->trigger();
        QVERIFY(fit->isChecked());
        QVERIFY(!actualSize->isChecked());
        actualSize->trigger();
        actualSize->trigger();
        QVERIFY(actualSize->isChecked());
        QVERIFY(!fit->isChecked());
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::red));
        QVERIFY(viewer.setImages({imageUrl}, imageUrl));
        fit->trigger();
        QVERIFY(fit->isChecked());
        QVERIFY(!actualSize->isChecked());
    }

    void testFitPreferencesAndShortcuts()
    {
        QTemporaryDir directory;
        const QUrl first = QUrl::fromLocalFile(directory.filePath(QStringLiteral("small.png")));
        const QUrl second = QUrl::fromLocalFile(directory.filePath(QStringLiteral("large.png")));
        QImage small(80, 60, QImage::Format_RGB32);
        small.fill(Qt::green);
        QVERIFY(small.save(first.toLocalFile()));
        QImage large(1600, 1200, QImage::Format_RGB32);
        large.fill(Qt::red);
        QVERIFY(large.save(second.toLocalFile()));
        DolphinImageViewer viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY(viewer.setImages({first, second}, first));
        viewer.resize(640, 480);
        viewer.showViewer();
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::green));
        const QPoint outerPoint(100, 100);
        QVERIFY(viewer.grab().toImage().pixelColor(outerPoint) != QColor(Qt::green));

        ViewerSettingsPage preferences;
        auto *fullscreen = preferences.findChild<QCheckBox *>(QStringLiteral("viewer_open_fullscreen"));
        auto *fitPolicy = preferences.findChild<QComboBox *>(QStringLiteral("viewer_fit_policy"));
        auto *actions = preferences.findChild<KActionCollection *>();
        QVERIFY(fullscreen);
        QVERIFY(fitPolicy);
        QVERIFY(actions);
        fitPolicy->setCurrentIndex(1);
        fullscreen->setChecked(true);
        actions->action(QStringLiteral("viewer_next"))->setShortcuts({QKeySequence(Qt::Key_N)});
        preferences.applySettings();
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(outerPoint), QColor(Qt::green));
        QVERIFY(viewer.actionCollection()->action(QStringLiteral("viewer_fit"))->isChecked());
        QVERIFY(!viewer.actionCollection()->action(QStringLiteral("viewer_actual_size"))->isChecked());
        viewer.resize(800, 600);
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(QPoint(720, 300)), QColor(Qt::green));

        QTest::keyClick(&viewer, Qt::Key_Right);
        QCOMPARE(viewer.currentUrl(), first);
        QTest::keyClick(&viewer, Qt::Key_N);
        QCOMPARE(viewer.currentUrl(), second);
        QTRY_COMPARE(viewer.grab().toImage().pixelColor(viewer.rect().center()), QColor(Qt::red));

        ViewerSettingsPage reopened;
        QVERIFY(reopened.findChild<QCheckBox *>(QStringLiteral("viewer_open_fullscreen"))->isChecked());
        QCOMPARE(reopened.findChild<QComboBox *>(QStringLiteral("viewer_fit_policy"))->currentIndex(), 1);
        QCOMPARE(reopened.findChild<KActionCollection *>()->action(QStringLiteral("viewer_next"))->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_N)});
        DolphinImageViewer nextViewer;
        nextViewer.setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY(nextViewer.setImages({first}, first));
        nextViewer.showViewer();
        QVERIFY(nextViewer.isFullScreen());
        nextViewer.close();

        preferences.restoreDefaults();
        preferences.applySettings();
        QVERIFY(!GeneralSettings::imageViewerOpenFullscreen());
        QVERIFY(!GeneralSettings::imageViewerEnlargeSmallerImages());
        QCOMPARE(viewer.actionCollection()->action(QStringLiteral("viewer_next"))->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_Right)});
    }

    void testPreferencesIsolationAndCancel()
    {
        const QString gwenviewPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/gwenviewrc");
        const QByteArray original("[ImageView]\nEnlargeSmallerImages=true\nApplyExifOrientation=true\n");
        QFile gwenviewConfig(gwenviewPath);
        QVERIFY(gwenviewConfig.open(QIODevice::WriteOnly));
        QCOMPARE(gwenviewConfig.write(original), original.size());
        gwenviewConfig.close();
        {
            ViewerSettingsPage preferences;
            preferences.resize(700, 560);
            preferences.show();
            auto *tree = preferences.findChild<QTreeWidget *>();
            QVERIFY(tree);
            QTRY_VERIFY(tree->topLevelItemCount() > 0);
            const QString screenshots = qEnvironmentVariable("DOLPHINPLUS_VIEWER_SCREENSHOT_DIR");
            if (!screenshots.isEmpty()) {
                QVERIFY(QDir().mkpath(screenshots));
                QVERIFY(preferences.grab().save(screenshots + QStringLiteral("/viewer-preferences.png")));
            }
            preferences.findChild<QCheckBox *>(QStringLiteral("viewer_open_fullscreen"))->setChecked(true);
            preferences.findChild<QComboBox *>(QStringLiteral("viewer_fit_policy"))->setCurrentIndex(1);
            preferences.findChild<KActionCollection *>()->action(QStringLiteral("viewer_next"))->setShortcuts({QKeySequence(Qt::Key_N)});
        }
        QVERIFY(!GeneralSettings::imageViewerOpenFullscreen());
        QVERIFY(!GeneralSettings::imageViewerEnlargeSmallerImages());
        ViewerSettingsPage preferences;
        QCOMPARE(preferences.findChild<KActionCollection *>()->action(QStringLiteral("viewer_next"))->shortcuts(),
                 QList<QKeySequence>{QKeySequence(Qt::Key_Right)});
        preferences.findChild<QComboBox *>(QStringLiteral("viewer_fit_policy"))->setCurrentIndex(1);
        preferences.applySettings();
        QVERIFY(gwenviewConfig.open(QIODevice::ReadOnly));
        QCOMPARE(gwenviewConfig.readAll(), original);
        preferences.restoreDefaults();
        preferences.applySettings();
    }

    void testMissingImageAndOwnerLifetime()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl valid = QUrl::fromLocalFile(directory.filePath(QStringLiteral("valid.png")));
        const QUrl missing = QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.png")));
        const QUrl corrupt = QUrl::fromLocalFile(directory.filePath(QStringLiteral("corrupt.png")));
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(valid.toLocalFile()));
        QFile corruptFile(corrupt.toLocalFile());
        QVERIFY(corruptFile.open(QIODevice::WriteOnly));
        QCOMPARE(corruptFile.write("not an image"), 12);
        corruptFile.close();

        auto *owner = new QWidget;
        QPointer<DolphinImageViewer> viewer = new DolphinImageViewer(owner);
        QVERIFY(viewer->setImages({missing, corrupt, valid}, missing));
        viewer->showViewer();
        QTRY_VERIFY(viewer->isActiveWindow());
        QCOMPARE(viewer->currentUrl(), missing);
        QTest::keyClick(viewer, Qt::Key_Right);
        QCOMPARE(viewer->currentUrl(), corrupt);
        QTest::keyClick(viewer, Qt::Key_Right);
        QCOMPARE(viewer->currentUrl(), valid);
        QTRY_COMPARE(viewer->grab().toImage().pixelColor(viewer->rect().center()), QColor(Qt::blue));
        delete owner;
        QVERIFY(viewer.isNull());
    }
};

QTEST_MAIN(DolphinImageViewerTest)

#include "dolphinimageviewertest.moc"
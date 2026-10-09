#include "dolphinimageviewer.h"
#include "dolphin_generalsettings.h"
#include "settings/viewer/viewersettingspage.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KFileItem>
#include <KIO/Global>
#include <KIO/ListJob>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KSharedConfig>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCollator>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsWidget>
#include <QImageReader>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <memory>

namespace
{
void sortNaturally(QStringList &names)
{
    QCollator collator(QLocale::system().language() == QLocale::C ? QLocale(QLocale::English) : QLocale::system());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&collator](const QString &left, const QString &right) {
        const int order = collator.compare(left, right);
        return order == 0 ? left < right : order < 0;
    });
}
}

DolphinImageViewer::DolphinImageViewer(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("dolphin_image_viewer"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(i18nc("@title:window", "Image Viewer - Dolphin Plus"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_navigationMessage = new KMessageWidget(this);
    m_navigationMessage->setWordWrap(true);
    m_navigationMessage->hide();
    layout->addWidget(m_navigationMessage);

    const auto metadata = KPluginMetaData::findPluginById(QStringLiteral("kf6/parts"), QStringLiteral("gvpart"));
    const auto result = KPluginFactory::instantiatePlugin<KParts::ReadOnlyPart>(metadata, this);
    if (!result) {
        m_errorString = result.errorString;
        return;
    }

    m_part = result.plugin;
    QPalette imagePalette = m_part->widget()->palette();
    imagePalette.setColor(QPalette::Base, Qt::black);
    m_part->widget()->setPalette(imagePalette);
    if (auto *imageView = qobject_cast<QGraphicsView *>(m_part->widget())) {
        imageView->setBackgroundBrush(Qt::black);
        if (imageView->scene()) {
            for (auto *item : imageView->scene()->items()) {
                if (!item->parentItem()) {
                    if (auto *documentView = qobject_cast<QGraphicsWidget *>(item->toGraphicsObject())) {
                        documentView->setPalette(imagePalette);
                    }
                }
            }
        }
    }
    layout->addWidget(m_part->widget());
    setFocusProxy(m_part->widget());
    for (auto *action : m_part->actionCollection()->actions()) {
        action->setShortcuts({});
    }
    m_actions = createActionCollection(this);
    addActions(m_actions->actions());
    connect(m_actions->action(QStringLiteral("viewer_previous")), &QAction::triggered, this, [this]() {
        navigate(-1);
    });
    connect(m_actions->action(QStringLiteral("viewer_next")), &QAction::triggered, this, [this]() {
        navigate(1);
    });
    connect(m_actions->action(QStringLiteral("viewer_first")), &QAction::triggered, this, [this]() {
        jumpToBoundary(false);
    });
    connect(m_actions->action(QStringLiteral("viewer_last")), &QAction::triggered, this, [this]() {
        jumpToBoundary(true);
    });
    connect(m_actions->action(QStringLiteral("viewer_previous_sibling")), &QAction::triggered, this, [this]() {
        navigateSibling(false, false);
    });
    connect(m_actions->action(QStringLiteral("viewer_next_sibling")), &QAction::triggered, this, [this]() {
        navigateSibling(true, false);
    });
    connect(m_actions->action(QStringLiteral("viewer_close")), &QAction::triggered, this, &QWidget::close);
    connect(m_actions->action(QStringLiteral("viewer_fullscreen")), &QAction::triggered, this, [this]() {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    connect(m_actions->action(QStringLiteral("viewer_configure")), &QAction::triggered, this, &DolphinImageViewer::configureViewer);
    for (const auto &names : {std::pair{"viewer_actual_size", "view_actual_size"},
                              std::pair{"viewer_fit", "view_zoom_to_fit"},
                              std::pair{"viewer_zoom_in", "view_zoom_in"},
                              std::pair{"viewer_zoom_out", "view_zoom_out"}}) {
        auto *action = m_actions->action(QString::fromLatin1(names.first));
        auto *partAction = m_part->actionCollection()->action(QString::fromLatin1(names.second));
        if (!partAction) {
            action->setEnabled(false);
            continue;
        }
        const auto sync = [action, partAction]() {
            action->setEnabled(partAction->isEnabled());
        };
        connect(partAction, &QAction::changed, action, sync);
        if (action->objectName() == QLatin1String("viewer_actual_size")) {
            action->setCheckable(true);
            connect(action, &QAction::triggered, this, [this]() {
                setZoomMode(ZoomMode::ActualSize);
            });
        } else if (action->objectName() == QLatin1String("viewer_fit")) {
            action->setCheckable(true);
            connect(action, &QAction::triggered, this, [this]() {
                setZoomMode(ZoomMode::Fit);
            });
        } else {
            connect(action, &QAction::triggered, partAction, &QAction::trigger);
        }
        sync();
    }
    connect(m_part, QOverload<>::of(&KParts::ReadOnlyPart::completed), this, [this]() {
        findZoomController();
        m_imageLoaded = true;
        applyZoomMode();
    });
    const QSize available = screen()->availableGeometry().size();
    resize(available * 0.85);
    qApp->installEventFilter(this);
    updateNavigationActions();
}

KActionCollection *DolphinImageViewer::createActionCollection(QObject *parent)
{
    auto *collection = new KActionCollection(parent);
    collection->setComponentName(QStringLiteral("dolphinplus-imageviewer"));
    collection->setComponentDisplayName(i18nc("@title:group", "Image Viewer"));
    collection->setConfigGroup(QStringLiteral("ImageViewer Shortcuts"));
    const auto add = [collection](const QString &name, const QString &text, const QString &icon, const QList<QKeySequence> &shortcuts) {
        QAction *action = collection->addAction(name);
        action->setText(text);
        action->setIcon(QIcon::fromTheme(icon));
        collection->setDefaultShortcuts(action, shortcuts);
        return action;
    };
    add(QStringLiteral("viewer_previous"), i18nc("@action", "Previous Image"), QStringLiteral("go-previous"), {QKeySequence(Qt::Key_Left)});
    add(QStringLiteral("viewer_next"), i18nc("@action", "Next Image"), QStringLiteral("go-next"), {QKeySequence(Qt::Key_Right)});
    add(QStringLiteral("viewer_first"), i18nc("@action", "First Image"), QStringLiteral("go-first"), {QKeySequence(Qt::Key_Home)});
    add(QStringLiteral("viewer_last"), i18nc("@action", "Last Image"), QStringLiteral("go-last"), {QKeySequence(Qt::Key_End)});
    add(QStringLiteral("viewer_previous_sibling"), i18nc("@action", "Previous Sibling Folder"), QStringLiteral("go-previous"), {});
    add(QStringLiteral("viewer_next_sibling"), i18nc("@action", "Next Sibling Folder"), QStringLiteral("go-next"), {});
    add(QStringLiteral("viewer_close"),
        i18nc("@action", "Close Viewer and Select Image"),
        QStringLiteral("window-close"),
        {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Escape)})
        ->setAutoRepeat(false);
    add(QStringLiteral("viewer_fullscreen"), i18nc("@action", "Fullscreen"), QStringLiteral("view-fullscreen"), {QKeySequence(Qt::Key_F11)})
        ->setAutoRepeat(false);
    add(QStringLiteral("viewer_actual_size"), i18nc("@action", "Zoom to Actual Size"), QStringLiteral("zoom-original"), {QKeySequence(Qt::Key_1)});
    add(QStringLiteral("viewer_fit"), i18nc("@action", "Zoom to Fit"), QStringLiteral("zoom-fit-best"), {QKeySequence(Qt::Key_0)});
    add(QStringLiteral("viewer_zoom_in"),
        i18nc("@action", "Zoom In"),
        QStringLiteral("zoom-in"),
        {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    add(QStringLiteral("viewer_zoom_out"), i18nc("@action", "Zoom Out"), QStringLiteral("zoom-out"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)});
    add(QStringLiteral("viewer_configure"), i18nc("@action", "Configure Image Viewer..."), QStringLiteral("configure"), {})->setAutoRepeat(false);
    KConfigGroup shortcuts(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("ImageViewer Shortcuts"));
    collection->readSettings(&shortcuts);
    return collection;
}

KActionCollection *DolphinImageViewer::actionCollection() const
{
    return m_actions;
}

void DolphinImageViewer::readSettings()
{
    if (m_actions) {
        for (auto *action : m_actions->actions()) {
            action->setShortcuts(KActionCollection::defaultShortcuts(action));
        }
        KConfigGroup shortcuts(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("ImageViewer Shortcuts"));
        m_actions->readSettings(&shortcuts);
    }
    applyZoomMode();
}

void DolphinImageViewer::findZoomController()
{
    if (m_zoomController) {
        return;
    }
    auto views = m_part->widget()->findChildren<QGraphicsView *>();
    if (auto *rootView = qobject_cast<QGraphicsView *>(m_part->widget())) {
        views.prepend(rootView);
    }
    for (auto *view : views) {
        if (!view->scene()) {
            continue;
        }
        for (auto *item : view->scene()->items()) {
            auto *candidate = qobject_cast<QGraphicsWidget *>(item->toGraphicsObject());
            if (!candidate || candidate->metaObject()->indexOfProperty("zoom") < 0 || candidate->metaObject()->indexOfProperty("zoomToFit") < 0) {
                continue;
            }
            m_zoomController = candidate;
            connect(candidate, SIGNAL(zoomChanged(qreal)), this, SLOT(syncZoomActions()));
            connect(candidate, SIGNAL(zoomToFitChanged(bool)), this, SLOT(syncZoomActions()));
            connect(candidate, SIGNAL(zoomToFillChanged(bool)), this, SLOT(syncZoomActions()));
            return;
        }
    }
}

void DolphinImageViewer::setZoomMode(ZoomMode mode)
{
    m_zoomMode = mode;
    applyZoomMode();
    const QScopedValueRollback<bool> applying(m_applyingZoom, true);
    syncZoomActions();
}

void DolphinImageViewer::applyZoomMode()
{
    if (!m_part || !m_imageLoaded || m_zoomMode == ZoomMode::Custom) {
        return;
    }
    {
        const QScopedValueRollback<bool> applying(m_applyingZoom, true);
        if (m_zoomMode == ZoomMode::ActualSize) {
            triggerPartAction("view_actual_size");
        } else if (m_zoomController && !m_imageSize.isEmpty()) {
            const QSizeF available = m_zoomController->size();
            const qreal ratio = m_part->widget()->devicePixelRatioF();
            qreal zoom = qMin(available.width() * ratio / m_imageSize.width(), available.height() * ratio / m_imageSize.height());
            if (!GeneralSettings::imageViewerEnlargeSmallerImages()) {
                zoom = qMin(zoom, qreal(1));
            }
            if (zoom > 0) {
                m_zoomController->setProperty("zoom", zoom);
            }
        } else {
            auto *fit = m_part->actionCollection()->action(QStringLiteral("view_zoom_to_fit"));
            if (fit && !fit->isChecked()) {
                fit->trigger();
            }
        }
    }
    const QScopedValueRollback<bool> applying(m_applyingZoom, true);
    syncZoomActions();
}

void DolphinImageViewer::syncZoomActions()
{
    if (!m_actions) {
        return;
    }
    if (!m_applyingZoom && m_imageLoaded && m_zoomController) {
        if (m_zoomController->property("zoomToFit").toBool()) {
            m_zoomMode = ZoomMode::Fit;
        } else if (!m_zoomController->property("zoomToFill").toBool() && qFuzzyCompare(m_zoomController->property("zoom").toDouble(), 1.0)) {
            m_zoomMode = ZoomMode::ActualSize;
        } else {
            m_zoomMode = ZoomMode::Custom;
        }
    }
    m_actions->action(QStringLiteral("viewer_actual_size"))->setChecked(m_zoomMode == ZoomMode::ActualSize);
    m_actions->action(QStringLiteral("viewer_fit"))->setChecked(m_zoomMode == ZoomMode::Fit);
}

void DolphinImageViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &DolphinImageViewer::applyZoomMode);
}

void DolphinImageViewer::showViewer()
{
    if (!isVisible() && GeneralSettings::imageViewerOpenFullscreen()) {
        showFullScreen();
    } else {
        show();
    }
    raise();
    activateWindow();
    setFocus();
}

void DolphinImageViewer::configureViewer()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18nc("@title:window", "Configure Image Viewer"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *page = new ViewerSettingsPage(&dialog);
    layout->addWidget(page);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, page]() {
        page->applySettings();
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, page, &ViewerSettingsPage::applySettings);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, page, &ViewerSettingsPage::restoreDefaults);
    dialog.resize(700, 560);
    dialog.exec();
}

DolphinImageViewer::~DolphinImageViewer()
{
    cancelSiblingNavigation();
    qApp->removeEventFilter(this);
}

bool DolphinImageViewer::supportsMimeType(const QString &mimeType)
{
    if (!mimeType.startsWith(QLatin1String("image/"))) {
        return false;
    }
    static const auto metadata = KPluginMetaData::findPluginById(QStringLiteral("kf6/parts"), QStringLiteral("gvpart"));
    return metadata.isValid() && metadata.supportsMimeType(mimeType);
}

bool DolphinImageViewer::setImages(const QList<QUrl> &images, const QUrl &current, const QUrl &directory)
{
    const qsizetype index = images.indexOf(current);
    if (!m_part || index < 0) {
        return false;
    }
    cancelSiblingNavigation();
    m_images = images;
    m_index = index;
    m_directory = directory.adjusted(QUrl::StripTrailingSlash);
    m_siblingNavigationEnabled = m_directory.isLocalFile() && !m_directory.fileName().isEmpty() && !m_directory.fileName().startsWith(QLatin1Char('.'))
        && std::all_of(images.cbegin(), images.cend(), [this](const QUrl &url) {
                                     return url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash) == m_directory;
                                 });
    m_wheelDelta = 0;
    openCurrentImage();
    updateNavigationActions();
    return true;
}

QUrl DolphinImageViewer::currentUrl() const
{
    return m_index >= 0 ? m_images.at(m_index) : QUrl();
}

QUrl DolphinImageViewer::currentDirectory() const
{
    return m_directory;
}

QString DolphinImageViewer::errorString() const
{
    return m_errorString;
}

void DolphinImageViewer::navigate(int offset)
{
    if (m_siblingJob || m_images.isEmpty()) {
        return;
    }
    const qsizetype target = m_index + offset;
    if (target >= 0 && target < m_images.size()) {
        m_navigationMessage->hide();
        m_index = target;
        openCurrentImage();
    } else {
        navigateSibling(offset > 0, offset < 0);
    }
}

void DolphinImageViewer::jumpToBoundary(bool last)
{
    cancelSiblingNavigation();
    if (!m_images.isEmpty()) {
        m_index = last ? m_images.size() - 1 : 0;
        openCurrentImage();
    }
}

void DolphinImageViewer::cancelSiblingNavigation()
{
    if (m_siblingJob) {
        m_siblingJob->kill();
        m_siblingJob = nullptr;
    }
    m_siblingFolders.clear();
    if (m_navigationMessage) {
        m_navigationMessage->hide();
    }
    updateNavigationActions();
}

void DolphinImageViewer::updateNavigationActions()
{
    if (!m_actions) {
        return;
    }
    const bool ready = !m_images.isEmpty() && !m_siblingJob;
    m_actions->action(QStringLiteral("viewer_previous"))->setEnabled(ready);
    m_actions->action(QStringLiteral("viewer_next"))->setEnabled(ready);
    m_actions->action(QStringLiteral("viewer_first"))->setEnabled(!m_images.isEmpty());
    m_actions->action(QStringLiteral("viewer_last"))->setEnabled(!m_images.isEmpty());
    m_actions->action(QStringLiteral("viewer_previous_sibling"))->setEnabled(ready && m_siblingNavigationEnabled);
    m_actions->action(QStringLiteral("viewer_next_sibling"))->setEnabled(ready && m_siblingNavigationEnabled);
    if (m_siblingJob) {
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
}

void DolphinImageViewer::showNavigationMessage(const QString &text, bool error)
{
    m_navigationMessage->setMessageType(error ? KMessageWidget::Error : KMessageWidget::Information);
    m_navigationMessage->setText(text);
    m_navigationMessage->show();
    QTimer::singleShot(0, this, &DolphinImageViewer::applyZoomMode);
    updateNavigationActions();
}

void DolphinImageViewer::navigateSibling(bool next, bool selectLast)
{
    if (!m_siblingNavigationEnabled || m_siblingJob) {
        return;
    }
    m_navigationMessage->hide();
    m_siblingFolders.clear();
    const QUrl parent = KIO::upUrl(m_directory);
    const auto folders = std::make_shared<QStringList>();
    auto *job = KIO::listDir(parent, KIO::HideProgressInfo);
    job->setParent(this);
    job->setUiDelegate(nullptr);
    m_siblingJob = job;
    updateNavigationActions();
    connect(job, &KIO::ListJob::entries, this, [folders](KIO::Job *, const KIO::UDSEntryList &entries) {
        for (const auto &entry : entries) {
            const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            if (entry.isDir() && !name.isEmpty() && !name.startsWith(QLatin1Char('.'))) {
                folders->append(name);
            }
        }
    });
    connect(job, &KJob::result, this, [this, job, folders, parent, next, selectLast]() {
        if (m_siblingJob != job) {
            return;
        }
        m_siblingJob = nullptr;
        if (job->error()) {
            showNavigationMessage(i18nc("@info", "Could not list sibling folders: %1", job->errorString()), true);
            return;
        }
        sortNaturally(*folders);
        const auto current = folders->indexOf(m_directory.fileName());
        if (current < 0) {
            showNavigationMessage(i18nc("@info", "The current folder is no longer in its parent directory."), true);
            return;
        }
        const int step = next ? 1 : -1;
        for (qsizetype index = current + step; index >= 0 && index < folders->size(); index += step) {
            m_siblingFolders.append(QUrl::fromLocalFile(QDir(parent.toLocalFile()).filePath(folders->at(index))));
        }
        findSiblingImages(selectLast);
    });
}

void DolphinImageViewer::findSiblingImages(bool selectLast)
{
    if (m_siblingFolders.isEmpty()) {
        showNavigationMessage(i18nc("@info", "No further sibling folder with supported images."));
        return;
    }
    const QUrl directory = m_siblingFolders.takeFirst();
    const auto names = std::make_shared<QStringList>();
    auto *job = KIO::listDir(directory, KIO::HideProgressInfo);
    job->setParent(this);
    job->setUiDelegate(nullptr);
    m_siblingJob = job;
    updateNavigationActions();
    connect(job, &KIO::ListJob::entries, this, [names, directory](KIO::Job *, const KIO::UDSEntryList &entries) {
        for (const auto &entry : entries) {
            const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            if (name.isEmpty() || name.startsWith(QLatin1Char('.')) || entry.isDir()) {
                continue;
            }
            const KFileItem item(entry, directory, false, true);
            if (item.isFile() && supportsMimeType(item.mimetype())) {
                names->append(name);
            }
        }
    });
    connect(job, &KJob::result, this, [this, job, names, directory, selectLast]() {
        if (m_siblingJob != job) {
            return;
        }
        m_siblingJob = nullptr;
        if (job->error()) {
            showNavigationMessage(i18nc("@info", "Could not list images in %1: %2", directory.toDisplayString(QUrl::PreferLocalFile), job->errorString()),
                                  true);
            return;
        }
        if (names->isEmpty()) {
            findSiblingImages(selectLast);
            return;
        }
        sortNaturally(*names);
        QList<QUrl> images;
        for (const QString &name : *names) {
            images.append(QUrl::fromLocalFile(QDir(directory.toLocalFile()).filePath(name)));
        }
        setImages(images, selectLast ? images.last() : images.first(), directory);
    });
}

void DolphinImageViewer::openCurrentImage()
{
    m_imageLoaded = m_imageLoaded && m_part->url() == currentUrl();
    m_zoomMode = ZoomMode::Fit;
    m_imageSize = {};
    if (currentUrl().isLocalFile()) {
        QImageReader reader(currentUrl().toLocalFile());
        m_imageSize = reader.size();
        const KConfigGroup imageConfig(KSharedConfig::openConfig(QStringLiteral("gwenviewrc")), QStringLiteral("ImageView"));
        if (imageConfig.readEntry("ApplyExifOrientation", true) && reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) {
            m_imageSize.transpose();
        }
    }
    setWindowTitle(i18nc("@title:window image name, position, count", "%1 (%2/%3) - Dolphin Plus", currentUrl().fileName(), m_index + 1, m_images.size()));
    m_part->openUrl(currentUrl());
    if (m_imageLoaded) {
        applyZoomMode();
    }
}

void DolphinImageViewer::triggerPartAction(const char *name)
{
    if (QAction *action = m_part->actionCollection()->action(QString::fromLatin1(name))) {
        action->trigger();
    }
}

bool DolphinImageViewer::eventFilter(QObject *watched, QEvent *event)
{
    const auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this || !m_part) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ContextMenu) {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        QMenu menu(this);
        for (const QString &name : {QStringLiteral("viewer_previous"),
                                    QStringLiteral("viewer_next"),
                                    QStringLiteral("viewer_first"),
                                    QStringLiteral("viewer_last"),
                                    QStringLiteral("viewer_previous_sibling"),
                                    QStringLiteral("viewer_next_sibling"),
                                    QStringLiteral("viewer_actual_size"),
                                    QStringLiteral("viewer_fit"),
                                    QStringLiteral("viewer_zoom_in"),
                                    QStringLiteral("viewer_zoom_out"),
                                    QStringLiteral("viewer_fullscreen")}) {
            menu.addAction(m_actions->action(name));
        }
        menu.addSeparator();
        for (const QString &name : {QStringLiteral("file_save_as"), QStringLiteral("file_show_properties")}) {
            if (auto *action = m_part->actionCollection()->action(name)) {
                menu.addAction(action);
            }
        }
        menu.addSeparator();
        menu.addAction(m_actions->action(QStringLiteral("viewer_configure")));
        menu.addAction(m_actions->action(QStringLiteral("viewer_close")));
        menu.exec(contextEvent->globalPos());
        return true;
    } else if (event->type() == QEvent::Wheel) {
        const auto *wheelEvent = static_cast<QWheelEvent *>(event);
        const int delta = wheelEvent->angleDelta().y();
        if (wheelEvent->modifiers() == Qt::ControlModifier) {
            if (delta != 0) {
                triggerPartAction(delta > 0 ? "view_zoom_in" : "view_zoom_out");
            }
            event->accept();
            return true;
        }
        if (wheelEvent->modifiers() == Qt::NoModifier) {
            m_wheelDelta += delta;
            while (qAbs(m_wheelDelta) >= 120) {
                const int direction = m_wheelDelta > 0 ? 1 : -1;
                m_wheelDelta -= direction * 120;
                navigate(-direction);
            }
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DolphinImageViewer::closeEvent(QCloseEvent *event)
{
    cancelSiblingNavigation();
    Q_EMIT returnToFileRequested(currentUrl(), m_directory);
    QWidget::closeEvent(event);
}
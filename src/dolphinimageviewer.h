#pragma once

#include <QCursor>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QWidget>

namespace KParts
{
class ReadOnlyPart;
}

namespace KIO
{
class ListJob;
}

class KActionCollection;
class KMessageWidget;
class QGraphicsWidget;
class QTimer;

class DolphinImageViewer : public QWidget
{
    Q_OBJECT

public:
    explicit DolphinImageViewer(QWidget *parent = nullptr);
    ~DolphinImageViewer() override;

    static bool supportsMimeType(const QString &mimeType);
    static KActionCollection *createActionCollection(QObject *parent);
    KActionCollection *actionCollection() const;
    void readSettings();
    void showViewer();
    bool setImages(const QList<QUrl> &images, const QUrl &current, const QUrl &directory = {});
    QUrl currentUrl() const;
    QUrl currentDirectory() const;
    QString errorString() const;

Q_SIGNALS:
    void returnToFileRequested(const QUrl &url, const QUrl &directory);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void syncZoomActions();
    void applyZoomMode();

private:
    enum class ZoomMode {
        Fit,
        ActualSize,
        Custom
    };
    void setZoomMode(ZoomMode mode);
    void findZoomController();
    void navigate(int offset);
    void jumpToBoundary(bool last);
    void navigateSibling(bool next, bool selectLast);
    void findSiblingImages(bool selectLast);
    void cancelSiblingNavigation();
    void updateNavigationActions();
    void showNavigationMessage(const QString &text, bool error = false);
    void openCurrentImage();
    void triggerPartAction(const char *name);
    void configureViewer();
    void restartCursorTimer();
    void restoreCursor();

    KParts::ReadOnlyPart *m_part = nullptr;
    QTimer *m_cursorTimer = nullptr;
    QPointer<QWidget> m_cursorWidget;
    QCursor m_savedCursor;
    bool m_cursorWasExplicit = false;
    bool m_cursorHidden = false;
    KActionCollection *m_actions = nullptr;
    KMessageWidget *m_navigationMessage = nullptr;
    QPointer<KIO::ListJob> m_siblingJob;
    QList<QUrl> m_siblingFolders;
    QUrl m_directory;
    bool m_siblingNavigationEnabled = false;
    QPointer<QGraphicsWidget> m_zoomController;
    QSize m_imageSize;
    ZoomMode m_zoomMode = ZoomMode::Fit;
    bool m_applyingZoom = false;
    bool m_imageLoaded = false;
    bool m_restoreView = false;
    qreal m_savedZoom = 1;
    QPointF m_relativeImageCenter;
    QList<QUrl> m_images;
    qsizetype m_index = -1;
    int m_wheelDelta = 0;
    QString m_errorString;
};
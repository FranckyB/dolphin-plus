#pragma once

#include <KJob>

#include <QEventLoopLocker>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

class QAction;
class QWidget;
class KFileItemListProperties;

class ArchiveExtractionJob : public KJob
{
    Q_OBJECT

public:
    explicit ArchiveExtractionJob(const QList<QUrl> &archives);
    void start() override;
    static QList<QAction *> actions(const KFileItemListProperties &properties, QWidget *parent);
    static QList<QAction *> compressionActions(const KFileItemListProperties &properties, QWidget *parent);

protected:
    bool doKill() override;

private:
    void extractNext();
    void publish();
    void fail(const QString &message);
    bool archiveUnchanged() const;

    QEventLoopLocker m_locker;
    QList<QUrl> m_archives;
    QFileInfo m_archive;
    std::unique_ptr<QTemporaryDir> m_staging;
    QPointer<KJob> m_extractionJob;
    QPointer<KJob> m_trashJob;
    QString m_destination;
    bool m_cancelled = false;
    bool m_incompleteExtraction = false;
};
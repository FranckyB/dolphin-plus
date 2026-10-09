#include "archiveextractionjob.h"

#include <KAbstractFileItemActionPlugin>
#include <KConfigGroup>
#include <KFileItem>
#include <KFileItemListProperties>
#include <KIO/CopyJob>
#include <KIO/JobTracker>
#include <KIO/JobUiDelegateFactory>
#include <KJobTrackerInterface>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

ArchiveExtractionJob::ArchiveExtractionJob(const QList<QUrl> &archives)
    : m_archives(archives)
{
    setCapabilities(KJob::Killable);
#ifdef Q_OS_UNIX
    m_process.setChildProcessModifier([]() {
        setsid();
    });
#endif
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_errors.append(m_process.readAllStandardError());
        m_errors = m_errors.right(65536);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_cancelled) {
            fail(m_process.errorString());
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int status, QProcess::ExitStatus exitStatus) {
        if (m_cancelled) {
            return;
        }
        if (status != 0 || exitStatus != QProcess::NormalExit) {
            fail(i18n("Extraction failed. Try Ark's Extract to action for unsupported or password-protected archives.\n%1", QString::fromLocal8Bit(m_errors)));
            return;
        }
        publish();
    });
}

void ArchiveExtractionJob::start()
{
    QTimer::singleShot(0, this, &ArchiveExtractionJob::extractNext);
}

void ArchiveExtractionJob::extractNext()
{
    if (m_cancelled) {
        return;
    }
    if (m_archives.isEmpty()) {
        emitResult();
        return;
    }
    const QUrl url = m_archives.first();
    m_archive = QFileInfo(url.toLocalFile());
    m_staging.reset();
    m_destination.clear();
    m_errors.clear();
    if (!url.isLocalFile() || !m_archive.isFile() || m_archive.isSymLink()) {
        fail(i18n("Select a local archive file, not a symbolic link."));
        return;
    }
    const QString program = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (program.isEmpty()) {
        fail(i18n("Extract and trash requires bsdtar from libarchive. The archive has not been changed."));
        return;
    }
    m_staging = std::make_unique<QTemporaryDir>(m_archive.absolutePath() + QStringLiteral("/.dolphinplus-extract-XXXXXX"));
    m_staging->setAutoRemove(false);
    if (!m_staging->isValid()) {
        fail(i18n("Cannot create an extraction folder beside %1.", m_archive.fileName()));
        return;
    }
    Q_EMIT description(this, i18n("Extracting and Trashing Archive"), qMakePair(i18n("Archive"), m_archive.absoluteFilePath()));
    if (m_cancelled) {
        return;
    }
    m_process.start(program,
                    {QStringLiteral("-xf"),
                     m_archive.absoluteFilePath(),
                     QStringLiteral("-C"),
                     m_staging->path(),
                     QStringLiteral("--no-same-owner"),
                     QStringLiteral("--no-same-permissions")});
    m_process.closeWriteChannel();
}

bool ArchiveExtractionJob::archiveUnchanged() const
{
    const QFileInfo current(m_archive.absoluteFilePath());
    return current.isFile() && !current.isSymLink() && current.size() == m_archive.size() && current.lastModified() == m_archive.lastModified();
}

void ArchiveExtractionJob::publish()
{
    const QFileInfoList entries = QDir(m_staging->path()).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) {
        fail(i18n("No files were extracted."));
        return;
    }
    if (!archiveUnchanged()) {
        fail(i18n("The archive changed during extraction and will not be trashed."));
        return;
    }
    QString name = m_archive.fileName();
    const QString suffix = QMimeDatabase().suffixForFileName(name);
    if (!suffix.isEmpty()) {
        name.chop(suffix.size() + 1);
    }
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        name = QStringLiteral("archive");
    }
    const QString source = entries.size() == 1 ? entries.first().absoluteFilePath() : m_staging->path();
    m_destination = m_archive.absolutePath() + QLatin1Char('/') + (entries.size() == 1 ? entries.first().fileName() : name);
    const QFileInfo destination(m_destination);
    if (destination.exists() || destination.isSymLink() || !QDir().rename(source, m_destination)) {
        fail(i18n("Cannot move the extracted item to %1. Existing files and folders will not be overwritten or merged.", m_destination));
        return;
    }
    if (entries.size() == 1) {
        QDir().rmdir(m_staging->path());
    }
    if (!archiveUnchanged()) {
        fail(i18n("The archive changed and will not be trashed."));
        return;
    }
    auto *trash = KIO::trash({QUrl::fromLocalFile(m_archive.absoluteFilePath())}, KIO::HideProgressInfo);
    trash->setUiDelegate(nullptr);
    m_trashJob = trash;
    connect(trash, &KJob::result, this, [this](KJob *job) {
        m_trashJob = nullptr;
        if (m_cancelled) {
            return;
        }
        if (job->error()) {
            fail(job->errorString());
            return;
        }
        m_archives.removeFirst();
        extractNext();
    });
}

void ArchiveExtractionJob::fail(const QString &message)
{
    setError(KJob::UserDefinedError);
    QString detail = message + QLatin1Char('\n') + i18n("The current archive has not been trashed.");
    if (m_staging && m_staging->isValid() && QFileInfo::exists(m_staging->path())) {
        detail += QLatin1Char('\n') + i18n("Extracted data is kept at: %1", m_staging->path());
    } else if (!m_destination.isEmpty()) {
        detail += QLatin1Char('\n') + i18n("Extracted data is at: %1", m_destination);
    }
    setErrorText(detail);
    emitResult();
}

bool ArchiveExtractionJob::doKill()
{
    m_cancelled = true;
    if (m_trashJob) {
        m_trashJob->kill();
    }
    m_process.kill();
    return true;
}

QList<QAction *> ArchiveExtractionJob::actions(const KFileItemListProperties &properties, QWidget *parent)
{
    const KConfigGroup show(KSharedConfig::openConfig(QStringLiteral("kservicemenurc"), KConfig::NoGlobals), QStringLiteral("Show"));
    if (!show.readEntry("extractfileitemaction", true)) {
        return {};
    }
    static auto *plugin = []() {
        const auto metadata = KPluginMetaData::findPluginById(QStringLiteral("kf6/kfileitemaction"), QStringLiteral("extractfileitemaction"));
        auto *instance = KPluginFactory::instantiatePlugin<KAbstractFileItemActionPlugin>(metadata, qApp).plugin;
        if (instance) {
            connect(instance, &KAbstractFileItemActionPlugin::error, qApp, [](const QString &error) {
                KMessageBox::error(nullptr, error);
            });
        }
        return instance;
    }();
    if (!plugin) {
        return {};
    }
    const QList<QAction *> result = plugin->actions(properties, parent);
    for (QAction *menuAction : result) {
        if (!menuAction->menu()) {
            continue;
        }
        for (QAction *original : menuAction->menu()->actions()) {
            if (original->icon().name() != QLatin1String("archive-remove")) {
                continue;
            }
            QList<QUrl> archives;
            for (const KFileItem &item : properties.items()) {
                QWidget temporaryParent;
                if (!plugin->actions(KFileItemListProperties(KFileItemList{item}), &temporaryParent).isEmpty()) {
                    archives.append(item.url());
                }
            }
            auto *replacement = new QAction(original->icon(), original->text(), parent);
            replacement->setObjectName(QStringLiteral("dolphinplus_extract_and_trash"));
            connect(replacement, &QAction::triggered, qApp, [archives, owner = QPointer<QWidget>(parent)]() {
                auto *job = new ArchiveExtractionJob(archives);
                job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, owner));
                KIO::getJobTracker()->registerJob(job);
                job->start();
            });
            menuAction->menu()->insertAction(original, replacement);
            menuAction->menu()->removeAction(original);
            delete original;
        }
    }
    return result;
}

QList<QAction *> ArchiveExtractionJob::compressionActions(const KFileItemListProperties &properties, QWidget *parent)
{
    const KConfigGroup show(KSharedConfig::openConfig(QStringLiteral("kservicemenurc"), KConfig::NoGlobals), QStringLiteral("Show"));
    if (!show.readEntry("compressfileitemaction", true)) {
        return {};
    }
    static auto *plugin = KPluginFactory::instantiatePlugin<KAbstractFileItemActionPlugin>(
                              KPluginMetaData::findPluginById(QStringLiteral("kf6/kfileitemaction"), QStringLiteral("compressfileitemaction")),
                              qApp)
                              .plugin;
    if (!plugin) {
        return {};
    }
    const auto result = plugin->actions(properties, parent);
    const QString program = QStandardPaths::findExecutable(QStringLiteral("ark"));
    for (QAction *menuAction : result) {
        if (!menuAction->menu()) {
            continue;
        }
        const auto originals = menuAction->menu()->actions();
        if (originals.size() < 2 || originals.size() > 3) {
            continue;
        }
        for (int index = 0; index < originals.size(); ++index) {
            QAction *original = originals.at(index);
            const QString suffix = index == originals.size() - 1 ? QString() : index == 0 ? QStringLiteral("tar.gz") : QStringLiteral("zip");
            auto *replacement = new QAction(parent);
            replacement->setObjectName(QStringLiteral("dolphinplus_compress_") + (suffix.isEmpty() ? QStringLiteral("dialog") : suffix));
            const auto sync = [original, replacement, program]() {
                replacement->setText(original->text());
                replacement->setIcon(original->icon());
                replacement->setEnabled(original->isEnabled() && !program.isEmpty());
            };
            sync();
            connect(original, &QAction::changed, replacement, sync);
            connect(replacement, &QAction::triggered, qApp, [program, suffix, urls = properties.urlList()]() {
                QStringList arguments{QStringLiteral("--add"), QStringLiteral("--changetofirstpath")};
                if (suffix.isEmpty()) {
                    arguments << QStringLiteral("--dialog");
                } else {
                    arguments << QStringLiteral("--autofilename") << suffix;
                }
                arguments << QStringLiteral("--");
                for (const QUrl &url : urls) {
                    arguments << url.toLocalFile();
                }
                auto *process = new QProcess(qApp);
                process->setObjectName(QStringLiteral("dolphinplus_compression_process"));
                connect(process, &QProcess::finished, process, [process, locker = std::make_shared<QEventLoopLocker>()]() {
                    process->deleteLater();
                });
                connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) {
                        KMessageBox::error(nullptr, process->errorString());
                        process->deleteLater();
                    }
                });
                process->start(program, arguments);
            });
            menuAction->menu()->insertAction(original, replacement);
            menuAction->menu()->removeAction(original);
        }
    }
    return result;
}

#include "moc_archiveextractionjob.cpp"
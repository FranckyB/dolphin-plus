#include "archiveextractionjob.h"
#include "dolphin_generalsettings.h"

#include <archive_kerfuffle.h>
#include <jobs.h>
#include <queries.h>

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
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

ArchiveExtractionJob::ArchiveExtractionJob(const QList<QUrl> &archives)
    : m_archives(archives)
{
    setCapabilities(KJob::Killable);
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
    m_incompleteExtraction = false;
    if (!url.isLocalFile() || !m_archive.isFile() || m_archive.isSymLink()) {
        fail(i18n("Select a local archive file, not a symbolic link."));
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
    auto *extraction = Kerfuffle::Archive::batchExtract(m_archive.absoluteFilePath(), m_staging->path(), false, true, this);
    m_extractionJob = extraction;
    connect(extraction, &Kerfuffle::Job::userQuery, this, [this](Kerfuffle::Query *query) {
        if (dynamic_cast<Kerfuffle::ContinueExtractionQuery *>(query) || dynamic_cast<Kerfuffle::LoadCorruptQuery *>(query)) {
            m_incompleteExtraction = true;
        }
        query->execute();
    });
    connect(extraction, &KJob::percentChanged, this, [this](KJob *, unsigned long percent) {
        setPercent(percent);
    });
    connect(extraction, &KJob::result, this, [this](KJob *finished) {
        m_extractionJob = nullptr;
        if (m_cancelled) {
            return;
        }
        if (finished->error()) {
            if (finished->error() == KJob::KilledJobError) {
                kill(KJob::EmitResult);
            } else {
                fail(finished->errorString());
            }
            return;
        }
        if (m_incompleteExtraction) {
            fail(i18n("Ark reported damaged or incomplete archive contents."));
            return;
        }
        publish();
    });
    extraction->start();
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
    if (m_extractionJob) {
        m_extractionJob->kill();
    }
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
    const QList<QAction *> arkActions = plugin->actions(properties, parent);
    if (!GeneralSettings::useImprovedArchiveExtraction()) {
        return arkActions;
    }
    QList<QAction *> result = arkActions;
    for (QAction *menuAction : arkActions) {
        if (!menuAction->menu()) {
            continue;
        }
        for (QAction *original : menuAction->menu()->actions()) {
            if (original->icon().name() != QLatin1String("archive-remove")) {
                continue;
            }
            menuAction->menu()->removeAction(original);
            QList<QUrl> archives;
            for (const KFileItem &item : properties.items()) {
                QWidget temporaryParent;
                if (!plugin->actions(KFileItemListProperties(KFileItemList{item}), &temporaryParent).isEmpty()) {
                    archives.append(item.url());
                }
            }
            auto *replacement = new QAction(original->icon(), i18nc("@action:inmenu", "Extract Here and Trash Archive"), parent);
            replacement->setObjectName(QStringLiteral("dolphinplus_extract_and_trash"));
            QPointer<QWidget> owner = parent;
            while (owner && qobject_cast<QMenu *>(owner.data())) {
                owner = owner->parentWidget();
            }
            if (owner) {
                owner = owner->window();
            }
            connect(replacement, &QAction::triggered, qApp, [archives, owner]() {
                auto *job = new ArchiveExtractionJob(archives);
                job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, owner));
                connect(job, &KJob::result, qApp, [owner](KJob *finished) {
                    if (finished->error() && finished->error() != KJob::KilledJobError) {
                        QTimer::singleShot(0, qApp, [owner, message = finished->errorString()]() {
                            KMessageBox::error(owner, message);
                        });
                    }
                });
                KIO::getJobTracker()->registerJob(job);
                job->start();
            });
            result.append(replacement);
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
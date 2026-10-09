#include "foldercover.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

FolderCover::Settings FolderCover::loadSettings()
{
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("FolderCovers"));
    Settings settings;
    settings.mode = static_cast<Mode>(qBound(0, group.readEntry("Mode", int(settings.mode)), 2));
    settings.templatePath = group.readEntry("Template", settings.templatePath);
    settings.contentRect = group.readEntry("ContentRect", settings.contentRect).intersected(QRect(0, 0, 256, 256));
    if (settings.contentRect.isEmpty()) {
        settings.contentRect = Settings().contentRect;
    }
    settings.radius = qBound(0, group.readEntry("Radius", settings.radius), 128);
    settings.subfolderDepth = qBound(0, group.readEntry("SubfolderDepth", settings.subfolderDepth), 4);
    settings.videoFallback = group.readEntry("VideoFallback", settings.videoFallback);
    settings.respectCustomIcons = group.readEntry("RespectCustomIcons", settings.respectCustomIcons);
    return settings;
}

void FolderCover::saveSettings(const Settings &settings)
{
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("FolderCovers"));
    group.writeEntry("Mode", int(settings.mode));
    group.writeEntry("Template", settings.templatePath);
    group.writeEntry("ContentRect", settings.contentRect);
    group.writeEntry("Radius", settings.radius);
    group.writeEntry("SubfolderDepth", settings.subfolderDepth);
    group.writeEntry("VideoFallback", settings.videoFallback);
    group.writeEntry("RespectCustomIcons", settings.respectCustomIcons);
    group.sync();
}

namespace
{
QImage readImage(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid()) {
        reader.setScaledSize(original.scaled(QSize(1024, 1024), Qt::KeepAspectRatio));
    }
    return reader.read();
}

QByteArray runMediaTool(const QString &program, const QStringList &arguments, const FolderCover::Cancellation &cancel)
{
    if (program.isEmpty() || cancel->load()) {
        return {};
    }
    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        if (cancel->load() || timer.elapsed() > 10000 || process.state() == QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? process.readAllStandardOutput() : QByteArray();
}

QImage videoFrame(const QString &path, const FolderCover::Cancellation &cancel)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        return {};
    }
    const auto probe = runMediaTool(QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
                                    {QStringLiteral("-v"),
                                     QStringLiteral("error"),
                                     QStringLiteral("-show_entries"),
                                     QStringLiteral("format=duration:stream=duration"),
                                     QStringLiteral("-select_streams"),
                                     QStringLiteral("v:0"),
                                     QStringLiteral("-of"),
                                     QStringLiteral("json"),
                                     path},
                                    cancel);
    const QJsonObject metadata = QJsonDocument::fromJson(probe).object();
    double duration = metadata.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    if (duration <= 0) {
        const auto streams = metadata.value(QStringLiteral("streams")).toArray();
        if (!streams.isEmpty()) {
            duration = streams.first().toObject().value(QStringLiteral("duration")).toString().toDouble();
        }
    }
    const QByteArray frame = runMediaTool(ffmpeg,
                                          {QStringLiteral("-nostdin"),
                                           QStringLiteral("-v"),
                                           QStringLiteral("error"),
                                           QStringLiteral("-threads"),
                                           QStringLiteral("1"),
                                           QStringLiteral("-filter_threads"),
                                           QStringLiteral("1"),
                                           QStringLiteral("-ss"),
                                           QString::number(duration > 0 ? duration / 2 : 1),
                                           QStringLiteral("-i"),
                                           path,
                                           QStringLiteral("-frames:v"),
                                           QStringLiteral("1"),
                                           QStringLiteral("-vf"),
                                           QStringLiteral("scale=1024:1024:force_original_aspect_ratio=decrease"),
                                           QStringLiteral("-f"),
                                           QStringLiteral("image2pipe"),
                                           QStringLiteral("-vcodec"),
                                           QStringLiteral("png"),
                                           QStringLiteral("pipe:1")},
                                          cancel);
    return QImage::fromData(frame, "PNG");
}
}

QImage FolderCover::generate(const QUrl &directory, const Settings &settings, const QSize &size, const Cancellation &cancel)
{
    if (!directory.isLocalFile() || cancel->load() || size.isEmpty() || size.width() > 2048 || size.height() > 2048) {
        return {};
    }
    const QString root = directory.toLocalFile();
    if (settings.respectCustomIcons && QFileInfo::exists(root + QStringLiteral("/.directory"))) {
        KConfig metadata(root + QStringLiteral("/.directory"), KConfig::SimpleConfig);
        if (!metadata.group(QStringLiteral("Desktop Entry")).readEntry("Icon", QString()).isEmpty()) {
            return {};
        }
    }
    const QImage folder = readImage(settings.templatePath);
    if (folder.isNull()) {
        return {};
    }
    QByteArray identity;
    QDataStream stream(&identity, QIODevice::WriteOnly);
    stream << QStringLiteral("folder-cover-v1") << directory << settings.templatePath << folder.size() << settings.contentRect << settings.radius << size;
    stream << QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(folder.constBits()), folder.sizeInBytes()), QCryptographicHash::Sha256);
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/dolphinplus/folder-covers");
    const QStringList imageExtensions{QStringLiteral("jpg"),
                                      QStringLiteral("jpeg"),
                                      QStringLiteral("png"),
                                      QStringLiteral("webp"),
                                      QStringLiteral("bmp"),
                                      QStringLiteral("gif")};
    const QStringList videoExtensions{QStringLiteral("mp4"),
                                      QStringLiteral("mkv"),
                                      QStringLiteral("avi"),
                                      QStringLiteral("mov"),
                                      QStringLiteral("wmv"),
                                      QStringLiteral("flv"),
                                      QStringLiteral("webm"),
                                      QStringLiteral("m4v"),
                                      QStringLiteral("mpg"),
                                      QStringLiteral("mpeg"),
                                      QStringLiteral("ts"),
                                      QStringLiteral("mts"),
                                      QStringLiteral("m2ts")};
    const QStringList preferred{QStringLiteral("fanart"), QStringLiteral("landscape"), QStringLiteral("poster")};
    QStringList directories{root};
    int scanned = 0;
    int attempted = 0;
    for (int depth = 0; depth <= qBound(0, settings.subfolderDepth, 4) && !directories.isEmpty(); ++depth) {
        QStringList nextDirectories;
        for (const QString &path : std::as_const(directories)) {
            if (cancel->load() || scanned >= 10000) {
                return {};
            }
            QFileInfoList candidates;
            QDirIterator entries(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
            while (entries.hasNext() && scanned++ < 10000 && !cancel->load()) {
                entries.next();
                const QFileInfo entry = entries.fileInfo();
                if (entry.isDir()) {
                    if (!entry.isSymLink() && nextDirectories.size() < 128) {
                        nextDirectories.append(entry.absoluteFilePath());
                    }
                } else if (imageExtensions.contains(entry.suffix().toLower())
                           || (settings.videoFallback && videoExtensions.contains(entry.suffix().toLower()))) {
                    candidates.append(entry);
                }
            }
            std::sort(candidates.begin(), candidates.end(), [&preferred, &imageExtensions](const QFileInfo &first, const QFileInfo &second) {
                const auto rank = [&preferred, &imageExtensions](const QFileInfo &entry) {
                    const int preferredIndex = preferred.indexOf(entry.completeBaseName().toLower());
                    return (imageExtensions.contains(entry.suffix().toLower()) ? 0 : 10) + (preferredIndex < 0 ? preferred.size() : preferredIndex);
                };
                if (rank(first) != rank(second)) {
                    return rank(first) < rank(second);
                }
                const int comparison = QString::compare(first.fileName(), second.fileName(), Qt::CaseInsensitive);
                return comparison ? comparison < 0 : first.fileName() < second.fileName();
            });
            for (const auto &candidate : std::as_const(candidates)) {
                if (cancel->load() || ++attempted > 32) {
                    return {};
                }
                QByteArray sourceIdentity = identity;
                QDataStream sourceStream(&sourceIdentity, QIODevice::Append);
                sourceStream << candidate.absoluteFilePath() << candidate.lastModified() << candidate.size();
                const QString cachePath = cacheDirectory + QLatin1Char('/')
                    + QString::fromLatin1(QCryptographicHash::hash(sourceIdentity, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png");
                QImage cover(cachePath);
                if (!cover.isNull() && cover.size() == size) {
                    return cover;
                }
                const QImage source = imageExtensions.contains(candidate.suffix().toLower()) ? readImage(candidate.absoluteFilePath())
                                                                                             : videoFrame(candidate.absoluteFilePath(), cancel);
                if (source.isNull() || cancel->load()) {
                    continue;
                }
                cover = compose(folder, source, settings, size);
                if (!cover.isNull() && QDir().mkpath(cacheDirectory)) {
                    QSaveFile cache(cachePath);
                    if (cache.open(QIODevice::WriteOnly) && cover.save(&cache, "PNG")) {
                        cache.commit();
                    }
                    const auto cached = QDir(cacheDirectory).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time);
                    qint64 bytes = 0;
                    for (int index = 0; index < cached.size(); ++index) {
                        bytes += cached.at(index).size();
                        if (index >= 1024 || bytes > 256 * 1024 * 1024) {
                            QFile::remove(cached.at(index).absoluteFilePath());
                        }
                    }
                }
                return cover;
            }
        }
        std::sort(nextDirectories.begin(), nextDirectories.end(), [](const QString &first, const QString &second) {
            const int comparison = QString::compare(first, second, Qt::CaseInsensitive);
            return comparison ? comparison < 0 : first < second;
        });
        directories = nextDirectories;
    }
    return {};
}

QImage FolderCover::compose(const QImage &folder, const QImage &source, const Settings &settings, const QSize &size)
{
    if (folder.isNull() || source.isNull() || size.isEmpty()) {
        return {};
    }
    const QRectF target = settings.contentRect.intersected(QRect(0, 0, 256, 256));
    if (target.isEmpty()) {
        return {};
    }
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.scale(size.width() / 256.0, size.height() / 256.0);
    painter.drawImage(QRectF(0, 0, 256, 256), folder);
    const qreal radius = qBound(0.0, qreal(settings.radius), qMin(target.width(), target.height()) / 2);
    QPainterPath clip;
    clip.addRoundedRect(target, radius, radius);
    painter.setClipPath(clip);
    QSizeF cropSize = source.size();
    const qreal scale = qMax(target.width() / cropSize.width(), target.height() / cropSize.height());
    cropSize = target.size() / scale;
    const QRectF crop((source.width() - cropSize.width()) / 2, (source.height() - cropSize.height()) / 2, cropSize.width(), cropSize.height());
    painter.drawImage(target, source, crop);
    return result;
}
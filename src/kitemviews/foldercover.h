#ifndef FOLDERCOVER_H
#define FOLDERCOVER_H

#include "dolphin_export.h"

#include <QImage>
#include <QRect>
#include <QString>
#include <QUrl>

#include <atomic>
#include <memory>

namespace FolderCover
{
enum class Mode {
    Standard,
    SingleCover,
    Disabled
};

struct Settings {
    Mode mode = Mode::SingleCover;
    QString templatePath = QStringLiteral(":/dolphinplus/foldercover.png");
    QRect contentRect{15, 67, 225, 141};
    int radius = 18;
    int subfolderDepth = 1;
    bool videoFallback = true;
    bool respectCustomIcons = true;
    bool operator==(const Settings &) const = default;
};

using Cancellation = std::shared_ptr<std::atomic_bool>;
DOLPHIN_EXPORT Settings loadSettings();
DOLPHIN_EXPORT void saveSettings(const Settings &settings);
DOLPHIN_EXPORT bool usesCustomIcon(const QUrl &directory, const Settings &settings);
DOLPHIN_EXPORT QImage generate(const QUrl &directory, const Settings &settings, const QSize &size, const Cancellation &cancel);
DOLPHIN_EXPORT QImage compose(const QImage &folder, const QImage &source, const Settings &settings, const QSize &size);
}

#endif
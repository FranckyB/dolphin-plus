/*
 * SPDX-FileCopyrightText: 2006 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PREVIEWSSETTINGSPAGE_H
#define PREVIEWSSETTINGSPAGE_H

#include <kiowidgets_export.h>

#include "kitemviews/foldercover.h"
#include "settings/settingspagebase.h"

class QCheckBox;
class QSpinBox;
class QListView;
class QModelIndex;
class QComboBox;
class QLineEdit;
class QLabel;

/**
 * @brief Allows the configuration of file previews.
 */
class PreviewsSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit PreviewsSettingsPage(QWidget *parent);
    ~PreviewsSettingsPage() override;

    /**
     * Applies the general settings for the view modes
     * The settings are persisted automatically when
     * closing Dolphin.
     */
    void applySettings() override;

    /** Restores the settings to default values. */
    void restoreDefaults() override;

protected:
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles = QList<int>());

private:
    void loadPreviewPlugins();
    void loadSettings();

private:
    bool m_initialized;
    QListView *m_listView;
    QStringList m_enabledPreviewPlugins;
    QSpinBox *m_localFileSizeBox;
    QSpinBox *m_remoteFileSizeBox;
    QCheckBox *m_enableRemoteFolderThumbnail;
};

class FolderCoversSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit FolderCoversSettingsPage(QWidget *parent = nullptr);
    void applySettings() override;
    void restoreDefaults() override;

private:
    FolderCover::Settings formSettings() const;
    void loadForm(const FolderCover::Settings &settings);
    void updatePreview();

    QComboBox *m_coverMode;
    QLineEdit *m_templatePath;
    QLineEdit *m_samplePath;
    QSpinBox *m_contentX;
    QSpinBox *m_contentY;
    QSpinBox *m_contentWidth;
    QSpinBox *m_contentHeight;
    QSpinBox *m_cornerRadius;
    QSpinBox *m_subfolderDepth;
    QCheckBox *m_videoFallback;
    QCheckBox *m_respectCustomIcons;
    QWidget *m_coverOptions;
    QLabel *m_coverPreview;
};

#endif

/*
 * SPDX-FileCopyrightText: 2006 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "previewssettingspage.h"

#include "dolphin_generalsettings.h"
#include "settings/servicemodel.h"

#include <KContextualHelpButton>
#include <KIO/PreviewJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginMetaData>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QScroller>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QToolButton>

// default settings
namespace
{
const int DefaultMaxLocalPreviewSize = 0; // 0 MB
const int DefaultMaxRemotePreviewSize = 0; // 0 MB
const bool EnableRemoteFolderThumbnail = false;
}

PreviewsSettingsPage::PreviewsSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
    , m_initialized(false)
    , m_listView(nullptr)
    , m_enabledPreviewPlugins()
    , m_localFileSizeBox(nullptr)
    , m_remoteFileSizeBox(nullptr)
    , m_enableRemoteFolderThumbnail(nullptr)
{
    QVBoxLayout *topLayout = new QVBoxLayout(this);

    QLabel *showPreviewsLabel = new QLabel(i18nc("@title:group", "Show previews in the view for:"), this);

    m_listView = new QListView(this);
    QScroller::grabGesture(m_listView->viewport(), QScroller::TouchGesture);

    ServiceModel *serviceModel = new ServiceModel(this);
    QSortFilterProxyModel *proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(serviceModel);
    proxyModel->setSortRole(Qt::DisplayRole);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_listView->setModel(proxyModel);
    m_listView->setVerticalScrollMode(QListView::ScrollPerPixel);
    m_listView->setUniformItemSizes(true);

    // i18n: This label forms a full sentence together with the spinbox content.
    // Depending on the option chosen in the spinbox, it reads "Show previews for [files below n MiB]"
    // or "Show previews for [files of any size]".
    QLabel *localFileSizeLabel = new QLabel(i18nc("@label:spinbox", "Show previews for"), this);

    m_localFileSizeBox = new QSpinBox(this);
    m_localFileSizeBox->setSingleStep(1);
    m_localFileSizeBox->setPrefix(i18nc("used as a prefix in a spinbox showing e.g. 'Show previews for [files below 3 MiB]'", "files below "));
    m_localFileSizeBox->setSuffix(i18nc("Mebibytes; used as a suffix in a spinbox showing e.g. '3 MiB'", " MiB"));
    m_localFileSizeBox->setRange(0, 9999999); /* MB */
    m_localFileSizeBox->setSpecialValueText(i18nc("e.g. 'Show previews for [files of any size]'", "files of any size"));

    QHBoxLayout *localFileSizeBoxLayout = new QHBoxLayout();
    localFileSizeBoxLayout->addWidget(localFileSizeLabel);
    localFileSizeBoxLayout->addWidget(m_localFileSizeBox);

    QLabel *remoteFileSizeLabel = new QLabel(i18nc("@label:spinbox", "Show previews for"), this);

    m_remoteFileSizeBox = new QSpinBox(this);
    m_remoteFileSizeBox->setSingleStep(1);
    m_remoteFileSizeBox->setPrefix(i18nc("used as a prefix in a spinbox showing e.g. 'Show previews for [files below 3 MiB]'", "files below "));
    m_remoteFileSizeBox->setSuffix(i18nc("Mebibytes; used as a suffix in a spinbox showing e.g. '3 MiB'", " MiB"));
    m_remoteFileSizeBox->setRange(0, 9999999); /* MB */
    m_remoteFileSizeBox->setSpecialValueText(i18nc("e.g. 'Show previews for [no file]'", "no file"));

    QHBoxLayout *remoteFileSizeBoxLayout = new QHBoxLayout();
    remoteFileSizeBoxLayout->addWidget(remoteFileSizeLabel);
    remoteFileSizeBoxLayout->addWidget(m_remoteFileSizeBox);

    // Enable remote folder thumbnail option
    m_enableRemoteFolderThumbnail = new QCheckBox(i18nc("@option:check", "Show previews for folders"), this);

    // Make the "Enable preview for remote folder" enabled only when "Remote file limit" is superior to 0
    m_enableRemoteFolderThumbnail->setEnabled(m_remoteFileSizeBox->value() > 0);
    connect(m_remoteFileSizeBox, &QSpinBox::valueChanged, this, [this](int i) {
        m_enableRemoteFolderThumbnail->setEnabled(i > 0);
    });

    const auto helpButtonInfo = xi18nc("@info",
                                       "<para>Creating <emphasis>previews</emphasis> for remote folders is "
                                       "very intensive in terms of network resource usage.</para>"
                                       "<para>Disable this if navigating remote folders in Dolphin "
                                       "is slow or when accessing storage over metered connections.</para>");
    auto contextualHelpButton = new KContextualHelpButton{helpButtonInfo, m_enableRemoteFolderThumbnail, this};

    QHBoxLayout *enableRemoteFolderThumbnailLayout = new QHBoxLayout();
    enableRemoteFolderThumbnailLayout->addWidget(m_enableRemoteFolderThumbnail);
    enableRemoteFolderThumbnailLayout->addWidget(contextualHelpButton);

    QFormLayout *formLayout = new QFormLayout();

    QLabel *localGroupLabel = new QLabel(i18nc("@title:group", "Local storage:"));
    // Makes sure it has the same height as the labeled sizeBoxLayout
    localGroupLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    formLayout->addRow(localGroupLabel, localFileSizeBoxLayout);

    QLabel *remoteGroupLabel = new QLabel(i18nc("@title:group", "Remote storage:"));
    remoteGroupLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    formLayout->addRow(remoteGroupLabel, remoteFileSizeBoxLayout);

    formLayout->addRow(QString(), enableRemoteFolderThumbnailLayout);

    topLayout->addWidget(showPreviewsLabel);
    topLayout->addWidget(m_listView);
    topLayout->addLayout(formLayout);

    // So that m_listView takes up all available space
    topLayout->setStretchFactor(m_listView, 1);

    loadSettings();

    connect(m_listView->model(), &QAbstractItemModel::dataChanged, this, &PreviewsSettingsPage::dataChanged);
    connect(m_localFileSizeBox, &QSpinBox::valueChanged, this, &PreviewsSettingsPage::changed);
    connect(m_remoteFileSizeBox, &QSpinBox::valueChanged, this, &PreviewsSettingsPage::changed);
    connect(m_enableRemoteFolderThumbnail, &QCheckBox::toggled, this, &PreviewsSettingsPage::changed);
}

PreviewsSettingsPage::~PreviewsSettingsPage() = default;

void PreviewsSettingsPage::applySettings()
{
    const QAbstractItemModel *model = m_listView->model();
    const int rowCount = model->rowCount();
    if (rowCount > 0) {
        m_enabledPreviewPlugins.clear();
        for (int i = 0; i < rowCount; ++i) {
            const QModelIndex index = model->index(i, 0);
            const bool checked = model->data(index, Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
            if (checked) {
                const QString enabledPlugin = model->data(index, Qt::UserRole).toString();
                m_enabledPreviewPlugins.append(enabledPlugin);
            }
        }
    }

    KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
    globalConfig.writeEntry("Plugins", m_enabledPreviewPlugins);

    if (!m_localFileSizeBox->value()) {
        globalConfig.deleteEntry("MaximumSize", KConfigBase::Normal | KConfigBase::Global);
    } else {
        const qulonglong maximumLocalSize = static_cast<qulonglong>(m_localFileSizeBox->value()) * 1024 * 1024;
        globalConfig.writeEntry("MaximumSize", maximumLocalSize, KConfigBase::Normal | KConfigBase::Global);
    }

    const qulonglong maximumRemoteSize = static_cast<qulonglong>(m_remoteFileSizeBox->value()) * 1024 * 1024;
    globalConfig.writeEntry("MaximumRemoteSize", maximumRemoteSize, KConfigBase::Normal | KConfigBase::Global);

    globalConfig.writeEntry("EnableRemoteFolderThumbnail", m_enableRemoteFolderThumbnail->isChecked(), KConfigBase::Normal | KConfigBase::Global);

    globalConfig.sync();
}

void PreviewsSettingsPage::restoreDefaults()
{
    m_localFileSizeBox->setValue(DefaultMaxLocalPreviewSize);
    m_remoteFileSizeBox->setValue(DefaultMaxRemotePreviewSize);
    m_enableRemoteFolderThumbnail->setChecked(EnableRemoteFolderThumbnail);
}

void PreviewsSettingsPage::showEvent(QShowEvent *event)
{
    if (!event->spontaneous() && !m_initialized) {
        loadPreviewPlugins();
        m_initialized = true;
    }
    SettingsPageBase::showEvent(event);
}

void PreviewsSettingsPage::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    Q_UNUSED(topLeft)
    Q_UNUSED(bottomRight)

    if (!m_initialized) {
        // Not initialized yet; ignore this.
        return;
    }

    if (roles.isEmpty() || roles.contains(Qt::CheckStateRole)) {
        Q_EMIT changed();
    }
}

void PreviewsSettingsPage::loadPreviewPlugins()
{
    QAbstractItemModel *model = m_listView->model();

    const QVector<KPluginMetaData> plugins = KIO::PreviewJob::availableThumbnailerPlugins();
    for (const KPluginMetaData &plugin : plugins) {
        const bool show = m_enabledPreviewPlugins.contains(plugin.pluginId());

        model->insertRow(0);
        const QModelIndex index = model->index(0, 0);
        model->setData(index, show ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        model->setData(index, plugin.name(), Qt::DisplayRole);
        model->setData(index, plugin.pluginId(), ServiceModel::DesktopEntryNameRole);
        model->setData(index, plugin.fileName(), Qt::ToolTipRole);
    }

    model->sort(Qt::DisplayRole);
}

void PreviewsSettingsPage::loadSettings()
{
    const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
    m_enabledPreviewPlugins = globalConfig.readEntry("Plugins", KIO::PreviewJob::defaultPlugins());

    const qulonglong defaultLocalPreview = static_cast<qulonglong>(DefaultMaxLocalPreviewSize) * 1024 * 1024;
    const qulonglong maxLocalByteSize = globalConfig.readEntry("MaximumSize", defaultLocalPreview);
    const int maxLocalMByteSize = maxLocalByteSize / (1024 * 1024);
    m_localFileSizeBox->setValue(maxLocalMByteSize);

    const qulonglong defaultRemotePreview = static_cast<qulonglong>(DefaultMaxRemotePreviewSize) * 1024 * 1024;
    const qulonglong maxRemoteByteSize = globalConfig.readEntry("MaximumRemoteSize", defaultRemotePreview);
    const int maxRemoteMByteSize = maxRemoteByteSize / (1024 * 1024);
    m_remoteFileSizeBox->setValue(maxRemoteMByteSize);

    m_enableRemoteFolderThumbnail->setChecked(globalConfig.readEntry("EnableRemoteFolderThumbnail", EnableRemoteFolderThumbnail));
}

FolderCoversSettingsPage::FolderCoversSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *modeForm = new QFormLayout;
    m_coverMode = new QComboBox(this);
    m_coverMode->setObjectName(QStringLiteral("folder_cover_mode"));
    m_coverMode->addItems(
        {i18nc("@item:inlistbox", "Standard Dolphin previews"), i18nc("@item:inlistbox", "Single cover"), i18nc("@item:inlistbox", "No folder previews")});
    modeForm->addRow(i18nc("@label:listbox", "Folder previews:"), m_coverMode);
    layout->addLayout(modeForm);

    m_coverOptions = new QWidget(this);
    auto *optionsLayout = new QHBoxLayout(m_coverOptions);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout;
    const auto addPath = [this, form](const QString &label, const QString &name) {
        auto *path = new QLineEdit(m_coverOptions);
        path->setObjectName(name);
        auto *browse = new QToolButton(m_coverOptions);
        browse->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
        browse->setToolTip(i18nc("@info:tooltip", "Choose image"));
        auto *row = new QHBoxLayout;
        row->addWidget(path);
        row->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QToolButton::clicked, this, [this, path]() {
            const QString chosen =
                QFileDialog::getOpenFileName(this, i18nc("@title:window", "Choose Image"), path->text(), i18n("Images (*.png *.webp *.jpg *.jpeg *.bmp)"));
            if (!chosen.isEmpty()) {
                path->setText(chosen);
            }
        });
        return path;
    };
    m_templatePath = addPath(i18nc("@label:textbox", "Folder template:"), QStringLiteral("folder_cover_template"));
    m_templatePath->setPlaceholderText(i18nc("@info:placeholder", "Built-in blue folder"));
    m_samplePath = addPath(i18nc("@label:textbox", "Preview image:"), QStringLiteral("folder_cover_sample"));
    const auto addNumber = [this, form](const QString &label, const QString &name, int minimum, int maximum) {
        auto *number = new QSpinBox(m_coverOptions);
        number->setObjectName(name);
        number->setRange(minimum, maximum);
        form->addRow(label, number);
        return number;
    };
    form->addRow(new QLabel(i18nc("@title:group", "Content region (256 x 256 template coordinates)"), m_coverOptions));
    m_contentX = addNumber(i18nc("@label:spinbox", "X:"), QStringLiteral("folder_cover_x"), 0, 255);
    m_contentY = addNumber(i18nc("@label:spinbox", "Y:"), QStringLiteral("folder_cover_y"), 0, 255);
    m_contentWidth = addNumber(i18nc("@label:spinbox", "Width:"), QStringLiteral("folder_cover_width"), 1, 256);
    m_contentHeight = addNumber(i18nc("@label:spinbox", "Height:"), QStringLiteral("folder_cover_height"), 1, 256);
    m_cornerRadius = addNumber(i18nc("@label:spinbox", "Corner radius:"), QStringLiteral("folder_cover_radius"), 0, 128);
    m_subfolderDepth = addNumber(i18nc("@label:spinbox", "Subfolder search depth:"), QStringLiteral("folder_cover_depth"), 0, 4);
    m_subfolderDepth->setSpecialValueText(i18nc("@item:inlistbox", "This folder only"));
    m_videoFallback = new QCheckBox(i18nc("@option:check", "Use a video frame when no image is readable"), m_coverOptions);
    m_respectCustomIcons = new QCheckBox(i18nc("@option:check", "Keep existing theme-based folder icons"), m_coverOptions);
    m_respectCustomIcons->setObjectName(QStringLiteral("folder_cover_respect_icons"));
    form->addRow(m_videoFallback);
    form->addRow(m_respectCustomIcons);
    optionsLayout->addLayout(form, 1);
    m_coverPreview = new QLabel(m_coverOptions);
    m_coverPreview->setObjectName(QStringLiteral("folder_cover_preview"));
    m_coverPreview->setFixedSize(256, 256);
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setWordWrap(true);
    optionsLayout->addWidget(m_coverPreview, 0, Qt::AlignTop);
    layout->addWidget(m_coverOptions);
    layout->addStretch();
    loadForm(FolderCover::loadSettings());

    const auto update = [this]() {
        updatePreview();
        Q_EMIT changed();
    };
    connect(m_coverMode, &QComboBox::currentIndexChanged, this, update);
    connect(m_templatePath, &QLineEdit::textChanged, this, update);
    connect(m_samplePath, &QLineEdit::textChanged, this, &FolderCoversSettingsPage::updatePreview);
    for (auto *number : {m_contentX, m_contentY, m_contentWidth, m_contentHeight, m_cornerRadius, m_subfolderDepth}) {
        connect(number, &QSpinBox::valueChanged, this, update);
    }
    connect(m_videoFallback, &QCheckBox::toggled, this, update);
    connect(m_respectCustomIcons, &QCheckBox::toggled, this, update);
}

FolderCover::Settings FolderCoversSettingsPage::formSettings() const
{
    FolderCover::Settings settings;
    settings.mode = static_cast<FolderCover::Mode>(m_coverMode->currentIndex());
    if (!m_templatePath->text().trimmed().isEmpty()) {
        settings.templatePath = m_templatePath->text().trimmed();
    }
    settings.contentRect = QRect(m_contentX->value(), m_contentY->value(), m_contentWidth->value(), m_contentHeight->value());
    settings.radius = m_cornerRadius->value();
    settings.subfolderDepth = m_subfolderDepth->value();
    settings.videoFallback = m_videoFallback->isChecked();
    settings.respectCustomIcons = m_respectCustomIcons->isChecked();
    return settings;
}

void FolderCoversSettingsPage::loadForm(const FolderCover::Settings &settings)
{
    m_coverMode->setCurrentIndex(int(settings.mode));
    m_templatePath->setText(settings.templatePath == FolderCover::Settings().templatePath ? QString() : settings.templatePath);
    m_contentX->setValue(settings.contentRect.x());
    m_contentY->setValue(settings.contentRect.y());
    m_contentWidth->setMaximum(256 - settings.contentRect.x());
    m_contentHeight->setMaximum(256 - settings.contentRect.y());
    m_contentWidth->setValue(settings.contentRect.width());
    m_contentHeight->setValue(settings.contentRect.height());
    m_cornerRadius->setValue(settings.radius);
    m_subfolderDepth->setValue(settings.subfolderDepth);
    m_videoFallback->setChecked(settings.videoFallback);
    m_respectCustomIcons->setChecked(settings.respectCustomIcons);
    updatePreview();
}

void FolderCoversSettingsPage::updatePreview()
{
    m_contentWidth->setMaximum(256 - m_contentX->value());
    m_contentHeight->setMaximum(256 - m_contentY->value());
    const auto settings = formSettings();
    m_coverOptions->setEnabled(settings.mode == FolderCover::Mode::SingleCover);
    QImageReader templateReader(settings.templatePath);
    templateReader.setScaledSize(QSize(256, 256));
    const QImage folder = templateReader.read();
    QImage sample;
    if (!m_samplePath->text().isEmpty()) {
        QImageReader reader(m_samplePath->text());
        reader.setAutoTransform(true);
        if (reader.size().isValid()) {
            reader.setScaledSize(reader.size().scaled(QSize(512, 512), Qt::KeepAspectRatio));
        }
        sample = reader.read();
    } else {
        sample = QImage(256, 256, QImage::Format_RGB32);
        QPainter painter(&sample);
        for (int row = 0; row < 16; ++row) {
            for (int column = 0; column < 16; ++column) {
                painter.fillRect(column * 16, row * 16, 16, 16, (row + column) % 2 ? QColor(220, 220, 220) : QColor(160, 160, 160));
            }
        }
    }
    const QImage cover = FolderCover::compose(folder, sample, settings, QSize(256, 256));
    if (cover.isNull()) {
        m_coverPreview->setText(i18nc("@info", "Cannot read the template or preview image."));
    } else {
        m_coverPreview->setPixmap(QPixmap::fromImage(cover));
    }
}

void FolderCoversSettingsPage::applySettings()
{
    const auto settings = formSettings();
    if (settings.mode == FolderCover::Mode::SingleCover && !QImageReader(settings.templatePath).canRead()) {
        KMessageBox::error(this, i18nc("@info", "Choose a readable folder template, or clear the template field to use the built-in folder."));
        return;
    }
    FolderCover::saveSettings(settings);
}

void FolderCoversSettingsPage::restoreDefaults()
{
    loadForm(FolderCover::Settings());
    Q_EMIT changed();
}

#include "moc_previewssettingspage.cpp"

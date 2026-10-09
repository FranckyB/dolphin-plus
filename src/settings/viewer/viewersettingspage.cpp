#include "viewersettingspage.h"

#include "dolphin_generalsettings.h"
#include "dolphinimageviewer.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KShortcutsEditor>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>

ViewerSettingsPage::ViewerSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_fullscreen = new QCheckBox(i18nc("@option:check", "Open in fullscreen"), this);
    m_fullscreen->setObjectName(QStringLiteral("viewer_open_fullscreen"));
    m_fullscreen->setChecked(GeneralSettings::imageViewerOpenFullscreen());
    form->addRow(m_fullscreen);
    m_keepZoomAndPosition = new QCheckBox(i18nc("@option:check", "Keep zoom and position between images"), this);
    m_keepZoomAndPosition->setObjectName(QStringLiteral("viewer_keep_zoom_position"));
    m_keepZoomAndPosition->setChecked(GeneralSettings::imageViewerKeepZoomAndPosition());
    form->addRow(m_keepZoomAndPosition);
    m_fitPolicy = new QComboBox(this);
    m_fitPolicy->setObjectName(QStringLiteral("viewer_fit_policy"));
    m_fitPolicy->addItem(i18nc("@item:inlistbox", "Fit larger images only"));
    m_fitPolicy->addItem(i18nc("@item:inlistbox", "Fit all images (enlarge smaller images)"));
    m_fitPolicy->setToolTip(i18nc("@info:tooltip", "Applies to local images with readable dimensions. Other images use the component's native fit behavior."));
    m_fitPolicy->setCurrentIndex(GeneralSettings::imageViewerEnlargeSmallerImages() ? 1 : 0);
    form->addRow(i18nc("@label:listbox", "Zoom to fit:"), m_fitPolicy);
    layout->addLayout(form);
    m_actions = DolphinImageViewer::createActionCollection(this);
    m_shortcuts = new KShortcutsEditor(m_actions, this, KShortcutsEditor::WidgetAction, KShortcutsEditor::LetterShortcutsAllowed);
    layout->addWidget(m_shortcuts);
    connect(m_fullscreen, &QCheckBox::toggled, this, &ViewerSettingsPage::changed);
    connect(m_keepZoomAndPosition, &QCheckBox::toggled, this, &ViewerSettingsPage::changed);
    connect(m_fitPolicy, &QComboBox::currentIndexChanged, this, &ViewerSettingsPage::changed);
    connect(m_shortcuts, &KShortcutsEditor::keyChange, this, &ViewerSettingsPage::changed);
}

ViewerSettingsPage::~ViewerSettingsPage()
{
    delete m_shortcuts;
}

void ViewerSettingsPage::applySettings()
{
    GeneralSettings::setImageViewerOpenFullscreen(m_fullscreen->isChecked());
    GeneralSettings::setImageViewerKeepZoomAndPosition(m_keepZoomAndPosition->isChecked());
    GeneralSettings::setImageViewerEnlargeSmallerImages(m_fitPolicy->currentIndex() == 1);
    GeneralSettings::self()->save();
    KConfigGroup shortcuts(KSharedConfig::openConfig(QStringLiteral("dolphinplusrc")), QStringLiteral("ImageViewer Shortcuts"));
    m_actions->writeSettings(&shortcuts);
    shortcuts.sync();
    for (auto *window : QApplication::topLevelWidgets()) {
        if (auto *viewer = qobject_cast<DolphinImageViewer *>(window)) {
            viewer->readSettings();
        }
    }
}

void ViewerSettingsPage::restoreDefaults()
{
    GeneralSettings::self()->useDefaults(true);
    m_fullscreen->setChecked(GeneralSettings::imageViewerOpenFullscreen());
    m_keepZoomAndPosition->setChecked(GeneralSettings::imageViewerKeepZoomAndPosition());
    m_fitPolicy->setCurrentIndex(GeneralSettings::imageViewerEnlargeSmallerImages() ? 1 : 0);
    GeneralSettings::self()->useDefaults(false);
    m_shortcuts->allDefault();
    Q_EMIT changed();
}
#pragma once

#include "settings/settingspagebase.h"

class KActionCollection;
class KShortcutsEditor;
class QCheckBox;
class QComboBox;

class ViewerSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit ViewerSettingsPage(QWidget *parent = nullptr);
    ~ViewerSettingsPage() override;
    void applySettings() override;
    void restoreDefaults() override;

private:
    QCheckBox *m_fullscreen;
    QComboBox *m_fitPolicy;
    KActionCollection *m_actions;
    KShortcutsEditor *m_shortcuts;
};
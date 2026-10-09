/*
 * SPDX-FileCopyrightText: 2006-2010 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2006 Gregor Kališnik <gregor@podnapisi.net>
 * SPDX-FileCopyrightText: 2012 Stuart Citrin <ctrn3e8@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILTERBAR_H
#define FILTERBAR_H

#include "animatedheightwidget.h"
#include "kitemviews/private/kfileitemmodelfilter.h"

class QLineEdit;
class QToolButton;
class QComboBox;
class QAction;

/**
 * @brief Provides an input field for filtering the currently shown items.
 *
 * @author Gregor Kališnik <gregor@podnapisi.net>
 */
class FilterBar : public AnimatedHeightWidget
{
    Q_OBJECT

public:
    enum class Mode {
        Filter,
        Find
    };
    explicit FilterBar(QWidget *parent = nullptr, Mode mode = Mode::Filter);
    ~FilterBar() override;
    QWidget *inputWidget() const;
    void setFindText(const QString &text, bool found);

    /** Called by view container to hide this **/
    void closeFilterBar();

    /**
     * Selects the whole text of the filter bar.
     */
    void selectAll();

    KFileItemModelFilter::FilterMode filterMode() const;
    bool isCaseSensitive() const;

public Q_SLOTS:
    /** Clears the input field. */
    void clear();
    /** Clears the input field if the "lock button" is disabled. */
    void clearIfUnlocked();
    /** The input field is cleared also if the "lock button" is released. */
    void slotToggleLockButton(bool checked);

Q_SIGNALS:
    void findTextEdited(const QString &text);
    void nextMatchRequested();
    /**
     * Signal that reports the name filter has been
     * changed to \a nameFilter.
     */
    void filterChanged(const QString &nameFilter);

    /**
     * Emitted when the case sensitive mode has been changed
     */
    void caseSensitiveChanged(bool caseSensitive);

    /**
     * Emitted when the filter mode has been changed
     */
    void filterModeChanged(KFileItemModelFilter::FilterMode mode);

    /**
     * Emitted as soon as the filterbar should get closed.
     */
    void closeRequest();

    /*
     * Emitted as soon as the focus should be returned back to the view.
     */
    void focusViewRequest();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    /** @see AnimatedHeightWidget::preferredHeight() */
    int preferredHeight() const override;

private:
    Mode m_mode;
    QLineEdit *m_filterInput;
    QToolButton *m_lockButton;
    QToolButton *m_caseSensitiveButton;
    QComboBox *m_filterModeComboBox;
    QAction *m_invalidPatternAction;

    /** Enable or disable the alterative view for when a pattern is invalid */
    void updateInvalidPatternView();
};

#endif

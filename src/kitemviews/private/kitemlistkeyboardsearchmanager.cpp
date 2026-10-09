/*
 * SPDX-FileCopyrightText: 2011 Tirtha Chatterjee <tirtha.p.chatterjee@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemlistkeyboardsearchmanager.h"

#include <QTextBoundaryFinder>

KItemListKeyboardSearchManager::KItemListKeyboardSearchManager(QObject *parent)
    : QObject(parent)
    , m_timeout(1000)
{
    m_keyboardInputTime.invalidate();
}

KItemListKeyboardSearchManager::~KItemListKeyboardSearchManager() = default;

bool KItemListKeyboardSearchManager::shouldClearSearchIfInputTimeReached()
{
    const bool keyboardTimeWasValid = m_keyboardInputTime.isValid();
    const qint64 keyboardInputTimeElapsed = m_keyboardInputTime.restart();
    return (keyboardInputTimeElapsed > m_timeout) || !keyboardTimeWasValid;
}

bool KItemListKeyboardSearchManager::isSearchAsYouTypeActive() const
{
    return !m_searchedString.isEmpty() && (m_persistentSearch || !m_keyboardInputTime.hasExpired(m_timeout));
}

void KItemListKeyboardSearchManager::addKeys(const QString &keys)
{
    if (m_persistentSearch) {
        if (keys.isEmpty() || (m_searchedString.isEmpty() && keys == QLatin1Char(' '))) {
            return;
        }
        setSearchText(m_searchedString + keys);
        return;
    }

    if (shouldClearSearchIfInputTimeReached()) {
        m_searchedString.clear();
        m_lastSuccessfulSearch.clear();
    }

    const bool newSearch = m_searchedString.isEmpty();

    // Do not start a new search if the user pressed Space. Only add
    // it to the search string if a search is in progress already.
    if (newSearch && keys == QLatin1Char(' ')) {
        return;
    }

    if (!keys.isEmpty()) {
        m_searchedString.append(keys);

        const QChar firstChar = m_searchedString.at(0);
        const bool allSameChars = m_searchedString.length() > 1 &&
                                 m_searchedString.count(firstChar) == m_searchedString.length();

        // Overall strategy:
        // 1. First attempt full string matching for exact file names
        // 2. If full match fails and user is typing repeating characters,
        //    fall back to rapid navigation using either:
        //    - Last successful search string (for extended searches like "444" -> "4444")
        //    - First character only (original rapid navigation behavior)
        bool found = false;
        Q_EMIT changeCurrentItem(m_searchedString, newSearch, &found);

        if (found) {
            m_lastSuccessfulSearch = m_searchedString;
        } else if (allSameChars && !newSearch) {
            QString rapidSearchString;

            if (!m_lastSuccessfulSearch.isEmpty() && m_searchedString.startsWith(m_lastSuccessfulSearch)) {
                rapidSearchString = m_lastSuccessfulSearch;
            } else {
                rapidSearchString = QString(firstChar);
            }

            Q_EMIT changeCurrentItem(rapidSearchString, true, &found);
        }
    }
    m_keyboardInputTime.start();
}

void KItemListKeyboardSearchManager::setSearchText(const QString &text)
{
    if (!m_persistentSearch) {
        return;
    }
    if (text.isEmpty()) {
        cancelSearch();
        return;
    }
    m_searchedString = text;
    bool found = false;
    Q_EMIT changeCurrentItem(m_searchedString, false, &found);
    Q_EMIT searchTextChanged(m_searchedString);
}

void KItemListKeyboardSearchManager::setTimeout(qint64 milliseconds)
{
    m_timeout = milliseconds;
}

qint64 KItemListKeyboardSearchManager::timeout() const
{
    return m_timeout;
}

void KItemListKeyboardSearchManager::cancelSearch()
{
    const bool hadSearch = !m_searchedString.isEmpty();
    m_searchedString.clear();
    m_lastSuccessfulSearch.clear();
    if (hadSearch) {
        Q_EMIT searchTextChanged(QString());
    }
}

void KItemListKeyboardSearchManager::setPersistentSearch(bool enabled)
{
    if (m_persistentSearch != enabled) {
        cancelSearch();
        m_persistentSearch = enabled;
    }
}

bool KItemListKeyboardSearchManager::persistentSearch() const
{
    return m_persistentSearch;
}

void KItemListKeyboardSearchManager::nextMatch()
{
    if (isSearchAsYouTypeActive()) {
        bool found = false;
        Q_EMIT changeCurrentItem(m_searchedString, true, &found);
    }
}

void KItemListKeyboardSearchManager::backspace()
{
    if (!isSearchAsYouTypeActive()) {
        return;
    }
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, m_searchedString);
    boundary.toEnd();
    const int end = qMax(0, boundary.toPreviousBoundary());
    if (end == 0) {
        cancelSearch();
        return;
    }
    m_searchedString.truncate(end);
    bool found = false;
    Q_EMIT changeCurrentItem(m_searchedString, false, &found);
    Q_EMIT searchTextChanged(m_searchedString);
}

void KItemListKeyboardSearchManager::slotCurrentChanged(int current, int previous)
{
    Q_UNUSED(previous)

    if (current < 0) {
        // The current item has been removed. We should cancel the search.
        cancelSearch();
    }
}

void KItemListKeyboardSearchManager::slotSelectionChanged(const KItemSet &current, const KItemSet &previous)
{
    if (!previous.isEmpty() && current.isEmpty() && previous.count() > 0 && current.count() == 0) {
        // The selection has been emptied. We should cancel the search.
        cancelSearch();
    }
}

#include "moc_kitemlistkeyboardsearchmanager.cpp"

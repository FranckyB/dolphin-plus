/*
 * SPDX-FileCopyrightText: 2019 David Hallas <david@davidhallas.dk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinbookmarkhandler.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "global.h"
#include <KActionCollection>
#include <KBookmarkMenu>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

DolphinBookmarkHandler::DolphinBookmarkHandler(DolphinMainWindow *mainWindow, KActionCollection *collection, QMenu *menu, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
    const QString bookmarksDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(bookmarksDirectory)) {
        qWarning() << "Could not create bookmarks directory:" << bookmarksDirectory;
    }
    const QString bookmarksFile = bookmarksDirectory + QStringLiteral("/bookmarks.xml");
    m_bookmarkManager = std::make_unique<KBookmarkManager>(bookmarksFile);
    m_bookmarkMenu.reset(new KBookmarkMenu(m_bookmarkManager.get(), this, menu));

    collection->addAction(QStringLiteral("add_bookmark"), m_bookmarkMenu->addBookmarkAction());
    collection->addAction(QStringLiteral("edit_bookmarks"), m_bookmarkMenu->editBookmarksAction());
    collection->addAction(QStringLiteral("add_bookmarks_list"), m_bookmarkMenu->bookmarkTabsAsFolderAction());
}

DolphinBookmarkHandler::~DolphinBookmarkHandler() = default;

QString DolphinBookmarkHandler::currentTitle() const
{
    return title(m_mainWindow->activeViewContainer());
}

QUrl DolphinBookmarkHandler::currentUrl() const
{
    return url(m_mainWindow->activeViewContainer());
}

QString DolphinBookmarkHandler::currentIcon() const
{
    return icon(m_mainWindow->activeViewContainer());
}

bool DolphinBookmarkHandler::supportsTabs() const
{
    return true;
}

QList<KBookmarkOwner::FutureBookmark> DolphinBookmarkHandler::currentBookmarkList() const
{
    const auto viewContainers = m_mainWindow->viewContainers();
    QList<FutureBookmark> bookmarks;
    bookmarks.reserve(viewContainers.size());
    for (const auto viewContainer : viewContainers) {
        bookmarks << FutureBookmark(title(viewContainer), url(viewContainer), icon(viewContainer));
    }
    return bookmarks;
}

bool DolphinBookmarkHandler::enableOption(KBookmarkOwner::BookmarkOption option) const
{
    switch (option) {
    case BookmarkOption::ShowAddBookmark:
        return true;
    case BookmarkOption::ShowEditBookmark:
        return true;
    }
    return false;
}

void DolphinBookmarkHandler::openBookmark(const KBookmark &bookmark, Qt::MouseButtons, Qt::KeyboardModifiers)
{
    m_mainWindow->changeUrl(bookmark.url());
}

void DolphinBookmarkHandler::openFolderinTabs(const KBookmarkGroup &bookmarkGroup)
{
    m_mainWindow->openDirectories(bookmarkGroup.groupUrlList(), false);
}

void DolphinBookmarkHandler::openInNewTab(const KBookmark &bookmark)
{
    m_mainWindow->openNewTab(bookmark.url());
}

void DolphinBookmarkHandler::openInNewWindow(const KBookmark &bookmark)
{
    Dolphin::openNewWindow({bookmark.url()}, m_mainWindow);
}

QString DolphinBookmarkHandler::title(DolphinViewContainer *viewContainer)
{
    return viewContainer->caption();
}

QUrl DolphinBookmarkHandler::url(DolphinViewContainer *viewContainer)
{
    return viewContainer->url();
}

QString DolphinBookmarkHandler::icon(DolphinViewContainer *viewContainer)
{
    return KIO::iconNameForUrl(viewContainer->url());
}

#include "moc_dolphinbookmarkhandler.cpp"

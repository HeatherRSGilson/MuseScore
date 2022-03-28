/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "navigableappmenumodel.h"

#include <QApplication>
#include <QWindow>
#include <QKeyEvent>

#include <private/qkeymapper_p.h>

#include "log.h"

using namespace mu::appshell;
using namespace mu::ui;
using namespace mu::uicomponents;

QSet<int> possibleKeys(QKeyEvent* keyEvent)
{
    QKeyEvent* correctedKeyEvent = keyEvent;
    //! NOTE: correct work only with alt modifier
    correctedKeyEvent->setModifiers(Qt::AltModifier);

    QList<int> keys = QKeyMapper::possibleKeys(correctedKeyEvent);

    return QSet<int>(keys.cbegin(), keys.cend());
}

QSet<int> possibleKeys(const QChar& keySymbol)
{
    QKeyEvent fakeKey(QKeyEvent::KeyRelease, Qt::Key_unknown, Qt::AltModifier, keySymbol);
    QList<int> keys = QKeyMapper::possibleKeys(&fakeKey);

    return QSet<int>(keys.cbegin(), keys.cend());
}

NavigableAppMenuModel::NavigableAppMenuModel(QObject* parent)
    : AppMenuModel(parent)
{
}

void NavigableAppMenuModel::load()
{
    AppMenuModel::load();

    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state){
        if (state != Qt::ApplicationActive) {
            resetNavigation();
        }
    });

    qApp->installEventFilter(this);
}

QWindow* NavigableAppMenuModel::appWindow() const
{
    return m_appWindow;
}

void NavigableAppMenuModel::setAppWindow(QWindow* appWindow)
{
    m_appWindow = appWindow;
}

void NavigableAppMenuModel::setHighlightedMenuId(QString highlightedMenuId)
{
    if (m_highlightedMenuId == highlightedMenuId) {
        return;
    }

    m_highlightedMenuId = highlightedMenuId;
    emit highlightedMenuIdChanged(m_highlightedMenuId);
}

void NavigableAppMenuModel::setOpenedMenuId(QString openedMenuId)
{
    if (m_openedMenuId == openedMenuId) {
        return;
    }

    m_openedMenuId = openedMenuId;
    emit openedMenuIdChanged(m_openedMenuId);
}

bool NavigableAppMenuModel::eventFilter(QObject* watched, QEvent* event)
{
    bool isMenuOpened = !m_openedMenuId.isEmpty();
    if (isMenuOpened && watched && watched->isWindowType()) {
        return processEventForOpenedMenu(event);
    }

    if (watched == appWindow()) {
        bool ok = processEventForAppMenu(event);
        if (ok) {
            return ok;
        }
    }

    return AbstractMenuModel::eventFilter(watched, event);
}

bool NavigableAppMenuModel::processEventForOpenedMenu(QEvent* event)
{
    if (event->type() != QEvent::ShortcutOverride) {
        return false;
    }

    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);

    bool isNavigationWithSymbol = !keyEvent->modifiers()
                                  && keyEvent->text().length() == 1;

    if (!isNavigationWithSymbol) {
        return false;
    }

    QSet<int> activatePossibleKeys = possibleKeys(keyEvent);
    if (hasSubItem(m_openedMenuId, activatePossibleKeys)) {
        navigateToSubItem(m_openedMenuId, activatePossibleKeys);
        event->accept();
        return true;
    }

    return false;
}

bool NavigableAppMenuModel::processEventForAppMenu(QEvent* event)
{
    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
    if (!keyEvent) {
        return false;
    }

    Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
    int key = keyEvent->key();
    bool isSingleSymbol = keyEvent->text().length() == 1;

    bool isNavigationStarted = this->isNavigationStarted();
    bool isNavigationWithSymbol = !modifiers
                                  && isSingleSymbol
                                  && isNavigationStarted;
    bool isNavigationWithAlt = (modifiers & Qt::AltModifier)
                               && !(modifiers & Qt::ShiftModifier)
                               && isSingleSymbol;

    bool isAltKey = key == Qt::Key_Alt
                    && key != Qt::Key_Shift
                    && !(modifiers & Qt::ShiftModifier);

    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        if (isNavigationStarted && isNavigateKey(key)) {
            event->accept();
            return true;
        } else if (isNavigationWithSymbol || isNavigationWithAlt) {
            QSet<int> activatePossibleKeys = possibleKeys(keyEvent);
            if (hasItem(activatePossibleKeys)) {
                event->accept();
                return true;
            }
        }

        break;
    }
    case QEvent::KeyPress: {
        if (isAltKey) {
            m_needActivateHighlight = true;
            break;
        }

        if (isNavigationStarted && isNavigateKey(key)) {
            navigate(key);
            m_needActivateHighlight = false;

            event->accept();
            return true;
        } else if (isNavigationWithSymbol || isNavigationWithAlt) {
            QSet<int> activatePossibleKeys = possibleKeys(keyEvent);
            if (hasItem(activatePossibleKeys)) {
                navigate(activatePossibleKeys);
                m_needActivateHighlight = true;

                event->accept();
                return true;
            }
        }

        break;
    }
    case QEvent::KeyRelease: {
        if (!isAltKey) {
            break;
        }

        if (isNavigationStarted) {
            resetNavigation();
            restoreMUNavigationSystemState();
        } else {
            if (m_needActivateHighlight) {
                saveMUNavigationSystemState();
                navigateToFirstMenu();
            } else {
                m_needActivateHighlight = true;
            }
        }

        break;
    }
    case QEvent::MouseButtonPress: {
        resetNavigation();
        break;
    }
    default:
        break;
    }

    return false;
}

bool NavigableAppMenuModel::isNavigationStarted() const
{
    return !m_highlightedMenuId.isEmpty();
}

bool NavigableAppMenuModel::isNavigateKey(int key) const
{
    static QList<Qt::Key> keys {
        Qt::Key_Left,
        Qt::Key_Right,
        Qt::Key_Down,
        Qt::Key_Space,
        Qt::Key_Escape,
        Qt::Key_Return
    };

    return keys.contains(static_cast<Qt::Key>(key));
}

void NavigableAppMenuModel::navigate(int scanCode)
{
    Qt::Key _key = static_cast<Qt::Key>(scanCode);
    switch (_key) {
    case Qt::Key_Left: {
        int newIndex = itemIndex(m_highlightedMenuId) - 1;
        if (newIndex < 0) {
            newIndex = rowCount() - 1;
        }

        setHighlightedMenuId(item(newIndex).id());
        break;
    }
    case Qt::Key_Right: {
        int newIndex = itemIndex(m_highlightedMenuId) + 1;
        if (newIndex > rowCount() - 1) {
            newIndex = 0;
        }

        setHighlightedMenuId(item(newIndex).id());
        break;
    }
    case Qt::Key_Down:
    case Qt::Key_Space:
    case Qt::Key_Return:
        activateHighlightedMenu();
        break;
    case Qt::Key_Escape:
        resetNavigation();
        restoreMUNavigationSystemState();
        break;
    default:
        break;
    }
}

bool NavigableAppMenuModel::hasItem(const QSet<int>& activatePossibleKeys)
{
    return !menuItemId(items(), activatePossibleKeys).isEmpty();
}

bool NavigableAppMenuModel::hasSubItem(const QString& menuId, const QSet<int>& activatePossibleKeys)
{
    MenuItem& menuItem = findMenu(menuId);
    if (menuItem.subitems().empty()) {
        return false;
    }

    return !menuItemId(menuItem.subitems(), activatePossibleKeys).isEmpty();
}

void NavigableAppMenuModel::navigate(const QSet<int>& activatePossibleKeys)
{
    saveMUNavigationSystemState();

    setHighlightedMenuId(menuItemId(items(), activatePossibleKeys));
    activateHighlightedMenu();
}

void NavigableAppMenuModel::navigateToSubItem(const QString& menuId, const QSet<int>& activatePossibleKeys)
{
    MenuItem& menuItem = findMenu(menuId);
    MenuItem& subItem = findItem(this->menuItemId(menuItem.subitems(), activatePossibleKeys));
    if (!subItem.isValid()) {
        return;
    }

    INavigationSection* section = navigationController()->activeSection();
    INavigationPanel* panel = navigationController()->activePanel();

    if (!section || !panel) {
        return;
    }

    navigationController()->requestActivateByName(section->name().toStdString(),
                                                  panel->name().toStdString(),
                                                  subItem.id().toStdString());

    INavigationControl* control = navigationController()->activeControl();
    if (!control) {
        return;
    }

    bool isMenu = !subItem.subitems().isEmpty();
    if (isMenu) {
        control->trigger();
    }
}

void NavigableAppMenuModel::resetNavigation()
{
    setHighlightedMenuId("");
}

void NavigableAppMenuModel::navigateToFirstMenu()
{
    setHighlightedMenuId(item(0).id());
}

void NavigableAppMenuModel::saveMUNavigationSystemState()
{
    if (!navigationController()->isHighlight()) {
        return;
    }

    ui::INavigationControl* activeControl = navigationController()->activeControl();
    if (activeControl) {
        m_lastActiveNavigationControl = activeControl;
        activeControl->setActive(false);
    }
}

void NavigableAppMenuModel::restoreMUNavigationSystemState()
{
    if (m_lastActiveNavigationControl) {
        m_lastActiveNavigationControl->requestActive();
    }
}

void NavigableAppMenuModel::activateHighlightedMenu()
{
    emit openMenu(m_highlightedMenuId);
    actionsDispatcher()->dispatch("nav-first-control");
}

QString NavigableAppMenuModel::highlightedMenuId() const
{
    return m_highlightedMenuId;
}

QString NavigableAppMenuModel::openedMenuId() const
{
    return m_openedMenuId;
}

QString NavigableAppMenuModel::menuItemId(const MenuItemList& items, const QSet<int>& activatePossibleKeys)
{
    for (const MenuItem* item : items) {
        QString title = item->action().title;

        int activateKeyIndex = title.indexOf('&');
        if (activateKeyIndex == -1) {
            continue;
        }

        auto menuActivatePossibleKeys = possibleKeys(title[activateKeyIndex + 1].toUpper());
        if (menuActivatePossibleKeys.intersects(activatePossibleKeys)) {
            return item->id();
        }
    }

    return QString();
}

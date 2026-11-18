#include "NativeMenu.h"

#include <QActionGroup>
#include <QCursor>
#include <QMenu>
#include <QPointer>

namespace {
QPointer<QMenu> g_menu;

QMenu *createMenu()
{
    if (auto *previous = g_menu.data()) {
        previous->close();
        previous->deleteLater();
    }
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setStyleSheet(
        "QMenu {background:#1f232b;color:#d7dae0;border:1px solid #3a4150;padding:4px;}"
        "QMenu::item {padding:6px 28px 6px 20px;}"
        "QMenu::item:selected {background:#3fa7f5;color:#ffffff;}");
    g_menu = menu;
    return menu;
}

void popupMenu(QMenu *menu, const QPoint &position)
{
    menu->popup(position);
    menu->setFocus();
}
}

void NativeMenu::showLanguages(const QVariantList &languages, int currentIndex)
{
    auto *menu = createMenu();
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (int i = 0; i < languages.size(); ++i) {
        const auto map = languages.at(i).toMap();
        const auto name = map.value(QStringLiteral("name")).toString();
        const auto code = map.value(QStringLiteral("code")).toString();
        auto *action = menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(i == currentIndex);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, this,
                         [this, code] { emit languageSelected(code); });
    }
    popupMenu(menu, QCursor::pos() + QPoint(0, 8));
}

void NativeMenu::showRecentIps(const QStringList &ips, const QPointF &anchor)
{
    if (ips.isEmpty())
        return;

    auto *menu = createMenu();
    for (const auto &ip : ips) {
        auto *action = menu->addAction(ip);
        connect(action, &QAction::triggered, this, [this, ip] { emit recentIpSelected(ip); });
    }
    popupMenu(menu, anchor.toPoint() - QPoint(menu->sizeHint().width(), 0));
}

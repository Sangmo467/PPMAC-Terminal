#include "NativeMenu.h"

#include <QActionGroup>
#include <QCursor>
#include <QMenu>
#include <QPointer>

namespace {
QPointer<QMenu> g_menu;
}

void NativeMenu::showLanguages(const QVariantList &languages, int currentIndex)
{
    if (g_menu) {
        g_menu->close();
        g_menu->deleteLater();
        g_menu = nullptr;
    }
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setStyleSheet(
        "QMenu {background:#1f232b;color:#d7dae0;border:1px solid #3a4150;padding:4px;}"
        "QMenu::item {padding:6px 28px 6px 20px;}"
        "QMenu::item:selected {background:#3fa7f5;color:#ffffff;}");
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
    g_menu = menu;
    QObject::connect(menu, &QObject::destroyed, [menu] {
        if (g_menu == menu) g_menu = nullptr;
    });
    menu->popup(QCursor::pos() + QPoint(0, 8));
    menu->setFocus();
}

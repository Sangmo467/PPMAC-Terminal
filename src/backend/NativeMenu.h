#pragma once
#include <QObject>
#include <QVariantList>
#include <qqmlregistration.h>

// 原生弹出菜单：用于语言选择，避免 QML 弹层被终端原生子窗口遮挡。
class NativeMenu : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit NativeMenu(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void showLanguages(const QVariantList &languages, int currentIndex);

signals:
    void languageSelected(const QString &code);
};

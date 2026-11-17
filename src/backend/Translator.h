#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <qqmlregistration.h>

// 界面与后台消息翻译（QML 与 C++ 共用）。
// 优先读取部署的 translations/<code>.json（macOS 在 Contents/Resources 下），
// 文件不存在时回退到内置资源 :/translations/<code>.json；
// 键缺失时回退 en，再退化为键名本身。
namespace Tr {

QString tr(const QString &language, const QString &key);
QVariantList languages();   // [{ code, name }...]，来自 languages.json

} // namespace Tr

// QML 单例：import PmacTerminal.Backend 后用 Translator.tr(...) / Translator.languages()
class Translator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Translator(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QString tr(const QString &language, const QString &key) const
    {
        return Tr::tr(language, key);
    }
    Q_INVOKABLE QVariantList languages() const { return Tr::languages(); }
};

#endif // TRANSLATOR_H

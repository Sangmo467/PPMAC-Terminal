#include "Translator.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

namespace {

QHash<QString, QJsonObject> g_dicts;
QMutex g_mutex;

// 读 JSON 对象：优先 exe 旁 translations/ 目录，回退内置资源 :/translations/
QJsonObject loadJson(const QString &name)
{
    QFile file(QCoreApplication::applicationDirPath()
               + QStringLiteral("/translations/") + name);
    if (!file.exists())
        file.setFileName(QStringLiteral(":/translations/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

// 加载并缓存某语言的词典；缺失/损坏时缓存空对象，避免重复尝试
QJsonObject dict(const QString &language)
{
    auto it = g_dicts.constFind(language);
    if (it != g_dicts.constEnd())
        return it.value();
    const QJsonObject obj = loadJson(language + QStringLiteral(".json"));
    g_dicts.insert(language, obj);
    return obj;
}

} // namespace

QString Tr::tr(const QString &language, const QString &key)
{
    QMutexLocker locker(&g_mutex);
    QString s = dict(language).value(key).toString();
    if (s.isEmpty() && language != QLatin1String("en"))
        s = dict(QStringLiteral("en")).value(key).toString();
    return s.isEmpty() ? key : s;
}

QVariantList Tr::languages()
{
    QMutexLocker locker(&g_mutex);
    const QJsonArray arr = loadJson(QStringLiteral("languages.json"))
                                   .value(QStringLiteral("languages")).toArray();
    QVariantList out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        out.append(QVariantMap{
            {QStringLiteral("code"), o.value(QStringLiteral("code")).toString()},
            {QStringLiteral("name"), o.value(QStringLiteral("name")).toString()},
        });
    }
    return out;
}

#pragma once
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <memory>

class Backend;

class TerminalPage : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Backend* backend READ backend WRITE setBackend NOTIFY backendChanged)
public:
    explicit TerminalPage(QQuickItem *parent = nullptr);
    ~TerminalPage() override;
    Backend *backend() const;
    void setBackend(Backend *backend);
    Q_INVOKABLE void newSession();
    Q_INVOKABLE int smokeTest();
signals:
    void backendChanged();
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void syncHost();
    void retranslate();
};
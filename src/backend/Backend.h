#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <qqmlregistration.h>

class BackendWorker;
class QThread;

// QML 门面：只保留 PMAC 连接、固件版本与终端收发/符号导入。
// 阻塞操作（连接、命令、读取符号文件）在 QThread 工作线程执行，
// 结果通过信号异步返回。
class Backend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 语言代码（对应 translations/<code>.json），写入后后台消息语言即时切换
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    // Terminal operations are queued on the connection worker.
    void sendTerminalCommand(const QString &requestId, const QString &command);
    void refreshTerminalSymbols();
    QStringList terminalSymbols() const { return m_terminalSymbols; }
    QStringList terminalHardware() const { return m_terminalHardware; }
    QStringList terminalHardwareRoots() const { return m_terminalHardwareRoots; }
    QStringList terminalAliases() const { return m_terminalAliases; }
    bool terminalHardwareKnown() const { return m_terminalHardwareKnown; }
    QString terminalSymbolsError() const { return m_terminalSymbolsError; }

    QString connectionState() const { return m_connectionState; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    bool busy() const { return m_busy; }
    QString language() const { return m_language; }

    Q_INVOKABLE void connectTo(const QString &user, const QString &password, const QString &ip);
    Q_INVOKABLE void disconnectFrom();
    void setLanguage(const QString &lang);

signals:
    void terminalResponse(const QString &requestId, const QString &text, double elapsedMs, const QString &error);
    void terminalSymbolsChanged();
    void connectionStateChanged();
    void firmwareVersionChanged();
    void busyChanged();
    void languageChanged();
    void operationFailed(const QString &message);

private:
    void setConnectionState(const QString &state);
    void setFirmwareVersion(const QString &version);
    void setBusy(bool busy);

    QStringList m_terminalSymbols;
    QStringList m_terminalHardware;
    QStringList m_terminalHardwareRoots;
    QStringList m_terminalAliases;
    bool m_terminalHardwareKnown = false;
    QString m_terminalSymbolsError;
    quint64 m_terminalGeneration = 0;
    bool m_terminalSymbolsLoading = false;
    QString m_connectionState = QStringLiteral("disconnected");
    QString m_firmwareVersion;
    QString m_language;
    bool m_busy = false;

    QThread *m_thread;
    BackendWorker *m_worker;
};

#endif // BACKEND_H

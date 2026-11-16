#include "Backend.h"

#include "PmacConnection.h"
#include "Translator.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <memory>
#include <stdexcept>

// 工作线程对象：连接、终端命令与符号文件读取都在这里执行。
class BackendWorker : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void terminalCommand(quint64 generation, const QString &id, const QString &command)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        QString text, error;
        try {
            if (!m_client || !m_client->isConnected())
                throw std::runtime_error("主连接未连接");
            text = QString::fromUtf8(m_client->getResponse(command.toUtf8().toStdString()));
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        emit terminalCommandReady(generation, id, text, elapsed.nsecsElapsed() / 1000000.0, error);
        if (m_client && !m_client->isConnected()) handleConnectionLost();
    }

    void terminalSymbols(quint64 generation)
    {
        QStringList files;
        QString error;
        try {
            if (!m_client || !m_client->isConnected())
                throw std::runtime_error("主连接未连接");
            const QStringList names = {"pp_swtbl0.sym", "pp_swtbl1.sym", "pp_swtbl2.sym",
                                       "pp_swtbl3.sym", "pp_global.sym", "pp_prog.sym", "pp_local.sym"};
            for (const auto &name : names) {
                const auto data = m_client->client().shell(
                    ("cat -- /var/ftp/usrflash/Database/" + name).toStdString(), false);
                files.append(QString::fromUtf8(reinterpret_cast<const char *>(data.data()), data.size()));
            }
        } catch (const std::exception &e) {
            files.clear();
            error = QString::fromUtf8(e.what());
        }
        emit terminalSymbolsReady(generation, files, error);
    }

    // 界面语言（translations/<code>.json 中的语言代码），由 GUI 线程排队设置
    void setLanguage(const QString &lang) { m_language = lang; }

    void connectTo(const QString &user, const QString &password, const QString &ip)
    {
        if (m_client) {
            try {
                m_client->client().close();
            } catch (const std::exception &e) {
                if (m_heartbeat)
                    m_heartbeat->start();
                emit operationFailed(QString::fromUtf8(e.what()));
                return;
            }
        }
        m_client.reset();
        try {
            m_client = std::make_shared<PmacConnection>(
                    ip.toStdString(), user.toStdString(), password.toStdString());

            // 每 5 秒检查 SDK 状态；SSH keepalive 由 SDK 负责
            if (!m_heartbeat) {
                m_heartbeat = new QTimer(this);
                m_heartbeat->setInterval(5000);
                connect(m_heartbeat, &QTimer::timeout, this, [this]() {
                    if (m_client && !m_client->isConnected())
                        handleConnectionLost();
                });
            }
            m_heartbeat->start();

            // 连接成功后查询固件版本（gpascii "vers"）
            QString firmware;
            try {
                firmware = QString::fromUtf8(m_client->getResponse("vers")).trimmed();
            } catch (...) {
                firmware.clear();
            }
            emit firmwareReady(firmware);
            emit connectionEstablished();
        } catch (const std::exception &e) {
            emit connectionFailed(Tr::tr(m_language, QStringLiteral("backend.connectionFailed"))
                                          .arg(QString::fromUtf8(e.what())));
        }
    }

    void disconnectFrom()
    {
        if (m_heartbeat)
            m_heartbeat->stop();
        if (m_client) {
            try {
                m_client->client().close();
            } catch (const std::exception &e) {
                if (m_heartbeat)
                    m_heartbeat->start();
                emit operationFailed(QString::fromUtf8(e.what()));
                return;
            }
        }
        m_client.reset();
        emit disconnected();
    }

    // 连接丢失：清理并通知（在 worker 线程调用）。
    void handleConnectionLost()
    {
        if (m_heartbeat)
            m_heartbeat->stop();
        if (m_client) {
            try {
                m_client->client().close();
            } catch (const std::exception &e) {
                if (m_heartbeat)
                    m_heartbeat->start();
                emit operationFailed(QString::fromUtf8(e.what()));
                return;
            }
        }
        m_client.reset();
        emit disconnected();
        emit backgroundError(Tr::tr(m_language, QStringLiteral("backend.connectionLost")));
    }

signals:
    void terminalCommandReady(quint64 generation, const QString &id, const QString &text,
                              double elapsedMs, const QString &error);
    void terminalSymbolsReady(quint64 generation, const QStringList &files, const QString &error);
    void connectionEstablished();
    void connectionFailed(const QString &reason);
    void disconnected();
    void firmwareReady(const QString &version);
    void operationFailed(const QString &message);
    // 后台事件失败（不涉及 busy 状态）
    void backgroundError(const QString &message);

private:
    std::shared_ptr<PmacConnection> m_client;
    QTimer *m_heartbeat = nullptr;
    QString m_language = QStringLiteral("zh");
};

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
    , m_worker(new BackendWorker)
{
    connect(m_worker, &BackendWorker::terminalCommandReady, this,
        [this](quint64 generation, const QString &id, const QString &text, double elapsed, const QString &error) {
            if (generation == m_terminalGeneration) emit terminalResponse(id, text, elapsed, error);
        });
    connect(m_worker, &BackendWorker::terminalSymbolsReady, this,
        [this](quint64 generation, const QStringList &files, const QString &error) {
            if (generation != m_terminalGeneration) return;
            m_terminalSymbolsLoading = false;
            m_terminalSymbols = files;
            m_terminalSymbolsError = error;
            emit terminalSymbolsChanged();
        });
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &BackendWorker::connectionEstablished, this, [this]() {
        setConnectionState(QStringLiteral("connected"));
        setBusy(false);
        refreshTerminalSymbols();
    });
    connect(m_worker, &BackendWorker::connectionFailed, this, [this](const QString &reason) {
        setConnectionState(QStringLiteral("failed"));
        setBusy(false);
        emit operationFailed(reason);
    });
    connect(m_worker, &BackendWorker::disconnected, this, [this]() {
        setConnectionState(QStringLiteral("disconnected"));
        setFirmwareVersion(QString());
        setBusy(false);
    });
    connect(m_worker, &BackendWorker::firmwareReady, this, [this](const QString &version) {
        setFirmwareVersion(version);
    });
    connect(m_worker, &BackendWorker::operationFailed, this, [this](const QString &message) {
        setBusy(false);
        emit operationFailed(message);
    });
    // 后台操作失败（不弹 busy），其错误也不应清除其他操作的 busy 状态
    connect(m_worker, &BackendWorker::backgroundError, this, [this](const QString &message) {
        emit operationFailed(message);
    });

    m_thread->start();
}

Backend::~Backend()
{
    m_thread->quit();
    // 等待有超时限制的 SDK 调用退出，避免强制终止线程破坏共享 Client。
    m_thread->wait();
}

void Backend::setLanguage(const QString &lang)
{
    if (m_language == lang)
        return;
    m_language = lang;
    emit languageChanged();
    BackendWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, lang]() {
        w->setLanguage(lang);
    }, Qt::QueuedConnection);
}

void Backend::setConnectionState(const QString &state)
{
    if (m_connectionState == state)
        return;
    ++m_terminalGeneration;
    m_terminalSymbols.clear();
    m_terminalSymbolsError.clear();
    m_terminalSymbolsLoading = false;
    m_connectionState = state;
    emit terminalSymbolsChanged();
    emit connectionStateChanged();
}

void Backend::setFirmwareVersion(const QString &version)
{
    if (m_firmwareVersion == version)
        return;
    m_firmwareVersion = version;
    emit firmwareVersionChanged();
}

void Backend::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::connectTo(const QString &user, const QString &password, const QString &ip)
{
    setConnectionState(QStringLiteral("connecting"));
    setBusy(true);
    BackendWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, user, password, ip]() {
        w->connectTo(user, password, ip);
    }, Qt::QueuedConnection);
}

void Backend::disconnectFrom()
{
    setBusy(true);
    BackendWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w]() {
        w->disconnectFrom();
    }, Qt::QueuedConnection);
}

void Backend::sendTerminalCommand(const QString &requestId, const QString &command)
{
    if (m_connectionState != "connected" || m_busy) {
        emit terminalResponse(requestId, {}, 0,
                              Tr::tr(m_language, QStringLiteral("terminal.needConnected")));
        return;
    }
    const auto generation = m_terminalGeneration;
    auto w = m_worker;
    QMetaObject::invokeMethod(w, [w, generation, requestId, command] {
        w->terminalCommand(generation, requestId, command);
    }, Qt::QueuedConnection);
}

void Backend::refreshTerminalSymbols()
{
    if (m_connectionState != "connected" || m_terminalSymbolsLoading) return;
    m_terminalSymbolsLoading = true;
    const auto generation = m_terminalGeneration;
    auto w = m_worker;
    QMetaObject::invokeMethod(w, [w, generation] { w->terminalSymbols(generation); }, Qt::QueuedConnection);
}

#include "Backend.moc"

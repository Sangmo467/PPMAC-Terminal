#include "Backend.h"

#include "PmacConnection.h"
#include "Translator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace {
// 与上游一致：除宏从站外，$$$ 开头的命令都按复位处理。
bool isResetCommand(const QString &command)
{
    const auto text = command.trimmed();
    return text.startsWith(QLatin1String("$$$"))
        && !text.contains(QLatin1String("macroslave"), Qt::CaseInsensitive);
}
}

// 工作线程对象：连接、终端命令与符号文件读取都在这里执行。
class BackendWorker : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void terminalCommand(quint64 generation, const QString &id, const QString &command)
    {
        if (PmacConnection::interrupted()) return;
        QElapsedTimer elapsed;
        elapsed.start();
        QString text, stderrText, error;
        const bool reset = isResetCommand(command);
        try {
            if (!m_client || !m_client->isConnected())
                throw std::runtime_error("主连接未连接");
            const auto response = m_client->getResponseWithStderr(command.toUtf8().toStdString());
            text = QString::fromUtf8(response.output);
            stderrText = QString::fromUtf8(response.stderrText);
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (PmacConnection::interrupted()) return;
        emit terminalCommandReady(generation, id, text, stderrText,
                                  elapsed.nsecsElapsed() / 1000000.0, error);
        if (!reset) {
            if (m_client && !m_client->isConnected()) handleConnectionLost();
            return;
        }
        // 复位会重建符号数据库。SDK 可能只重开 GPASCII/维护面，也可能断开传输；
        // 两种情况下都要恢复连接再重新读取符号文件。
        QString recoveryError;
        if (restoreAfterReset(&recoveryError)) {
            terminalSymbols(generation);
        } else if (PmacConnection::interrupted()) {
            return;
        } else if (m_client && m_client->isConnected()) {
            emit backgroundError(Tr::tr(m_language, QStringLiteral("backend.resetRecoveryFailed"))
                                     .arg(recoveryError));
        } else {
            handleConnectionLost();
        }
    }

    // 复位后优先修复维护面（传输未断时）；若传输已被关闭，则用保存的凭据重建连接。
    bool restoreAfterReset(QString *error)
    {
        if (PmacConnection::interrupted()) return false;
        if (m_client && m_client->isConnected()) {
            try {
                m_client->repairAfterReset();
                return true;
            } catch (const std::exception &) {
                // 传输可能已被复位关闭，转入重建连接。
            }
        }
        if (PmacConnection::interrupted()) return false;
        if (m_client) {
            try { m_client->client().close(); } catch (const std::exception &) {}
            m_client.reset();
        }
        if (m_ip.isEmpty()) {
            if (error) *error = QStringLiteral("没有可用的连接参数");
            return false;
        }
        QElapsedTimer deadline;
        deadline.start();
        QString lastError;
        while (deadline.elapsed() < 60000 && !PmacConnection::interrupted()) {
            try {
                m_client = std::make_shared<PmacConnection>(
                        m_ip.toStdString(), m_user.toStdString(), m_password.toStdString(), m_port);
                return true;
            } catch (const std::exception &e) {
                lastError = QString::fromUtf8(e.what());
                m_client.reset();
            }
            // 重试间隔同样可中断，退出后不再等满一秒或发起下一次连接。
            for (int waited = 0; waited < 1000 && !PmacConnection::interrupted(); waited += 50)
                QThread::msleep(50);
        }
        if (error) *error = lastError;
        return false;
    }

    void terminalSymbols(quint64 generation)
    {
        if (PmacConnection::interrupted()) return;
        QStringList files;
        QStringList hardware, hardwareRoots, aliasPairs;
        bool hardwareKnown = false;
        QString error;
        try {
            if (!m_client || !m_client->isConnected())
                throw std::runtime_error("主连接未连接");
            const QStringList names = {"pp_swtbl0.sym", "pp_swtbl1.sym", "pp_swtbl2.sym",
                                       "pp_swtbl3.sym", "pp_global.sym", "pp_prog.sym", "pp_local.sym"};
            for (const auto &name : names) {
                const auto data = m_client->shell(
                    ("cat -- /var/ftp/usrflash/Database/" + name).toStdString(), false);
                files.append(QString::fromUtf8(reinterpret_cast<const char *>(data.data()), data.size()));
            }
            // 按控制器实时读数探测硬件：PartNum 为 0/nan/非法均视为不存在。
            auto present = [&](const QString &instance) {
                try {
                    const auto reply = QString::fromUtf8(
                        m_client->getResponse((instance + QStringLiteral(".PartNum")).toStdString()).c_str());
                    const int equals = reply.lastIndexOf(QLatin1Char('='));
                    bool ok = false;
                    const double value = reply.mid(equals + 1).trimmed().toDouble(&ok);
                    if (!ok) return std::make_pair(false, false);  // 命令不可用/无读数
                    return std::make_pair(true, !std::isnan(value) && value != 0.0);
                } catch (const PmacConnection::Interrupted &) {
                    throw;
                } catch (const std::exception &) {
                    return std::make_pair(false, false);
                }
            };
            bool anyChecked = false;
            const struct { const char *root; int count; } bases[] = {
                {"Gate1", 20}, {"Gate2", 16}, {"Gate3", 16}, {"GateIo", 16}};
            QStringList gates;
            for (const auto &base : bases) {
                bool checkedAny = false;
                for (int index = 0; index < base.count; ++index) {
                    const auto name = QStringLiteral("%1[%2]").arg(QLatin1String(base.root)).arg(index);
                    const auto result = present(name);
                    if (result.second) gates << name;
                    checkedAny = checkedAny || result.first;
                }
                if (checkedAny) hardwareRoots << QString::fromLatin1(base.root).toLower();
                anyChecked = anyChecked || checkedAny;
            }
            // 别名结构（CK3WAX[]/CK3WMD[] 等）与其等效结构在相同下标上探测。
            struct AliasDef { QString name; QString target; };
            QList<AliasDef> aliasDefs;
            for (const auto &line : files.value(0).split(QLatin1Char('\n'))) {
                const auto fields = line.split(QLatin1Char('\x01'));
                if (fields.size() < 5) continue;
                const auto name = fields[1].trimmed();
                const auto target = fields.last().trimmed();
                if (!name.endsWith(QLatin1String("[]")) || !target.endsWith(QLatin1String("[]")))
                    continue;
                if (target.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0) continue;
                const AliasDef def{name.left(name.size() - 2), target.left(target.size() - 2)};
                bool exists = false;
                for (const auto &known : aliasDefs)
                    if (known.name.compare(def.name, Qt::CaseInsensitive) == 0) { exists = true; break; }
                if (!exists) aliasDefs << def;
            }
            QStringList aliasInstances;
            for (const auto &def : aliasDefs) {
                bool checkedAny = false;
                for (const auto &instance : gates) {
                    const int bracket = instance.indexOf(QLatin1Char('['));
                    if (bracket <= 0
                        || instance.left(bracket).compare(def.target, Qt::CaseInsensitive) != 0)
                        continue;
                    const auto aliasName = def.name + instance.mid(bracket);
                    const auto result = present(aliasName);
                    if (result.second) aliasInstances << aliasName;
                    checkedAny = checkedAny || result.first;
                }
                if (checkedAny) {
                    hardwareRoots << def.name.toLower();
                    aliasPairs << def.name + QLatin1Char('=') + def.target;
                }
            }
            hardwareRoots.removeDuplicates();
            hardware = gates;
            hardware << aliasInstances;
            // 探测每个实际实例的有效 Chan 数量。
            const auto instances = hardware;
            for (const auto &instance : instances) {
                for (int channel = 0; channel < 8; ++channel) {
                    const auto probe = QStringLiteral("%1.Chan[%2].EncCtrl").arg(instance).arg(channel);
                    try {
                        const auto reply = QString::fromUtf8(
                            m_client->getResponse(probe.toStdString()).c_str());
                        const int equals = reply.lastIndexOf(QLatin1Char('='));
                        bool ok = false;
                        reply.mid(equals + 1).trimmed().toDouble(&ok);
                        if (!ok) break;
                        hardware << QStringLiteral("%1.Chan[%2]").arg(instance).arg(channel);
                    } catch (const PmacConnection::Interrupted &) {
                        throw;
                    } catch (const std::exception &) {
                        break;
                    }
                }
            }
            hardwareKnown = anyChecked;
            if (!m_client->isConnected()) {
                hardware.clear();
                hardwareRoots.clear();
                hardwareKnown = false;
            }
        } catch (const std::exception &e) {
            files.clear();
            error = QString::fromUtf8(e.what());
        }
        if (PmacConnection::interrupted()) return;
        emit terminalSymbolsReady(generation, files, hardware, hardwareRoots, aliasPairs,
                                  hardwareKnown, error);
    }

    // 界面语言（translations/<code>.json 中的语言代码），由 GUI 线程排队设置
    void setLanguage(const QString &lang) { m_language = lang; }

    void connectTo(const QString &user, const QString &password, const QString &ip, uint16_t port = 22)
    {
        if (PmacConnection::interrupted()) return;
        m_port = port;
        m_user = user;
        m_password = password;
        m_ip = ip;
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
        if (PmacConnection::interrupted()) return;
        try {
            m_client = std::make_shared<PmacConnection>(
                    ip.toStdString(), user.toStdString(), password.toStdString(), m_port);

            // 每 5 秒检查 SDK 状态；SSH keepalive 由 SDK 负责
            if (!m_heartbeat) {
                m_heartbeat = new QTimer(this);
                m_heartbeat->setInterval(5000);
                connect(m_heartbeat, &QTimer::timeout, this, [this]() {
                    if (!PmacConnection::interrupted() && m_client && !m_client->isConnected())
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
            if (PmacConnection::interrupted()) return;
            emit firmwareReady(firmware);
            emit connectionEstablished();
        } catch (const std::exception &e) {
            if (PmacConnection::interrupted()) return;
            emit connectionFailed(Tr::tr(m_language, QStringLiteral("backend.connectionFailed"))
                                          .arg(QString::fromUtf8(e.what())));
        }
    }

    void disconnectFrom()
    {
        if (PmacConnection::interrupted()) return;
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
        if (PmacConnection::interrupted()) return;
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
                              const QString &stderrText, double elapsedMs, const QString &error);
    void terminalSymbolsReady(quint64 generation, const QStringList &files,
                              const QStringList &hardware, const QStringList &hardwareRoots,
                              const QStringList &aliases, bool hardwareKnown,
                              const QString &error);
    void connectionEstablished();
    void connectionFailed(const QString &reason);
    void disconnected();
    void firmwareReady(const QString &version);
    void operationFailed(const QString &message);
    // 后台事件失败（不涉及 busy 状态）
    void backgroundError(const QString &message);

private:
    std::shared_ptr<PmacConnection> m_client;
    QString m_user, m_password, m_ip;
    uint16_t m_port = 22;
    QTimer *m_heartbeat = nullptr;
    QString m_language = QStringLiteral("zh");
};

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
    , m_worker(new BackendWorker)
{
    connect(m_worker, &BackendWorker::terminalCommandReady, this,
        [this](quint64 generation, const QString &id, const QString &text,
               const QString &stderrText, double elapsed, const QString &error) {
            if (generation == m_terminalGeneration)
                emit terminalResponse(id, text, stderrText, elapsed, error);
        });
    connect(m_worker, &BackendWorker::terminalSymbolsReady, this,
        [this](quint64 generation, const QStringList &files, const QStringList &hardware,
               const QStringList &hardwareRoots, const QStringList &aliases,
               bool hardwareKnown, const QString &error) {
            if (generation != m_terminalGeneration) return;
            m_terminalSymbolsLoading = false;
            m_terminalSymbols = files;
            m_terminalHardware = hardware;
            m_terminalHardwareRoots = hardwareRoots;
            m_terminalAliases = aliases;
            m_terminalHardwareKnown = hardwareKnown;
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

    if (auto *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this,
                [this] { m_thread->requestInterruption(); });
    m_thread->start();
}

Backend::~Backend()
{
    // quit() 只退出事件循环，不能打断正在执行的 SDK 等待或重试循环。
    m_thread->requestInterruption();
    m_thread->quit();
    // 请求取消并到达终态后由工作线程安全释放 Client，保持 SDK 收尾顺序。
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
    m_terminalHardware.clear();
    m_terminalHardwareRoots.clear();
    m_terminalAliases.clear();
    m_terminalHardwareKnown = false;
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
        emit terminalResponse(requestId, {}, {}, 0,
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

// 只使用本机临时端口，不连接控制器或发送 PMAC 命令。
int Backend::shutdownSmokeTest()
{
    QSemaphore listening, accepted, recovering;
    std::atomic<uint16_t> port{0};
    std::atomic_int connections{0}, emissions{0};
    std::atomic_bool reject{false};
    // 独立事件循环保持模拟服务器运行，GUI 析构等待时也能执行保险超时。
    std::unique_ptr<QThread> fixture(QThread::create([&] {
        QTcpServer server;
        if (server.listen(QHostAddress::LocalHost, 0)) port = server.serverPort();
        listening.release();
        if (!port) return;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                ++connections;
                if (reject) {
                    accepted.release();
                    socket->abort();
                } else {
                    // SDK 默认先做 TCP 探测，只有收到客户端 SSH 标识才算进入握手。
                    connect(socket, &QTcpSocket::readyRead, socket, [socket, &accepted] {
                        const auto bytes = socket->property("received").toByteArray() + socket->readAll();
                        socket->setProperty("received", bytes);
                        if (!socket->property("announced").toBool() && bytes.contains("SSH-")) {
                            socket->setProperty("announced", true);
                            accepted.release();
                        }
                    });
                    socket->write("SSH-2.0-PmacTerminalSmoke\r\n");
                    QTimer::singleShot(5000, socket, &QTcpSocket::abort);
                }
            }
        });
        QEventLoop loop;
        loop.exec();
    }));
    const auto finishFixture = qScopeGuard([&] { fixture->quit(); fixture->wait(); });
    fixture->start();
    if (!listening.tryAcquire(1, 3000) || !port) return 1;
    const auto watchSignals = [&](BackendWorker *worker) {
        connect(worker, &BackendWorker::connectionFailed, worker,
                [&](const QString &) { ++emissions; }, Qt::DirectConnection);
        connect(worker, &BackendWorker::connectionEstablished, worker,
                [&] { ++emissions; }, Qt::DirectConnection);
        connect(worker, &BackendWorker::terminalCommandReady, worker,
                [&] { ++emissions; }, Qt::DirectConnection);
        connect(worker, &BackendWorker::terminalSymbolsReady, worker,
                [&] { ++emissions; }, Qt::DirectConnection);
    };
    // 模拟 TCP 已接通、SSH 握手迟迟不返回。关闭必须取消活动请求，
    // 随后同一工作回调中排好的恢复、探测及命令也不能继续执行。
    auto backend = std::make_unique<Backend>();
    auto *worker = backend->m_worker;
    watchSignals(worker);
    QMetaObject::invokeMethod(worker, [worker, targetPort = port.load()] {
        worker->connectTo("smoke", "unused", "127.0.0.1", targetPort);
        QString error;
        worker->restoreAfterReset(&error);
        worker->terminalSymbols(0);
        worker->terminalCommand(0, "shutdown-test", "$$$");
    }, Qt::QueuedConnection);
    if (!accepted.tryAcquire(1, 3000)) return 2;
    const int pendingConnections = connections.load();
    QElapsedTimer elapsed;
    elapsed.start();
    backend.reset();
    const auto pendingMs = elapsed.elapsed();
    if (pendingMs >= 2000) return 3;
    if (connections != pendingConnections || emissions != 0) return 4;
    // 模拟连接失败后进入复位重连。退出应停止重试及一秒的等待间隔。
    reject = true;
    backend = std::make_unique<Backend>();
    worker = backend->m_worker;
    watchSignals(worker);
    QMetaObject::invokeMethod(worker, [worker, &recovering, targetPort = port.load()] {
        worker->connectTo("smoke", "unused", "127.0.0.1", targetPort);
        recovering.release();
        QString error;
        worker->restoreAfterReset(&error);
    }, Qt::QueuedConnection);
    if (!recovering.tryAcquire(1, 3000) || !accepted.tryAcquire(2, 3000)) return 5;
    QThread::msleep(100);
    const int beforeStop = connections.load();
    elapsed.restart();
    backend.reset();
    const auto retryMs = elapsed.elapsed();
    if (retryMs >= 750 || connections != beforeStop) return 6;
    // 反复创建后立即销毁，覆盖线程刚启动和退出请求竞争的情况。
    for (int i = 0; i < 20; ++i) {
        Backend idle;
        idle.setLanguage("en");
    }
    qInfo() << "Shutdown: pending SSH" << pendingMs << "ms, retry" << retryMs
            << "ms, no follow-up requests, idle lifecycle passed";
    return 0;
}

#include "Backend.moc"

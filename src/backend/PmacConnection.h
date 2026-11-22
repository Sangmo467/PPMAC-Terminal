#ifndef PMACCONNECTION_H
#define PMACCONNECTION_H

#include "pmac.hpp"
#include <QThread>
#include <QDeadlineTimer>
#include <stdexcept>

// 一个 SDK Client 由 BackendWorker 持有，终端与 Watch 请求都在该线程执行。
class PmacConnection
{
public:
    class Interrupted : public std::runtime_error {
    public:
        Interrupted() : std::runtime_error("PMAC worker is stopping") {}
    };

    static constexpr int connectTimeoutMs = 10000;

    static bool interrupted() { return QThread::currentThread()->isInterruptionRequested(); }
    static void checkInterrupted() { if (interrupted()) throw Interrupted(); }

    PmacConnection(const std::string &host, const std::string &user,
                   const std::string &password, uint16_t port = 22,
                   QDeadlineTimer deadline = QDeadlineTimer(connectTimeoutMs, Qt::PreciseTimer))
        : m_client(m_runtime.createClient())
    {
        checkInterrupted();
        if (deadline.hasExpired()) throw std::runtime_error("Connection timed out (10 s)");
        pmac::ConnectOptions options;
        options.host = host;
        options.username = user;
        options.password = password;
        options.port = port;
        options.controlPoolSize = 1;
        // TCP、SSH 握手与认证共用外层 deadline；探测并不代表 SSH 已可用。
        options.tcpProbeTimeoutMs = 5000;
        options.keepaliveIntervalMs = 1000;
        options.keepaliveMax = 3;
        // 保持原程序按地址和密码连接的行为。
        options.acceptAnyHostKey = true;
        auto request = m_client.connect(options);
        wait(request, deadline);
    }

    struct Response {
        std::string output;
        std::string stderrText;
    };

    static uint32_t extendedTimeoutMs(const std::string &command)
    {
        const auto text = QString::fromUtf8(command.data(), qsizetype(command.size())).simplified();
        return text.compare(QLatin1String("ecat reset"), Qt::CaseInsensitive) == 0
                || text.compare(QLatin1String("ecat rescan"), Qt::CaseInsensitive) == 0
            ? 20000 : 0;
    }

    // ECAT 维护命令使用 20 秒；其他命令保留 SDK 默认及 $$$ 的加长预算。
    Response getResponseWithStderr(const std::string &command, const std::string &pattern = "",
                                   uint32_t timeoutMs = 0)
    {
        checkInterrupted();
        pmac::Request request;
        if (timeoutMs == 0) timeoutMs = extendedTimeoutMs(command);
        if (timeoutMs != 0) {
            pmac::RequestOptions options;
            options.timeoutMs = timeoutMs;
            request = m_client.getResponseAsync(command, options);
        } else {
            request = m_client.getResponseAsync(command);
        }
        wait(request);
        const auto bytes = pattern.empty() ? request.response() : request.match(pattern);
        const auto diagnostics = request.stderrBytes();
        return {std::string(bytes.begin(), bytes.end()),
                std::string(diagnostics.begin(), diagnostics.end())};
    }

    std::string getResponse(const std::string &command, const std::string &pattern = "",
                            uint32_t timeoutMs = 0)
    {
        return getResponseWithStderr(command, pattern, timeoutMs).output;
    }

    std::vector<uint8_t> shell(const std::string &command, bool persistent = false,
                               const pmac::RequestOptions &options = {})
    {
        checkInterrupted();
        auto request = m_client.shellAsync(command, persistent, options);
        wait(request);
        return request.response();
    }

    bool isConnected() const
    {
        try { return m_client.isConnected(); }
        catch (const pmac::Error &) { return false; }
    }

    // 复位会重启 gpascii；显式修复维护面后再读取符号文件。
    void repairAfterReset()
    {
        checkInterrupted();
        auto request = m_client.repairMaintenanceAsync();
        wait(request);
        const auto state = m_client.poolState();
        checkInterrupted();
        if (state.unknown_write)
            m_client.confirmRecovery(state.revision, PMAC_RECOVERY_CONFIRM_UNKNOWN_WRITES);
    }

    pmac::Client &client() { return m_client; }

private:
    static void wait(pmac::Request &request,
                     QDeadlineTimer deadline = QDeadlineTimer(QDeadlineTimer::Forever))
    {
        bool cancellationSent = false;
        bool timedOut = false;
        for (;;) {
            if (interrupted() && !cancellationSent) {
                // 完成与取消可能竞争；无论取消返回什么，都等到请求终态后再释放。
                (void)pmac_request_cancel(request.id());
                cancellationSent = true;
            }
            const auto remaining = deadline.remainingTime();
            const auto waitMs = remaining < 0 ? 50u : uint32_t(qBound(qint64(1), remaining, qint64(50)));
            const auto info = request.waitNoThrow(waitMs);
            if (info.status == PMAC_WAIT_PENDING) {
                if (!cancellationSent && deadline.hasExpired()) {
                    (void)pmac_request_cancel(request.id());
                    cancellationSent = true;
                    timedOut = true;
                }
                continue;
            }
            if (interrupted()) throw Interrupted();
            if (timedOut) throw std::runtime_error("Connection timed out (10 s)");
            // 请求已到终态；交给 SDK 保留原有错误详情和执行确定性。
            request.wait();
            return;
        }
    }

    pmac::Runtime m_runtime;
    pmac::Client m_client;
};

#endif // PMACCONNECTION_H

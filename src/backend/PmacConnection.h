#ifndef PMACCONNECTION_H
#define PMACCONNECTION_H

#include "pmac.hpp"
#include <QThread>
#include <stdexcept>

// 一个 SDK Client 由 BackendWorker 持有，终端与 Watch 请求都在该线程执行。
class PmacConnection
{
public:
    class Interrupted : public std::runtime_error {
    public:
        Interrupted() : std::runtime_error("PMAC worker is stopping") {}
    };

    static bool interrupted() { return QThread::currentThread()->isInterruptionRequested(); }
    static void checkInterrupted() { if (interrupted()) throw Interrupted(); }

    PmacConnection(const std::string &host, const std::string &user,
                   const std::string &password, uint16_t port = 22)
        : m_client(m_runtime.createClient())
    {
        checkInterrupted();
        pmac::ConnectOptions options;
        options.host = host;
        options.username = user;
        options.password = password;
        options.port = port;
        options.controlPoolSize = 1;
        // 保持原程序按地址和密码连接的行为。
        options.acceptAnyHostKey = true;
        auto request = m_client.connect(options);
        wait(request);
    }

    struct Response {
        std::string output;
        std::string stderrText;
    };

    // 使用 SDK 默认执行超时（包括复位命令的加长预算），只缩短单次等待以响应退出。
    Response getResponseWithStderr(const std::string &command, const std::string &pattern = "")
    {
        checkInterrupted();
        auto request = m_client.getResponseAsync(command);
        wait(request);
        const auto bytes = pattern.empty() ? request.response() : request.match(pattern);
        const auto diagnostics = request.stderrBytes();
        return {std::string(bytes.begin(), bytes.end()),
                std::string(diagnostics.begin(), diagnostics.end())};
    }

    std::string getResponse(const std::string &command, const std::string &pattern = "")
    {
        return getResponseWithStderr(command, pattern).output;
    }

    std::vector<uint8_t> shell(const std::string &command, bool persistent = false)
    {
        checkInterrupted();
        auto request = m_client.shellAsync(command, persistent);
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
    static void wait(pmac::Request &request)
    {
        bool cancellationSent = false;
        for (;;) {
            if (interrupted() && !cancellationSent) {
                // 完成与取消可能竞争；无论取消返回什么，都等到请求终态后再释放。
                (void)pmac_request_cancel(request.id());
                cancellationSent = true;
            }
            const auto info = request.waitNoThrow(50);
            if (info.status == PMAC_WAIT_PENDING) continue;
            if (cancellationSent || interrupted()) throw Interrupted();
            // 请求已到终态；交给 SDK 保留原有错误详情和执行确定性。
            request.wait();
            return;
        }
    }

    pmac::Runtime m_runtime;
    pmac::Client m_client;
};

#endif // PMACCONNECTION_H

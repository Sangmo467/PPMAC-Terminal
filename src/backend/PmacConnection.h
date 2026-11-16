#ifndef PMACCONNECTION_H
#define PMACCONNECTION_H

#include "pmac.hpp"

// 一个 SDK Client 由工作线程和两个轮询线程共同持有。
// SDK 为各调用线程分配 Channel；控制池只配置一个成员。
class PmacConnection
{
public:
    PmacConnection(const std::string &host, const std::string &user,
                   const std::string &password)
        : m_client(m_runtime.createClient())
    {
        pmac::ConnectOptions options;
        options.host = host;
        options.username = user;
        options.password = password;
        options.controlPoolSize = 1;
        // 保持原程序按地址和密码连接的行为。
        options.acceptAnyHostKey = true;
        m_client.connectSync(options);
    }

    // 不覆盖超时等请求参数，使用 SDK 默认值（复位命令由 SDK 自动放宽）。
    std::string getResponse(const std::string &command, const std::string &pattern = "")
    {
        auto bytes = m_client.getResponse(command, pattern);
        return std::string(bytes.begin(), bytes.end());
    }

    bool isConnected() const
    {
        try { return m_client.isConnected(); }
        catch (const pmac::Error &) { return false; }
    }

    // 复位会重启 gpascii，SDK 维护面必须显式修复；否则下一次 shell 读取
    // （符号文件）会置 unknown_write，后续请求全部返回 RECOVERY_REQUIRED(-27)。
    void repairAfterReset()
    {
        m_client.repairMaintenanceAsync().wait();
        const auto state = m_client.poolState();
        if (state.unknown_write)
            m_client.confirmRecovery(state.revision, PMAC_RECOVERY_CONFIRM_UNKNOWN_WRITES);
    }

    pmac::Client &client() { return m_client; }

private:
    pmac::Runtime m_runtime;
    pmac::Client m_client;
};

#endif // PMACCONNECTION_H
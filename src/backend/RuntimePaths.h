#pragma once
// 程序旁的动态库文件名按平台不同：Windows .dll / macOS .dylib / Linux .so。
#include <QCoreApplication>
#include <QString>
#include <QtGlobal>

namespace RuntimePaths {
inline QString terminalCoreLibrary()
{
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pmac_terminal_core.dll");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac_terminal_core.dylib");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac_terminal_core.so");
#endif
}

inline QString pmacLibrary()
{
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pmac.dll");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac.dylib");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac.so");
#endif
}
}

#pragma once
// Windows/Linux 动态库在程序旁；macOS 动态库在 Contents/Frameworks。
#include <QCoreApplication>
#include <QString>
#include <QtGlobal>

namespace RuntimePaths {
inline QString terminalCoreLibrary()
{
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pmac_terminal_core.dll");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../Frameworks/libpmac_terminal_core.dylib");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac_terminal_core.so");
#endif
}

inline QString pmacLibrary()
{
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pmac.dll");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../Frameworks/libpmac.dylib");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libpmac.so");
#endif
}
}

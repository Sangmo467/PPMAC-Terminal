// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QApplication>
#include <QTimer>
#include "backend/TerminalPage.h"
#include "backend/Backend.h"
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QQuickWidget>
#include <QScreen>
#include <QVBoxLayout>

#include "app_environment.h"
#include "import_qml_plugins.h"
#include "backend/Backend.h"

#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTimer>

#ifdef Q_OS_WIN
#include <dwmapi.h>

// 让原生标题栏（最小化/最大化/关闭）跟随深色主题，与界面风格一致。
// DWMWA_USE_IMMERSIVE_DARK_MODE：Win10 20H1+ 为 20，1809 为 19。
static void applyDarkTitleBar(QWindow *window)
{
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL useDark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark))))
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
}
#else
static void applyDarkTitleBar(QWindow *) {}
#endif

// macOS 的 Cocoa 会将嵌套 QWidgetWindow 的普通按键送给其最外层 QWindow。
// 使用 QQuickWidget 将 QML 与终端纳入同一个 QWidget 顶层窗口的焦点链。
static int runWidgetShell(QApplication &app)
{
    QWidget shell;
    shell.setObjectName("widgetShell");
    shell.setWindowTitle(QStringLiteral("PMAC Terminal"));
    shell.winId();
    applyDarkTitleBar(shell.windowHandle());
    auto *layout = new QVBoxLayout(&shell);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *view = new QQuickWidget(&shell);
    view->setObjectName("qmlShell");
    view->setFocusPolicy(Qt::StrongFocus);
    view->setClearColor(QColor("#16181d"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->engine()->addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    view->engine()->addImportPath(":/");
    layout->addWidget(view);
    view->setSource(QUrl(QStringLiteral("qrc:/content/MainScreen.qml")));
    if (view->status() != QQuickWidget::Ready || !view->rootObject()) {
        for (const auto &error : view->errors()) qWarning() << error;
        return -1;
    }
    const auto available = shell.screen()->availableGeometry().size();
    const auto preferred = view->initialSize();
    shell.setMinimumSize(qMin(1100, available.width()), qMin(680, available.height() - 40));
    shell.resize(qMin(preferred.width(), available.width() - 80),
                 qMin(preferred.height(), available.height() - 120));
    shell.show();
    const bool focusSmoke = app.arguments().contains("--terminal-focus-smoke");
    if (app.arguments().contains("--terminal-device-smoke")) {
        QTimer::singleShot(800, &app, [&] {
            auto *terminal = view->rootObject()->findChild<TerminalPage *>("terminalPage");
            if (!terminal || !terminal->backend()) { qInfo() << "no backend"; app.exit(20); return; }
            auto *backend = terminal->backend();
            QObject::connect(backend, &Backend::operationFailed, &app, [&app](const QString &message) {
                qWarning().noquote() << "Device connection failed:" << message;
                app.exit(22);
            });
            backend->connectTo(qEnvironmentVariable("PMAC_USER", "root"),
                               qEnvironmentVariable("PMAC_PASSWORD"),
                               qEnvironmentVariable("PMAC_HOST", "192.168.0.200"));
            auto *poll = new QTimer(&app);
            poll->setInterval(200);
            QObject::connect(poll, &QTimer::timeout, &app, [&, backend, terminal, poll] {
                if (backend->connectionState() != QLatin1String("connected") || backend->busy()) return;
                poll->stop();
                QPlainTextEdit *input = nullptr, *output = nullptr;
                for (auto *widget : QApplication::allWidgets()) {
                    if (widget->objectName() == QLatin1String("commandInput"))
                        input = qobject_cast<QPlainTextEdit *>(widget);
                    else if (widget->objectName() == QLatin1String("terminalOutput"))
                        output = qobject_cast<QPlainTextEdit *>(widget);
                }
                if (!input) { qInfo() << "no input"; app.exit(21); return; }
                QObject::connect(backend, &Backend::terminalResponse, &app,
                    [](const QString &id, const QString &text, const QString &stderrText,
                       double, const QString &error) {
                        qInfo().noquote() << "SIGNAL id=" << id << "stdout=" << text
                                          << "stderr=" << stderrText << "error=" << error;
                    });
                input->setPlainText(QStringLiteral("sefsoefunha"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
                QApplication::sendEvent(input, &enter);
                QTimer::singleShot(3000, &app, [&, output, terminal] {
                    qInfo().noquote() << "OUTPUT>>";
                    if (output) qInfo().noquote() << output->toPlainText();
                    app.exit(terminal->deviceCompletionSmokeTest());
                });
            });
            poll->start();
        });
    } else if (focusSmoke || app.arguments().contains("--terminal-smoke")) {
        QTimer::singleShot(800, &app, [&] {
            auto *terminal = view->rootObject()->findChild<TerminalPage *>("terminalPage");
            if (!terminal) { app.exit(20); return; }
            const int result = focusSmoke ? terminal->focusSmokeTest() : terminal->smokeTest();
            qInfo() << (focusSmoke ? "Terminal focus smoke:" : "Terminal integration smoke:") << result;
            app.exit(result);
        });
    }
    return app.exec();
}

int main(int argc, char *argv[])
{
    set_qt_environment();

    // Windows 的轻量 QML 外壳默认使用光栅绘制，避免 GPU 合成失效使整个 QWidget 窗口白屏。
    // 保留显式后端配置，方便按显卡/驱动排查；macOS 继续使用平台默认 Metal。
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")
        && qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")
        && qEnvironmentVariableIsEmpty("QMLSCENE_DEVICE"))
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PmacTerminal"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PMACT_APP_VERSION));
    app.setWindowIcon(QIcon(u":/icons/PmacTerminal.png"_qs));

    if (app.arguments().contains("--backend-shutdown-smoke")) {
        const int result = Backend::shutdownSmokeTest();
        qInfo() << "Backend shutdown smoke:" << result;
        return result;
    }

    return runWidgetShell(app);
}

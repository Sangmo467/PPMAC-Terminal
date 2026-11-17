// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QApplication>
#include <QTimer>
#include "backend/TerminalPage.h"
#include "backend/Backend.h"
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickWidget>
#include <QScreen>
#include <QVBoxLayout>

#include "app_environment.h"
#include "import_qml_plugins.h"

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
    if (focusSmoke || app.arguments().contains("--terminal-smoke")) {
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

    // Windows 嵌入原生 QWidget 时使用 OpenGL；macOS 保持默认 Metal 后端。
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(u":/icons/PmacTerminal.png"_qs));

    if (app.arguments().contains("--backend-shutdown-smoke")) {
        const int result = Backend::shutdownSmokeTest();
        qInfo() << "Backend shutdown smoke:" << result;
        return result;
    }

#ifdef Q_OS_MACOS
    return runWidgetShell(app);
#else
    // 其他平台可显式运行相同宿主进行离线回归；Windows 正常界面沿用原路径。
    if (app.arguments().contains("--widget-shell")) return runWidgetShell(app);
#endif

    QQmlApplicationEngine engine;
    const QUrl url(u"qrc:Main/main.qml"_qs);
    QObject::connect(
                &engine, &QQmlApplicationEngine::objectCreated, &app,
                [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    },
    Qt::QueuedConnection);

    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.addImportPath(":/");

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    applyDarkTitleBar(qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()));

    if (app.arguments().contains("--terminal-smoke")) {
        QTimer::singleShot(800, &app, [&] {
            auto root = engine.rootObjects().constFirst();
            auto terminal = root->findChild<TerminalPage *>("terminalPage");
            if (!terminal) { app.exit(20); return; }
            const int result = terminal->smokeTest();
            qInfo() << "Terminal integration smoke:" << result;
            app.exit(result);
        });
    }
    return app.exec();
}

// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QApplication>
#include <QTimer>
#include "backend/TerminalPage.h"
#include <QQmlApplicationEngine>
#include <QQuickWindow>

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

int main(int argc, char *argv[])
{
    set_qt_environment();

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(u":/icons/PITBall.png"_qs));

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

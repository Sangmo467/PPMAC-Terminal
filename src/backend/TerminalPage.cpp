#include "TerminalPage.h"
#include <QQuickWindow>
#include <QScopeGuard>
#include "TerminalWidgets.inc"
#include "WatchPanel.h"


namespace {
// 至少保留一个终端：仅剩一页时隐藏其关闭按钮。
void updateTabCloseButtons(QTabWidget *tabs)
{
    const bool closable = tabs->count() > 1;
    for (int i = 0; i < tabs->count(); ++i) {
        for (auto side : {QTabBar::RightSide, QTabBar::LeftSide}) {
            if (auto *button = tabs->tabBar()->tabButton(i, side))
                button->setVisible(closable);
        }
    }
}
}

struct TerminalPage::Impl {
    QPointer<Backend> backend;
    std::unique_ptr<Api> api;
    std::unique_ptr<QWidget> host;
    QTabWidget *tabs = nullptr;
    WatchPanel *watch = nullptr;
    QPushButton *addButton = nullptr;
    QLabel *hintLabel = nullptr;
    QMetaObject::Connection frameConnection;
    QMetaObject::Connection activeConnection;
    QPointer<QWidget> lastFocus;
    int number = 0;
};

TerminalPage::TerminalPage(QQuickItem *parent) : QQuickItem(parent), d(std::make_unique<Impl>())
{
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *w) {
        QObject::disconnect(d->frameConnection);
        QObject::disconnect(d->activeConnection);
        if (d->host) d->host->hide();
        if (w) {
            d->frameConnection = connect(w, &QQuickWindow::afterAnimating, this, &TerminalPage::syncHost);
            // 原生子窗口不会随主窗口重新激活恢复焦点；切回本窗口时补回。
            d->activeConnection = connect(w, &QQuickWindow::activeChanged, this, [this, w] {
                if (!d->host) return;
                if (!w->isActive()) {
                    if (auto *focused = d->host->focusWidget()) d->lastFocus = focused;
                    if (d->tabs) for (int i = 0; i < d->tabs->count(); ++i)
                        static_cast<Terminal *>(d->tabs->widget(i))->hideCompletion();
                    return;
                }
                if (!d->host->isVisible()) return;
                d->host->activateWindow();
                if (d->lastFocus && d->lastFocus->isVisible()) {
                    d->lastFocus->setFocus(Qt::ActiveWindowFocusReason);
                } else if (d->tabs && d->tabs->currentWidget()) {
                    static_cast<Terminal *>(d->tabs->currentWidget())
                        ->focusInput(Qt::ActiveWindowFocusReason);
                }
                if (d->tabs && d->tabs->currentWidget())
                    static_cast<Terminal *>(d->tabs->currentWidget())->ensureEnglish();
            });
        }
        syncHost();
    });
    connect(this, &QQuickItem::visibleChanged, this, &TerminalPage::syncHost);
}

TerminalPage::~TerminalPage()
{
    QObject::disconnect(d->frameConnection);
    // Destroy sessions (and join their workers) before unloading the Rust DLL.
    d->host.reset();
    d->api.reset();
}

void TerminalPage::newSession()
{
    if (!d->api || !d->tabs) return;
    try {
        auto terminal = new Terminal(*d->api, d->backend, d->tabs);
        const QString lang = d->backend ? d->backend->language() : QStringLiteral("en");
        d->tabs->setCurrentIndex(d->tabs->addTab(
            terminal, Tr::tr(lang, QStringLiteral("terminal.tab")).arg(++d->number)));
        updateTabCloseButtons(d->tabs);
    } catch (const std::exception &e) {
        const QString lang = d->backend ? d->backend->language() : QStringLiteral("en");
        QMessageBox::warning(d->host.get(), Tr::tr(lang, QStringLiteral("terminal.title")),
                             QString::fromUtf8(e.what()));
    }
}

void TerminalPage::syncHost()
{
    if (!window() || !isVisible() || !window()->isVisible() || width() <= 0 || height() <= 0) {
        if (d->host) {
            if (auto *focused = d->host->focusWidget()) d->lastFocus = focused;
            d->host->hide();
        }
        return;
    }
    if (!d->host) {
        d->host = std::make_unique<QWidget>(nullptr, Qt::SubWindow | Qt::FramelessWindowHint);
        d->host->setObjectName("embeddedTerminal");
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#16181d"));
        palette.setColor(QPalette::WindowText, QColor("#d7dae0"));
        palette.setColor(QPalette::Base, QColor("#16181d"));
        palette.setColor(QPalette::AlternateBase, QColor("#1c2026"));
        palette.setColor(QPalette::Text, QColor("#d7dae0"));
        palette.setColor(QPalette::Button, QColor("#262b33"));
        palette.setColor(QPalette::ButtonText, QColor("#f0f3f7"));
        palette.setColor(QPalette::Highlight, QColor("#287db6"));
        palette.setColor(QPalette::HighlightedText, QColor("#f0f3f7"));
        palette.setColor(QPalette::ToolTipBase, QColor("#262b33"));
        palette.setColor(QPalette::ToolTipText, QColor("#f0f3f7"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#6b7280"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b7280"));
        d->host->setPalette(palette);
        d->host->setAutoFillBackground(true);
        d->host->setStyleSheet(
            "QPushButton, QLabel, QCheckBox, QTabBar {font-family:'Microsoft YaHei UI','PingFang SC','Helvetica Neue',sans-serif; font-size:16px; font-weight:600;}"
            "QPushButton {background:#348bcd;color:#ffffff;border:0;border-radius:5px;padding:9px 12px;font-size:18px;font-weight:600;}"
            "QLabel {color:#f0f3f7;background:transparent;}"
            "QLabel#sharedConnectionHint {color:#ffffff;background:#262b33;border:1px solid #3a4150;border-radius:5px;padding:8px;}"
            "QPushButton:hover {background:#3fa7f5;} QPushButton:pressed {background:#2873a9;}"
            "QPushButton:disabled {background:#262b33;color:#6b7280;}"
            "QTabWidget::pane {border:1px solid #3a4150;border-radius:5px;}"
            "QTabBar::tab {background:#1c2026;color:#9ba5b4;padding:8px 18px;border-bottom:2px solid transparent;}"
            "QTabBar::tab:selected {color:#f0f3f7;border-bottom:2px solid #3fa7f5;}"
            "QPlainTextEdit {border:1px solid #3a4150;border-radius:5px;padding:6px;}"
            "QPlainTextEdit:focus {border:1px solid #3fa7f5;}"
            "QSplitter::handle {background:#262b33;height:6px;}"
            "QCheckBox {color:#d7dae0;spacing:6px;}"
            "QCheckBox::indicator {width:15px;height:15px;border:1px solid #5f6875;border-radius:3px;background:#1c2026;}"
            "QCheckBox::indicator:checked {background:#3fa7f5;border-color:#3fa7f5;}"
            "QToolTip {background:#ffffff;color:#000000;border:1px solid #8b95a5;padding:6px;font-size:18px;}");
        auto layout = new QVBoxLayout(d->host.get());
        layout->setContentsMargins(8, 4, 8, 4);
        try {
            d->api = std::make_unique<Api>(RuntimePaths::terminalCoreLibrary());
            auto row = new QHBoxLayout;
            d->addButton = new QPushButton;
            d->addButton->setObjectName("newTerminal");
            row->addWidget(d->addButton);
            d->hintLabel = new QLabel;
            d->hintLabel->setObjectName("sharedConnectionHint");
            d->hintLabel->setWordWrap(true);
            row->addWidget(d->hintLabel, 1);
            layout->addLayout(row);
            d->tabs = new QTabWidget;
            d->tabs->setTabsClosable(true);
            auto *splitter = new QSplitter(Qt::Horizontal);
            splitter->setObjectName("terminalSplitter");
            splitter->setChildrenCollapsible(false);
            d->watch = new WatchPanel(*d->api, d->backend, [this] {
                if (!d->tabs) return false;
                for (int i = 0; i < d->tabs->count(); ++i)
                    if (static_cast<Terminal *>(d->tabs->widget(i))->hasPendingRequest())
                        return true;
                return false;
            }, splitter);
            d->watch->setMinimumWidth(320);
            splitter->addWidget(d->watch);
            splitter->addWidget(d->tabs);
            splitter->setStretchFactor(0, 0);
            splitter->setStretchFactor(1, 1);
            layout->addWidget(splitter, 1);
            connect(d->addButton, &QPushButton::clicked, this, &TerminalPage::newSession);
            connect(d->tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
                if (d->tabs->count() <= 1) return;
                auto page = d->tabs->widget(index);
                d->tabs->removeTab(index);
                delete page;
                updateTabCloseButtons(d->tabs);
            });
            newSession();
            retranslate();
        } catch (const std::exception &e) {
            const QString lang = d->backend ? d->backend->language() : QStringLiteral("en");
            auto error = new QLabel(Tr::tr(lang, QStringLiteral("terminal.coreFailed"))
                                        .arg(QString::fromUtf8(e.what())));
            error->setWordWrap(true);
            layout->addWidget(error);
        }
        d->host->winId();
    }
    if (d->host->windowHandle()->parent() != window())
        d->host->windowHandle()->setParent(window());
    const QPoint topLeft = mapToScene(QPointF(0, 0)).toPoint();
    const QRect geometry(topLeft, QSize(qRound(width()), qRound(height())));
    if (d->host->geometry() != geometry) d->host->setGeometry(geometry);
    if (!d->host->isVisible()) {
        d->host->show();
        if (d->lastFocus && d->lastFocus->isVisible())
            d->lastFocus->setFocus(Qt::OtherFocusReason);
    }
}

int TerminalPage::smokeTest()
{
    syncHost();
    if (!d->tabs || d->tabs->count() != 1 || !d->host->isVisible()) return 1;
    newSession();
    if (d->tabs->count() != 2) return 2;
    auto page = d->tabs->currentWidget();
    auto input = page->findChild<QPlainTextEdit *>("commandInput");
    auto output = page->findChild<QPlainTextEdit *>("terminalOutput");
    if (!input || !output) return 3;
    // Render the completion delegate without focus: native inactive selection
    // must still be blue/white, while the remaining rows stay white/black.
    {
        QListView view;
        QStringListModel model({"Motor[]", "Sys"});
        view.setModel(&model);
        view.setCurrentIndex(model.index(0, 0));
        view.selectionModel()->clearSelection();
        CompletionDelegate delegate(&view);
        for (bool active : {false, true}) {
            for (int row = 0; row < 2; ++row) {
                QImage canvas(240, 40, QImage::Format_ARGB32_Premultiplied);
                canvas.fill(Qt::magenta);
                QStyleOptionViewItem option;
                option.rect = canvas.rect();
                option.font = TerminalColors::terminalFont();
                option.state = QStyle::State_Enabled;
                if (active) option.state |= QStyle::State_Active;
                QPainter painter(&canvas);
                delegate.paint(&painter, option, model.index(row, 0));
                painter.end();
                const QColor background = row == 0 ? QColor("#0078d7") : QColor("#ffffff");
                const QColor foreground = row == 0 ? QColor("#ffffff") : QColor("#000000");
                if (canvas.pixelColor(2, 2) != background) return 24;
                int foregroundPixels = 0;
                for (int y = 0; y < canvas.height(); ++y)
                    for (int x = 0; x < canvas.width(); ++x)
                        if (canvas.pixelColor(x, y) == foreground) ++foregroundPixels;
                if (foregroundPixels < 10) return 25;
            }
        }
    }
    auto command = static_cast<CommandEdit *>(input);
    // A connection/busy transition calls setReadOnly through the base pointer.
    // Auto English must still reject IME queries, preedit and commit events.
    command->setAutoEnglish(true);
    command->clear();
    for (bool readOnly : {true, false, true, false}) {
        input->setReadOnly(readOnly);
        QInputMethodQueryEvent query(Qt::ImEnabled);
        QApplication::sendEvent(command, &query);
        if (query.value(Qt::ImEnabled).toBool()
            || command->testAttribute(Qt::WA_InputMethodEnabled)) return 43;
    }
    QInputMethodEvent preedit(QStringLiteral("zhongwen"), {});
    QApplication::sendEvent(command, &preedit);
    if (!command->document()->firstBlock().layout()->preeditAreaText().isEmpty()) return 44;
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(command, &commit);
    if (!command->toPlainText().isEmpty()) return 45;
    command->setAutoEnglish(false);
    QInputMethodEvent allowedCommit;
    allowedCommit.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(command, &allowedCommit);
    if (command->toPlainText() != QStringLiteral("中文")) return 46;
    command->clear();
    command->setAutoEnglish(true);
    QKeyEvent ascii(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(command, &ascii);
    if (command->toPlainText() != "a") return 47;
    command->hideCompletion();
    command->clear();
    qInfo() << "Input method: editable transitions, preedit/commit suppression, opt-out and ASCII passed";
#ifdef Q_OS_WIN
    // Exercise actual Windows layouts and native focus in the embedded terminal.
    {
        command->setAutoEnglish(false);
        const HKL originalLayout = GetKeyboardLayout(0);
        const auto restoreLayout = qScopeGuard([&] {
            command->setAutoEnglish(false);
            ActivateKeyboardLayout(originalLayout, 0);
            command->setAutoEnglish(true);
        });
        QList<HKL> layouts(GetKeyboardLayoutList(0, nullptr));
        const int count = GetKeyboardLayoutList(layouts.size(), layouts.data());
        HKL otherLayout = nullptr;
        for (int i = 0; i < count; ++i) {
            if (PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layouts[i]))) != LANG_ENGLISH) {
                otherLayout = layouts[i];
                break;
            }
        }
        if (otherLayout) {
            d->host->activateWindow();
            SetFocus(reinterpret_cast<HWND>(d->host->winId()));
            output->setFocus();
            QApplication::processEvents();
            if (!ActivateKeyboardLayout(otherLayout, 0)) return 34;
            command->setAutoEnglish(true);
            command->ensureEnglish();
            // Reactivation while Watch/output owns focus must not force English.
            if (GetKeyboardLayout(0) != otherLayout) return 35;
            command->setFocus();
            QApplication::processEvents();
            if (!command->hasInputFocus() || !GetFocus() || !command->englishActive()) {
                qWarning() << "Input layout focus failed:" << QGuiApplication::focusObject()
                           << "layout" << reinterpret_cast<quintptr>(GetKeyboardLayout(0));
                return 36;
            }
            if (QInputMethod::queryFocusObject(Qt::ImEnabled, {}).toBool()) return 48;
            const HIMC context = ImmGetContext(GetFocus());
            if (context) {
                ImmReleaseContext(GetFocus(), context);
                return 49;
            }
            // A manual layout change must invalidate the status immediately.
            ActivateKeyboardLayout(otherLayout, 0);
            if (command->englishActive()) return 37;
            command->ensureEnglish();
            if (!command->englishActive()) return 38;
            command->setAutoEnglish(false);
            if (GetKeyboardLayout(0) != otherLayout) return 39;
            command->setAutoEnglish(true);
            for (int i = 0; i < 5; ++i) {
                output->setFocus();
                if (GetKeyboardLayout(0) != otherLayout) return 40;
                command->setFocus();
            }
            // Drain pending focus retries and native messages after rapid refocus.
            QEventLoop settle;
            QTimer::singleShot(200, &settle, &QEventLoop::quit);
            settle.exec();
            if (!command->englishActive()) return 41;
            command->setAutoEnglish(false);
            if (GetKeyboardLayout(0) != otherLayout) return 42;
            qInfo() << "Input layout: focus, recheck, restore and rapid refocus passed";
        } else {
            qWarning() << "Input layout smoke skipped: no non-English layout is loaded";
        }
    }
#endif
    command->setPlainText(QString::fromUtf8("前缀🙂=Mot"));
    command->moveCursor(QTextCursor::End);
    command->acceptCompletion("Motor[]");
    if (command->toPlainText() != QString::fromUtf8("前缀🙂=Motor[]")
        || command->textCursor().position() != QString::fromUtf8("前缀🙂=Motor[").size()) return 4;
    auto completeWithKey = [&](const QString &prefix, const QStringList &choices, int selected, int key) {
        command->setPlainText(prefix);
        command->moveCursor(QTextCursor::End);
        command->model.setStringList(choices);
        command->completer.setCompletionPrefix(QString());
        command->completer.complete(command->cursorRect());
        command->completer.popup()->setCurrentIndex(command->completer.completionModel()->index(selected, 0));
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(command, &event);
    };
    completeWithKey("Gate3[0].Ch", {"Chan[]"}, 0, Qt::Key_Tab);
    if (command->toPlainText() != "Gate3[0].Chan[]" || command->textCursor().position() != 14) return 13;
    completeWithKey("Sys.Serv", {"ServoCtrl", "ServoTime"}, 1, Qt::Key_Return);
    if (command->toPlainText() != "Sys.ServoTime") return 14;
    if (!page->findChildren<QLineEdit *>().isEmpty()) return 15;
    if (page->findChild<QCheckBox *>("responseTiming")->isChecked()) return 16;
    if (!d->backend) return 17;
    // Exercise the real Rust index via symbols supplied by the shared connection path.
    QJsonArray files;
    files.append(QStringList{"1","Motor[]","Motor help","0","","","",""}.join(QChar(1)) + "\n");
    files.append(QStringList{"1","Motor[]","ActPos","Position help","0","","","","",""}.join(QChar(1)) + "\n");
    for (int i = 0; i < 5; ++i) files.append("");
    const auto symbolJson = QJsonDocument(files).toJson(QJsonDocument::Compact);
    if (static_cast<Terminal *>(page)->loadTestSymbols(symbolJson) != PMACT_OK) return 18;
    if (!command->complete("Mot").contains("Motor[]")) return 19;
    if (!command->complete("Motor[1].Act").contains("ActPos")) return 20;
    int dollarSubmissions = 0, pulses = 0;
    const auto originalSubmit = command->submit;
    command->submit = [&] { ++dollarSubmissions; };
    QTimer heartbeat;
    heartbeat.setInterval(0);
    connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++pulses; });
    heartbeat.start();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < 1000; ++i) {
        command->clear();
        for (int j = 0; j < 3; ++j) {
            QKeyEvent dollar(QEvent::KeyPress, Qt::Key_Dollar, Qt::NoModifier, "$");
            QApplication::sendEvent(command, &dollar);
        }
        if (command->toPlainText() != "$$$" || dollarSubmissions) return 21;
        QApplication::processEvents();
    }
    heartbeat.stop();
    if (!pulses || elapsed.elapsed() > 10000) return 22;
    command->handleEnter();
    if (dollarSubmissions != 1) return 23;
    command->submit = originalSubmit;
    qInfo() << "Dollar input: 1000 cycles, GUI heartbeat" << pulses << "elapsed ms" << elapsed.elapsed();
    command->help.insert("ServoTime", QStringLiteral("伺服周期帮助"));
    command->remember("draft");
    int submissions = 0;
    command->submit = [&submissions] { ++submissions; };
    command->handleEnter();
    command->submit = {};
    if (submissions != 1) return 5;
    command->remember("Sys.ServoTime");
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(command, &up);
    if (command->toPlainText() != "Sys.ServoTime") return 6;
    if (output->palette().color(QPalette::Base) != QColor("#000080")) return 7;
    output->setPlainText(QString::fromUtf8("中文响应🙂"));
    auto saved = std::make_unique<QMimeData>();
    const auto original = QApplication::clipboard()->mimeData();
    if (original) for (const auto &format : original->formats()) saved->setData(format, original->data(format));
    // Paste is data: even a multi-line $$$ payload must never invoke submit.
    const auto pasteSubmit = command->submit;
    int pasteSubmissions = 0;
    command->submit = [&] { ++pasteSubmissions; };
    QApplication::clipboard()->setText(QString::fromUtf8("$$$\n中文🙂=$$$"));
    command->clear();
    command->paste();
    const bool pastedSafely = command->toPlainText() == QString::fromUtf8("$$$\n中文🙂=$$$") && pasteSubmissions == 0;
    command->submit = pasteSubmit;
    page->findChild<QPushButton *>("copyAll")->click();
    const bool copied = QApplication::clipboard()->text() == output->toPlainText();
    QApplication::clipboard()->setMimeData(saved.release());
    if (!pastedSafely) return 27;
    if (!copied) return 8;
    page->findChild<QPushButton *>("clearOutput")->click();
    if (!output->toPlainText().isEmpty()) return 9;
    emit d->tabs->tabCloseRequested(1);
    if (d->tabs->count() != 1) return 10;
    // 最后一页不可关闭。
    emit d->tabs->tabCloseRequested(0);
    if (d->tabs->count() != 1) return 26;
    // Watch 面板：存在性、增行、转换确认、补全与移除。
    if (!d->watch) return 28;
    auto *watchTable = d->watch->findChild<QTableWidget *>("watchTable");
    auto *watchPeriod = d->watch->findChild<QComboBox *>("watchPeriod");
    auto *watchAsButton = d->watch->findChild<QCheckBox *>("watchAddAsButton");
    if (!watchTable || !watchPeriod || !watchAsButton) return 28;
    d->watch->addRowForTest("Motor[1].ActPos");
    if (d->watch->rowCountForTest() != 2 || d->watch->rowRequestForTest(0) != "Motor[1].ActPos") return 28;
    if (d->watch->convertRow(0, false) || d->watch->rowKindForTest(0) != 0) return 29;
    if (!d->watch->convertRow(0, true) || d->watch->rowKindForTest(0) != 1) return 30;
    QJsonArray watchFiles;
    watchFiles.append(QStringList{"1","Motor[]","Motor help","0","","","",""}.join(QChar(1)) + "\n");
    watchFiles.append(QStringList{"1","Motor[]","ActPos","Position help","0","","","","",""}.join(QChar(1)) + "\n");
    for (int i = 0; i < 5; ++i) watchFiles.append("");
    if (d->watch->loadTestSymbols(QJsonDocument(watchFiles).toJson(QJsonDocument::Compact)) != PMACT_OK) return 31;
    if (!d->watch->suggestForTest("Mot").contains("Motor[]")) return 31;
    d->watch->removeRowForTest(0);
    if (d->watch->rowCountForTest() != 1 || !d->watch->rowRequestForTest(0).isEmpty()) return 32;
    for (int i = 0; i < 5; ++i) d->watch->addRowForTest(QStringLiteral("P%1").arg(i + 1));
    if (d->watch->rowCountForTest() != 6) return 33;
    d->watch->retranslate();
    {
        const int languages = Tr::languages().size();
        if (languages != 8) return 100 + languages;
    }
    setVisible(false);
    if (d->host->isVisible()) return 11;
    setVisible(true);
    syncHost();
    QApplication::processEvents();
    syncHost();
    if (!d->host->isVisible()) return 12;
    return 0;
}
void TerminalPage::retranslate()
{
    const QString lang = d->backend ? d->backend->language() : QStringLiteral("en");
    if (d->addButton) d->addButton->setText(Tr::tr(lang, QStringLiteral("terminal.newTab")));
    if (d->hintLabel) d->hintLabel->setText(Tr::tr(lang, QStringLiteral("terminal.hint")));
    if (d->watch) d->watch->retranslate();
    if (d->tabs) for (int i = 0; i < d->tabs->count(); ++i)
        d->tabs->setTabText(i, Tr::tr(lang, QStringLiteral("terminal.tab")).arg(i + 1));
}
Backend *TerminalPage::backend() const { return d->backend; }
void TerminalPage::setBackend(Backend *backend)
{
    if (d->backend == backend) return;
    d->backend = backend;
    if (backend) connect(backend, &Backend::languageChanged, this, &TerminalPage::retranslate);
    if (d->tabs) for (int i = 0; i < d->tabs->count(); ++i)
        static_cast<Terminal *>(d->tabs->widget(i))->setBackend(backend);
    if (d->watch) d->watch->setBackend(backend);
    retranslate();
    emit backendChanged();
}
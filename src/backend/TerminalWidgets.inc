// 终端控件；共用部分见 TerminalWidgets.h。
#include "TerminalWidgets.h"
class Terminal : public QWidget {
    Api &api;
    pmact_handle handle = 0;
    QPointer<Backend> backend;
    bool pending = false;
    QString pendingId;
    QStringList queue;
    QCheckBox *timing;
    QPlainTextEdit *output;
    CommandEdit *input;
    QLabel *status;
    QPushButton *refreshButton = nullptr, *copyButton = nullptr, *clearButton = nullptr,
                *exportButton = nullptr, *logButton = nullptr, *fontButton = nullptr, *printButton = nullptr;
    QCheckBox *english = nullptr;
    QFile log;
    QList<QMetaObject::Connection> backendConnections;
    QString t(const char *key) const {
        return Tr::tr(backend ? backend->language() : QStringLiteral("en"), QString::fromLatin1(key));
    }
    void append(const QString &text, const QColor &color = TerminalColors::response) {
        auto cursor = output->textCursor(); cursor.movePosition(QTextCursor::End);
        QTextCharFormat format; format.setForeground(color);
        cursor.insertText(text + "\n", format);
        output->setTextCursor(cursor); output->ensureCursorVisible();
        if (log.isOpen() && log.write((text + "\n").toUtf8()) < 0) {
            log.close(); status->setText(t("terminal.logWriteFailed"));
        }
    }
    void retranslate() {
        refreshButton->setText(t("terminal.refresh"));
        copyButton->setText(t("terminal.copyAll"));
        clearButton->setText(t("terminal.clear"));
        exportButton->setText(t("terminal.export"));
        logButton->setText(t(log.isOpen() ? "terminal.logStop" : "terminal.log"));
        fontButton->setText(t("terminal.font"));
        printButton->setText(t("terminal.print"));
        timing->setText(t("terminal.timing"));
        english->setText(t("terminal.autoEnglish"));
        english->setToolTip(t("terminal.autoEnglishTip"));
        input->setPlaceholderText(t("terminal.placeholder"));
        updateState();
    }
    void updateState() {
        const bool connected = backend && backend->connectionState() == "connected";
        input->setReadOnly(pending || (backend && backend->busy()));
        refreshButton->setEnabled(connected && !pending && !backend->busy());
        if (!connected) {
            pending = false; pendingId.clear(); queue.clear();
            input->completer.popup()->hide(); input->model.setStringList({});
            status->setText(t("terminal.notConnected"));
        } else if (queue.isEmpty()) {
            status->setText(pending ? t("terminal.waiting") : t("terminal.connected"));
        } else {
            status->setText(t("terminal.waitingRemaining").arg(queue.size()));
        }
    }
    void importSymbols() {
        input->completer.popup()->hide(); input->model.setStringList({}); input->help.clear();
        api.setSymbols(handle, reinterpret_cast<const uint8_t *>("[]"), 2);
        if (!backend || backend->connectionState() != "connected") return;
        const auto files = backend->terminalSymbols();
        if (files.isEmpty()) {
            if (!backend->terminalSymbolsError().isEmpty())
                append(t("terminal.suggestFailed").arg(backend->terminalSymbolsError()), TerminalColors::error);
            return;
        }
        QJsonArray array; for (const auto &file : files) array.append(file);
        const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
        const int code = api.setSymbols(handle, reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
        if (code == PMACT_OK) status->setText(t("terminal.suggestReady"));
        else append(t("terminal.symbolParseFailed").arg(code), TerminalColors::error);
    }
    void startLine(const QString &line) {
        pending = true; pendingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        append(line, TerminalColors::typed());
        backend->sendTerminalCommand(pendingId, line);
    }
    void send() {
        if (pending || !backend || backend->connectionState() != "connected" || backend->busy()) return;
        QStringList lines;
        for (const auto &raw : input->toPlainText().split(QLatin1Char('\n')))
            if (const auto line = raw.trimmed(); !line.isEmpty()) lines << line;
        if (lines.isEmpty()) return;
        input->remember(lines.join(QLatin1Char('\n'))); input->clear();
        queue = lines.mid(1);
        startLine(lines.first());
        updateState();
    }
    QByteArray candidates(const QByteArray &prefix) {
        size_t n = 0;
        int code = api.suggest(handle, reinterpret_cast<const uint8_t *>(prefix.constData()), size_t(prefix.size()), nullptr, 0, &n);
        if ((code != PMACT_BUFFER_TOO_SMALL && code != PMACT_OK) || n > 16 * 1024 * 1024) return {};
        QByteArray bytes(qsizetype(n), '\0');
        code = api.suggest(handle, reinterpret_cast<const uint8_t *>(prefix.constData()), size_t(prefix.size()),
            reinterpret_cast<uint8_t *>(bytes.data()), size_t(bytes.size()), &n);
        return code == PMACT_OK ? bytes : QByteArray();
    }
public:
    bool hasPendingRequest() const { return pending || !queue.isEmpty(); }
    void hideCompletion() { input->hideCompletion(); }
    void focusInput(Qt::FocusReason reason = Qt::OtherFocusReason) { input->setFocus(reason); }
    explicit Terminal(Api &api, Backend *connection, QWidget *parent = nullptr) : QWidget(parent), api(api) {
        const auto path = (QCoreApplication::applicationDirPath() + "/pmac.dll").toUtf8();
        if (api.create(reinterpret_cast<const uint8_t *>(path.constData()), size_t(path.size()), &handle) != PMACT_OK)
            throw std::runtime_error("pmact_create failed");
        auto layout = new QVBoxLayout(this); layout->setContentsMargins(0, 8, 0, 0);
        auto tools = new QHBoxLayout;
        refreshButton = new QPushButton; refreshButton->setObjectName("refreshSymbols");
        tools->addWidget(refreshButton);
        copyButton = new QPushButton; copyButton->setObjectName("copyAll");
        clearButton = new QPushButton; clearButton->setObjectName("clearOutput");
        exportButton = new QPushButton;
        logButton = new QPushButton;
        fontButton = new QPushButton;
        printButton = new QPushButton;
        for (auto b : {copyButton, clearButton, exportButton, logButton, fontButton, printButton}) tools->addWidget(b);
        tools->addStretch();
        timing = new QCheckBox; timing->setObjectName("responseTiming"); timing->setChecked(false);
        english = new QCheckBox;
        english->setObjectName("autoEnglish");
        english->setChecked(true);
        layout->addLayout(tools);
        // 复选框单独一行，终端变窄时不会被挤出右侧边框。
        auto *optionsRow = new QHBoxLayout;
        optionsRow->addWidget(timing);
        optionsRow->addWidget(english);
        optionsRow->addStretch();
        layout->addLayout(optionsRow);
        auto splitter = new QSplitter(Qt::Vertical);
        output = new QPlainTextEdit; output->setObjectName("terminalOutput");
        TerminalColors::apply(output, TerminalColors::background, TerminalColors::response);
        output->setReadOnly(true); output->setMaximumBlockCount(20000);
        output->setFont(TerminalColors::terminalFont());
        input = new CommandEdit; input->setFont(output->font()); input->completer.popup()->setFont(output->font());
        connect(english, &QCheckBox::toggled, input, &CommandEdit::setAutoEnglish);
        splitter->addWidget(output); splitter->addWidget(input); splitter->setSizes({520,130});
        layout->addWidget(splitter, 1);
        status = new QLabel; status->setTextFormat(Qt::PlainText);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status);
        connect(refreshButton, &QPushButton::clicked, this, [this] { if (backend) backend->refreshTerminalSymbols(); });
        connect(copyButton, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(output->toPlainText()); });
        connect(clearButton, &QPushButton::clicked, output, &QPlainTextEdit::clear);
        connect(exportButton, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getSaveFileName(this, t("terminal.exportTitle"), "terminal.txt", "Text (*.txt)");
            if (path.isEmpty()) return;
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(output->toPlainText().toUtf8()) < 0)
                QMessageBox::warning(this, t("terminal.exportFailed"), file.errorString());
        });
        connect(logButton, &QPushButton::clicked, this, [this] {
            if (log.isOpen()) { log.close(); retranslate(); return; }
            const auto path = QFileDialog::getSaveFileName(this, t("terminal.logTitle"), "session.log", "Log (*.log)");
            if (path.isEmpty()) return;
            log.setFileName(path);
            if (!log.open(QIODevice::WriteOnly | QIODevice::Append))
                QMessageBox::warning(this, t("terminal.logFailed"), log.errorString());
            else retranslate();
        });
        connect(fontButton, &QPushButton::clicked, this, [this] {
            bool ok = false; const auto font = QFontDialog::getFont(&ok, output->font(), this);
            if (ok) { output->setFont(font); input->setFont(font); input->completer.popup()->setFont(font); }
        });
        connect(printButton, &QPushButton::clicked, this, [this] {
            QPrinter printer; QPrintDialog dialog(&printer, this);
            if (dialog.exec() == QDialog::Accepted) output->print(&printer);
        });
        input->submit = [this] { send(); };
        input->complete = [this](const QString &prefix) {
            QStringList choices; input->help.clear();
            for (const auto &item : QJsonDocument::fromJson(candidates(prefix.toUtf8())).array()) {
                const auto obj = item.toObject(); const auto name = obj.value("text").toString();
                choices << name; input->help.insert(name, obj.value("help").toString());
            }
            return choices;
        };
        setBackend(connection);
    }
    void setBackend(Backend *connection) {
        for (const auto &c : backendConnections) QObject::disconnect(c);
        backendConnections.clear(); backend = connection; pending = false; pendingId.clear(); queue.clear();
        if (backend) {
            backendConnections << connect(backend, &Backend::connectionStateChanged, this, [this] {
                pending = false; pendingId.clear(); updateState(); importSymbols();
            });
            backendConnections << connect(backend, &Backend::busyChanged, this, [this] { updateState(); });
            backendConnections << connect(backend, &Backend::languageChanged, this, [this] { retranslate(); });
            backendConnections << connect(backend, &Backend::terminalSymbolsChanged, this, [this] { importSymbols(); });
            backendConnections << connect(backend, &QObject::destroyed, this, [this] {
                backend = nullptr; updateState(); importSymbols();
            });
            backendConnections << connect(backend, &Backend::terminalResponse, this,
                [this](const QString &id, const QString &text, double elapsed, const QString &error) {
                    if (!pending || id != pendingId) return;
                    pending = false; pendingId.clear();
                    if (!text.isEmpty()) for (const auto &line : text.split('\n'))
                        append(line, line.contains("error #", Qt::CaseInsensitive) ? TerminalColors::error : ((line.contains("warning", Qt::CaseInsensitive) || line.contains(QStringLiteral("警告"))) ? TerminalColors::message : TerminalColors::response));
                    if (!error.isEmpty()) append(error, TerminalColors::error);
                    if (timing->isChecked()) append(t("terminal.timingLine").arg(elapsed, 0, 'f', 3), TerminalColors::message);
                    if (!error.isEmpty()) {
                        // 出错即停止，未发送的行放回输入框供修改后重发。
                        if (!queue.isEmpty() && input->toPlainText().isEmpty())
                            input->setPlainText(queue.join(QLatin1Char('\n')));
                        queue.clear();
                    } else if (!queue.isEmpty()) {
                        startLine(queue.takeFirst());
                    }
                    updateState();
                });
        }
        retranslate(); importSymbols();
    }
    int loadTestSymbols(const QByteArray &json) {
        return api.setSymbols(handle, reinterpret_cast<const uint8_t *>(json.constData()), size_t(json.size()));
    }
    ~Terminal() override { if (handle) api.destroy(handle); }
};

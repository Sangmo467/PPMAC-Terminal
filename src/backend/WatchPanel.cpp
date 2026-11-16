#include "WatchPanel.h"
#include "Backend.h"
#include "Translator.h"

#include <QtWidgets>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int kPollFloorMs = 50;
constexpr int kPollWatchdogMs = 10000;
constexpr int kMaxWatchRows = 100;

// 从 "对象=值" 中只保留数值部分，避免显示整条响应。
QString extractValueText(const QString &line)
{
    QString value = line.trimmed();
    const int equals = value.indexOf(QLatin1Char('='));
    if (equals >= 0) value = value.mid(equals + 1).trimmed();
    return value;
}

// 从 "对象=值"、裸数值或 $/0x 十六进制中取数值与位模式。
bool parseResponseValue(const QString &line, double *number, quint64 *bits)
{
    const QString value = extractValueText(line);
    if (value.isEmpty()) return false;
    bool ok = false;
    if (value.startsWith(QLatin1Char('$'))) {
        *bits = value.mid(1).toULongLong(&ok, 16);
        if (!ok) return false;
        *number = double(*bits);
        return true;
    }
    if (value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        *bits = value.mid(2).toULongLong(&ok, 16);
        if (!ok) return false;
        *number = double(*bits);
        return true;
    }
    const double real = value.toDouble(&ok);
    if (ok) { *number = real; *bits = quint64(real); return true; }
    const qlonglong integer = value.toLongLong(&ok);
    if (ok) { *number = double(integer); *bits = quint64(integer); return true; }
    return false;
}

// 支持鼠标拖动调整行序，Delete 删除选中行。
class WatchTable : public QTableWidget {
public:
    std::function<void(int, int)> moveRow;
    std::function<void()> deleteSelected;
    explicit WatchTable(QWidget *parent = nullptr) : QTableWidget(0, 3, parent) {}
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Delete && deleteSelected) {
            deleteSelected();
            event->accept();
            return;
        }
        // 最后一行为空占位行：直接键入即可开始编辑，像原始 Watch 表一样。
        if (currentRow() == rowCount() - 1 && currentRow() >= 0
            && event->text().size() == 1 && event->text().at(0).isPrint()) {
            auto *item = this->item(currentRow(), 0);
            if (item && item->text().isEmpty()) {
                editItem(item);
                const auto text = event->text();
                QTimer::singleShot(0, this, [this, text] {
                    if (auto *editor = findChild<QPlainTextEdit *>("watchCellEditor")) {
                        editor->insertPlainText(text);
                        editor->setFocus();
                    }
                });
                event->accept();
                return;
            }
        }
        QTableWidget::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            dragRow_ = rowAt(int(event->position().y()));
            start_ = event->position().toPoint();
        }
        QTableWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if ((event->buttons() & Qt::LeftButton) && dragRow_ >= 0
            && (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
            const int row = rowAt(int(event->position().y()));
            targetRow_ = row >= 0 ? row
                : (event->position().y() < 0 ? 0 : rowCount() - 1);
            viewport()->update();
        }
        QTableWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const bool moved = dragRow_ >= 0 && targetRow_ >= 0 && targetRow_ != dragRow_
            && (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance();
        const int from = dragRow_, to = targetRow_;
        dragRow_ = targetRow_ = -1;
        viewport()->update();
        if (moved && moveRow) { moveRow(from, to); return; }
        QTableWidget::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *event) override {
        QTableWidget::paintEvent(event);
        if (targetRow_ < 0) return;
        QPainter painter(viewport());
        painter.setPen(QPen(QColor("#3fa7f5"), 2));
        const int y = rowViewportPosition(targetRow_);
        painter.drawLine(0, y, viewport()->width(), y);
    }
private:
    int dragRow_ = -1, targetRow_ = -1;
    QPoint start_;
};

// 表格内编辑请求列，使用与终端一致的 CommandEdit/提词器。
class WatchEditDelegate : public QStyledItemDelegate {
public:
    std::function<QStringList(const QString &, QHash<QString, QString> *)> complete;
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        auto *editor = new CommandEdit(parent);
        editor->setObjectName("watchCellEditor");
        editor->setMaximumBlockCount(1);
        editor->setTabChangesFocus(true);
        editor->setFont(TerminalColors::terminalFont());
        editor->completer.popup()->setFont(editor->font());
        auto *self = const_cast<WatchEditDelegate *>(this);
        editor->complete = [self, editor](const QString &prefix) {
            return self->complete ? self->complete(prefix, &editor->help) : QStringList();
        };
        editor->submit = [self, editor] {
            emit self->commitData(editor);
            emit self->closeEditor(editor, QAbstractItemDelegate::SubmitModelCache);
        };
        return editor;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto *edit = static_cast<CommandEdit *>(editor);
        if (!edit) return;
        edit->setPlainText(index.data(Qt::EditRole).toString());
        edit->moveCursor(QTextCursor::End);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        auto *edit = static_cast<CommandEdit *>(editor);
        if (!edit) return;
        const auto text = edit->toPlainText().trimmed();
        model->setData(index, text, Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override {
        editor->setGeometry(option.rect);
    }
};
}

struct WatchPanel::Impl {
    using RowKind = WatchPanel::RowKind;
    enum class Format { Auto, Float, Hex, Fixed, Binary };
    explicit Impl(Api &api) : api(api) {}
    Api &api;
    pmact_handle handle = 0;
    QPointer<Backend> backend;
    std::function<bool()> terminalBusy;
    QComboBox *periodCombo = nullptr;
    QCheckBox *pauseBox = nullptr;
    QLabel *titleLabel = nullptr, *periodLabel = nullptr;
    QCheckBox *addAsButton = nullptr;
    QTableWidget *table = nullptr;
    int placeholderIndex = -1;
    QTimer pollTimer, watchdog;
    QList<QMetaObject::Connection> connections;
    QHash<QString, int> inflight;   // request id -> row
    QVector<RowKind> kinds;
    QVector<Format> formats;
    QVector<quint64> masks;
    QVector<QString> raw;
    QString pollId;
    int nextQueryRow = 0;
    int periodMs = 100;
    bool updating = false;
};

WatchPanel::WatchPanel(Api &api, Backend *backend, std::function<bool()> terminalBusy,
                       QWidget *parent)
    : QWidget(parent), d(std::make_unique<Impl>(api))
{
    d->terminalBusy = std::move(terminalBusy);
    const auto path = (QCoreApplication::applicationDirPath() + "/pmac.dll").toUtf8();
    if (d->api.create(reinterpret_cast<const uint8_t *>(path.constData()), size_t(path.size()),
                      &d->handle) != PMACT_OK)
        d->handle = 0;
    setWindowTitle(QStringLiteral("Watch"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 4, 0);
    d->titleLabel = new QLabel;
    d->titleLabel->setObjectName("watchTitle");
    layout->addWidget(d->titleLabel);

    auto *options = new QHBoxLayout;
    d->periodLabel = new QLabel;
    d->periodCombo = new QComboBox;
    d->periodCombo->setObjectName("watchPeriod");
    for (const auto &period : {100, 500, 1000, 2000, 5000}) {
        d->periodCombo->addItem(QStringLiteral("%1 s").arg(period / 1000.0), period);
    }
    d->periodCombo->setCurrentIndex(0);
    d->pauseBox = new QCheckBox;
    d->pauseBox->setObjectName("watchPause");
    d->addAsButton = new QCheckBox;
    d->addAsButton->setObjectName("watchAddAsButton");
    options->addWidget(d->periodLabel);
    options->addWidget(d->periodCombo);
    options->addWidget(d->pauseBox);
    options->addWidget(d->addAsButton);
    options->addStretch();
    layout->addLayout(options);

    auto *watchTable = new WatchTable;
    watchTable->moveRow = [this](int from, int to) { moveRow(from, to); };
    watchTable->deleteSelected = [this] { removeSelectedRows(); };
    d->table = watchTable;
    d->table->setObjectName("watchTable");
    d->table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->table->setFont(TerminalColors::terminalFont());
    d->table->verticalHeader()->setVisible(false);
    d->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->table->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked
                              | QAbstractItemView::EditKeyPressed);
    d->table->setContextMenuPolicy(Qt::CustomContextMenu);
    d->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    d->table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    d->table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    d->table->setColumnWidth(0, 210);
    // 表格固定为白底黑字，避免深色背景下数值看不清。
    QPalette tablePalette = d->table->palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        tablePalette.setColor(group, QPalette::Base, QColor("#ffffff"));
        tablePalette.setColor(group, QPalette::AlternateBase, QColor("#f5f7fa"));
        tablePalette.setColor(group, QPalette::Text, QColor("#000000"));
        tablePalette.setColor(group, QPalette::Button, QColor("#eef1f5"));
        tablePalette.setColor(group, QPalette::ButtonText, QColor("#000000"));
        tablePalette.setColor(group, QPalette::Highlight, QColor("#0078d7"));
        tablePalette.setColor(group, QPalette::HighlightedText, QColor("#ffffff"));
    }
    d->table->setPalette(tablePalette);
    auto *delegate = new WatchEditDelegate(d->table);
    delegate->complete = [this](const QString &prefix, QHash<QString, QString> *help) {
        return suggestFor(prefix, help);
    };
    d->table->setItemDelegateForColumn(0, delegate);
    layout->addWidget(d->table, 1);
    setMinimumWidth(320);

    connect(d->periodCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (d->updating) return;
        d->periodMs = d->periodCombo->currentData().toInt();
        schedulePolling();
    });
    connect(d->pauseBox, &QCheckBox::toggled, this, [this] { schedulePolling(); });
    connect(d->table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (d->updating || !item || item->column() != 0) return;
        const int row = item->row();
        if (d->kinds.value(row) != RowKind::Query) return;
        const auto text = item->text().trimmed();
        if (text.isEmpty()) {
            setResponse(row, QString(), false);
            schedulePolling();
            return;
        }
        const bool newEntry = row == d->table->rowCount() - 1;
        if (newEntry && realRowCount() > kMaxWatchRows) {
            d->updating = true;
            item->setText(QString());
            d->updating = false;
            return;
        }
        d->updating = true;
        item->setText(text);
        if (!newEntry && d->table->item(row, 2)) {
            d->table->item(row, 2)->setText(t("watch.query"));
            d->table->item(row, 2)->setForeground(QBrush(QColor("#333333")));
        }
        d->updating = false;
        if (newEntry && d->addAsButton->isChecked()) {
            // 直接在表格中新建为按钮行，不经过查询行。
            attachButton(row, text);
        } else {
            schedulePolling();
            refreshRow(row);
        }
        ensurePlaceholder();
    });
    connect(d->table, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = d->table->indexAt(position);
        const int row = index.row();
        if (row < 0) return;
        QMenu menu(this);
        if (index.column() == 1) {
            auto *formats = menu.addMenu(t("watch.format"));
            auto *group = new QActionGroup(formats);
            const auto addFormat = [&](const char *key, Impl::Format value) {
                auto *action = formats->addAction(t(key));
                action->setCheckable(true);
                action->setChecked(d->formats.value(row) == value);
                group->addAction(action);
                connect(action, &QAction::triggered, this, [this, row, value] {
                    if (value == Impl::Format::Binary) { chooseFormat(row); return; }
                    d->formats[row] = value;
                    applyFormat(row);
                });
            };
            addFormat("watch.fmtAuto", Impl::Format::Auto);
            addFormat("watch.fmtFloat", Impl::Format::Float);
            addFormat("watch.fmtHex", Impl::Format::Hex);
            addFormat("watch.fmtFixed", Impl::Format::Fixed);
            addFormat("watch.fmtBinary", Impl::Format::Binary);
            menu.addSeparator();
        }
        if (d->kinds.value(row) == RowKind::Button) {
            menu.addAction(t("watch.makeQuery"), this, [this, row] {
                const auto request = d->table->item(row, 0) ? d->table->item(row, 0)->text() : QString();
                if (QMessageBox::question(this, t("watch.confirmQueryTitle"),
                                          t("watch.confirmQueryText").arg(request),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                    == QMessageBox::Yes)
                    makeQuery(row);
            });
        } else {
            // 查询转按钮不需要确认。
            menu.addAction(t("watch.makeButton"), this, [this, row] { convertRow(row, true); });
        }
        menu.addAction(t("watch.refreshNow"), this, [this, row] { refreshRow(row); });
        menu.addSeparator();
        menu.addAction(t("watch.removeRow"), this, [this, row] { removeRow(row); });
        menu.exec(d->table->viewport()->mapToGlobal(position));
    });
    connect(&d->pollTimer, &QTimer::timeout, this, [this] { pollTick(); });
    d->watchdog.setSingleShot(true);
    connect(&d->watchdog, &QTimer::timeout, this, [this] {
        if (d->pollId.isEmpty()) return;
        auto it = d->inflight.find(d->pollId);
        if (it != d->inflight.end()) {
            const int row = it.value();
            d->inflight.erase(it);
            setResponse(row, t("watch.timeout"), true);
        }
        d->pollId.clear();
        schedulePolling();
    });

    ensurePlaceholder();
    retranslate();
    setBackend(backend);
}

WatchPanel::~WatchPanel()
{
    if (d->handle) d->api.destroy(d->handle);
}

QString WatchPanel::t(const char *key) const
{
    return Tr::tr(d->backend ? d->backend->language() : QStringLiteral("en"),
                  QString::fromLatin1(key));
}

void WatchPanel::setBackend(Backend *backend)
{
    for (const auto &connection : d->connections) QObject::disconnect(connection);
    d->connections.clear();
    d->backend = backend;
    d->inflight.clear();
    d->pollId.clear();
    d->watchdog.stop();
    d->pollTimer.stop();
    if (backend) {
        d->connections << connect(backend, &Backend::terminalResponse, this,
            [this](const QString &id, const QString &text, double, const QString &error) {
                auto it = d->inflight.find(id);
                if (it == d->inflight.end()) return;
                const int row = it.value();
                d->inflight.erase(it);
                if (id == d->pollId) { d->pollId.clear(); d->watchdog.stop(); }
                setResponse(row, error.isEmpty() ? text : error, !error.isEmpty());
                schedulePolling();
            });
        d->connections << connect(backend, &Backend::connectionStateChanged, this, [this] {
            if (!d->backend || d->backend->connectionState() != QLatin1String("connected")) {
                for (auto it = d->inflight.begin(); it != d->inflight.end(); ++it)
                    setResponse(it.value(), t("watch.notConnected"), true);
                d->inflight.clear();
                d->pollId.clear();
                d->watchdog.stop();
            }
            schedulePolling();
        });
        d->connections << connect(backend, &Backend::busyChanged, this, [this] { schedulePolling(); });
        d->connections << connect(backend, &Backend::terminalSymbolsChanged, this, [this] { importSymbols(); });
        d->connections << connect(backend, &Backend::languageChanged, this, [this] { retranslate(); });
        d->connections << connect(backend, &QObject::destroyed, this, [this] {
            d->backend = nullptr;
            d->inflight.clear();
            d->pollId.clear();
            d->watchdog.stop();
            d->pollTimer.stop();
            retranslate();
        });
    }
    retranslate();
    importSymbols();
    schedulePolling();
}

void WatchPanel::retranslate()
{
    d->titleLabel->setText(t("watch.title"));
    d->periodLabel->setText(t("watch.period"));
    d->pauseBox->setText(t("watch.pause"));
    d->addAsButton->setText(t("watch.addAsButton"));
    d->table->setHorizontalHeaderLabels({t("watch.request"), t("watch.response"), t("watch.type")});
    for (int row = 0; row < d->table->rowCount(); ++row) {
        const auto *requestItem = d->table->item(row, 0);
        const bool placeholder = !requestItem || requestItem->text().trimmed().isEmpty();
        if (auto *type = d->table->item(row, 2))
            type->setText(placeholder ? QString()
                                      : t(d->kinds.value(row) == RowKind::Button
                                              ? "watch.commandButton" : "watch.query"));
        if (auto *button = qobject_cast<QPushButton *>(d->table->cellWidget(row, 0)))
            button->setToolTip(t("watch.buttonTooltip"));
    }
    d->updating = true;
    for (int i = 0; i < d->periodCombo->count(); ++i)
        d->periodCombo->setItemText(i, QStringLiteral("%1 s").arg(d->periodCombo->itemData(i).toInt() / 1000.0));
    d->updating = false;
}

void WatchPanel::importSymbols()
{
    if (!d->handle) return;
    d->api.setSymbols(d->handle, reinterpret_cast<const uint8_t *>("[]"), 2);
    if (!d->backend || d->backend->connectionState() != QLatin1String("connected")) return;
    const auto files = d->backend->terminalSymbols();
    if (files.isEmpty()) return;
    QJsonArray array;
    for (const auto &file : files) array.append(file);
    const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
    d->api.setSymbols(d->handle, reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
}

QStringList WatchPanel::suggestFor(const QString &prefix, QHash<QString, QString> *help) const
{
    QStringList choices;
    if (help) help->clear();
    for (const auto &item : QJsonDocument::fromJson(candidates(prefix.toUtf8())).array()) {
        const auto obj = item.toObject();
        const auto name = obj.value("text").toString();
        choices << name;
        if (help) help->insert(name, obj.value("help").toString());
    }
    return choices;
}

QByteArray WatchPanel::candidates(const QByteArray &prefix) const
{
    if (!d->handle) return {};
    size_t required = 0;
    int code = d->api.suggest(d->handle, reinterpret_cast<const uint8_t *>(prefix.constData()),
                              size_t(prefix.size()), nullptr, 0, &required);
    if ((code != PMACT_BUFFER_TOO_SMALL && code != PMACT_OK) || required > 16 * 1024 * 1024) return {};
    QByteArray bytes(qsizetype(required), '\0');
    code = d->api.suggest(d->handle, reinterpret_cast<const uint8_t *>(prefix.constData()),
                          size_t(prefix.size()), reinterpret_cast<uint8_t *>(bytes.data()),
                          size_t(bytes.size()), &required);
    return code == PMACT_OK ? bytes : QByteArray();
}

void WatchPanel::addRow(const QString &request, bool asButton)
{
    const auto text = request.trimmed();
    if (text.isEmpty()) return;
    for (int row = 0; row < d->table->rowCount(); ++row) {
        auto *existing = d->table->item(row, 0);
        if (existing && !existing->text().trimmed().isEmpty()
            && existing->text().compare(text, Qt::CaseInsensitive) == 0) {
            if (asButton && d->kinds.value(row) == RowKind::Query) attachButton(row, existing->text());
            else refreshRow(row);
            return;
        }
    }
    if (realRowCount() >= kMaxWatchRows) return;
    int row = placeholderRow();
    d->updating = true;
    if (row < 0) {
        row = d->table->rowCount();
        d->table->insertRow(row);
        auto *requestItem = new QTableWidgetItem(text);
        requestItem->setForeground(QBrush(QColor("#000000")));
        auto *responseItem = new QTableWidgetItem;
        responseItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        auto *typeItem = new QTableWidgetItem;
        typeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        typeItem->setForeground(QBrush(QColor("#333333")));
        d->table->setItem(row, 0, requestItem);
        d->table->setItem(row, 1, responseItem);
        d->table->setItem(row, 2, typeItem);
        d->kinds.append(RowKind::Query);
        d->formats.append(Impl::Format::Auto);
        d->masks.append(~quint64(0));
        d->raw.append(QString());
    } else {
        d->table->item(row, 0)->setText(text);
        if (auto *response = d->table->item(row, 1)) response->setText(QString());
        d->kinds[row] = RowKind::Query;
        d->formats[row] = Impl::Format::Auto;
        d->masks[row] = ~quint64(0);
        d->raw[row].clear();
    }
    d->updating = false;
    // 显式勾选“添加为按钮”即视为已确认，直接按按钮行创建。
    if (asButton) attachButton(row, text);
    else refreshRow(row);
    ensurePlaceholder();
    schedulePolling();
}

int WatchPanel::placeholderRow() const
{
    const int last = d->table->rowCount() - 1;
    if (last < 0) return -1;
    const auto *item = d->table->item(last, 0);
    return item && item->text().trimmed().isEmpty() ? last : -1;
}

int WatchPanel::realRowCount() const
{
    int count = 0;
    for (int row = 0; row < d->table->rowCount(); ++row) {
        const auto *item = d->table->item(row, 0);
        if (item && !item->text().trimmed().isEmpty()) ++count;
    }
    return count;
}

void WatchPanel::ensurePlaceholder()
{
    const int last = d->table->rowCount() - 1;
    if (last >= 0) {
        auto *item = d->table->item(last, 0);
        if (item && item->text().trimmed().isEmpty()) {
            d->placeholderIndex = last;
            if (auto *type = d->table->item(last, 2)) {
                d->updating = true;
                type->setText(QString());
                d->updating = false;
            }
            return;
        }
    }
    const int row = d->table->rowCount();
    d->updating = true;
    d->table->insertRow(row);
    auto *requestItem = new QTableWidgetItem(QString());
    requestItem->setForeground(QBrush(QColor("#000000")));
    auto *responseItem = new QTableWidgetItem;
    responseItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    auto *typeItem = new QTableWidgetItem(QString());
    typeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    typeItem->setForeground(QBrush(QColor("#333333")));
    d->table->setItem(row, 0, requestItem);
    d->table->setItem(row, 1, responseItem);
    d->table->setItem(row, 2, typeItem);
    d->kinds.append(RowKind::Query);
    d->formats.append(Impl::Format::Auto);
    d->masks.append(~quint64(0));
    d->raw.append(QString());
    d->updating = false;
    d->placeholderIndex = row;
}

bool WatchPanel::convertRow(int row, bool confirmed)
{
    if (row < 0 || row >= d->table->rowCount() || !confirmed) return false;
    if (d->kinds.value(row) != RowKind::Query) return false;
    auto *requestItem = d->table->item(row, 0);
    if (!requestItem) return false;
    attachButton(row, requestItem->text());
    schedulePolling();
    return true;
}

void WatchPanel::attachButton(int row, const QString &request)
{
    auto *button = new QPushButton(request);
    button->setObjectName("watchButton");
    button->setToolTip(t("watch.buttonTooltip"));
    connect(button, &QPushButton::clicked, this, [this, button] {
        for (int current = 0; current < d->table->rowCount(); ++current)
            if (d->table->cellWidget(current, 0) == button) { sendRow(current); return; }
    });
    d->table->setCellWidget(row, 0, button);
    if (row < d->kinds.size()) d->kinds[row] = RowKind::Button;
    d->updating = true;
    if (auto *type = d->table->item(row, 2)) type->setText(t("watch.commandButton"));
    d->updating = false;
}

void WatchPanel::makeQuery(int row)
{
    if (row < 0 || row >= d->table->rowCount() || d->kinds.value(row) != RowKind::Button) return;
    if (auto *widget = d->table->cellWidget(row, 0)) {
        d->table->removeCellWidget(row, 0);
        widget->deleteLater();
    }
    d->kinds[row] = RowKind::Query;
    d->updating = true;
    if (auto *type = d->table->item(row, 2)) type->setText(t("watch.query"));
    d->updating = false;
    schedulePolling();
    refreshRow(row);
}

void WatchPanel::removeRow(int row)
{
    if (row < 0 || row >= d->table->rowCount()) return;
    if (auto *widget = d->table->cellWidget(row, 0)) {
        d->table->removeCellWidget(row, 0);
        widget->deleteLater();
    }
    for (auto it = d->inflight.begin(); it != d->inflight.end();) {
        if (it.value() == row) it = d->inflight.erase(it);
        else { if (it.value() > row) --it.value(); ++it; }
    }
    d->updating = true;
    d->table->removeRow(row);
    d->kinds.removeAt(row);
    d->formats.removeAt(row);
    d->masks.removeAt(row);
    d->raw.removeAt(row);
    d->updating = false;
    if (d->nextQueryRow > row) --d->nextQueryRow;
    if (d->nextQueryRow >= d->table->rowCount()) d->nextQueryRow = 0;
    ensurePlaceholder();
    schedulePolling();
}

void WatchPanel::removeSelectedRows()
{
    if (!d->table->selectionModel()) return;
    QList<int> rows;
    for (const auto &index : d->table->selectionModel()->selectedRows()) rows << index.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (const int row : rows) removeRow(row);
}

void WatchPanel::moveRow(int from, int to)
{
    const int rows = d->table->rowCount();
    if (from < 0 || from >= rows || to < 0 || to >= rows || from == to) return;
    const auto request = d->table->item(from, 0) ? d->table->item(from, 0)->text() : QString();
    if (request.trimmed().isEmpty()) return;
    const auto raw = d->raw.value(from);
    const auto kind = d->kinds.value(from);
    const auto format = d->formats.value(from);
    const auto mask = d->masks.value(from);
    for (auto it = d->inflight.begin(); it != d->inflight.end(); ++it) {
        int &value = it.value();
        if (value == from) value = to;
        else if (from < to && value > from && value <= to) --value;
        else if (from > to && value >= to && value < from) ++value;
    }
    if (auto *widget = d->table->cellWidget(from, 0)) {
        d->table->removeCellWidget(from, 0);
        widget->deleteLater();
    }
    d->updating = true;
    d->table->removeRow(from);
    d->table->insertRow(to);
    auto *requestItem = new QTableWidgetItem(request);
    requestItem->setForeground(QBrush(QColor("#000000")));
    auto *responseItem = new QTableWidgetItem;
    responseItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    auto *typeItem = new QTableWidgetItem(t(kind == RowKind::Button ? "watch.commandButton" : "watch.query"));
    typeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    typeItem->setForeground(QBrush(QColor("#333333")));
    d->table->setItem(to, 0, requestItem);
    d->table->setItem(to, 1, responseItem);
    d->table->setItem(to, 2, typeItem);
    d->kinds.removeAt(from); d->kinds.insert(to, kind);
    d->formats.removeAt(from); d->formats.insert(to, format);
    d->masks.removeAt(from); d->masks.insert(to, mask);
    d->raw.removeAt(from); d->raw.insert(to, raw);
    d->updating = false;
    if (kind == RowKind::Button) attachButton(to, request);
    applyFormat(to);
    d->table->selectRow(to);
    d->nextQueryRow = 0;
    ensurePlaceholder();
    schedulePolling();
}

void WatchPanel::setResponse(int row, const QString &text, bool error)
{
    if (row < 0 || row >= d->table->rowCount()) return;
    if (row < d->raw.size()) d->raw[row] = text;
    auto *item = d->table->item(row, 1);
    if (!item) return;
    if (error) {
        QString first = text;
        const int newline = first.indexOf(QLatin1Char('\n'));
        if (newline >= 0) first = first.left(newline);
        item->setText(first.trimmed());
        item->setToolTip(text);
        auto font = item->font();
        font.setBold(true);
        item->setFont(font);
        item->setForeground(QBrush(QColor("#c62828")));
        return;
    }
    applyFormat(row);
}

void WatchPanel::applyFormat(int row)
{
    if (row < 0 || row >= d->table->rowCount()) return;
    auto *item = d->table->item(row, 1);
    if (!item) return;
    QString first = d->raw.value(row);
    const int newline = first.indexOf(QLatin1Char('\n'));
    if (newline >= 0) first = first.left(newline);
    QString display = first.trimmed();
    double number = 0;
    quint64 bits = 0;
    if (parseResponseValue(first, &number, &bits)) {
        switch (d->formats.value(row, Impl::Format::Auto)) {
        case Impl::Format::Float:
            display = QString::number(number, 'g', 10);
            break;
        case Impl::Format::Hex: {
            // 不补前导零，只保留有效位。
            display = QStringLiteral("0x") + QString::number(bits, 16).toUpper();
            break;
        }
        case Impl::Format::Fixed:
            display = QString::number(qint64(bits));
            break;
        case Impl::Format::Binary: {
            const quint64 mask = d->masks.value(row, ~quint64(0));
            int width = 1;
            for (int bit = 63; bit > 0; --bit)
                if (mask & (quint64(1) << bit)) { width = bit + 1; break; }
            display = QString::number(bits & mask, 2).rightJustified(width, QLatin1Char('0'));
            break;
        }
        case Impl::Format::Auto:
            // 默认只显示提取出的数值，不显示整条响应。
            display = extractValueText(first);
            break;
        }
    }
    item->setText(display);
    item->setToolTip(d->raw.value(row));
    auto font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setForeground(QBrush(QColor("#000000")));
}

void WatchPanel::chooseFormat(int row)
{
    if (row < 0 || row >= d->table->rowCount()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(t("watch.maskTitle"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(t("watch.maskPrompt"));
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *edit = new QLineEdit(QStringLiteral("0x%1").arg(d->masks.value(row, ~quint64(0)), 0, 16).toUpper());
    layout->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(t("watch.ok"));
    buttons->button(QDialogButtonBox::Cancel)->setText(t("watch.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto text = edit->text().trimmed();
    bool ok = false;
    const quint64 mask = text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
        ? text.mid(2).toULongLong(&ok, 16) : text.toULongLong(&ok, 16);
    if (!ok) return;
    d->masks[row] = mask;
    d->formats[row] = Impl::Format::Binary;
    applyFormat(row);
}

QString WatchPanel::rowRequest(int row) const
{
    const auto *item = d->table->item(row, 0);
    return item ? item->text() : QString();
}

void WatchPanel::sendRow(int row)
{
    const auto request = rowRequest(row).trimmed();
    if (request.isEmpty()) return;
    if (!d->backend || d->backend->connectionState() != QLatin1String("connected")) {
        setResponse(row, t("watch.notConnected"), true);
        return;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d->inflight.insert(id, row);
    d->backend->sendTerminalCommand(id, request);
}

void WatchPanel::refreshRow(int row)
{
    if (row < 0 || row >= d->table->rowCount() || d->kinds.value(row) != RowKind::Query) return;
    if (!d->backend || d->backend->connectionState() != QLatin1String("connected")) {
        setResponse(row, t("watch.notConnected"), true);
        return;
    }
    if (d->backend->busy() || (d->terminalBusy && d->terminalBusy())) return;
    const auto request = rowRequest(row).trimmed();
    if (request.isEmpty()) return;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d->inflight.insert(id, row);
    d->backend->sendTerminalCommand(id, request);
}

void WatchPanel::schedulePolling()
{
    const bool connected = d->backend && d->backend->connectionState() == QLatin1String("connected");
    const int queries = queryRowCount();
    if (!connected || d->pauseBox->isChecked() || queries == 0) {
        d->pollTimer.stop();
        return;
    }
    // 周期是相邻查询的间隔；N 行时每行约每 N × 周期刷新一次。
    const int interval = qMax(kPollFloorMs, d->periodMs);
    if (!d->pollTimer.isActive() || d->pollTimer.interval() != interval)
        d->pollTimer.start(interval);
}

void WatchPanel::pollTick()
{
    if (!d->backend || d->backend->connectionState() != QLatin1String("connected")
        || d->backend->busy() || d->pauseBox->isChecked()
        || (d->terminalBusy && d->terminalBusy()) || !d->inflight.isEmpty())
        return;
    const int rows = d->table->rowCount();
    if (rows == 0) return;
    for (int offset = 0; offset < rows; ++offset) {
        const int row = (d->nextQueryRow + offset) % rows;
        if (d->kinds.value(row) != RowKind::Query) continue;
        const auto request = rowRequest(row).trimmed();
        if (request.isEmpty()) continue;
        d->nextQueryRow = (row + 1) % rows;
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        d->inflight.insert(id, row);
        d->pollId = id;
        d->watchdog.start(kPollWatchdogMs);
        d->backend->sendTerminalCommand(id, request);
        return;
    }
}

int WatchPanel::queryRowCount() const
{
    int count = 0;
    for (int row = 0; row < d->table->rowCount(); ++row) {
        const auto *item = d->table->item(row, 0);
        if (d->kinds.value(row) == RowKind::Query && item && !item->text().trimmed().isEmpty())
            ++count;
    }
    return count;
}

int WatchPanel::loadTestSymbols(const QByteArray &json)
{
    if (!d->handle) return PMACT_INVALID_ARGUMENT;
    return d->api.setSymbols(d->handle, reinterpret_cast<const uint8_t *>(json.constData()),
                             size_t(json.size()));
}

void WatchPanel::addRowForTest(const QString &request) { addRow(request); }

int WatchPanel::rowCountForTest() const { return d->table->rowCount(); }

int WatchPanel::rowKindForTest(int row) const
{
    return row >= 0 && row < d->kinds.size() ? int(d->kinds[row]) : -1;
}

QString WatchPanel::rowRequestForTest(int row) const { return rowRequest(row); }

QString WatchPanel::rowResponseForTest(int row) const
{
    const auto *item = d->table->item(row, 1);
    return item ? item->text() : QString();
}

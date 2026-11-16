#pragma once
#include <QWidget>
#include <functional>
#include <memory>

#include "TerminalWidgets.h"

class Backend;

// 终端页左侧的 Watch 面板：查询行周期刷新，可确认后转为按钮行，点击按钮立即发送。
class WatchPanel : public QWidget
{
    Q_OBJECT
public:
    enum class RowKind { Query, Button };
    WatchPanel(Api &api, Backend *backend, std::function<bool()> terminalBusy,
               QWidget *parent = nullptr);
    ~WatchPanel() override;

    void setBackend(Backend *backend);
    void retranslate();

    // 离线自检入口
    int loadTestSymbols(const QByteArray &json);
    void addRowForTest(const QString &request);
    void removeRowForTest(int row) { removeRow(row); }
    bool convertRow(int row, bool confirmed);
    int rowCountForTest() const;
    int rowKindForTest(int row) const;
    QString rowRequestForTest(int row) const;
    QString rowResponseForTest(int row) const;
    QStringList suggestForTest(const QString &prefix) const { return suggestFor(prefix, nullptr); }

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    QString t(const char *key) const;
    QByteArray candidates(const QByteArray &prefix) const;
    QStringList suggestFor(const QString &prefix, QHash<QString, QString> *help) const;
    void importSymbols();
    void addRow(const QString &request, bool asButton = false);
    void makeQuery(int row);
    void removeRow(int row);
    void removeSelectedRows();
    void moveRow(int from, int to);
    void attachButton(int row, const QString &request);
    int placeholderRow() const;
    int realRowCount() const;
    void ensurePlaceholder();
    void setResponse(int row, const QString &text, bool error);
    void applyFormat(int row);
    void chooseFormat(int row);
    QString rowRequest(int row) const;
    void sendRow(int row);
    void refreshRow(int row);
    void schedulePolling();
    void pollTick();
    int queryRowCount() const;
};

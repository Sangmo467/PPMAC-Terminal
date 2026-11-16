#pragma once
// 终端页与 Watch 面板共用的原生控件。
// Adapted from pmac-terminal-rs/examples/qt/main.cpp. See docs/terminal.md.
#include <QtWidgets>
#include <QtPrintSupport>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include "pmac_terminal.h"
#include "Backend.h"
#include "RuntimePaths.h"
#include "Translator.h"
#include <QPointer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <imm.h>
#ifdef _MSC_VER
#pragma comment(lib, "imm32.lib")
#endif
#endif
namespace TerminalColors {
const QColor background("#000080"), response("#ffffff"), inputBackground("#ffffff"),
    inputText("#000000"), message("#ffff00"), error("#ff0000");
inline QColor typed() { return QColor("#b9d1ea"); }
inline QFont terminalFont() {
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(14);
    font.setWeight(QFont::DemiBold);
    return font;
}
inline void apply(QPlainTextEdit* edit,const QColor& baseColor,const QColor& text){
    auto palette=edit->palette();for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}){palette.setColor(group,QPalette::Base,baseColor);palette.setColor(group,QPalette::Text,text);}edit->setPalette(palette);
}
}
// 控制器实际硬件实例，由连接时探测 Gate*.PartNum / Chan*.EncCtrl 得到。
// 只对硬件结构（Gate1/Gate2/Gate3/GateIo）和探测到的成员数组给出具体下标。
struct HardwareConfig {
    QSet<QString> hardwareRoots;          // 探测过的硬件结构（小写，无 []）
    QHash<QString, QStringList> top;      // "gate3" -> ["Gate3[1]"]
    QHash<QString, QStringList> members;  // "gate3[]|chan" -> ["Chan[0]", ...]
    QHash<QString, QString> aliases;      // "ck3wax" -> "Gate3"
    bool valid = false;

    static QString normalizePath(const QString &path)
    {
        QString out;
        bool bracket = false;
        for (const QChar c : path) {
            if (c == QLatin1Char('[')) { bracket = true; out += c; }
            else if (c == QLatin1Char(']')) { bracket = false; out += c; }
            else if (!bracket) out += c;
        }
        return out.toLower();
    }

    void setInstances(const QStringList &instances, const QStringList &roots,
                      const QStringList &aliasPairs, bool known)
    {
        top.clear();
        members.clear();
        hardwareRoots.clear();
        aliases.clear();
        valid = known;
        for (const auto &root : roots) hardwareRoots.insert(root.toLower());
        for (const auto &pair : aliasPairs) {
            const int equals = pair.indexOf(QLatin1Char('='));
            if (equals > 0)
                aliases.insert(pair.left(equals).toLower(), pair.mid(equals + 1));
        }
        for (const auto &instance : instances) {
            const int dot = instance.lastIndexOf(QLatin1Char('.'));
            const auto last = dot >= 0 ? instance.mid(dot + 1) : instance;
            const int bracket = last.indexOf(QLatin1Char('['));
            if (bracket <= 0) continue;
            const auto base = last.left(bracket);
            if (dot < 0) {
                if (!top[base.toLower()].contains(last)) top[base.toLower()] << last;
            } else {
                const auto key = normalizePath(instance.left(dot))
                    + QLatin1Char('|') + base.toLower();
                if (!members[key].contains(last)) members[key] << last;
            }
        }
    }

    static bool matches(const QString &instance, const QString &typedBase, const QString &typedIndex)
    {
        if (!instance.startsWith(typedBase, Qt::CaseInsensitive)) return false;
        if (typedIndex.isEmpty()) return true;
        return instance.mid(typedBase.size())
            .startsWith(QStringLiteral("[") + typedIndex + QStringLiteral("]"));
    }

    static int tokenStart(const QString &prefix)
    {
        for (int i = prefix.size() - 1; i >= 0; --i) {
            const auto c = prefix.at(i);
            if (c.isSpace() || QStringLiteral("=+-*/(),;").contains(c)) return i + 1;
        }
        return 0;
    }

    // 别名成员（CK3WAX[0].）按等效结构（Gate3[0].）查询符号表。
    QString rewriteAlias(const QString &prefix) const
    {
        if (aliases.isEmpty()) return prefix;
        const int start = tokenStart(prefix);
        const auto token = prefix.mid(start);
        const int dot = token.indexOf(QLatin1Char('.'));
        if (dot < 0) return prefix;
        const auto first = token.left(dot);
        const int bracket = first.indexOf(QLatin1Char('['));
        const auto base = bracket > 0 ? first.left(bracket) : first;
        const auto it = aliases.constFind(base.toLower());
        if (it == aliases.constEnd()) return prefix;
        const auto suffix = bracket >= 0 ? first.mid(bracket) : QString();
        return prefix.left(start) + it.value() + suffix + token.mid(dot);
    }

    // 顶层别名（CK3WAX）不会出现在符号提示中，按探测到的硬件实例补上。
    void appendAliasInstances(const QString &prefix, QStringList *choices) const
    {
        if (!valid || !choices || aliases.isEmpty()) return;
        const auto token = prefix.mid(tokenStart(prefix));
        if (token.contains(QLatin1Char('.'))) return;
        static const QRegularExpression re(
            QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\[([^\\]]*)\\]?$"));
        const auto match = re.match(token);
        const auto typedBase = match.hasMatch() ? match.captured(1) : token;
        const auto typedIndex = match.hasMatch() ? match.captured(2).trimmed() : QString();
        if (typedBase.isEmpty()) return;
        for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
            if (!it.key().startsWith(typedBase.toLower())) continue;
            for (const auto &instance : top.value(it.key())) {
                bool ok = instance.startsWith(typedBase, Qt::CaseInsensitive);
                if (ok && !typedIndex.isEmpty()) {
                    const int bracket = instance.indexOf(QLatin1Char('['));
                    ok = instance.left(bracket).compare(typedBase, Qt::CaseInsensitive) == 0
                        && instance.mid(bracket).startsWith(
                               QStringLiteral("[") + typedIndex + QStringLiteral("]"));
                }
                if (ok && !choices->contains(instance)) choices->append(instance);
            }
        }
    }
};

inline QStringList completionChoices(const QString &prefix, const QString &candidate,
                                     const HardwareConfig *hardware)
{
    int start = 0;
    for (int i = prefix.size() - 1; i >= 0; --i) {
        const auto c = prefix.at(i);
        if (c.isSpace() || QStringLiteral("=+-*/(),;").contains(c)) { start = i + 1; break; }
    }
    const auto token = prefix.mid(start);
    const int dot = token.lastIndexOf(QLatin1Char('.'));
    const auto leaf = dot >= 0 ? token.mid(dot + 1) : token;
    const auto parent = dot >= 0 ? token.left(dot) : QString();
    static const QRegularExpression re(
        QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\[([^\\]]*)\\]?$"));
    // 父路径指向不存在的 Gate 实例时不再给出成员（如 Gate3[0].）。
    if (hardware && hardware->valid && !parent.isEmpty()) {
        const auto first = parent.section(QLatin1Char('.'), 0, 0);
        const auto root = re.match(first);
        if (root.hasMatch() && hardware->hardwareRoots.contains(root.captured(1).toLower())) {
            bool exists = false;
            for (const auto &instance : hardware->top.value(root.captured(1).toLower()))
                if (instance.compare(first, Qt::CaseInsensitive) == 0) { exists = true; break; }
            if (!exists) return {};
        }
    }
    const auto match = re.match(leaf);
    const auto typedBase = match.hasMatch() ? match.captured(1) : leaf;
    const auto typedIndex = match.hasMatch() ? match.captured(2).trimmed() : QString();
    if (!candidate.endsWith(QLatin1String("[]"))) return {candidate};
    const auto base = candidate.left(candidate.size() - 2);

    if (hardware && hardware->valid) {
        if (parent.isEmpty()) {
            if (hardware->hardwareRoots.contains(base.toLower())) {
                QStringList found;
                for (const auto &instance : hardware->top.value(base.toLower()))
                    if (HardwareConfig::matches(instance, typedBase, typedIndex)) found << instance;
                return found;  // 硬件没有的 Gate 结构直接隐藏
            }
        } else {
            const auto key = HardwareConfig::normalizePath(parent)
                + QLatin1Char('|') + base.toLower();
            const auto instances = hardware->members.value(key);
            if (!instances.isEmpty()) {
                QStringList found;
                for (const auto &instance : instances)
                    if (HardwareConfig::matches(instance, typedBase, typedIndex)) found << instance;
                return found;
            }
        }
    }
    if (match.hasMatch()) {
        const auto index = typedIndex.isEmpty() ? QStringLiteral("0") : typedIndex;
        return {base + QLatin1Char('[') + index + QLatin1Char(']')};
    }
    return {candidate};
}
struct Api {
    QLibrary library;
    decltype(&pmact_create) create=nullptr; decltype(&pmact_destroy) destroy=nullptr;
    decltype(&pmact_set_symbol_files) setSymbols=nullptr;
    decltype(&pmact_suggest) suggest=nullptr;
    explicit Api(const QString& path):library(path) {
        if(!library.load())throw std::runtime_error(library.errorString().toStdString());
#define LOAD(member,name) member=reinterpret_cast<decltype(member)>(library.resolve(name)); if(!member)throw std::runtime_error(name)
        LOAD(create,"pmact_create");LOAD(destroy,"pmact_destroy");
        LOAD(setSymbols,"pmact_set_symbol_files");LOAD(suggest,"pmact_suggest");
#undef LOAD
    }
};
// QCompleter keeps focus in the editor. Paint the current row explicitly so
// native styles and inactive palettes cannot erase its selection colors.
class CompletionDelegate : public QStyledItemDelegate {
    QAbstractItemView *view;
public:
    explicit CompletionDelegate(QAbstractItemView *view) : QStyledItemDelegate(view), view(view) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const bool selected = (option.state & QStyle::State_Selected) || index == view->currentIndex();
        painter->save();
        painter->fillRect(option.rect, selected ? QColor("#0078d7") : QColor("#ffffff"));
        painter->setPen(selected ? QColor("#ffffff") : QColor("#000000"));
        auto font = option.font;
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        const QRect textRect = option.rect.adjusted(8, 2, -8, -2);
        const QString text = QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), QFontMetrics(option.font).height() + 10));
        return size;
    }
};
class CommandEdit:public QPlainTextEdit {
    Q_OBJECT
    bool autoEnglish = true;

    void syncInputMethodPolicy() {
        // setReadOnly(false) 会重新设置此属性，必须在状态变化后重新应用策略。
        setAttribute(Qt::WA_InputMethodEnabled, !autoEnglish && isEnabled() && !isReadOnly());
        if (!hasInputFocus()) return;
        if (autoEnglish) {
            // 清除已存在的预编辑；reset() 可能提交旧组合串，由下方处理器拦截。
            QInputMethodEvent cancel;
            QPlainTextEdit::inputMethodEvent(&cancel);
            QGuiApplication::inputMethod()->reset();
        }
        QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImHints);
    }
#ifdef Q_OS_WIN
    HKL previousLayout = nullptr;

    static bool focusOwned(HWND window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        return pid == GetCurrentProcessId();
    }
    static bool isEnglishLayout(HKL layout) {
        return layout && PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout))) == LANG_ENGLISH;
    }
    static bool applyLayout(HKL layout) {
        if (!layout) return false;
        // 同步切换当前 GUI 线程，避免排队的中/英文请求在焦点变化后相互覆盖。
        if (!ActivateKeyboardLayout(layout, 0)) return false;
        return GetKeyboardLayout(0) == layout;
    }
    void preferEnglish() {
        if (!autoEnglish || !hasInputFocus()) return;
        const HWND window = GetFocus();
        if (!window || !focusOwned(window)) return;
        const HKL current = GetKeyboardLayout(0);
        if (isEnglishLayout(current)) return;
        if (!previousLayout) previousLayout = current;
        // LoadKeyboardLayout 返回有效句柄不代表英文已经生效，必须验证实际布局。
        static HKL englishLayout = nullptr;
        if (!englishLayout) englishLayout = LoadKeyboardLayoutW(L"00000409", 0);
        if (isEnglishLayout(englishLayout)) applyLayout(englishLayout);
    }
    void restoreInputMode() {
        const HKL layout = previousLayout;
        previousLayout = nullptr;
        // 保留用户在输入期间主动选择的其他输入法。
        if (layout && isEnglishLayout(GetKeyboardLayout(0))) applyLayout(layout);
    }
#endif
public:
    ~CommandEdit() override {
#ifdef Q_OS_WIN
        QObject::disconnect(qGuiApp, nullptr, this, nullptr);
        restoreInputMode();
#endif
    }
    void setAutoEnglish(bool enabled) {
        autoEnglish = enabled;
        syncInputMethodPolicy();
#ifdef Q_OS_WIN
        if (!enabled) restoreInputMode();
        else if (hasInputFocus()) preferEnglish();
#endif
        if (stateChanged) stateChanged();
    }
    void ensureEnglish() {
        syncInputMethodPolicy();
#ifdef Q_OS_WIN
        if (!autoEnglish) return;
        preferEnglish();
        if (stateChanged) stateChanged();
        // QML 原生宿主可能在 focusObjectChanged 之后才取得真实键盘焦点。
        QTimer::singleShot(150, this, [this] {
            syncInputMethodPolicy();
            preferEnglish();
            if (stateChanged) stateChanged();
        });
#endif
    }
    bool hasInputFocus() const {
        return hasFocus() || QGuiApplication::focusObject() == this;
    }
    bool englishActive() const {
#ifdef Q_OS_WIN
        return isEnglishLayout(GetKeyboardLayout(0));
#else
        return false;
#endif
    }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        return inputMethodQuery(query, {});
    }
    // Qt 的参数化查询走元对象调用，两个入口都必须报告相同的 IME 策略。
    Q_INVOKABLE QVariant inputMethodQuery(Qt::InputMethodQuery query, QVariant argument) const {
        if (autoEnglish && query == Qt::ImEnabled) return false;
        return QPlainTextEdit::inputMethodQuery(query, argument);
    }
    QStringListModel model; QCompleter completer; QHash<QString,QString> help;
    std::function<void()> submit;
    std::function<void()> stateChanged;
    std::function<QStringList(const QString&)> complete;
    QStringList history; int historyIndex=-1; QString draft;
    explicit CommandEdit(QWidget* parent=nullptr):QPlainTextEdit(parent),completer(&model,this) {
        setObjectName("commandInput");
        syncInputMethodPolicy();
#ifdef Q_OS_WIN
        // 嵌入 QQuickWindow 后 QApplication::focusWidget()/hasFocus() 可能为空，
        // 实际键盘接收者由 QGuiApplication::focusObject() 给出。
        connect(qGuiApp, &QGuiApplication::focusObjectChanged, this, [this](QObject *object) {
            if (object == this) {
                ensureEnglish();
            } else {
                if (!completer.popup()->isVisible()) restoreInputMode();
                if (stateChanged) stateChanged();
            }
        });
#endif
        setMaximumBlockCount(1000);TerminalColors::apply(this,TerminalColors::inputBackground,TerminalColors::inputText);completer.setWidget(this);completer.setCompletionMode(QCompleter::PopupCompletion);
        completer.popup()->setStyleSheet("QAbstractItemView { background:#ffffff; color:#000000; border:1px solid #8b95a5; selection-background-color:#0078d7; selection-color:#ffffff; padding:4px; }");
        completer.popup()->setItemDelegate(new CompletionDelegate(completer.popup()));
        completer.popup()->setSelectionMode(QAbstractItemView::SingleSelection);
        completer.popup()->installEventFilter(this);installEventFilter(this);
        completer.setCaseSensitivity(Qt::CaseInsensitive);completer.setFilterMode(Qt::MatchStartsWith);
        QObject::connect(&completer,QOverload<const QString&>::of(&QCompleter::activated),this,[this](const QString& text){
            acceptCompletion(text);
        });
        QObject::connect(completer.popup()->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex& index,const QModelIndex&){
            const auto text=index.data().toString();const auto hint=help.value(text);
            if(hint.isEmpty()){QToolTip::hideText();return;}
            // 帮助显示在候选框外侧，避免遮挡候选列表。
            const auto box=completer.popup()->geometry();
            const auto screen=completer.popup()->screen()->availableGeometry();
            const int width=qMin(420,QFontMetrics(QToolTip::font()).horizontalAdvance(hint)+24);
            int x=box.right()+8;
            if(x+width>screen.right())x=box.left()-width-8;
            if(x<screen.left())x=qMax(screen.left(),box.left());
            const int y=qBound(screen.top(),box.top(),qMax(screen.top(),screen.bottom()-60));
            QToolTip::showText(QPoint(x,y),hint,this);
        });
    }
    void hideCompletion(){completer.popup()->hide();QToolTip::hideText();}
    void acceptCompletion(const QString& text){
        auto c=textCursor();int start=c.position();const QString value=toPlainText();
        while(start>0){const auto ch=value.at(start-1);if(ch.isSpace()||QStringLiteral(".=+-*/(),;").contains(ch))break;--start;}
        int end=c.position();while(end<value.size()){const auto ch=value.at(end);if(ch.isSpace()||QStringLiteral(".=+-*/(),;").contains(ch))break;++end;}
        c.setPosition(start);c.setPosition(end,QTextCursor::KeepAnchor);c.insertText(text);
        const auto bracket=text.indexOf(QStringLiteral("[]"));if(bracket>=0)c.setPosition(start+int(bracket)+1);
        setTextCursor(c);hideCompletion();setFocus();
    }
    void handleEnter(){
        const auto index=completer.popup()->currentIndex();
        if(completer.popup()->isVisible()&&index.isValid()){acceptCompletion(index.data().toString());return;}
        hideCompletion();if(submit)submit();
    }
    void remember(const QString& s){history.removeAll(s);history.prepend(s);while(history.size()>200)history.removeLast();historyIndex=-1;}
protected:
    bool event(QEvent *event) override {
        const bool handled = QPlainTextEdit::event(event);
        if (event->type() == QEvent::ReadOnlyChange || event->type() == QEvent::EnabledChange) {
            syncInputMethodPolicy();
            if (autoEnglish && !isReadOnly() && hasInputFocus()) ensureEnglish();
        }
        return handled;
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        if (autoEnglish) {
            // 英文模式拒绝滞后的 IME 预编辑/提交；粘贴不走此路径。
            event->accept();
            return;
        }
        QPlainTextEdit::inputMethodEvent(event);
    }
    void focusInEvent(QFocusEvent *event) override {
        QPlainTextEdit::focusInEvent(event);
#ifdef Q_OS_WIN
        ensureEnglish();
#endif
    }
    void focusOutEvent(QFocusEvent *event) override {
#ifdef Q_OS_WIN
        if (event->reason() != Qt::PopupFocusReason) restoreInputMode();
        if (stateChanged) stateChanged();
#endif
        QPlainTextEdit::focusOutEvent(event);
    }
    bool eventFilter(QObject* object,QEvent* event)override {
        if(object==completer.popup()&&event->type()==QEvent::Hide){QToolTip::hideText();}
        if((object==completer.popup()||object==this)&&event->type()==QEvent::KeyPress){
            auto key=static_cast<QKeyEvent*>(event);
            if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&!(key->modifiers()&Qt::ShiftModifier)){handleEnter();return true;}
            if(key->key()==Qt::Key_Tab&&completer.popup()->isVisible()){const auto index=completer.popup()->currentIndex();if(index.isValid())acceptCompletion(index.data().toString());return true;}
        }
        return QPlainTextEdit::eventFilter(object,event);
    }
    void keyPressEvent(QKeyEvent* e)override {
#ifdef Q_OS_WIN
        // 焦点可能被原生宿主窗口吞掉，按键时核验真实布局。
        preferEnglish();
#endif
        if (stateChanged) stateChanged();
        if((e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter)&&!(e->modifiers()&Qt::ShiftModifier)){handleEnter();e->accept();return;}
        const bool popup=completer.popup()->isVisible();
        if(popup&&(e->key()==Qt::Key_Enter||e->key()==Qt::Key_Return||e->key()==Qt::Key_Tab||e->key()==Qt::Key_Escape||e->key()==Qt::Key_Up||e->key()==Qt::Key_Down)){e->ignore();return;}
        // 接受数组候选后光标停在 [] 内：补完下标再输入 '.' 时把点号放到 ']' 之后，
        // 输入 ']' 时若右侧已有 ']' 则直接跳过，避免得到 Gate3[0.] / Gate3[0]]。
        if(!e->modifiers().testFlag(Qt::ControlModifier)){
            auto c=textCursor();const auto value=toPlainText();
            if(!c.hasSelection()&&c.position()<value.size()){
                const auto next=value.at(c.position());
                if(e->text()==QLatin1String(".")&&next==QLatin1Char(']')){
                    int depth=0;bool inside=false;
                    for(int i=c.position()-1;i>=0;--i){
                        const auto ch=value.at(i);
                        if(ch==QLatin1Char(']'))++depth;
                        else if(ch==QLatin1Char('[')){
                            if(depth==0){inside=true;break;}
                            --depth;
                        }
                    }
                    if(inside){c.movePosition(QTextCursor::Right);setTextCursor(c);}
                }else if(e->text()==QLatin1String("]")&&next==QLatin1Char(']')){
                    c.movePosition(QTextCursor::Right);setTextCursor(c);e->accept();return;
                }
            }
        }

        if((e->key()==Qt::Key_Up||e->key()==Qt::Key_Down)&&e->modifiers()==Qt::NoModifier&&!history.isEmpty()){
            if(historyIndex<0)draft=toPlainText();
            historyIndex=e->key()==Qt::Key_Up?qMin(historyIndex+1,int(history.size()-1)):qMax(-1,historyIndex-1);
            setPlainText(historyIndex<0?draft:history.at(historyIndex));moveCursor(QTextCursor::End);e->accept();return;
        }
        QPlainTextEdit::keyPressEvent(e);
        if(!complete||e->modifiers().testFlag(Qt::ControlModifier))return;
        auto c=textCursor();const auto prefix=toPlainText().left(c.position());
        if(prefix.isEmpty()||prefix.back().isSpace()){hideCompletion();return;}
        auto choices=complete(prefix);model.setStringList(choices);completer.setCompletionPrefix(QString());
        if(choices.isEmpty()){hideCompletion();return;}
        auto rect=cursorRect();rect.setWidth(qMax(320,completer.popup()->sizeHintForColumn(0)+30));completer.complete(rect);
        completer.popup()->setCurrentIndex(completer.completionModel()->index(0,0));
    }
};

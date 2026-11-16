#pragma once
// 终端页与 Watch 面板共用的原生控件。
// Adapted from pmac-terminal-rs/examples/qt/main.cpp. See docs/terminal.md.
#include <QtWidgets>
#include <QtPrintSupport>
#include <functional>
#include <memory>
#include <stdexcept>
#include "pmac_terminal.h"
#include "Backend.h"
#include "Translator.h"
#include <QPointer>
#ifdef Q_OS_WIN
#include <windows.h>

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
    bool autoEnglish = true;
#ifdef Q_OS_WIN
    HKL originalLayout = nullptr, englishLayout = nullptr;

    void restoreInputMode() {
        // Restore only the layout we selected; respect a subsequent manual switch.
        if (originalLayout && englishLayout && GetKeyboardLayout(0) == englishLayout)
            ActivateKeyboardLayout(originalLayout, 0);
        originalLayout = nullptr;
        englishLayout = nullptr;
    }
    void preferEnglish() {
        if (!autoEnglish || originalLayout) return;
        const HKL current = GetKeyboardLayout(0);
        if (PRIMARYLANGID(LOWORD(reinterpret_cast<quintptr>(current))) == LANG_ENGLISH) return;
        HKL layouts[64];
        const int count = GetKeyboardLayoutList(64, layouts);
        for (int i = 0; i < count; ++i) {
            if (PRIMARYLANGID(LOWORD(reinterpret_cast<quintptr>(layouts[i]))) == LANG_ENGLISH) {
                if (ActivateKeyboardLayout(layouts[i], 0)) {
                    originalLayout = current;
                    englishLayout = layouts[i];
                }
                return;
            }
        }
        // No installed English layout: leave the input method unchanged.
    }
#endif
public:
    ~CommandEdit() override {
#ifdef Q_OS_WIN
        restoreInputMode();
#endif
    }
    void setAutoEnglish(bool enabled) {
        autoEnglish = enabled;
#ifdef Q_OS_WIN
        if (!enabled) restoreInputMode();
        else if (hasFocus()) preferEnglish();
#endif
    }
    QStringListModel model; QCompleter completer; QHash<QString,QString> help;
    std::function<void()> submit;
    std::function<QStringList(const QString&)> complete;
    QStringList history; int historyIndex=-1; QString draft;
    explicit CommandEdit(QWidget* parent=nullptr):QPlainTextEdit(parent),completer(&model,this) {
        setObjectName("commandInput");
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
    void focusInEvent(QFocusEvent *event) override {
        QPlainTextEdit::focusInEvent(event);
#ifdef Q_OS_WIN
        preferEnglish();
#endif
    }
    void focusOutEvent(QFocusEvent *event) override {
#ifdef Q_OS_WIN
        if (event->reason() != Qt::PopupFocusReason) restoreInputMode();
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
        if((e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter)&&!(e->modifiers()&Qt::ShiftModifier)){handleEnter();e->accept();return;}
        const bool popup=completer.popup()->isVisible();
        if(popup&&(e->key()==Qt::Key_Enter||e->key()==Qt::Key_Return||e->key()==Qt::Key_Tab||e->key()==Qt::Key_Escape||e->key()==Qt::Key_Up||e->key()==Qt::Key_Down)){e->ignore();return;}

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

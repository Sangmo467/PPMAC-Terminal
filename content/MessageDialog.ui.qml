import QtQuick 6.5
import QtQuick.Controls 6.5
import PmacTerminal

Dialog {
    id: dlg

    property alias text: msgLabel.text
    property alias okButton: okBtn

    modal: true
    padding: 24
    // 文本宽度固定，implicitHeight 才能按最终换行结果计算，
    // 否则 Dialog 布局前的瞬时宽度会让内容高度少算一行，导致按钮压住文字
    readonly property int textWidth: Math.round(420 * Constants.uiScale)
    contentWidth: textWidth
    contentHeight: msgLabel.implicitHeight + 10
    standardButtons: Dialog.NoButton

    background: Rectangle {
        radius: 6
        color: "#1f232b"
        border.color: "#3a4150"
    }

    header: Rectangle {
        // 必须设 implicitHeight：Dialog 用各部分的 implicit 高度累加总高度，
        // 显式 height 不参与该计算，会导致内容区被压掉一个 header 的高度
        implicitHeight: Math.round(52 * Constants.uiScale)
        radius: 6
        color: "#262b33"
        Text {
            anchors.centerIn: parent
            text: dlg.title
            color: "#e8ecf1"
            font.pixelSize: Constants.fontButton
            font.bold: true
        }
    }

    contentItem: Text {
        id: msgLabel
        width: dlg.textWidth
        wrapMode: Text.WordWrap
        color: "#d7dae0"
        font.pixelSize: Constants.fontBody
        lineHeight: 1.3
    }

    footer: Row {
        spacing: 12
        padding: 14
        layoutDirection: Qt.RightToLeft

        AppButton { id: okBtn; text: LanguageManager.confirm }
    }
}

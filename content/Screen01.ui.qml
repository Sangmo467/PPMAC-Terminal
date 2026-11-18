/*
This is a UI file (.ui.qml) that is intended to be edited in Qt Design Studio only.
It is supposed to be strictly declarative and only uses a subset of QML. If you edit
this file manually, you might introduce QML code that is not supported by Qt Design Studio.
Check out https://doc.qt.io/qtcreator/creator-quick-ui-forms.html for details on .ui.qml files.
*/

import QtQuick 6.5
import PmacTerminal
import PmacTerminal.Backend

Rectangle {
    id: root

    width: Constants.width
    height: Constants.height

    color: "#16181d"

    // 连接状态：disconnected | connecting | connected | failed
    // 由 MainScreen.qml 绑定 C++ Backend 状态驱动
    property string connectionState: "disconnected"

    // C++ Backend 实例（由 MainScreen.qml 注入）
    property var backend: null

    // 供外部（MainScreen.qml）使用的别名
    property alias username: connBar.username
    property alias password: connBar.password
    property alias ipAddress: connBar.ipAddress
    property alias recentIpSelector: connBar.recentIpSelector
    property alias connectButton: connBar.connectButton
    property alias firmwareVersion: connBar.firmwareVersion
    property alias languageSelector: connBar.languageSelector
    property alias terminalPage: terminalPageItem

    // 全宽连接状态长条
    StatusStrip {
        id: statusStrip
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        state: root.connectionState
    }

    ConnectionBar {
        id: connBar
        anchors.top: statusStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        connectionState: root.connectionState
        hasRecentIps: root.backend !== null && root.backend.recentIps.length > 0
    }

    TerminalPage {
        id: terminalPageItem
        objectName: "terminalPage"
        backend: root.backend
        anchors.top: connBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}

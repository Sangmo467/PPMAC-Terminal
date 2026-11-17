import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Window 6.5
import PmacTerminal

Rectangle {
    id: bar
    objectName: "connectionBar"

    property alias username: userField.text
    property alias password: passField.text
    property alias ipAddress: ipField.text
    property string connectionState: "disconnected"   // disconnected | connecting | connected | failed
    property string firmwareVersion: ""
    property alias connectButton: connectButton
    property alias languageSelector: languageSelector

    height: Math.round(88 * Constants.uiScale)
    color: "#1f232b"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#343a45"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 10

        Text {
            text: LanguageManager.pmacConnection
            color: "#e8ecf1"
            font.pixelSize: Constants.fontTitle
            font.bold: true
            Layout.rightMargin: 10
            Layout.minimumWidth: implicitWidth
        }

        Text {
            text: LanguageManager.username
            color: "#9aa3b2"
            font.pixelSize: Constants.fontBody
            Layout.minimumWidth: implicitWidth
        }
        AppTextField {
            id: userField
            objectName: "connectionUsername"
            Layout.fillWidth: true
            Layout.minimumWidth: Math.round(90 * Constants.uiScale)
            Layout.preferredWidth: Math.round(140 * Constants.uiScale)
            Layout.maximumWidth: Math.round(140 * Constants.uiScale)
            text: "root"
            placeholderText: LanguageManager.phUsername
        }

        Text {
            text: LanguageManager.password
            color: "#9aa3b2"
            font.pixelSize: Constants.fontBody
            Layout.minimumWidth: implicitWidth
        }
        AppTextField {
            id: passField
            objectName: "connectionPassword"
            Layout.fillWidth: true
            Layout.minimumWidth: Math.round(90 * Constants.uiScale)
            Layout.preferredWidth: Math.round(140 * Constants.uiScale)
            Layout.maximumWidth: Math.round(140 * Constants.uiScale)
            text: "deltatau"
            placeholderText: LanguageManager.phPassword
            echoMode: TextInput.Password
        }

        Text {
            text: LanguageManager.ipAddress
            color: "#9aa3b2"
            font.pixelSize: Constants.fontBody
            Layout.minimumWidth: implicitWidth
        }
        AppTextField {
            id: ipField
            objectName: "connectionIpAddress"
            Layout.fillWidth: true
            Layout.minimumWidth: Math.round(120 * Constants.uiScale)
            Layout.preferredWidth: Math.round(200 * Constants.uiScale)
            Layout.maximumWidth: Math.round(200 * Constants.uiScale)
            text: Qt.platform.os === "osx" ? "192.168.250.200" : "192.168.0.200"
            placeholderText: "192.168.0.200"
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        AppButton {
            id: connectButton
            text: bar.connectionState === "connected" ? LanguageManager.disconnect
                : bar.connectionState === "connecting" ? LanguageManager.connecting
                : LanguageManager.connect
            enabled: bar.connectionState !== "connecting"
            accent: bar.connectionState === "connected" ? "#d64545" : "#3fa7f5"
            Layout.minimumWidth: implicitWidth
        }

        // 状态圆点 + 文字
        Rectangle {
            width: 14
            height: 14
            radius: 6
            Layout.leftMargin: 8
            Layout.minimumWidth: width
            Layout.maximumWidth: width
            color: bar.connectionState === "connected" ? "#2ea44f"
                 : bar.connectionState === "connecting" ? "#d9a13b"
                 : bar.connectionState === "failed" ? "#d64545" : "#5a616d"
        }
        Text {
            text: bar.connectionState === "connected" ? LanguageManager.statusConnected
                : bar.connectionState === "connecting" ? LanguageManager.statusConnecting
                : bar.connectionState === "failed" ? LanguageManager.statusFailed
                : LanguageManager.statusDisconnected
            color: bar.connectionState === "connected" ? "#2ea44f"
                 : bar.connectionState === "connecting" ? "#d9a13b"
                 : bar.connectionState === "failed" ? "#d64545" : "#8a93a3"
            font.pixelSize: Constants.fontBody
            Layout.minimumWidth: implicitWidth
        }

        // 固件版本（连接成功后由后台通过 gpascii "vers" 查询）
        Text {
            visible: bar.connectionState === "connected" && bar.firmwareVersion.length > 0
            text: LanguageManager.firmwareLabel + ": " + bar.firmwareVersion
            color: "#8a93a3"
            font.pixelSize: Constants.fontBody
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.maximumWidth: Math.round(320 * Constants.uiScale)
        }

        Item { Layout.fillWidth: true }

        Text {
            text: LanguageManager.languageLabel
            color: "#9aa3b2"
            font.pixelSize: Constants.fontBody
            Layout.minimumWidth: implicitWidth
        }
        Rectangle {
            id: languageSelector
            objectName: "languageSelector"
            signal activated()
            readonly property string currentName: {
                const index = LanguageManager.indexOf(LanguageManager.language)
                return LanguageManager.languageNames[index] || ""
            }
            implicitWidth: Math.round(160 * Constants.uiScale)
            implicitHeight: Math.round(42 * Constants.uiScale)
            Layout.minimumWidth: implicitWidth
            Layout.preferredWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            radius: 4
            color: "#16181d"
            border.color: languageMouse.containsMouse ? "#3fa7f5" : "#3a4150"
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: arrow.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: languageSelector.currentName
                color: "#d7dae0"
                font.pixelSize: Constants.fontBody
                elide: Text.ElideRight
            }
            Text {
                id: arrow
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "▾"
                color: "#d7dae0"
                font.pixelSize: Constants.fontBody
            }
            MouseArea {
                id: languageMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: languageSelector.activated()
            }
        }
    }
}

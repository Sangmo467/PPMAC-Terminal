// StatusStrip.qml
// 全宽连接状态长条：connected 绿 / failed 红 / connecting 黄色闪烁 / disconnected 灰。
import QtQuick 6.5

Rectangle {
    id: strip

    // disconnected | connecting | connected | failed
    property string state: "disconnected"

    height: 10
    color: state === "connected" ? "#2ea44f"
         : state === "connecting" ? "#d9a13b"
         : state === "failed" ? "#d64545" : "#5a616d"

    // 连接中闪烁覆盖层
    Rectangle {
        id: flashOverlay
        anchors.fill: parent
        visible: strip.state === "connecting"
        color: "#f5d76e"

        SequentialAnimation on opacity {
            running: flashOverlay.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0.1; to: 0.9; duration: 500 }
            NumberAnimation { from: 0.9; to: 0.1; duration: 500 }
        }
    }
}

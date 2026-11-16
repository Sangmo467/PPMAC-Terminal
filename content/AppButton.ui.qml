import QtQuick 6.5
import QtQuick.Controls 6.5
import PmacTerminal

Button {
    id: control

    property color accent: "#3fa7f5"

    implicitWidth: Math.max(Math.round(110 * Constants.uiScale), contentItem.implicitWidth + 36)
    implicitHeight: Math.round(46 * Constants.uiScale)

    contentItem: Text {
        text: control.text
        color: control.enabled ? "#f0f3f7" : "#6b7280"
        font.pixelSize: Constants.fontButton
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 5
        color: !control.enabled ? "#262b33"
               : control.down ? Qt.darker(control.accent, 1.45)
               : control.hovered ? control.accent
               : Qt.darker(control.accent, 1.2)
    }
}

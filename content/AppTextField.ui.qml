import QtQuick 6.5
import QtQuick.Controls 6.5
import PmacTerminal

TextField {
    id: control

    implicitHeight: Math.round(42 * Constants.uiScale)
    color: "#d7dae0"
    placeholderTextColor: "#5f6875"
    selectionColor: "#ffffff"
    selectedTextColor: "#000000"
    font.pixelSize: Constants.fontBody

    background: Rectangle {
        radius: 5
        color: control.enabled ? "#16181d" : "#1c2026"
        border.width: 1
        border.color: control.activeFocus ? "#3fa7f5" : "#3a4150"
    }
}

// MainScreen.qml
// Screen01（纯声明式 UI）+ 交互逻辑：C++ Backend 接线、连接、全局提示。
import QtQuick 6.5
import QtQuick.Controls 6.5
import PmacTerminal
import PmacTerminal.Backend

Screen01 {
    id: screen

    Backend {
        id: backendObj
        language: LanguageManager.language
    }

    backend: backendObj
    connectionState: backendObj.connectionState
    firmwareVersion: backendObj.firmwareVersion

    // 连接 / 断开
    Connections {
        target: screen.connectButton
        function onClicked() {
            if (backendObj.connectionState === "connected")
                backendObj.disconnectFrom();
            else
                backendObj.connectTo(screen.username, screen.password, screen.ipAddress);
        }
    }

    // 语言切换：使用原生菜单，浮在终端原生窗口之上。
    Connections {
        target: screen.languageSelector
        function onActivated() {
            NativeMenu.showLanguages(LanguageManager.languages,
                                     LanguageManager.indexOf(LanguageManager.language));
        }
    }
    Connections {
        target: NativeMenu
        function onLanguageSelected(code) {
            LanguageManager.language = code;
        }
    }

    // 后端全局通知（连接失败、掉线）
    Connections {
        target: backendObj
        function onOperationFailed(message) {
            msgDlg.text = message;
            msgDlg.open();
        }
    }

    // 全局提示
    MessageDialog {
        id: msgDlg
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: LanguageManager.promptTitle
    }
    Connections {
        target: msgDlg.okButton
        function onClicked() { msgDlg.accept(); }
    }
}

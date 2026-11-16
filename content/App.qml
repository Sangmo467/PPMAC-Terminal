// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick 6.5
import QtQuick.Window
import PmacTerminal

Window {
    // 默认尺寸不超过屏幕可用区域
    width: Math.min(Constants.width, Screen.width - 80)
    height: Math.min(Constants.height, Screen.height - 120)
    minimumWidth: 1100
    minimumHeight: 680

    visible: true
    title: "PMAC Terminal"
    // 窗口底色与界面一致，避免缩放/启动时露出白色
    color: "#16181d"
    // 程序图标由 main.cpp 的 setWindowIcon 与 icons/app.rc 提供

    MainScreen {
        id: mainScreen
        anchors.fill: parent
    }

}

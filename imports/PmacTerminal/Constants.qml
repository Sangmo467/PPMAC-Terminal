pragma Singleton
import QtQuick 6.5

QtObject {
    readonly property int width: 1920
    readonly property int height: 1080

    // 全局界面字号缩放。调大此值可整体放大界面文字与控件高度（1.0 = 原始尺寸）。
    readonly property real uiScale: 1.25

    // 语义字号（按 uiScale 计算，单位 px）。
    // 各界面统一引用这些值，避免散落硬编码导致大小不一。
    readonly property int fontSmall: Math.round(14 * uiScale)   // 行号、状态栏、日志
    readonly property int fontTable: Math.round(15 * uiScale)   // 表格单元、菜单项
    readonly property int fontBody: Math.round(16 * uiScale)    // 标签、输入框、下拉框
    readonly property int fontButton: Math.round(17 * uiScale)  // 按钮、选项卡、对话框标题
    readonly property int fontTitle: Math.round(20 * uiScale)   // 连接栏标题
    readonly property int fontLarge: Math.round(24 * uiScale)   // 状态大字（错误/链路）
    readonly property int fontDisplay: Math.round(28 * uiScale) // 主站状态色块
}

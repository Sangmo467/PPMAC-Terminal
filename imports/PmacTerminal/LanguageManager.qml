pragma Singleton
import QtQuick 6.5
import PmacTerminal.Backend

// 文案由 C++ Translator 单例统一加载（与后台消息同一份数据）：
// 优先部署的 translations/ 目录（macOS 在 Contents/Resources 下），回退内置 qrc。
// 新增语言：translations/ 下加 <code>.json 并在 languages.json 中登记即可，
// 下拉框会自动出现新语言（本文件与界面代码无需改动）。
QtObject {
    id: root

    // 可选语言列表（[{ code, name }...]），来自 languages.json
    readonly property var languages: {
        var l = Translator.languages();
        return l.length > 0 ? l : [{ "code": "en", "name": "English" }];
    }

    // 当前语言代码；默认跟随操作系统语言：先精确匹配（如 zh_TW），
    // 再匹配主语言部分（如 zh_HK → zh），匹配不到时用英语
    property string language: {
        var n = Qt.locale().name;                 // 如 zh_CN / zh_TW / en_US
        var baseCode = n.split(/[-_]/)[0];        // 如 zh
        var fallback = "";
        for (var i = 0; i < languages.length; ++i) {
            if (languages[i].code === n)
                return n;
            if (languages[i].code === baseCode)
                fallback = baseCode;
        }
        return fallback || "en";
    }

    // 语言下拉框显示名列表
    readonly property var languageNames: languages.map(function(l) { return l.name; })

    function codeAt(index) {
        return languages[index] ? languages[index].code : "en";
    }
    function indexOf(code) {
        for (var i = 0; i < languages.length; ++i) {
            if (languages[i].code === code)
                return i;
        }
        return 0;
    }

    // 取文案：当前语言 → 英语 → 键名本身
    function t(key) {
        return Translator.tr(language, key);
    }

    // 带 %1/%2/%3 占位符的文案
    function tf(key, a, b, c) {
        var s = t(key);
        if (a !== undefined && a !== null) s = s.replace("%1", String(a));
        if (b !== undefined && b !== null) s = s.replace("%2", String(b));
        if (c !== undefined && c !== null) s = s.replace("%3", String(c));
        return s;
    }

    // 连接栏
    readonly property string pmacConnection: t("pmacConnection")
    readonly property string username: t("username")
    readonly property string password: t("password")
    readonly property string ipAddress: t("ipAddress")
    readonly property string recentIps: t("recentIps")
    readonly property string phUsername: t("phUsername")
    readonly property string phPassword: t("phPassword")
    readonly property string connect: t("connect")
    readonly property string disconnect: t("disconnect")
    readonly property string connecting: t("connecting")
    readonly property string statusConnected: t("statusConnected")
    readonly property string statusConnecting: t("statusConnecting")
    readonly property string statusFailed: t("statusFailed")
    readonly property string statusDisconnected: t("statusDisconnected")
    readonly property string firmwareLabel: t("firmwareLabel")
    readonly property string languageLabel: t("languageLabel")

    // 主页面标签
    readonly property string tabConfig: t("tabConfig")
    readonly property string tabCompensation: t("tabCompensation")
    readonly property string tabMotorParams: t("tabMotorParams")
    readonly property string tabEcat: t("tabEcat")
    readonly property string tabStateMachine: t("tabStateMachine")
    readonly property string tabGather: t("tabGather")
    readonly property string tabTerminal: t("tabTerminal")

    // 功能页1：配置
    readonly property string configFile: t("configFile")
    readonly property string read: t("read")
    readonly property string write: t("write")
    readonly property string statusNotLoaded: t("statusNotLoaded")
    readonly property string colVarName: t("colVarName")
    readonly property string colValue: t("colValue")

    // 功能页2：补偿表
    readonly property string compDir: t("compDir")
    readonly property string newFile: t("newFile")
    readonly property string deleteFile: t("deleteFile")
    readonly property string noFiles: t("noFiles")
    readonly property string colPointNo: t("colPointNo")
    readonly property string colX: t("colX")
    readonly property string colY: t("colY")
    readonly property string colCompValue: t("colCompValue")

    // 配置页
    readonly property string addRow: t("addRow")
    readonly property string deleteRow: t("deleteRow")
    readonly property string statusLoaded: t("statusLoaded")

    // 补偿页
    readonly property string compTableList: t("compTableList")
    readonly property string compDataTitle: t("compDataTitle")
    readonly property string addComp: t("addComp")
    readonly property string removeComp: t("removeComp")
    readonly property string colSource: t("colSource")
    readonly property string colType: t("colType")
    readonly property string colFileName: t("colFileName")
    readonly property string colPosition: t("colPosition")
    readonly property string currentFile: t("currentFile")
    readonly property string noFileSelected: t("noFileSelected")
    readonly property string selectCompHint: t("selectCompHint")

    // EtherCAT 状态页
    readonly property string ecatStatusLabel: t("ecatStatusLabel")
    readonly property string ecatLogLabel: t("ecatLogLabel")
    readonly property string ecatLinkOk: t("ecatLinkOk")
    readonly property string ecatLinkLost: t("ecatLinkLost")
    readonly property string ecatReset: t("ecatReset")
    readonly property string ecatResetting: t("ecatResetting")
    readonly property string ecatEnable: t("ecatEnable")
    readonly property string ecatDisable: t("ecatDisable")

    // 状态机监控页
    readonly property string smMotorTitle: t("smMotorTitle")
    readonly property string smExeTitle: t("smExeTitle")
    readonly property string smMotorMtrLabel: t("smMotorMtrLabel")
    readonly property string smExeMtrLabel: t("smExeMtrLabel")
    readonly property string smCurrentState: t("smCurrentState")
    readonly property string smPrevState: t("smPrevState")

    // 对话框
    readonly property string confirm: t("confirm")
    readonly property string cancel: t("cancel")
    readonly property string promptTitle: t("promptTitle")
    readonly property string confirmCreateTitle: t("confirmCreateTitle")
    readonly property string createBaseDirMsg: t("createBaseDirMsg")
    readonly property string createCompDirMsg: t("createCompDirMsg")
    readonly property string createConfigFileMsg: t("createConfigFileMsg")
    readonly property string createMotorFileMsg: t("createMotorFileMsg")
    readonly property string deleteCompTitle: t("deleteCompTitle")
    readonly property string deleteCompMsg: t("deleteCompMsg")
    readonly property string addCompTitle: t("addCompTitle")
    readonly property string sourceHint: t("sourceHint")
    readonly property string typeHint: t("typeHint")
    readonly property string fileHint: t("fileHint")
    readonly property string refreshComp: t("refreshComp")
    readonly property string needConnect: t("needConnect")
    readonly property string copy: t("copy")
    readonly property string copyAll: t("copyAll")
    readonly property string cut: t("cut")
    readonly property string paste: t("paste")
    readonly property string clear: t("clear")
    readonly property string allSourcesUsed: t("allSourcesUsed")
}

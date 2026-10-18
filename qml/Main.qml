import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1120
    height: 780
    minimumWidth: 880
    minimumHeight: 620
    visible: true
    title: "Reword"
    property int page: 0
    property color ink: Backend.dark ? "#eef0f3" : "#202733"
    property color muted: Backend.dark ? "#929ba8" : "#647080"
    property color surface: Backend.dark ? "#191e26" : "#ffffff"
    property color line: Backend.dark ? "#2c3440" : "#dce2e9"
    property color accent: Backend.dark ? "#85d4bc" : "#176d58"
    property var titles: ["Overview", "Correction", "Languages", "Applications", "History", "Insights", "Privacy"]
    property var subtitles: ["Offline spelling assistance.", "Choose the rules for spelling and punctuation.", "Choose languages and add personal words.", "You decide where Reword can work.", "Copy originals and manage saved corrections.", "Review words you often mistype.", "Control saved data and desktop shortcuts."]
    property string undoOriginal: ""
    property string undoResult: ""
    property var selectedHistory: ({})
    color: Backend.dark ? "#12161d" : "#f5f7fa"
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "Noto Sans"
    font.pixelSize: 14
    palette.window: color
    palette.windowText: ink
    palette.text: ink
    palette.placeholderText: muted
    palette.base: surface
    palette.button: surface
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: Backend.dark ? "#101b17" : "white"
    onClosing: function (close) {
        close.accepted = false;
        if (Backend.trayAvailable)
            root.hide();
        else
            Backend.quit();
    }

    function presentPendingConsent() {
        if (!root.visible || Backend.pendingName.length === 0 || consentDialog.visible)
            return;
        for (let dialog of [pauseDialog, historyDialog, clearDialog, practiceDialog]) {
            if (dialog.visible)
                return;
        }
        consentDialog.open();
    }

    component AppDialog: Dialog {
        id: appDialog
        header: Label {
            text: appDialog.title
            visible: text.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.bold: true
            padding: 12
            color: root.ink
            background: Rectangle { color: root.surface }
        }
        onVisibleChanged: if (!visible) Qt.callLater(root.presentPendingConsent)
        palette.window: root.surface
        palette.windowText: root.ink
        palette.text: root.ink
        palette.base: root.surface
        palette.button: root.surface
        palette.buttonText: root.ink
        palette.highlight: root.accent
        palette.highlightedText: Backend.dark ? "#101b17" : "white"
    }

    Connections {
        target: Backend
        function onShowWindow() {
            root.show();
            root.raise();
            root.requestActivate();
            root.presentPendingConsent();
        }
        function onPauseDialog() {
            root.show();
            pauseDialog.open();
        }
        function onRecoveryCleared() {
            root.undoOriginal = "";
            root.undoResult = "";
        }
        function onCorrectedText(original, corrected) {
            if (editor.text !== original)
                return;
            if (original !== corrected) {
                root.undoOriginal = original;
                root.undoResult = corrected;
                editor.text = corrected;
                Backend.recordApplied(original, corrected);
            }
        }
        function onUpdated() {
            root.presentPendingConsent();
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 214
            color: root.surface
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                RowLayout {
                    Layout.topMargin: 10
                    Layout.bottomMargin: 36
                    Rectangle {
                        width: 34
                        height: 34
                        radius: 9
                        color: root.accent
                        Label {
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            text: "r"
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            color: Backend.dark ? "#13231c" : "white"
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "reword"
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        Layout.leftMargin: 8
                    }
                }
                Repeater {
                    model: root.titles
                    delegate: Button {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        text: modelData
                        Accessible.name: modelData
                        contentItem: Label {
                            textFormat: Text.PlainText
                            text: parent.text
                            color: root.page === index ? root.accent : root.muted
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 14
                            font.weight: root.page === index ? Font.DemiBold : Font.Normal
                        }
                        background: Rectangle {
                            radius: 6
                            color: root.page === index ? (Backend.dark ? "#253a35" : "#e6f2ed") : parent.hovered ? (Backend.dark ? "#232a34" : "#edf1f5") : "transparent"
                            border.width: parent.activeFocus ? 1 : 0
                            border.color: root.accent
                        }
                        onClicked: root.page = index
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.line
                }
                Label {
                    textFormat: Text.PlainText
                    text: "ON THIS DEVICE"
                    color: root.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                    Layout.topMargin: 10
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Offline processing"
                    font.pixelSize: 13
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Development build · 0.1.0"
                    color: root.muted
                    font.pixelSize: 11
                }
                RowLayout {
                    Layout.topMargin: 8
                    Label {
                        textFormat: Text.PlainText
                        text: "Appearance"
                        color: root.muted
                        font.pixelSize: 12
                    }
                    ComboBox {
                        model: ["Dark", "Light"]
                        currentIndex: Backend.dark ? 0 : 1
                        implicitWidth: 85
                        onActivated: Backend.set("dark", currentIndex === 0)
                    }
                }
            }
        }
        Rectangle {
            Layout.fillHeight: true
            width: 1
            color: root.line
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 32
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        textFormat: Text.PlainText
                        text: root.titles[root.page]
                        font.pixelSize: 27
                        font.weight: Font.DemiBold
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: root.subtitles[root.page]
                        color: root.muted
                        font.pixelSize: 13
                    }
                }
                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: Backend.paused ? root.muted : root.accent
                }
                Label {
                    textFormat: Text.PlainText
                    text: Backend.status
                    color: Backend.paused ? root.muted : root.accent
                    font.weight: Font.Medium
                }
                Button {
                    text: Backend.paused ? "Resume" : "Pause"
                    onClicked: Backend.pause(Backend.paused ? 0 : -1)
                }
                Button {
                    text: "Pause for…"
                    onClicked: pauseDialog.open()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: root.line
            }
            Rectangle {
                visible: Backend.notice.length > 0
                Layout.fillWidth: true
                implicitHeight: noticeRow.implicitHeight + 24
                color: Backend.dark ? "#26352f" : "#e8f3ed"
                RowLayout {
                    id: noticeRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 32
                    anchors.rightMargin: 24
                    Label {
                        textFormat: Text.PlainText
                        text: Backend.notice
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: root.ink
                    }
                    ToolButton {
                        text: "Dismiss"
                        onClicked: Backend.clearNotice()
                    }
                }
            }
            StackLayout {
                currentIndex: root.page
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.topMargin: 24
                Layout.bottomMargin: 24
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Overview / writing workspace
                ColumnLayout {
                    Layout.margins: 32
                    spacing: 16
                    Label {
                        textFormat: Text.PlainText
                        text: "Writing workspace"
                        font.pixelSize: 18
                        font.weight: Font.Medium
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Paste a passage here when an app does not support direct correction."
                        color: root.muted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            id: editor
                            objectName: "workspaceEditor"
                            property string acceptedText: ""
                            property bool restoringText: false
                            onTextChanged: {
                                if (restoringText)
                                    return;
                                const count = Backend.characterCount(text);
                                if (count > Backend.settings.maxCharacters && count > Backend.characterCount(acceptedText)) {
                                    restoringText = true;
                                    text = acceptedText;
                                    restoringText = false;
                                    Backend.rejectOversizedText();
                                } else
                                    acceptedText = text;
                            }
                            placeholderText: "Write or paste your text…"
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            textFormat: TextEdit.PlainText
                            font.pixelSize: 16
                            padding: 20
                            background: Rectangle {
                                color: root.surface
                                border.color: editor.activeFocus ? root.accent : root.line
                                radius: 6
                            }
                            Accessible.name: "Writing workspace"
                        }
                    }
                    RowLayout {
                        Button {
                            text: Backend.busy ? "Correcting…" : "Correct text"
                            enabled: !Backend.busy && !Backend.paused && editor.length > 0
                            highlighted: true
                            onClicked: Backend.fix(editor.text)
                        }
                        Button {
                            text: "Cancel"
                            visible: Backend.busy
                            onClicked: Backend.cancel()
                        }
                        Button {
                            text: "Undo correction"
                            enabled: root.undoResult.length > 0 && editor.text === root.undoResult
                            onClicked: {
                                editor.restoringText = true;
                                editor.text = root.undoOriginal;
                                editor.acceptedText = root.undoOriginal;
                                editor.restoringText = false;
                                root.undoResult = "";
                            }
                        }
                        Button {
                            text: "Copy"
                            enabled: editor.length > 0
                            onClicked: Backend.copy(editor.text)
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: Backend.characterCount(editor.text).toLocaleString() + " / " + Backend.settings.maxCharacters.toLocaleString()
                            color: Backend.characterCount(editor.text) > Backend.settings.maxCharacters ? "#e48c80" : root.muted
                            font.pixelSize: 12
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Backend.recoveryAvailable
                        Label {
                            Layout.fillWidth: true
                            text: "Latest passage original is kept in memory for this session."
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            color: root.muted
                            font.pixelSize: 12
                        }
                        Button {
                            objectName: "copyRecoveryOriginal"
                            text: "Copy original"
                            enabled: Backend.recoveryAvailable
                            onClicked: Backend.copyRecoveryOriginal()
                        }
                        Button {
                            text: "Clear"
                            onClicked: Backend.clearRecovery()
                            Accessible.name: "Clear session recovery"
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: Backend.platform
                        color: root.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Direct correction waits for permission and a supported, non-sensitive field."
                        color: root.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Correction
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 24
                        anchors.margins: 32
                        Label {
                            textFormat: Text.PlainText
                            text: "While you type"
                            font.pixelSize: 18
                            font.weight: Font.Medium
                            Layout.topMargin: 24
                            Layout.leftMargin: 32
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 32
                            Layout.rightMargin: 32
                            spacing: 20
                            SettingRow {
                                title: "Spelling"
                                detail: "Correct confident mistakes at word boundaries. Preserve letter case."
                                settingKey: "spelling"
                            }
                            SettingRow {
                                title: "Correction notifications"
                                detail: "Show the spelling after a live correction. Off by default."
                                settingKey: "notifications"
                            }
                            RowLayout {
                                Label {
                                    textFormat: Text.PlainText
                                    text: "Sensitivity"
                                    Layout.fillWidth: true
                                }
                                ComboBox {
                                    model: ["Conservative", "Balanced"]
                                    currentIndex: Backend.settings.sensitivity
                                    onActivated: Backend.set("sensitivity", currentIndex)
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: root.line
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Selected text and workspace"
                                font.pixelSize: 18
                                font.weight: Font.Medium
                            }
                            SettingRow {
                                title: "Expand abbreviations"
                                detail: "Expand recognized forms such as im → I am."
                                settingKey: "abbreviations"
                            }
                            SettingRow {
                                title: "Capitalize sentence beginnings"
                                detail: "Capitalize English text after sentence or paragraph breaks."
                                settingKey: "capitalization"
                            }
                            SettingRow {
                                title: "Periods"
                                detail: "Fix duplicate periods while keeping intentional ellipses."
                                settingKey: "periods"
                            }
                            SettingRow {
                                title: "Finish prose paragraphs"
                                detail: "Add a terminal period to unpunctuated paragraphs of three or more words."
                                settingKey: "paragraphPeriods"
                                enabled: Backend.settings.periods
                            }
                            SettingRow {
                                title: "Commas"
                                detail: "Fix repeated English and Persian commas."
                                settingKey: "commas"
                            }
                            SettingRow {
                                title: "Commas after introductory phrases"
                                detail: "Recognize openings such as However and بنابراین."
                                settingKey: "introductoryCommas"
                                enabled: Backend.settings.commas
                            }
                            SettingRow {
                                title: "Question marks"
                                detail: "Enable spacing fixes for English and Persian question marks."
                                settingKey: "questionMarks"
                            }
                            SettingRow {
                                title: "Exclamation marks"
                                detail: "Enable spacing fixes for exclamation marks."
                                settingKey: "exclamationMarks"
                            }
                            SettingRow {
                                title: "Semicolons"
                                detail: "Enable spacing fixes for English and Persian semicolons."
                                settingKey: "semicolons"
                            }
                            SettingRow {
                                title: "Colons"
                                detail: "Enable spacing fixes for colons."
                                settingKey: "colons"
                            }
                            SettingRow {
                                title: "Punctuation spacing"
                                detail: "Remove spaces before the punctuation categories you enable."
                                settingKey: "spacing"
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Punctuation rules are conservative. Reword does not guess the meaning of a sentence."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: root.line
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Keyboard shortcuts"
                                font.pixelSize: 18
                            }
                            Repeater {
                                model: [
                                    {
                                        key: "fixShortcut",
                                        title: "Correct selection"
                                    },
                                    {
                                        key: "undoShortcut",
                                        title: "Undo last direct correction"
                                    },
                                    {
                                        key: "pauseShortcut",
                                        title: "Pause / resume"
                                    }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.title
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        id: shortcut
                                        text: Backend.settings[modelData.key]
                                        implicitWidth: 170
                                        Accessible.name: modelData.title + " shortcut"
                                    }
                                    Button {
                                        text: "Save"
                                        onClicked: Backend.set(modelData.key, shortcut.text)
                                    }
                                }
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: Backend.settings.shortcutsAvailable ? "Global shortcuts registered." : "Global shortcuts unavailable here. Wayland uses input-method integration."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Item {
                                height: 24
                            }
                        }
                    }
                }

                // Languages
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 24
                        ColumnLayout {
                            Layout.topMargin: 32
                            Layout.leftMargin: 32
                            Layout.rightMargin: 32
                            Layout.fillWidth: true
                            spacing: 24
                            SettingRow {
                                title: "English"
                                detail: "Enable English spelling and writing rules."
                                settingKey: "english"
                            }
                            SettingRow {
                                title: "Persian"
                                detail: "Enable Persian spelling and writing rules."
                                settingKey: "persian"
                            }
                            SettingRow {
                                title: "Normalize Persian characters"
                                detail: "Use Persian forms of Arabic yeh and kaf."
                                settingKey: "normalizePersian"
                            }
                            SettingRow {
                                title: "Persian half-space"
                                detail: "Join recognized verb prefixes with a half-space."
                                settingKey: "halfSpace"
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: root.line
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Personal dictionary"
                                font.pixelSize: 18
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Keep names and specialist words out of spelling corrections."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                TextField {
                                    id: newWord
                                    placeholderText: "Add a word"
                                    Layout.fillWidth: true
                                    onAccepted: {
                                        Backend.addWord(text);
                                        text = "";
                                    }
                                }
                                Button {
                                    text: "Add"
                                    onClicked: {
                                        Backend.addWord(newWord.text);
                                        newWord.clear();
                                    }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: childrenRect.height
                                spacing: 8
                                Repeater {
                                    model: Backend.words
                                    delegate: Button {
                                        required property string modelData
                                        text: modelData + "  ×"
                                        contentItem: Label {
                                            textFormat: Text.PlainText
                                            text: parent.text
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        Accessible.name: "Remove " + modelData
                                        onClicked: Backend.removeWord(modelData)
                                    }
                                }
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Custom replacements"
                                font.pixelSize: 18
                            }
                            RowLayout {
                                TextField {
                                    id: replaceFrom
                                    placeholderText: "Typed word"
                                    Layout.fillWidth: true
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: "→"
                                }
                                TextField {
                                    id: replaceTo
                                    placeholderText: "Replacement"
                                    Layout.fillWidth: true
                                }
                                Button {
                                    text: "Add"
                                    onClicked: {
                                        Backend.replacement(replaceFrom.text, replaceTo.text);
                                        replaceFrom.clear();
                                        replaceTo.clear();
                                    }
                                }
                            }
                            Repeater {
                                model: Backend.replacements
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.original + " → " + modelData.corrected
                                        Layout.fillWidth: true
                                    }
                                    Button {
                                        text: "Remove"
                                        onClicked: Backend.replacement(modelData.original, "")
                                    }
                                }
                            }
                            Item {
                                height: 20
                            }
                        }
                    }
                }

                // Applications
                ColumnLayout {
                    Layout.margins: 32
                    spacing: 20
                    SettingRow {
                        title: "Allow browsers"
                        detail: "Individual browsers also need your permission."
                        settingKey: "browsers"
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "New apps stay off until you allow them. Passwords and uncertain fields stay protected even in allowed apps."
                        color: root.muted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 1
                        model: Backend.apps
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 86
                            color: root.surface
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.name
                                        font.weight: Font.Medium
                                    }
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.id
                                        elide: Text.ElideMiddle
                                        color: root.muted
                                        font.pixelSize: 11
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        textFormat: Text.PlainText
                                        visible: modelData.decision === "later"
                                        text: "Ask again from " + modelData.remind
                                        color: root.muted
                                        font.pixelSize: 11
                                    }
                                }
                                ComboBox {
                                    model: ["Allowed", "Disabled", "Ask tomorrow"]
                                    displayText: currentIndex < 0 ? "Not decided" : model[currentIndex]
                                    currentIndex: modelData.decision === "allow" ? 0 : modelData.decision === "deny" ? 1 : modelData.decision === "later" ? 2 : -1
                                    Accessible.name: "Permission for " + modelData.name
                                    onActivated: Backend.decide(modelData.id, currentIndex === 0 ? "allow" : currentIndex === 2 ? "later" : "deny")
                                }
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            visible: Backend.apps.length === 0
                            text: "Detected apps will appear here."
                            color: root.muted
                        }
                    }
                }

                // History
                ColumnLayout {
                    Layout.margins: 32
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Backend.recoveryAvailable
                        Label {
                            Layout.fillWidth: true
                            text: "Session recovery · latest passage"
                            textFormat: Text.PlainText
                            color: root.muted
                            wrapMode: Text.WordWrap
                        }
                        Button {
                            text: "Copy original"
                            onClicked: Backend.copyRecoveryOriginal()
                        }
                        Button {
                            text: "Clear"
                            Accessible.name: "Clear session recovery"
                            onClicked: Backend.clearRecovery()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            textFormat: Text.PlainText
                            text: Backend.history.length + " saved corrections"
                            color: root.muted
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Clear history"
                            enabled: Backend.history.length > 0
                            onClicked: clearDialog.open()
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        clip: true
                        model: Backend.history
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view.width
                            height: 100
                            background: Rectangle {
                                color: root.surface
                                radius: 4
                                border.color: parent.activeFocus ? root.accent : root.line
                            }
                            contentItem: ColumnLayout {
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.time
                                    color: root.muted
                                    font.pixelSize: 11
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.corrected.slice(0, 180).replace(/\n/g, " ")
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: "View original and correction"
                                    color: root.accent
                                    font.pixelSize: 12
                                }
                            }
                            onClicked: {
                                root.selectedHistory = Backend.historyDetail(modelData.id);
                                historyDialog.open();
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            visible: Backend.history.length === 0
                            text: "No saved corrections yet."
                            color: root.muted
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Up to " + Backend.settings.historyCount + " records · " + Backend.settings.historyMiB + " MiB · " + Backend.settings.historyDays + " days. Oldest records are removed first."
                        color: root.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Insights
                ColumnLayout {
                    Layout.margins: 32
                    spacing: 20
                    SettingRow {
                        title: "Count spelling mistakes"
                        detail: "Save word pairs and counts from live corrections. Never sentences."
                        settingKey: "statistics"
                    }
                    RowLayout {
                        Label {
                            textFormat: Text.PlainText
                            text: "Your recurring words"
                            font.pixelSize: 18
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Clear counts"
                            enabled: Backend.insights.length > 0
                            onClicked: Backend.clearStatistics()
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: Backend.insights
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 64
                            color: root.surface
                            radius: 4
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.original
                                    color: root.muted
                                    Layout.preferredWidth: 140
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: "→"
                                    color: root.muted
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.corrected
                                    Layout.fillWidth: true
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.count + " times"
                                    color: root.muted
                                }
                                Button {
                                    text: "Practice"
                                    onClicked: {
                                        practiceWord.text = "";
                                        practiceDialog.answer = modelData.corrected;
                                        practiceDialog.open();
                                    }
                                }
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            visible: Backend.insights.length === 0
                            text: Backend.settings.statistics ? "Words appear after live corrections." : "Counting is off. Enable it when you want to track mistakes."
                            color: root.muted
                            wrapMode: Text.WordWrap
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                // Privacy
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 24
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.margins: 32
                            spacing: 24
                            Label {
                                textFormat: Text.PlainText
                                text: "Your text stays here."
                                font.pixelSize: 22
                                font.weight: Font.Medium
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "No cloud processing. No telemetry. No keystroke log. Reword skips password and unrecognized fields."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            SettingRow {
                                title: "Save correction history"
                                detail: "Store originals and results only for explicit passage corrections."
                                settingKey: "history"
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "History is local, not encrypted at rest. Turning it off does not delete existing records."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Repeater {
                                model: [
                                    {
                                        key: "historyCount",
                                        title: "Maximum records",
                                        min: 1,
                                        max: 500
                                    },
                                    {
                                        key: "historyMiB",
                                        title: "Maximum history size (MiB)",
                                        min: 1,
                                        max: 100
                                    },
                                    {
                                        key: "historyDays",
                                        title: "Keep history for (days)",
                                        min: 1,
                                        max: 365
                                    },
                                    {
                                        key: "maxCharacters",
                                        title: "Characters per correction",
                                        min: 100,
                                        max: 50000
                                    }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.title
                                        Layout.fillWidth: true
                                    }
                                    SpinBox {
                                        from: modelData.min
                                        to: modelData.max
                                        value: Backend.settings[modelData.key]
                                        editable: true
                                        onValueModified: Backend.set(modelData.key, value)
                                        Accessible.name: modelData.title
                                    }
                                }
                            }
                            Button {
                                text: "Delete saved history…"
                                onClicked: clearDialog.open()
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: root.line
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Desktop integration"
                                font.pixelSize: 18
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: Backend.platform
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Wayland needs the Reword Fcitx5 addon and a compatible input-method connection. Some apps expose no safe editing API; use the writing workspace in those cases. Tray support depends on your desktop."
                                color: root.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Button {
                                text: "Quit Reword"
                                onClicked: Backend.quit()
                            }
                            Item {
                                height: 20
                            }
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: consentDialog
        anchors.centerIn: parent
        width: 480
        modal: false
        title: "Allow Reword in " + Backend.pendingName + "?"
        header: Label {
            color: root.ink
            textFormat: Text.PlainText
            text: consentDialog.title
            wrapMode: Text.Wrap
            padding: 20
            font.pixelSize: 18
        }
        closePolicy: Popup.NoAutoClose
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            Label {
                color: root.ink
                textFormat: Text.PlainText
                text: "Reword will only correct recognized, non-sensitive text fields. You can change this decision in Applications."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Button {
                    text: "No"
                    onClicked: {
                        consentDialog.close();
                        Backend.consent("deny");
                    }
                }
                Button {
                    text: "Not now"
                    onClicked: {
                        consentDialog.close();
                        Backend.consent("later");
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Yes"
                    highlighted: true
                    onClicked: {
                        consentDialog.close();
                        Backend.consent("allow");
                    }
                }
            }
            Label {
                textFormat: Text.PlainText
                text: "Not now asks again on your first use tomorrow."
                color: root.muted
                font.pixelSize: 12
            }
        }
    }
    AppDialog {
        id: pauseDialog
        anchors.centerIn: parent
        title: "Pause Reword"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Backend.pause(hours.value * 60 + minutes.value)
        RowLayout {
            SpinBox {
                id: hours
                from: 0
                to: 168
                editable: true
                Accessible.name: "Hours"
            }
            Label {
                color: root.ink
                textFormat: Text.PlainText
                text: "hours"
            }
            SpinBox {
                id: minutes
                from: 0
                to: 59
                value: 15
                editable: true
                Accessible.name: "Minutes"
            }
            Label {
                color: root.ink
                textFormat: Text.PlainText
                text: "minutes"
            }
        }
    }
    AppDialog {
        id: historyDialog
        anchors.centerIn: parent
        width: Math.min(820, root.width - 80)
        height: Math.min(560, root.height - 80)
        title: "Saved correction"
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Label {
                        color: root.ink
                        textFormat: Text.PlainText
                        text: "Original"
                        font.weight: Font.Medium
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            color: root.ink
                            text: root.selectedHistory.original || ""
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            Accessible.name: "Original text"
                        }
                    }
                    Button {
                        text: "Copy original"
                        onClicked: Backend.copy(root.selectedHistory.original || "")
                    }
                }
                Rectangle {
                    width: 1
                    Layout.fillHeight: true
                    color: root.line
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Label {
                        color: root.ink
                        textFormat: Text.PlainText
                        text: "Corrected"
                        font.weight: Font.Medium
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            color: root.ink
                            text: root.selectedHistory.corrected || ""
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            Accessible.name: "Corrected text"
                        }
                    }
                    Button {
                        text: "Copy corrected"
                        onClicked: Backend.copy(root.selectedHistory.corrected || "")
                    }
                }
            }
            Button {
                text: "Delete this record"
                onClicked: {
                    Backend.deleteHistory(root.selectedHistory.id);
                    root.selectedHistory = ({});
                    historyDialog.close();
                }
            }
        }
        onClosed: root.selectedHistory = ({})
    }
    AppDialog {
        id: clearDialog
        anchors.centerIn: parent
        title: "Delete all saved text?"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            color: root.ink
            textFormat: Text.PlainText
            text: "Originals and corrections will be removed from local history."
            wrapMode: Text.WordWrap
            width: 400
        }
        onAccepted: Backend.clearHistory()
    }
    AppDialog {
        id: practiceDialog
        anchors.centerIn: parent
        title: "Practice spelling"
        modal: true
        standardButtons: Dialog.Close
        property string answer: ""
        ColumnLayout {
            Label {
                color: root.ink
                textFormat: Text.PlainText
                text: "Type: " + practiceDialog.answer
                font.pixelSize: 20
            }
            TextField {
                color: root.ink
                id: practiceWord
                placeholderText: "Write the word"
                Accessible.name: "Practice word"
                background: Rectangle {
                    color: root.surface
                    border.color: practiceWord.activeFocus ? root.accent : root.line
                }
            }
            Label {
                textFormat: Text.PlainText
                text: practiceWord.text === practiceDialog.answer ? "Correct." : ""
                color: root.accent
            }
        }
    }
}

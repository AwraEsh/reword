import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string title
    property string detail
    property string settingKey
    required property var settingsSnapshot
    Layout.fillWidth: true
    spacing: 24
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Label {
            textFormat: Text.PlainText
            text: row.title
            font.pixelSize: 15
            font.weight: Font.Medium
        }
        Label {
            textFormat: Text.PlainText
            text: row.detail
            opacity: 0.62
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    Switch {
        id: control
        checked: Boolean(row.settingsSnapshot[row.settingKey])
        Accessible.name: row.title
        onToggled: Backend.set(row.settingKey, checked)
        indicator: Rectangle {
            implicitWidth: 44
            implicitHeight: 24
            x: control.leftPadding
            y: (control.height - height) / 2
            radius: height / 2
            color: control.checked ? control.palette.highlight : Backend.dark ? "#2c3440" : "#e1e5eb"
            border.color: control.checked ? control.palette.highlight : Backend.dark ? "#536071" : "#b4becb"
            opacity: control.enabled ? 1 : 0.45
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: height / 2
                color: "transparent"
                border.width: 2
                border.color: control.activeFocus ? control.palette.highlight : "transparent"
            }
            Rectangle {
                width: 16
                height: 16
                radius: 8
                x: control.checked ? parent.width - width - 4 : 4
                y: 4
                color: control.checked && Backend.dark ? "#14251f" : "#ffffff"
                border.color: control.checked ? "transparent" : "#8995a4"
            }
        }
    }
}

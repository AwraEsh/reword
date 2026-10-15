import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string title
    property string detail
    property string settingKey
    Layout.fillWidth: true
    spacing: 24
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Label { text: row.title; font.pixelSize: 15; font.weight: Font.Medium }
        Label { text: row.detail; opacity: 0.62; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    Switch {
        checked: Boolean(Backend.settings[row.settingKey])
        Accessible.name: row.title
        onToggled: Backend.set(row.settingKey, checked)
    }
}

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: qsTr("Профили")
    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.mediumSpacing
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: qsTr("Профили извлекаются статически из NFQWS2_OPT. Shell-код и Lua не исполняются. Запись выполняется через helper с проверкой SHA-256, shell-синтаксиса и резервным копированием. Служба автоматически не перезапускается.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Controls.Button { text: profileController.dirty ? qsTr("Отменить изменения") : qsTr("Обновить"); icon.name: profileController.dirty ? "edit-undo" : "view-refresh"; enabled: !profileController.busy; onClicked: { if (profileController.dirty) discardDialog.open(); else profileController.read() } }
            Controls.Button { text: qsTr("Создать"); icon.name: "list-add"; enabled: profileController.loaded && !profileController.busy && profileController.profiles.length < 128; onClicked: editor.openFor(-1, []) }
            Controls.Button { text: qsTr("Сохранить"); icon.name: "document-save"; enabled: profileController.dirty && !profileController.busy; onClicked: saveDialog.open() }
        }
        Repeater {
            model: profileController.profiles
            delegate: Controls.ItemDelegate {
                required property var modelData
                Layout.fillWidth: true
                highlighted: profileController.selectedIndex === modelData.index
                text: modelData.name + " — " + modelData.arguments.length + qsTr(" параметров")
                onClicked: profileController.selectedIndex = modelData.index
                contentItem: ColumnLayout {
                    Controls.Label {
                        Layout.fillWidth: true
                        text: modelData.name + " — " + modelData.arguments.join(" ")
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Controls.Button { text: qsTr("Изменить"); enabled: !profileController.busy; onClicked: editor.openFor(modelData.index, modelData.arguments) }
                        Controls.Button { text: qsTr("Копировать"); enabled: !profileController.busy && profileController.profiles.length < 128; onClicked: profileController.copy(modelData.index) }
                        Controls.Button { text: qsTr("Удалить"); enabled: !profileController.busy && profileController.profiles.length > 1; onClicked: { deleteDialog.indexToDelete = modelData.index; deleteDialog.open() } }
                    }
                }
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: profileController.status.length > 0
            type: profileController.status.startsWith("Не удалось") || profileController.status.startsWith("Ошибка") || profileController.status.startsWith("config changed") || profileController.status.startsWith("stale config") ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
            text: profileController.status
        }
        Controls.ProgressBar { Layout.fillWidth: true; visible: profileController.busy; indeterminate: true }
    }
    Controls.Dialog {
        id: editor
        property int profileIndex: -1
        title: profileIndex < 0 ? qsTr("Новый профиль") : qsTr("Изменить профиль")
        modal: true
        standardButtons: Controls.Dialog.Save | Controls.Dialog.Cancel
        function openFor(index, args) { profileIndex = index; argumentEditor.text = args.join("\n"); open() }
        onAccepted: {
            const args = argumentEditor.text.split("\n").map(function(s) { return s.trim() }).filter(function(s) { return s.length > 0 })
            if (profileIndex < 0) profileController.add(args)
            else profileController.edit(profileIndex, args)
        }
        ColumnLayout {
            width: Math.min(page.availableWidth, Kirigami.Units.gridUnit * 40)
            Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Один аргумент на строку. Не добавляйте --new, кавычки, shell-подстановки или пробелы внутри аргумента.") }
            Controls.TextArea { id: argumentEditor; Layout.fillWidth: true; Layout.preferredHeight: 320; Layout.minimumWidth: 0; wrapMode: TextEdit.NoWrap; font.family: "monospace"; selectByMouse: true; Accessible.name: qsTr("Аргументы профиля"); Accessible.description: qsTr("Один аргумент на строку.") }
        }
    }
    Controls.Dialog {
        id: discardDialog
        title: qsTr("Отменить изменения?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: profileController.read(true)
        Controls.Label { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Все несохранённые изменения профилей будут потеряны. Конфигурация будет повторно загружена с диска.") }
    }
    Controls.Dialog {
        id: saveDialog
        title: qsTr("Сохранить профили?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: profileController.save()
        Controls.Label { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Helper проверит контрольную сумму и shell-синтаксис, создаст резервную копию и атомарно заменит конфигурацию. Служба не будет перезапущена.") }
    }
    Controls.Dialog {
        id: deleteDialog
        property int indexToDelete: -1
        title: qsTr("Удалить профиль?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: profileController.remove(indexToDelete)
        Controls.Label { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Удалить выбранный профиль? Изменения можно отменить повторной загрузкой конфигурации.") }
    }
    Component.onCompleted: profileController.read()
}

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    required property var configController
    title: qsTr("Конфигурация")

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.mediumSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: qsTr("Файл config является shell include. Он не исполняется при проверке. Перед записью helper повторно проверяет синтаксис, контрольную сумму и создаёт резервную копию. Служба автоматически не перезапускается.")
        }

        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Controls.Button {
                text: qsTr("Прочитать")
                icon.name: "document-open"
                enabled: !page.configController.busy
                onClicked: page.configController.read()
            }
            Controls.Button {
                text: qsTr("Проверить")
                icon.name: "check"
                enabled: page.configController.loaded && !page.configController.busy
                onClicked: page.configController.validate()
            }
            Controls.Button {
                text: qsTr("Сохранить")
                icon.name: "document-save"
                enabled: page.configController.loaded && !page.configController.busy
                onClicked: saveConfirmation.open()
            }
            Controls.Button {
                text: qsTr("Откатить")
                icon.name: "edit-undo"
                enabled: page.configController.loaded && !page.configController.busy
                onClicked: rollbackConfirmation.open()
            }
        }

        Controls.TextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 360
            enabled: page.configController.loaded && !page.configController.busy
            text: page.configController.configText
            Accessible.name: qsTr("Содержимое файла конфигурации")
            Accessible.description: qsTr("Редактор shell-конфигурации zapret2. Проверка не исполняет файл.")
            onTextChanged: if (text !== page.configController.configText) page.configController.configText = text
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            selectByMouse: true
            placeholderText: qsTr("Нажмите «Прочитать», чтобы загрузить /opt/zapret2/config")
        }

        Controls.ProgressBar {
            Layout.fillWidth: true
            visible: page.configController.busy
            indeterminate: true
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.configController.status.length > 0
            type: page.configController.status.startsWith("Ошибка") || page.configController.status.startsWith("Не удалось")
                  ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
            text: page.configController.status
        }
    }

    Controls.Dialog {
        id: saveConfirmation
        title: qsTr("Сохранить конфигурацию?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: page.configController.save()
        Controls.Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Перед записью будет создана резервная копия. Helper проверит контрольную сумму, повторно проверит shell-синтаксис и атомарно заменит файл. Служба не будет перезапущена.")
        }
    }

    Controls.Dialog {
        id: rollbackConfirmation
        title: qsTr("Откатить конфигурацию?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: page.configController.rollback()
        Controls.Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Будет восстановлена последняя резервная копия. Текущая конфигурация также будет сохранена в резервную копию.")
        }
    }

    Component.onCompleted: page.configController.read()
}

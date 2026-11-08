import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
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
                enabled: !configController.busy
                onClicked: configController.read()
            }
            Controls.Button {
                text: qsTr("Проверить")
                icon.name: "check"
                enabled: configController.loaded && !configController.busy
                onClicked: configController.validate()
            }
            Controls.Button {
                text: qsTr("Сохранить")
                icon.name: "document-save"
                enabled: configController.loaded && !configController.busy
                onClicked: saveConfirmation.open()
            }
            Controls.Button {
                text: qsTr("Откатить")
                icon.name: "edit-undo"
                enabled: configController.loaded && !configController.busy
                onClicked: rollbackConfirmation.open()
            }
        }

        Controls.TextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 360
            enabled: configController.loaded && !configController.busy
            text: configController.configText
            Accessible.name: qsTr("Содержимое файла конфигурации")
            Accessible.description: qsTr("Редактор shell-конфигурации zapret2. Проверка не исполняет файл.")
            onTextChanged: if (text !== configController.configText) configController.configText = text
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            selectByMouse: true
            placeholderText: qsTr("Нажмите «Прочитать», чтобы загрузить /opt/zapret2/config")
        }

        Controls.ProgressBar {
            Layout.fillWidth: true
            visible: configController.busy
            indeterminate: true
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: configController.status.length > 0
            type: configController.status.startsWith("Ошибка") || configController.status.startsWith("Не удалось")
                  ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
            text: configController.status
        }
    }

    Controls.Dialog {
        id: saveConfirmation
        title: qsTr("Сохранить конфигурацию?")
        modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: configController.save()
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
        onAccepted: configController.rollback()
        Controls.Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Будет восстановлена последняя резервная копия. Текущая конфигурация также будет сохранена в резервную копию.")
        }
    }

    Component.onCompleted: configController.read()
}

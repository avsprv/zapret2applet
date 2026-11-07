import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    title: qsTr("Подбор стратегий")

    Connections {
        target: blockcheckController
        function onConfirmationRequired(title, message) {
            stopServiceDialog.title = title
            stopServiceDialog.message = message
            stopServiceDialog.open()
        }
    }

    FileDialog {
        id: saveLogDialog
        title: qsTr("Сохранить журнал диагностики")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Текстовые файлы (*.txt)"), qsTr("Все файлы (*)")]
        defaultSuffix: "txt"
        onAccepted: blockcheckController.saveLog(selectedFile)
    }

    Kirigami.Dialog {
        id: stopServiceDialog
        property string message: ""
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: blockcheckController.confirmStop(true)
        onRejected: blockcheckController.confirmStop(false)
        Controls.Label {
            Layout.fillWidth: true
            text: stopServiceDialog.message
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: blockcheckController.recoveryRequired
            type: Kirigami.MessageType.Error
            text: qsTr("Восстановление zapret2 не подтверждено. Управление службой и обновление заблокированы; проверьте журнал и повторите восстановление после устранения причины.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: blockcheckController.status.length > 0
            text: blockcheckController.status
            type: {
                switch (blockcheckController.statusSeverity) {
                case "Error": return Kirigami.MessageType.Error
                case "Warning": return Kirigami.MessageType.Warning
                case "Success": return Kirigami.MessageType.Positive
                default: return Kirigami.MessageType.Information
                }
            }
        }

        Controls.GroupBox {
            Layout.fillWidth: true
            title: qsTr("Параметры диагностики")
            ColumnLayout {
                anchors.fill: parent
                Controls.Label { text: qsTr("Домены (от 1 до 10, через пробел)") }
                Controls.TextField {
                    Layout.fillWidth: true
                    text: blockcheckController.domains
                    onTextEdited: blockcheckController.domains = text
                    enabled: !blockcheckController.busy
                    placeholderText: "rutracker.org"
                    Accessible.name: qsTr("Домены для проверки")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    Controls.Label { text: qsTr("IP-протокол") }
                    Controls.ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("IPv4"), qsTr("IPv6"), qsTr("IPv4 + IPv6")]
                        currentIndex: blockcheckController.ipVersion === "6" ? 1 : blockcheckController.ipVersion === "46" ? 2 : 0
                        onActivated: blockcheckController.ipVersion = ["4", "6", "46"][currentIndex]
                        enabled: !blockcheckController.busy
                    }
                    Controls.Label { text: qsTr("Уровень сканирования") }
                    Controls.ComboBox {
                        Layout.fillWidth: true
                        model: ["veryquick", "quick", "standard", "force"]
                        currentIndex: model.indexOf(blockcheckController.scanLevel)
                        onActivated: blockcheckController.scanLevel = currentText
                        enabled: !blockcheckController.busy
                    }
                    Controls.Label { text: qsTr("Количество повторов") }
                    Controls.SpinBox {
                        from: 1; to: 10
                        value: blockcheckController.repeats
                        onValueModified: blockcheckController.repeats = value
                        enabled: !blockcheckController.busy
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Controls.CheckBox { text: "HTTP"; checked: blockcheckController.http; onToggled: blockcheckController.http = checked; enabled: !blockcheckController.busy }
                    Controls.CheckBox { text: "HTTPS TLS 1.2"; checked: blockcheckController.tls12; onToggled: blockcheckController.tls12 = checked; enabled: !blockcheckController.busy }
                    Controls.CheckBox { text: "HTTPS TLS 1.3"; checked: blockcheckController.tls13; onToggled: blockcheckController.tls13 = checked; enabled: !blockcheckController.busy }
                    Controls.CheckBox { text: "HTTP/3 (QUIC)"; checked: blockcheckController.http3; onToggled: blockcheckController.http3 = checked; enabled: !blockcheckController.busy }
                }
                RowLayout {
                    Controls.Button {
                        text: qsTr("Начать подбор")
                        icon.name: "media-playback-start"
                        enabled: !blockcheckController.busy && !blockcheckController.updateBusy && !blockcheckController.recoveryRequired
                        onClicked: blockcheckController.start()
                    }
                    Controls.Button {
                        text: qsTr("Отмена")
                        icon.name: "dialog-cancel"
                        enabled: blockcheckController.busy
                        onClicked: blockcheckController.cancel()
                    }
                }
            }
        }

        Kirigami.Heading { level: 2; text: qsTr("Найденные стратегии") }
        Controls.Label {
            Layout.fillWidth: true
            visible: blockcheckController.results.length === 0
            text: qsTr("Стратегии пока не найдены.")
            opacity: 0.7
        }
        Repeater {
            model: blockcheckController.results
            delegate: Controls.ItemDelegate {
                Layout.fillWidth: true
                text: modelData.label
                onClicked: blockcheckController.copyStrategy(index)
                Accessible.name: qsTr("Копировать стратегию: %1").arg(modelData.label)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading { level: 2; text: qsTr("Журнал диагностики") }
            Item { Layout.fillWidth: true }
            Controls.Button {
                text: qsTr("Сохранить журнал")
                icon.name: "document-save"
                enabled: blockcheckController.logText.length > 0
                onClicked: saveLogDialog.open()
            }
        }
        Controls.TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            readOnly: true
            text: blockcheckController.logText
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            Accessible.name: qsTr("Журнал диагностики")
            Accessible.description: qsTr("Вывод процесса blockcheck2 в режиме только чтения")
        }
        Controls.Label {
            Layout.fillWidth: true
            text: qsTr("Найденные стратегии являются рекомендациями blockcheck2 и требуют ручной проверки перед применением.")
            wrapMode: Text.WordWrap
            opacity: 0.75
        }
    }
}

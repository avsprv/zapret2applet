import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    required property var blockcheckController
    title: qsTr("Подбор стратегий")

    Connections {
        target: page.blockcheckController
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
        onAccepted: page.blockcheckController.saveLog(selectedFile)
    }

    Kirigami.Dialog {
        id: stopServiceDialog
        property string message: ""
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: page.blockcheckController.confirmStop(true)
        onRejected: page.blockcheckController.confirmStop(false)
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
            visible: page.blockcheckController.recoveryRequired
            type: Kirigami.MessageType.Error
            text: qsTr("Восстановление zapret2 не подтверждено. Управление службой и обновление заблокированы. Проверьте журнал; после устранения причины можно повторить восстановление.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.blockcheckController.status.length > 0
            text: page.blockcheckController.status
            type: {
                switch (page.blockcheckController.statusSeverity) {
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
                    text: page.blockcheckController.domains
                    onTextEdited: page.blockcheckController.domains = text
                    enabled: !page.blockcheckController.busy
                    placeholderText: "rutracker.org"
                    Accessible.name: qsTr("Домены для проверки")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.availableWidth < Kirigami.Units.gridUnit * 35 ? 1 : 2
                    Controls.Label { text: qsTr("IP-протокол") }
                    Controls.ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("IPv4"), qsTr("IPv6"), qsTr("IPv4 + IPv6")]
                        currentIndex: page.blockcheckController.ipVersion === "6" ? 1 : page.blockcheckController.ipVersion === "46" ? 2 : 0
                        onActivated: page.blockcheckController.ipVersion = ["4", "6", "46"][currentIndex]
                        enabled: !page.blockcheckController.busy
                    }
                    Controls.Label { text: qsTr("Уровень сканирования") }
                    Controls.ComboBox {
                        Layout.fillWidth: true
                        model: ["veryquick", "quick", "standard", "force"]
                        currentIndex: model.indexOf(page.blockcheckController.scanLevel)
                        onActivated: page.blockcheckController.scanLevel = currentText
                        enabled: !page.blockcheckController.busy
                    }
                    Controls.Label { text: qsTr("Количество повторов") }
                    Controls.SpinBox {
                        from: 1; to: 10
                        value: page.blockcheckController.repeats
                        onValueModified: page.blockcheckController.repeats = value
                        enabled: !page.blockcheckController.busy
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Controls.CheckBox { text: "HTTP"; checked: page.blockcheckController.http; onToggled: page.blockcheckController.http = checked; enabled: !page.blockcheckController.busy }
                    Controls.CheckBox { text: "HTTPS TLS 1.2"; checked: page.blockcheckController.tls12; onToggled: page.blockcheckController.tls12 = checked; enabled: !page.blockcheckController.busy }
                    Controls.CheckBox { text: "HTTPS TLS 1.3"; checked: page.blockcheckController.tls13; onToggled: page.blockcheckController.tls13 = checked; enabled: !page.blockcheckController.busy }
                    Controls.CheckBox { text: "HTTP/3 (QUIC)"; checked: page.blockcheckController.http3; onToggled: page.blockcheckController.http3 = checked; enabled: !page.blockcheckController.busy }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Controls.Button {
                        text: qsTr("Начать подбор")
                        icon.name: "media-playback-start"
                        enabled: !page.blockcheckController.busy && !page.blockcheckController.updateBusy && !page.blockcheckController.recoveryRequired
                        onClicked: page.blockcheckController.start()
                    }
                    Controls.Button {
                        text: qsTr("Отмена")
                        icon.name: "dialog-cancel"
                        enabled: page.blockcheckController.cancelAvailable
                        onClicked: page.blockcheckController.cancel()
                    }
                    Controls.Button {
                        text: qsTr("Повторить восстановление")
                        icon.name: "view-refresh"
                        visible: page.blockcheckController.recoveryRequired
                        enabled: page.blockcheckController.recoveryRetryAvailable
                        onClicked: page.blockcheckController.retryRecovery()
                    }
                }
            }
        }

        Kirigami.Heading { level: 2; text: qsTr("Найденные стратегии") }
        Controls.Label {
            Layout.fillWidth: true
            visible: page.blockcheckController.results.length === 0
            text: qsTr("Стратегии пока не найдены.")
            opacity: 0.7
        }
        Repeater {
            model: page.blockcheckController.results
            delegate: Controls.ItemDelegate {
                Layout.fillWidth: true
                text: modelData.label
                onClicked: page.blockcheckController.copyStrategy(index)
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
                enabled: page.blockcheckController.logText.length > 0
                onClicked: saveLogDialog.open()
            }
        }
        Controls.TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            readOnly: true
            text: page.blockcheckController.logText
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

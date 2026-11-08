import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    required property var logsController

    title: qsTr("Мониторинг")

    property bool autoRefresh: false

    Component.onCompleted: page.logsController.readLogs(page.logsController.lineCount)
    onVisibleChanged: {
        if (visible && autoRefresh)
            refreshTimer.start()
        else
            refreshTimer.stop()
    }

    Timer {
        id: refreshTimer
        interval: 10000
        repeat: true
        onTriggered: page.logsController.readLogs(page.logsController.lineCount)
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.mediumSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: qsTr("Показывается журнал только фиксированной службы zapret2.service. Чтение выполняется через read-only действие Polkit; журнал не изменяет состояние службы.")
        }

        RowLayout {
            Layout.fillWidth: true

            Controls.ComboBox {
                id: lineCountSelector
                enabled: !page.logsController.busy
                model: [
                    { text: qsTr("Последние 50 строк"), value: 50 },
                    { text: qsTr("Последние 100 строк"), value: 100 },
                    { text: qsTr("Последние 200 строк"), value: 200 },
                    { text: qsTr("Последние 500 строк"), value: 500 }
                ]
                textRole: "text"
                currentIndex: {
                    const index = model.findIndex(item => item.value === page.logsController.lineCount)
                    return Math.max(0, index)
                }
                Accessible.name: qsTr("Количество строк журнала")
                onActivated: page.logsController.readLogs(model[currentIndex].value)
            }

            Controls.Button {
                text: qsTr("Обновить журнал")
                icon.name: "view-refresh"
                enabled: !page.logsController.busy
                onClicked: page.logsController.readLogs(page.logsController.lineCount)
            }

            Item { Layout.fillWidth: true }

            Controls.CheckBox {
                text: qsTr("Автообновление каждые 10 секунд")
                checked: page.autoRefresh
                onToggled: {
                    page.autoRefresh = checked
                    if (checked && page.visible)
                        refreshTimer.start()
                    else
                        refreshTimer.stop()
                }
            }
        }

        Controls.Label {
            Layout.fillWidth: true
            text: page.logsController.status
            wrapMode: Text.WordWrap
        }

        Controls.Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: 360

            Controls.TextArea {
                anchors.fill: parent
                text: page.logsController.logText
                readOnly: true
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                font.family: "monospace"
                Accessible.name: qsTr("Журнал службы zapret2")
                Accessible.description: qsTr("Последние записи журнала systemd службы zapret2.service")
            }
        }
    }
}

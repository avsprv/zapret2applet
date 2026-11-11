import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root

    required property var settingsManager
    required property var autostartController
    required property var trayController

    title: qsTr("Настройки")

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: qsTr("Внешний вид")
            }

            ColumnLayout {
                Layout.fillWidth: true
                Kirigami.FormData.label: qsTr("Тема оформления")
                Kirigami.FormData.labelAlignment: Qt.AlignTop

                Controls.ComboBox {
                    Layout.fillWidth: true
                    model: [
                        { text: qsTr("Системная"), value: "system" },
                        { text: qsTr("Светлая"), value: "light" },
                        { text: qsTr("Тёмная"), value: "dark" }
                    ]
                    textRole: "text"
                    Accessible.name: qsTr("Тема оформления")
                    currentIndex: {
                        const index = model.findIndex(item => item.value === root.settingsManager.theme)
                        return Math.max(0, index)
                    }
                    onActivated: root.settingsManager.theme = model[currentIndex].value
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.75
                    text: qsTr("Изменение применяется сразу. Системный режим следует настройкам рабочего стола; принудительная схема зависит от поддержки платформы.")
                }
            }

            Controls.ComboBox {
                Layout.fillWidth: true
                Kirigami.FormData.label: qsTr("Язык интерфейса")
                model: [
                    { text: qsTr("Системный"), value: "system" },
                    { text: qsTr("Русский"), value: "ru" },
                    { text: qsTr("English"), value: "en" }
                ]
                textRole: "text"
                Accessible.name: qsTr("Язык интерфейса")
                currentIndex: {
                    const index = model.findIndex(item => item.value === root.settingsManager.language)
                    return Math.max(0, index)
                }
                onActivated: root.settingsManager.language = model[currentIndex].value
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: qsTr("Запуск приложения")
            }

            Controls.Switch {
                Kirigami.FormData.label: qsTr("Запускать приложение при входе в систему")
                Accessible.name: qsTr("Запускать приложение при входе в систему")
                checked: root.autostartController.enabled
                onToggled: root.autostartController.setEnabled(checked)
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.autostartController.errorMessage.length > 0
                type: Kirigami.MessageType.Error
                text: root.autostartController.errorMessage
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: qsTr("Поведение окна")
            }

            Controls.Switch {
                Kirigami.FormData.label: qsTr("Сворачивать окно в системный трей при закрытии")
                Accessible.name: qsTr("Сворачивать окно в системный трей при закрытии")
                checked: root.settingsManager.closeToTray
                enabled: root.trayController.registered
                onToggled: root.settingsManager.closeToTray = checked
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: !root.trayController.registered
                type: Kirigami.MessageType.Information
                text: qsTr("Системный трей недоступен. Сворачивание окна в трей включится после регистрации StatusNotifierItem.")
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: qsTr("Обновления")
            }

            Controls.Switch {
                Kirigami.FormData.label: qsTr("Автоматически проверять обновления")
                Accessible.name: qsTr("Автоматически проверять обновления")
                checked: root.settingsManager.autoCheckUpdates
                onToggled: root.settingsManager.autoCheckUpdates = checked
            }

        }
    }
}

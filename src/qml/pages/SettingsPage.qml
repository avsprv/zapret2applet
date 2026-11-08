import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: qsTr("Настройки")

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 1
            text: qsTr("Внешний вид")
        }

        Controls.Label {
            Layout.fillWidth: true
            text: qsTr("Тема оформления")
        }

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
                const index = model.findIndex(item => item.value === settingsManager.theme)
                return Math.max(0, index)
            }
            onActivated: settingsManager.theme = model[currentIndex].value
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.75
            text: qsTr("Изменение применяется сразу. Системный режим следует настройкам рабочего стола; принудительная схема зависит от поддержки платформы.")
        }

        Controls.Label {
            text: qsTr("Язык интерфейса")
        }

        Controls.ComboBox {
            Layout.fillWidth: true
            model: [
                { text: qsTr("Системный"), value: "system" },
                { text: qsTr("Русский"), value: "ru" },
                { text: qsTr("English"), value: "en" }
            ]
            textRole: "text"
            Accessible.name: qsTr("Язык интерфейса")
            currentIndex: {
                const index = model.findIndex(item => item.value === settingsManager.language)
                return Math.max(0, index)
            }
            onActivated: settingsManager.language = model[currentIndex].value
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Запуск приложения")
        }

        Controls.CheckBox {
            Layout.fillWidth: true
            text: qsTr("Запускать приложение при входе в систему")
            checked: autostartController.enabled
            onToggled: autostartController.setEnabled(checked)
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: autostartController.errorMessage.length > 0
            type: Kirigami.MessageType.Error
            text: autostartController.errorMessage
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Поведение окна")
        }

        Controls.CheckBox {
            Layout.fillWidth: true
            text: qsTr("Сворачивать окно в системный трей при закрытии")
            checked: settingsManager.closeToTray
            onToggled: settingsManager.closeToTray = checked
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Обновления")
        }

        Controls.CheckBox {
            Layout.fillWidth: true
            text: qsTr("Автоматически проверять обновления")
            checked: settingsManager.autoCheckUpdates
            onToggled: settingsManager.autoCheckUpdates = checked
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: qsTr("Параметры сохраняются в прежние ключи QSettings. Изменения темы и языка применяются сразу.")
        }
    }
}

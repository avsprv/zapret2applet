import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    required property var page.updateController
    title: qsTr("Обновления")

    Kirigami.Dialog {
        id: confirmDialog
        property bool installing: false
        title: installing ? qsTr("Чистая установка zapret2") : qsTr("Обновление zapret2")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: confirmDialog.installing
                  ? qsTr("Установить zapret2 %1 в /opt/zapret2? Будет проверена структура архива и SHA-256 бинарных компонентов, создана конфигурация по умолчанию с NFQWS2_ENABLE=0 и установлен systemd unit. Служба не будет запущена или добавлена в автозагрузку. Потребуется пароль администратора.")
                    .arg(page.updateController.latestVersion)
                  : qsTr("Обновить zapret2 с %1 до %2? Если служба запущена, она будет остановлена на время обновления и запущена снова. Потребуется пароль администратора.")
                    .arg(page.updateController.installedVersion).arg(page.updateController.latestVersion)
        }

        onAccepted: {
            if (installing)
                page.updateController.install()
            else
                page.updateController.update()
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.updateController.status.length > 0
            text: page.updateController.status
            type: page.updateController.busy || page.updateController.checking
                  ? Kirigami.MessageType.Information
                  : (page.updateController.canInstall || page.updateController.canUpdate
                     ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information)
        }

        Controls.GroupBox {
            Layout.fillWidth: true
            title: qsTr("Версия zapret2")

            GridLayout {
                anchors.fill: parent
                columns: 2

                Controls.Label { text: qsTr("Установлена:") }
                Controls.Label { text: page.updateController.installedVersion }

                Controls.Label { text: qsTr("Последняя версия:") }
                Controls.Label { text: page.updateController.latestVersion }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Controls.Button {
                text: qsTr("Проверить обновления")
                icon.name: "view-refresh"
                enabled: !page.updateController.busy && !page.updateController.checking
                onClicked: page.updateController.checkNow()
            }

            Controls.Button {
                text: page.updateController.installed
                      ? qsTr("Обновить до %1").arg(page.updateController.latestVersion)
                      : qsTr("Установить %1").arg(page.updateController.latestVersion)
                icon.name: "system-software-update"
                enabled: page.updateController.canInstall || page.updateController.canUpdate
                onClicked: {
                    confirmDialog.installing = page.updateController.canInstall
                    confirmDialog.open()
                }
            }

            Controls.Button {
                text: qsTr("Что нового…")
                icon.name: "document-open"
                enabled: page.updateController.releaseNotesUrl.length > 0
                onClicked: Qt.openUrlExternally(page.updateController.releaseNotesUrl)
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: qsTr("Загрузка, проверка контрольных сумм, резервное копирование и откат выполняются привилегированным помощником. Подписи upstream-релиза отсутствуют: SHA-256 защищает от повреждения файлов, но не от компрометации репозитория.")
        }

        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading { level: 2; text: qsTr("Журнал обновления") }
            Item { Layout.fillWidth: true }
            Controls.Button {
                text: qsTr("Очистить")
                icon.name: "edit-clear"
                enabled: page.updateController.logText.length > 0 && !page.updateController.busy
                onClicked: page.updateController.clearLog()
            }
        }

        Controls.TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            readOnly: true
            text: page.updateController.logText
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            Accessible.name: qsTr("Журнал обновления")
        }
    }
}

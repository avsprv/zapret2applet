import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: qsTr("О программе")

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 1
            text: qsTr("Zapret2 Control")
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Независимый графический интерфейс на Qt 6 для управления zapret2 в Linux.")
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Версия и компоненты")
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Версия приложения: %1").arg(Qt.application.version)
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Qt 6 — графическая платформа приложения.")
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("zapret2 — отдельный upstream-компонент; он не входит в состав приложения.")
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("systemd и Polkit используются для системной интеграции и авторизации операций.")
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Лицензии")
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Код Zapret2 Control распространяется по лицензии MIT. Лицензии сторонних компонентов сохраняют собственную область действия. Подробности приведены в NOTICE и в документации upstream.")
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Лицензия приложения (MIT)")
            url: "https://github.com/avsprv/zapret2control/blob/main/LICENSE"
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: qsTr("Проект и документация")
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Репозиторий Zapret2 Control")
            url: "https://github.com/avsprv/zapret2control"
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Документация приложения")
            url: "https://github.com/avsprv/zapret2control/blob/main/README.md"
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Релизы приложения")
            url: "https://github.com/avsprv/zapret2control/releases"
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Сообщить об ошибке")
            url: "https://github.com/avsprv/zapret2control/issues"
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Уведомления о сторонних компонентах")
            url: "https://github.com/avsprv/zapret2control/blob/main/NOTICE.md"
        }

        Kirigami.UrlButton {
            Layout.fillWidth: true
            text: qsTr("Upstream-проект zapret2")
            url: "https://github.com/bol-van/zapret2"
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: qsTr("Zapret2 Control — независимый проект и не связан с автором или сопровождающими upstream-проекта zapret2.")
        }
    }
}

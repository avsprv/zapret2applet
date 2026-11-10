pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "pages"

Kirigami.ApplicationWindow {
    id: root

    required property bool startMinimized
    required property var appController
    required property var settingsManager
    required property var autostartController
    required property var trayController
    required property var configController
    required property var profileController
    required property var logsController
    required property var listsController
    required property var blockcheckController
    required property var updateController

    title: qsTr("Zapret2 Control")
    visible: !startMinimized
    width: Kirigami.Units.gridUnit * 54
    height: Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 36
    minimumHeight: Kirigami.Units.gridUnit * 27
    pageStack.initialPage: dashboardPage

    Kirigami.Dialog {
        id: quitBlockedDialog
        title: qsTr("Нельзя завершить приложение")
        standardButtons: Controls.Dialog.Ok

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Дождитесь завершения текущей операции или восстановления zapret2. Принудительное завершение может повредить установленную систему.")
        }
    }

    onClosing: function(close) {
        if (root.appController.operationsBusy) {
            close.accepted = false
            root.showWindow()
            quitBlockedDialog.open()
            return
        }
        if (root.settingsManager.closeToTray && root.trayController.registered) {
            close.accepted = false
            hide()
        }
    }

    function showWindow() {
        show()
        raise()
        requestActivate()
    }

    function toggleWindow() {
        if (visible && visibility !== Window.Minimized)
            hide()
        else
            showWindow()
    }

    function requestQuit() {
        if (root.appController.operationsBusy) {
            showWindow()
            quitBlockedDialog.open()
            return
        }
        Qt.quit()
    }

    function openSettings() {
        showWindow()
        pageStack.push(settingsPageComponent)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: qsTr("Zapret2 Control")
        titleIcon: "zapret2control"
        isMenu: false
        // Keep primary navigation visible on desktop, but use an overlay on narrow windows.
        modal: root.width < Kirigami.Units.gridUnit * 50

        actions: [
            Kirigami.Action {
                text: qsTr("Статус")
                icon.name: "network-connect"
                onTriggered: root.pageStack.replace(dashboardPage)
            },
            Kirigami.Action {
                text: qsTr("Конфигурация")
                icon.name: "document-edit"
                onTriggered: root.pageStack.replace(configPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Профили")
                icon.name: "view-list-details"
                onTriggered: root.pageStack.replace(profilesPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Списки")
                icon.name: "view-list-text"
                onTriggered: root.pageStack.replace(listsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Мониторинг")
                icon.name: "utilities-system-monitor"
                onTriggered: root.pageStack.replace(logsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Подбор стратегий")
                icon.name: "search"
                onTriggered: root.pageStack.replace(blockcheckPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Обновления")
                icon.name: "system-software-update"
                onTriggered: root.pageStack.replace(updatePageComponent)
            },
            Kirigami.Action {
                text: qsTr("Настройки")
                icon.name: "settings-configure"
                onTriggered: root.pageStack.replace(settingsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("О программе")
                icon.name: "help-about"
                onTriggered: root.pageStack.replace(aboutPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Выход")
                icon.name: "application-exit"
                onTriggered: root.requestQuit()
            }
        ]
    }

    Component {
        id: dashboardPage

        Kirigami.ScrollablePage {
            title: qsTr("Обзор")
            actions: [
                Kirigami.Action {
                    text: qsTr("Обновить")
                    icon.name: "view-refresh"
                    onTriggered: root.appController.refresh()
                }
            ]

            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    level: 1
                    text: qsTr("Состояние zapret2")
                }

                Controls.Frame {
                    Layout.fillWidth: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true

                            Kirigami.Heading {
                                level: 2
                                text: root.appController.serviceState
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                Layout.preferredWidth: Kirigami.Units.smallSpacing * 3
                                Layout.preferredHeight: Kirigami.Units.smallSpacing * 3
                                radius: width / 2
                                color: root.appController.serviceStateCode === "active"
                                     ? Kirigami.Theme.highlightColor
                                     : Kirigami.Theme.textColor
                                Accessible.ignored: true
                            }
                        }

                        Controls.Label {
                            Layout.fillWidth: true
                            text: qsTr("Управление системной службой zapret2")
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                        }

                        Controls.CheckBox {
                            text: qsTr("Запускать zapret2 при загрузке системы")
                            checked: root.appController.bootEnabled
                            onToggled: root.appController.setBootEnabled(checked)
                            enabled: !root.appController.operationsBusy
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Controls.Button {
                                text: qsTr("Запустить")
                                icon.name: "media-playback-start"
                                enabled: !root.appController.serviceActive && !root.appController.operationsBusy
                                onClicked: root.appController.startService()
                            }

                            Controls.Button {
                                text: qsTr("Остановить")
                                icon.name: "media-playback-stop"
                                enabled: root.appController.serviceActive && !root.appController.operationsBusy
                                onClicked: root.appController.stopService()
                            }

                            Controls.Button {
                                text: qsTr("Обновить состояние")
                                icon.name: "view-refresh"
                                enabled: !root.appController.operationsBusy
                                onClicked: root.appController.refresh()
                            }
                        }
                    }
                }

                Controls.Frame {
                    Layout.fillWidth: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            level: 2
                            text: qsTr("Версия zapret2")
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Controls.Label {
                                text: qsTr("Установленная версия")
                                opacity: 0.75
                            }

                            Item { Layout.fillWidth: true }

                            Controls.Label {
                                text: root.updateController.installedVersion
                                font.weight: Font.DemiBold
                            }
                        }

                        Controls.Label {
                            Layout.fillWidth: true
                            text: root.updateController.installed
                                  ? qsTr("Проверить наличие новых версий можно в разделе «Обновления».")
                                  : qsTr("zapret2 пока не установлен. Перейдите в раздел «Обновления», чтобы выполнить установку.")
                            wrapMode: Text.WordWrap
                            opacity: 0.75
                        }

                        Controls.Button {
                            text: qsTr("Открыть обновления")
                            icon.name: "system-software-update"
                            onClicked: root.pageStack.replace(updatePageComponent)
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.appController.errorMessage.length > 0
                    type: Kirigami.MessageType.Error
                    text: root.appController.errorMessage
                }

            }
        }
    }

    Component {
        id: configPageComponent
        ConfigPage { configController: root.configController }
    }

    Component {
        id: profilesPageComponent
        ProfilesPage { profileController: root.profileController }
    }

    Component {
        id: settingsPageComponent
        SettingsPage {
            settingsManager: root.settingsManager
            autostartController: root.autostartController
            trayController: root.trayController
        }
    }

    Component {
        id: aboutPageComponent
        AboutPage {}
    }

    Component {
        id: logsPageComponent
        LogsPage { logsController: root.logsController }
    }

    Component {
        id: listsPageComponent
        ListsPage { listsController: root.listsController }
    }

    Component {
        id: blockcheckPageComponent
        BlockcheckPage { blockcheckController: root.blockcheckController }
    }

    Component {
        id: updatePageComponent
        UpdatePage { updateController: root.updateController }
    }
}

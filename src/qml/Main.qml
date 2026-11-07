import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "pages"

Kirigami.ApplicationWindow {
    id: root

    title: qsTr("zapret2applet")
    width: 980
    height: 680
    minimumWidth: 640
    minimumHeight: 480
    pageStack.initialPage: dashboardPage

    function showWindow() {
        show()
        raise()
        requestActivate()
    }

    function openSettings() {
        showWindow()
        pageStack.push(settingsPageComponent)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: qsTr("zapret2applet")
        titleIcon: "zapret2applet"
        isMenu: true

        actions: [
            Kirigami.Action {
                text: qsTr("Статус")
                icon.name: "network-connect"
                onTriggered: root.pageStack.replace(dashboardPage)
            },
            Kirigami.Action {
                text: qsTr("Конфигурация")
                icon.name: "document-edit"
                onTriggered: root.pageStack.push(configPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Профили")
                icon.name: "view-list-details"
                onTriggered: root.pageStack.push(profilesPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Списки")
                icon.name: "view-list-text"
                onTriggered: root.pageStack.push(listsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Мониторинг")
                icon.name: "utilities-system-monitor"
                onTriggered: root.pageStack.push(logsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Подбор стратегий")
                icon.name: "search"
                onTriggered: root.pageStack.push(blockcheckPageComponent)
            },
            Kirigami.Action {
                text: qsTr("Обновления")
                icon.name: "system-software-update"
                onTriggered: root.pageStack.push(updatePageComponent)
            },
            Kirigami.Action {
                text: qsTr("Настройки")
                icon.name: "settings-configure"
                onTriggered: root.pageStack.push(settingsPageComponent)
            },
            Kirigami.Action {
                text: qsTr("О программе")
                icon.name: "help-about"
                onTriggered: root.pageStack.push(aboutPageComponent)
            }
        ]
    }

    Component {
        id: dashboardPage

        Kirigami.ScrollablePage {
            title: qsTr("Статус")
            actions: [
                Kirigami.Action {
                    text: qsTr("Обновить")
                    icon.name: "view-refresh"
                    onTriggered: appController.refresh()
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
                        spacing: Kirigami.Units.mediumSpacing

                        RowLayout {
                            Layout.fillWidth: true

                            Kirigami.Heading {
                                level: 2
                                text: appController.serviceState
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                width: 12
                                height: 12
                                radius: width / 2
                                color: appController.serviceStateCode === "inactive" ? "#d79921"
                                     : appController.serviceStateCode === "failed" ? Kirigami.Theme.negativeTextColor
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
                            checked: appController.bootEnabled
                            onToggled: appController.setBootEnabled(checked)
                            enabled: !appController.operationsBusy
                        }

                        RowLayout {
                            Controls.Button {
                                text: qsTr("Запустить")
                                icon.name: "media-playback-start"
                                enabled: !appController.serviceActive && !appController.operationsBusy
                                onClicked: appController.startService()
                            }

                            Controls.Button {
                                text: qsTr("Остановить")
                                icon.name: "media-playback-stop"
                                enabled: appController.serviceActive && !appController.operationsBusy
                                onClicked: appController.stopService()
                            }

                            Item { Layout.fillWidth: true }

                            Controls.Button {
                                text: qsTr("Проверить")
                                icon.name: "view-refresh"
                                enabled: !appController.operationsBusy
                                onClicked: appController.refresh()
                            }
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: appController.errorMessage.length > 0
                    type: Kirigami.MessageType.Error
                    text: appController.errorMessage
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: true
                    type: Kirigami.MessageType.Information
                    text: qsTr("Предварительный интерфейс на QML и Kirigami. Он проходит поэтапную проверку; основная версия приложения пока использует стабильный интерфейс.")
                }
            }
        }
    }

    Component {
        id: configPageComponent
        ConfigPage {}
    }

    Component {
        id: profilesPageComponent
        ProfilesPage {}
    }

    Component {
        id: settingsPageComponent
        SettingsPage {}
    }

    Component {
        id: aboutPageComponent
        AboutPage {}
    }

    Component {
        id: logsPageComponent
        LogsPage {}
    }

    Component {
        id: listsPageComponent
        ListsPage {}
    }

    Component {
        id: blockcheckPageComponent
        BlockcheckPage {}
    }

    Component {
        id: updatePageComponent
        UpdatePage {}
    }
}

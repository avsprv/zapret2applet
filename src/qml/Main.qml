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
    property var pendingNavigationPage: null
    property bool pendingNavigationPush: false
    property bool pendingNavigationBack: false
    property bool pendingNavigationQuit: false
    property bool allowCloseAfterDiscard: false


    Controls.Dialog {
        id: discardListsChangesDialog
        modal: true
        title: qsTr("Несохранённые изменения")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No

        Controls.Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.pendingNavigationQuit
                  ? qsTr("Текущий хостлист изменён. Отбросить несохранённые изменения и завершить приложение?")
                  : qsTr("Текущий хостлист изменён. Отбросить несохранённые изменения и перейти в другой раздел?")
        }

        onAccepted: {
            const destination = root.pendingNavigationPage
            root.pendingNavigationPage = null
            const shouldPush = root.pendingNavigationPush
            const shouldPop = root.pendingNavigationBack
            const shouldQuit = root.pendingNavigationQuit
            root.pendingNavigationPage = null
            root.pendingNavigationPush = false
            root.pendingNavigationBack = false
            root.pendingNavigationQuit = false
            if (shouldQuit) {
                root.allowCloseAfterDiscard = true
                Qt.quit()
                return
            }
            if (shouldPop) {
                root.pageStack.pop()
            } else if (destination) {
                if (shouldPush)
                    root.pageStack.push(destination)
                else
                    root.pageStack.replace(destination)
            }
        }

        onRejected: {
            root.pendingNavigationPage = null
            root.pendingNavigationPush = false
            root.pendingNavigationBack = false
            root.pendingNavigationQuit = false
        }
    }

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
        if (root.allowCloseAfterDiscard)
            return
        if (root.settingsManager.closeToTray && root.trayController.registered) {
            close.accepted = false
            hide()
            return
        }
        if (root.pageStack.currentItem
                && root.pageStack.currentItem.objectName === "listsPage"
                && root.listsController.dirty) {
            close.accepted = false
            root.pendingNavigationQuit = true
            root.showWindow()
            discardListsChangesDialog.open()
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
        if (root.pageStack.currentItem
                && root.pageStack.currentItem.objectName === "listsPage"
                && root.listsController.dirty) {
            root.pendingNavigationQuit = true
            root.discardListsChangesDialog.open()
            return
        }
        root.allowCloseAfterDiscard = true
        Qt.quit()
    }

    function openSettings() {
        showWindow()
        navigateTo(settingsPageComponent, true)
    }

    function confirmListsPageBack() {
        pendingNavigationPage = null
        pendingNavigationPush = false
        pendingNavigationBack = true
        pendingNavigationQuit = false
        discardListsChangesDialog.open()
    }

    function navigateTo(pageComponent, shouldPush) {
        const leavingEditedListsPage = pageStack.currentItem
                && pageStack.currentItem.objectName === "listsPage"
        if (leavingEditedListsPage && pageComponent === listsPageComponent)
            return
        if (leavingEditedListsPage && listsController.dirty) {
            pendingNavigationPage = pageComponent
            pendingNavigationPush = shouldPush === true
            pendingNavigationBack = false
            pendingNavigationQuit = false
            discardListsChangesDialog.open()
            return
        }
        if (shouldPush === true)
            pageStack.push(pageComponent)
        else
            pageStack.replace(pageComponent)
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
                onTriggered: root.navigateTo(dashboardPage, false)
            },
            Kirigami.Action {
                text: qsTr("Конфигурация")
                icon.name: "document-edit"
                onTriggered: root.navigateTo(configPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Профили")
                icon.name: "view-list-details"
                onTriggered: root.navigateTo(profilesPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Списки")
                icon.name: "view-list-text"
                onTriggered: root.navigateTo(listsPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Мониторинг")
                icon.name: "utilities-system-monitor"
                onTriggered: root.navigateTo(logsPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Подбор стратегий")
                icon.name: "search"
                onTriggered: root.navigateTo(blockcheckPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Обновления")
                icon.name: "system-software-update"
                onTriggered: root.navigateTo(updatePageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("Настройки")
                icon.name: "settings-configure"
                onTriggered: root.navigateTo(settingsPageComponent, false)
            },
            Kirigami.Action {
                text: qsTr("О программе")
                icon.name: "help-about"
                onTriggered: root.navigateTo(aboutPageComponent, false)
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
                            onClicked: root.pageStack.push(updatePageComponent)
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
        ListsPage {
            listsController: root.listsController
            onBackRequested: event => {
                if (root.listsController.dirty) {
                    event.accepted = true
                    root.confirmListsPageBack()
                }
            }
        }
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

import QtQuick
import QtQuick.Controls as Controls
import QtTest

Item {
    id: testRoot
    width: 800
    height: 600

    QtObject {
        id: appControllerStub
        property string serviceState: "Неизвестно"
        property string serviceStateCode: "inactive"
        property bool bootEnabled: false
        property bool serviceActive: false
        property bool operationsBusy: false
        property string errorMessage: ""
        function refresh() {}
        function setBootEnabled(value) {}
        function startService() {}
        function stopService() {}
    }

    QtObject {
        id: listsControllerStub
        property bool dirty: false
        property bool busy: false
        property bool loaded: false
        property bool editable: false
        property string content: ""
        property string status: ""
        property string loadedName: ""
        function readList(fileName) {}
        function setContent(value) {}
        function saveList() {}
    }

    QtObject {
        id: settingsManagerStub
        property string theme: "system"
        property string language: "system"
        property bool closeToTray: false
        property bool autoCheckUpdates: true
    }

    QtObject {
        id: autostartControllerStub
        property bool enabled: false
        property string errorMessage: ""
        function setEnabled(value) {}
    }

    QtObject {
        id: trayControllerStub
        property bool registered: true
    }

    QtObject {
        id: updateControllerStub
        property string installedVersion: "test"
        property bool installed: true
    }

    TestCase {
        id: testCase
        name: "MainNavigation"
        when: windowShown

        property var window: null
        property var mainComponent: null
        function createMainWindow() {
            mainComponent = Qt.createComponent(Qt.resolvedUrl("../src/qml/Main.qml"))
            tryCompare(mainComponent, "status", Component.Ready)
            compare(mainComponent.status, Component.Ready, mainComponent.errorString())

            return mainComponent.createObject(null, {
                startMinimized: true,
                appController: appControllerStub,
                settingsManager: settingsManagerStub,
                autostartController: autostartControllerStub,
                trayController: trayControllerStub,
                configController: ({}),
                profileController: ({}),
                logsController: ({}),
                listsController: listsControllerStub,
                blockcheckController: ({}),
                updateController: updateControllerStub
            })
        }

        function init() {
            appControllerStub.operationsBusy = false
            listsControllerStub.dirty = false
            settingsManagerStub.closeToTray = false
            window = createMainWindow()
            verify(window !== null)
            window.show()
            wait(50)
        }

        function cleanup() {
            if (window) {
                window.destroy()
                window = null
            }
            if (mainComponent) {
                mainComponent.destroy()
                mainComponent = null
            }
        }

        function findWindowChild(objectName) {
            const child = findChild(window, objectName)
            verify(child !== null, "Missing object: " + objectName)
            return child
        }

        function drawerAction(objectName) {
            const actions = window.globalDrawer.actions
            for (let i = 0; i < actions.length; ++i) {
                if (actions[i].objectName === objectName)
                    return actions[i]
            }
            verify(false, "Missing drawer action: " + objectName)
            return null
        }

        function openListsPage() {
            drawerAction("listsAction").trigger()
            tryCompare(window.pageStack.currentItem, "objectName", "listsPage")
        }

        function discardDialog() {
            return findWindowChild("discardListsChangesDialog")
        }

        function aboutPageComponent() {
            const component = Qt.createComponent(Qt.resolvedUrl("../src/qml/pages/AboutPage.qml"))
            tryCompare(component, "status", Component.Ready)
            compare(component.status, Component.Ready, component.errorString())
            return component
        }

        function test_drawer_actions_have_stable_test_names() {
            const names = [
                "dashboardAction", "configAction", "profilesAction", "listsAction",
                "logsAction", "blockcheckAction", "updateAction", "settingsAction",
                "aboutAction", "exitAction"
            ]
            const actions = window.globalDrawer.actions
            compare(actions.length, names.length)
            for (let i = 0; i < names.length; ++i)
                compare(actions[i].objectName, names[i])
        }

        function test_drawer_navigation_replaces_without_stack_growth() {
            const originalDepth = window.pageStack.depth
            drawerAction("aboutAction").trigger()
            tryCompare(window.pageStack, "depth", originalDepth)
            drawerAction("dashboardAction").trigger()
            tryCompare(window.pageStack, "depth", originalDepth)
        }

        function test_dirty_lists_block_navigation_and_cancel_keeps_editor() {
            openListsPage()
            listsControllerStub.dirty = true
            const originalPage = window.pageStack.currentItem

            window.navigateTo(aboutPageComponent(), false)
            verify(discardDialog().visible)
            discardDialog().standardButton(Controls.Dialog.No).click()
            tryCompare(discardDialog(), "visible", false)
            compare(window.pageStack.currentItem, originalPage)
            compare(listsControllerStub.dirty, true)
        }

        function test_confirmed_navigation_replaces_page() {
            openListsPage()
            listsControllerStub.dirty = true
            const originalPage = window.pageStack.currentItem

            window.navigateTo(aboutPageComponent(), false)
            discardDialog().standardButton(Controls.Dialog.Yes).click()
            tryVerify(function() { return window.pageStack.currentItem !== originalPage })
            compare(window.pendingNavigationPage, null)
        }

        function test_reselecting_active_lists_does_not_prompt() {
            openListsPage()
            listsControllerStub.dirty = true
            drawerAction("listsAction").trigger()
            verify(!discardDialog().visible)
            compare(window.pageStack.currentItem.objectName, "listsPage")
        }

        function test_back_confirmation_cancel_preserves_current_page() {
            window.pageStack.push(aboutPageComponent())
            openListsPage()
            listsControllerStub.dirty = true
            const originalPage = window.pageStack.currentItem

            const backEvent = { accepted: false }
            window.pageStack.goBack(backEvent)
            compare(backEvent.accepted, true)
            verify(discardDialog().visible)
            discardDialog().standardButton(Controls.Dialog.No).click()
            tryCompare(discardDialog(), "visible", false)
            compare(window.pageStack.currentItem, originalPage)
        }

        function test_back_confirmation_accepts_pop() {
            window.pageStack.push(aboutPageComponent())
            openListsPage()
            listsControllerStub.dirty = true

            const backEvent = { accepted: false }
            window.pageStack.goBack(backEvent)
            compare(backEvent.accepted, true)
            verify(discardDialog().visible)
            discardDialog().standardButton(Controls.Dialog.Yes).click()
            tryCompare(window.pageStack.currentItem, "title", "Обзор")
        }

        function test_settings_from_tray_uses_push_semantics() {
            const originalDepth = window.pageStack.depth
            window.openSettings()
            tryCompare(window.pageStack, "depth", originalDepth + 1)
            compare(window.pageStack.currentItem.title, "Настройки")
        }

        function test_window_close_with_dirty_lists_requires_confirmation() {
            openListsPage()
            listsControllerStub.dirty = true
            window.show()
            window.close()
            verify(discardDialog().visible)
            verify(window.visible)
        }

        function test_tray_quit_is_blocked_during_active_operation() {
            appControllerStub.operationsBusy = true
            window.requestQuit()
            const blockedDialog = findWindowChild("quitBlockedDialog")
            verify(blockedDialog.visible)
            verify(!discardDialog().visible)
        }

        function test_quit_rechecks_operation_state_after_confirmation_opens() {
            openListsPage()
            listsControllerStub.dirty = true
            window.requestQuit()
            verify(discardDialog().visible)

            appControllerStub.operationsBusy = true
            discardDialog().standardButton(Controls.Dialog.Yes).click()

            const blockedDialog = findWindowChild("quitBlockedDialog")
            verify(blockedDialog.visible)
            compare(window.allowCloseAfterDiscard, false)
        }
    }
}

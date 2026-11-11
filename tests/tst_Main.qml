import QtQuick
import QtQuick.Controls as Controls
import QtTest

Item {
    id: testRoot
    width: 800
    height: 600

    TestCase {
        id: testCase
        name: "MainNavigation"
        when: windowShown

        property var window: null
        property var mainComponent: null
        property var appControllerStub: null
        property var listsControllerStub: null

        function createMainWindow() {
            mainComponent = Qt.createComponent(Qt.resolvedUrl("../src/qml/Main.qml"))
            tryCompare(mainComponent, "status", Component.Ready)
            compare(mainComponent.status, Component.Ready, mainComponent.errorString())

            appControllerStub = {
                serviceState: "Неизвестно",
                serviceStateCode: "inactive",
                bootEnabled: false,
                serviceActive: false,
                operationsBusy: false,
                errorMessage: "",
                refresh: function() {},
                setBootEnabled: function() {},
                startService: function() {},
                stopService: function() {}
            }
            listsControllerStub = {
                dirty: false,
                busy: false,
                loaded: false,
                editable: false,
                content: "",
                status: "",
                loadedName: "",
                readList: function(fileName) {},
                setContent: function(value) {},
                saveList: function() {}
            }

            return mainComponent.createObject(null, {
                startMinimized: true,
                appController: appControllerStub,
                settingsManager: {
                    theme: "system",
                    language: "system",
                    closeToTray: false,
                    autoCheckUpdates: true
                },
                autostartController: {
                    enabled: false,
                    errorMessage: "",
                    setEnabled: function(value) {}
                },
                trayController: { registered: true },
                configController: ({}),
                profileController: ({}),
                logsController: ({}),
                listsController: listsControllerStub,
                blockcheckController: ({}),
                updateController: {
                    installedVersion: "test",
                    installed: true
                }
            })
        }

        function init() {
            window = createMainWindow()
            verify(window !== null)
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

            window.confirmListsPageBack()
            verify(discardDialog().visible)
            discardDialog().standardButton(Controls.Dialog.No).click()
            tryCompare(discardDialog(), "visible", false)
            compare(window.pageStack.currentItem, originalPage)
        }

        function test_back_confirmation_accepts_pop() {
            window.pageStack.push(aboutPageComponent())
            openListsPage()
            listsControllerStub.dirty = true

            window.confirmListsPageBack()
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

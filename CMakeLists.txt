cmake_minimum_required(VERSION 3.21)
project(zapret2control VERSION 0.5.0 LANGUAGES CXX)

# Minimum supported Qt release. Keep the API compatible with the Qt 6.8 LTS baseline.
set(QZAPRET_QT_MIN 6.8 CACHE STRING "Minimum supported Qt version (LTS)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)

# KDE Frameworks / Kirigami dependencies for the production QML application.
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(ECMFindQmlModule)
find_package(KF6 REQUIRED COMPONENTS Kirigami)
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)

find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS
    Network LinguistTools
    Core Gui Qml Quick QuickControls2 QuickDialogs2 DBus
)

qt_standard_project_setup(REQUIRES ${QZAPRET_QT_MIN})

set(QZAPRET_HELPER "${CMAKE_INSTALL_FULL_LIBEXECDIR}/zapret2control/zapret2control-helper")


qt_add_library(zapret2control_i18n STATIC src/i18n.cpp src/i18n.h)
target_include_directories(zapret2control_i18n PUBLIC src)
target_link_libraries(zapret2control_i18n PUBLIC Qt6::Core)
qt_add_translations(zapret2control_i18n
    TS_FILES i18n/zapret2control_en.ts
    SOURCES
        src/i18n.cpp
        src/qml/main.cpp
        src/qml/applicationcontroller.cpp
        src/qml/configcontroller.cpp
        src/qml/profilecontroller.cpp
        src/qml/settingsmanager.cpp
        src/qml/logscontroller.cpp
        src/qml/listscontroller.cpp
        src/qml/blockcheckjobcontroller.cpp
        src/qml/updatecontroller.cpp
        src/qml/autostartcontroller.cpp
        src/qml/traycontroller.cpp
        src/qml/dbusmenu.cpp
        src/backend/blockcheckrunner.cpp
        src/backend/systemdbackend.cpp
        src/backend/updatechecker.cpp
        src/backend/zapretupdater.cpp
        src/profileanalyzer.cpp
        src/qml/Main.qml
        src/qml/pages/SettingsPage.qml
        src/qml/pages/ConfigPage.qml
        src/qml/pages/ProfilesPage.qml
        src/qml/pages/AboutPage.qml
        src/qml/pages/LogsPage.qml
        src/qml/pages/ListsPage.qml
        src/qml/pages/BlockcheckPage.qml
        src/qml/pages/UpdatePage.qml
    INCLUDE_DIRECTORIES src
    LUPDATE_OPTIONS -no-obsolete
    LUPDATE_TARGET zapret2control_i18n_lupdate
)

# The Kirigami application is the sole production executable.
qt_add_executable(zapret2control
    src/qml/main.cpp
    src/qml/applicationcontroller.cpp
    src/qml/applicationcontroller.h
    src/qml/settingsmanager.cpp
    src/qml/settingsmanager.h
    src/qml/autostartcontroller.cpp
    src/qml/autostartcontroller.h
    src/qml/traycontroller.cpp
    src/qml/traycontroller.h
    src/qml/dbusmenu.cpp
    src/qml/dbusmenu.h
    src/qml/configcontroller.cpp
    src/qml/configcontroller.h
    src/qml/profilecontroller.cpp
    src/qml/profilecontroller.h
    src/qml/logscontroller.cpp
    src/qml/logscontroller.h
    src/qml/listscontroller.cpp
    src/qml/listscontroller.h
    src/qml/blockcheckjobcontroller.cpp
    src/qml/blockcheckjobcontroller.h
    src/qml/updatecontroller.cpp
    src/qml/updatecontroller.h
    src/backend/updatechecker.cpp
    src/backend/updatechecker.h
    src/backend/zapretupdater.cpp
    src/backend/zapretupdater.h
    src/backend/zapretpaths.h
    src/backend/blockcheckrunner.cpp
    src/backend/blockcheckrunner.h
    src/profileanalyzer.cpp
    src/profileanalyzer.h
    src/backend/systemdbackend.cpp
    src/backend/systemdbackend.h
    src/backend/zapretbackend.h
    src/backend/helperpath.h
)
qt_add_qml_module(zapret2control
    URI org.avsprv.zapret2control
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/pages/SettingsPage.qml
        src/qml/pages/ConfigPage.qml
        src/qml/pages/ProfilesPage.qml
        src/qml/pages/AboutPage.qml
        src/qml/pages/LogsPage.qml
        src/qml/pages/ListsPage.qml
        src/qml/pages/BlockcheckPage.qml
        src/qml/pages/UpdatePage.qml
)
target_include_directories(zapret2control PRIVATE src src/qml)
target_link_libraries(zapret2control PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::DBus
    zapret2control_i18n
)
target_compile_definitions(zapret2control PRIVATE
    QZAPRET_VERSION="${PROJECT_VERSION}"
    QZAPRET_HELPER_PATH="${QZAPRET_HELPER}")
target_compile_options(zapret2control PRIVATE -Wall -Wextra)
target_link_options(zapret2control PRIVATE -Wl,-z,now)

# The production executable must never acquire a Qt Widgets dependency.
get_target_property(QZAPRET_PRODUCTION_LINK_LIBRARIES zapret2control LINK_LIBRARIES)
if("Qt6::Widgets" IN_LIST QZAPRET_PRODUCTION_LINK_LIBRARIES)
    message(FATAL_ERROR "The production QML target must not link Qt6::Widgets")
endif()

configure_file(data/org.zapret2control.policy.in ${CMAKE_BINARY_DIR}/org.zapret2control.policy @ONLY)

configure_file(data/systemd/zapret2control-blockcheck.service.in
    ${CMAKE_BINARY_DIR}/zapret2control-blockcheck.service @ONLY)
configure_file(data/systemd/zapret2control-blockcheck-recovery.service.in
    ${CMAKE_BINARY_DIR}/zapret2control-blockcheck-recovery.service @ONLY)

install(TARGETS zapret2control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/helper/zapret2control-helper
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/zapret2control)
install(FILES
        packaging/helper/blockcheck_manager.py
        packaging/helper/blockcheck_state.py
        packaging/helper/blockcheck_staging.py
        packaging/helper/blockcheck_worker.py
        packaging/helper/blockcheck_recovery.py
        packaging/helper/blockcheck_submit.py
        packaging/helper/blockcheck_status.py
        packaging/helper/blockcheck_probe.py
        packaging/helper/blockcheck_admission.py
        packaging/helper/blockcheck_cancel.py
        packaging/helper/updater_transaction.py
        packaging/helper/updater_staging.py
        packaging/helper/updater_activation.py
        packaging/helper/updater_service.py
        packaging/helper/updater_cli.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/zapret2control)
install(FILES ${CMAKE_BINARY_DIR}/org.zapret2control.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES
        ${CMAKE_BINARY_DIR}/zapret2control-blockcheck.service
        ${CMAKE_BINARY_DIR}/zapret2control-blockcheck-recovery.service
        DESTINATION lib/systemd/system)
install(FILES data/zapret2control.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/zapret2control.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES data/org.avsprv.zapret2control.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/zapret2control.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/icons/hicolor/symbolic/apps/zapret2control-tray-symbolic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps)

if(BUILD_TESTING)
    find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
    qt_add_executable(test_blockcheck tests/test_blockcheck.cpp
        src/backend/blockcheckrunner.cpp src/backend/blockcheckrunner.h)
    target_include_directories(test_blockcheck PRIVATE src)
    target_link_libraries(test_blockcheck PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_blockcheck PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME blockcheck COMMAND test_blockcheck)

    qt_add_executable(test_zapretcompatibility tests/test_zapretcompatibility.cpp
        src/backend/blockcheckrunner.cpp src/backend/blockcheckrunner.h
        src/backend/zapretcompatibility.cpp src/backend/zapretcompatibility.h)
    target_include_directories(test_zapretcompatibility PRIVATE src)
    target_link_libraries(test_zapretcompatibility PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_zapretcompatibility PRIVATE QZAPRET_HELPER_PATH="/nonexistent" QZAPRET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QZAPRET_UPSTREAM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party/zapret2")
    add_test(NAME zapretcompatibility COMMAND test_zapretcompatibility)

    qt_add_executable(test_autostartcontroller tests/test_autostartcontroller.cpp
        src/qml/autostartcontroller.cpp src/qml/autostartcontroller.h)
    target_include_directories(test_autostartcontroller PRIVATE src src/qml)
    target_link_libraries(test_autostartcontroller PRIVATE Qt6::Test Qt6::Core)
    add_test(NAME autostart_controller COMMAND test_autostartcontroller)

    qt_add_executable(test_settingsmanager tests/test_settingsmanager.cpp
        src/qml/settingsmanager.cpp src/qml/settingsmanager.h)
    target_include_directories(test_settingsmanager PRIVATE src src/qml)
    target_link_libraries(test_settingsmanager PRIVATE Qt6::Test Qt6::Core)
    add_test(NAME settingsmanager COMMAND test_settingsmanager)

    qt_add_executable(test_dbusmenu tests/test_dbusmenu.cpp
        src/qml/dbusmenu.cpp src/qml/dbusmenu.h)
    target_include_directories(test_dbusmenu PRIVATE src src/qml)
    target_link_libraries(test_dbusmenu PRIVATE Qt6::Test Qt6::Core Qt6::DBus)
    add_test(NAME dbusmenu COMMAND test_dbusmenu)

    qt_add_executable(test_dbusmenu_dbus tests/test_dbusmenu_dbus.cpp
        src/qml/dbusmenu.cpp src/qml/dbusmenu.h
        src/qml/traycontroller.cpp src/qml/traycontroller.h)
    target_include_directories(test_dbusmenu_dbus PRIVATE src src/qml)
    target_link_libraries(test_dbusmenu_dbus PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::DBus)
    add_test(NAME dbusmenu_session_bus
             COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:test_dbusmenu_dbus>)
    set_tests_properties(dbusmenu_session_bus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(test_i18n tests/test_i18n.cpp)
    target_link_libraries(test_i18n PRIVATE Qt6::Test zapret2control_i18n)
    target_compile_definitions(test_i18n PRIVATE QZAPRET_I18N_DIR="${CMAKE_SOURCE_DIR}/i18n")
    add_test(NAME i18n COMMAND test_i18n)

    qt_add_executable(test_logscontroller tests/test_logscontroller.cpp
        src/qml/logscontroller.cpp src/qml/logscontroller.h)
    target_include_directories(test_logscontroller PRIVATE src src/qml)
    target_link_libraries(test_logscontroller PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_logscontroller PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME logs_controller COMMAND test_logscontroller)

    qt_add_executable(test_listscontroller tests/test_listscontroller.cpp
        src/qml/listscontroller.cpp src/qml/listscontroller.h)
    target_include_directories(test_listscontroller PRIVATE src src/qml)
    target_link_libraries(test_listscontroller PRIVATE Qt6::Test Qt6::Core Qt6::Network)
    target_compile_definitions(test_listscontroller PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME lists_controller COMMAND test_listscontroller)

    qt_add_executable(test_systemdbackend tests/test_systemdbackend.cpp
        src/backend/systemdbackend.cpp src/backend/systemdbackend.h
        src/backend/zapretbackend.h src/backend/helperpath.h)
    target_include_directories(test_systemdbackend PRIVATE src)
    target_link_libraries(test_systemdbackend PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_systemdbackend PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME systemd_backend COMMAND test_systemdbackend)

    qt_add_executable(test_blockcheckjobcontroller tests/test_blockcheckjobcontroller.cpp
        src/qml/blockcheckjobcontroller.cpp src/qml/blockcheckjobcontroller.h
        src/backend/blockcheckrunner.cpp src/backend/blockcheckrunner.h)
    target_include_directories(test_blockcheckjobcontroller PRIVATE src src/qml)
    target_link_libraries(test_blockcheckjobcontroller PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
    target_compile_definitions(test_blockcheckjobcontroller PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME blockcheck_job_controller COMMAND test_blockcheckjobcontroller)

    qt_add_executable(test_update tests/test_update.cpp
        src/backend/updatechecker.cpp src/backend/updatechecker.h)
    target_include_directories(test_update PRIVATE src)
    target_link_libraries(test_update PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME update_versions COMMAND test_update)

    qt_add_executable(test_zapretupdater tests/test_zapretupdater.cpp
        src/backend/zapretupdater.cpp src/backend/zapretupdater.h)
    target_include_directories(test_zapretupdater PRIVATE src)
    target_link_libraries(test_zapretupdater PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_zapretupdater PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME zapretupdater COMMAND test_zapretupdater)

    add_test(NAME update_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_update_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    add_test(NAME helper_validation
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    add_test(NAME blockcheck_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME blockcheck_state
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_state.py)

    add_test(NAME blockcheck_manager
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_manager.py)

    add_test(NAME blockcheck_staging
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_staging.py)

    add_test(NAME blockcheck_worker
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_worker.py)

    add_test(NAME blockcheck_submit
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_submit.py)

    add_test(NAME blockcheck_cancel
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_cancel.py)

    add_test(NAME blockcheck_status
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_status.py)

    add_test(NAME blockcheck_probe
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_probe.py)

    add_test(NAME blockcheck_admission
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_admission.py)

    add_test(NAME updater_transaction
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_updater_transaction.py)

    add_test(NAME updater_staging
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_updater_staging.py)

    add_test(NAME updater_activation
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_updater_activation.py)

    add_test(NAME updater_service
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_updater_service.py)

    add_test(NAME updater_cli
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_updater_cli.py)

    add_test(NAME package_layout
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_package_layout.py)

    add_test(NAME polkit_policy
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_polkit_policy.py
                     ${CMAKE_BINARY_DIR}/org.zapret2control.policy)

    add_test(NAME config_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_config_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    add_test(NAME lists_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_lists_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    add_test(NAME logs_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_logs_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2control-helper)

    qt_add_executable(test_profileanalyzer tests/test_profileanalyzer.cpp
        src/profileanalyzer.cpp src/profileanalyzer.h)
    target_include_directories(test_profileanalyzer PRIVATE src)
    target_link_libraries(test_profileanalyzer PRIVATE Qt6::Test Qt6::Core)
    add_test(NAME profile_analyzer COMMAND test_profileanalyzer)

    qt_add_executable(test_profilecontroller tests/test_profilecontroller.cpp
        src/qml/profilecontroller.cpp src/qml/profilecontroller.h
        src/profileanalyzer.cpp src/profileanalyzer.h)
    target_include_directories(test_profilecontroller PRIVATE src src/qml)
    target_link_libraries(test_profilecontroller PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(test_profilecontroller PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME profile_controller COMMAND test_profilecontroller)
endif()

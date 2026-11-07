cmake_minimum_required(VERSION 3.21)
project(zapret2applet VERSION 0.4.0 LANGUAGES CXX)

# Minimum supported Qt release. Keep the API compatible with the Qt 6.8 LTS baseline.
set(QZAPRET_QT_MIN 6.8 CACHE STRING "Minimum supported Qt version (LTS)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)

# KDE Frameworks / Kirigami dependencies for the parallel QML migration target.
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(ECMFindQmlModule)
find_package(KF6 REQUIRED COMPONENTS Kirigami)
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)

find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS
    Widgets Network LinguistTools
    Core Gui Qml Quick QuickControls2 QuickDialogs2
)

qt_standard_project_setup(REQUIRES ${QZAPRET_QT_MIN})

set(QZAPRET_HELPER "${CMAKE_INSTALL_FULL_LIBEXECDIR}/zapret2applet/zapret2applet-helper")

set(QZAPRET_SOURCES
    src/appsettings.cpp
    src/appsettings.h
    src/main.cpp
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/aboutpage.cpp
    src/aboutpage.h
    src/autostart.cpp
    src/autostart.h
    src/blockcheckpage.cpp
    src/blockcheckpage.h
    src/configpage.cpp
    src/configpage.h
    src/profileanalyzer.cpp
    src/profileanalyzer.h
    src/profilepage.cpp
    src/profilepage.h
    src/listsmanagerpage.cpp
    src/listsmanagerpage.h
    src/logmonitorpage.cpp
    src/logmonitorpage.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/updatepage.cpp
    src/updatepage.h
    src/backend/blockcheckrunner.cpp
    src/backend/blockcheckrunner.h
    src/backend/helperpath.h
    src/backend/systemdbackend.cpp
    src/backend/systemdbackend.h
    src/backend/updatechecker.cpp
    src/backend/updatechecker.h
    src/backend/zapretpaths.h
    src/backend/zapretcompatibility.cpp
    src/backend/zapretcompatibility.h
    src/backend/zapretupdater.cpp
    src/backend/zapretupdater.h
    src/backend/zapretbackend.h
)

qt_add_library(zapret2applet_i18n STATIC src/i18n.cpp src/i18n.h)
target_include_directories(zapret2applet_i18n PUBLIC src)
target_link_libraries(zapret2applet_i18n PUBLIC Qt6::Core)
qt_add_translations(zapret2applet_i18n
    TS_FILES i18n/zapret2applet_en.ts i18n/zapret2applet_about_en.ts
    SOURCES
        ${QZAPRET_SOURCES}
        src/i18n.cpp
        src/qml/logscontroller.cpp
        src/qml/listscontroller.cpp
        src/qml/blockcheckcontroller.cpp
        src/qml/updatecontroller.cpp
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
)

# Current production application. It remains the installed executable until the
# QML implementation reaches feature parity and passes the full regression suite.
qt_add_executable(zapret2applet ${QZAPRET_SOURCES})
target_include_directories(zapret2applet PRIVATE src)
target_link_libraries(zapret2applet PRIVATE Qt6::Widgets Qt6::Network zapret2applet_i18n)
target_compile_definitions(zapret2applet PRIVATE
    QZAPRET_VERSION="${PROJECT_VERSION}"
    QZAPRET_HELPER_PATH="${QZAPRET_HELPER}")
target_compile_options(zapret2applet PRIVATE -Wall -Wextra)

# Parallel Kirigami application shell. This target is deliberately not installed
# or packaged yet; it is built in CI while features are migrated incrementally.
qt_add_executable(zapret2applet-qml-preview
    src/qml/main.cpp
    src/qml/applicationcontroller.cpp
    src/qml/applicationcontroller.h
    src/qml/settingsmanager.cpp
    src/qml/settingsmanager.h
    src/qml/configcontroller.cpp
    src/qml/configcontroller.h
    src/qml/profilecontroller.cpp
    src/qml/profilecontroller.h
    src/qml/logscontroller.cpp
    src/qml/logscontroller.h
    src/qml/listscontroller.cpp
    src/qml/listscontroller.h
    src/qml/blockcheckcontroller.cpp
    src/qml/blockcheckcontroller.h
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
qt_add_qml_module(zapret2applet-qml-preview
    URI org.avsprv.zapret2applet
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
target_include_directories(zapret2applet-qml-preview PRIVATE src src/qml)
target_link_libraries(zapret2applet-qml-preview PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2
    zapret2applet_i18n
)
target_compile_definitions(zapret2applet-qml-preview PRIVATE
    QZAPRET_VERSION="${PROJECT_VERSION}"
    QZAPRET_HELPER_PATH="${QZAPRET_HELPER}")
target_compile_options(zapret2applet-qml-preview PRIVATE -Wall -Wextra)

configure_file(data/org.zapret2applet.policy.in ${CMAKE_BINARY_DIR}/org.zapret2applet.policy @ONLY)

install(TARGETS zapret2applet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/helper/zapret2applet-helper
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/zapret2applet)
install(FILES ${CMAKE_BINARY_DIR}/org.zapret2applet.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/zapret2applet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.avsprv.zapret2applet.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/zapret2applet.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(BUILD_TESTING)
    find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS Test)
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

    qt_add_executable(test_blockcheck_lifecycle tests/test_blockcheck_lifecycle.cpp
        src/blockcheckpage.cpp src/blockcheckpage.h
        src/backend/blockcheckrunner.cpp src/backend/blockcheckrunner.h
        src/backend/zapretbackend.h src/backend/helperpath.h)
    target_include_directories(test_blockcheck_lifecycle PRIVATE src)
    target_link_libraries(test_blockcheck_lifecycle PRIVATE Qt6::Test Qt6::Widgets)
    target_compile_definitions(test_blockcheck_lifecycle PRIVATE QZAPRET_HELPER_PATH="/nonexistent" QZAPRET_TEST_SKIP_COMPATIBILITY=1)
    add_test(NAME blockcheck_lifecycle COMMAND test_blockcheck_lifecycle)
    set_tests_properties(blockcheck_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(test_appsettings tests/test_appsettings.cpp
        src/appsettings.cpp src/appsettings.h)
    target_include_directories(test_appsettings PRIVATE src)
    target_link_libraries(test_appsettings PRIVATE Qt6::Test Qt6::Core Qt6::Widgets)
    add_test(NAME appsettings COMMAND test_appsettings)

    qt_add_executable(test_settingsmanager tests/test_settingsmanager.cpp
        src/qml/settingsmanager.cpp src/qml/settingsmanager.h)
    target_include_directories(test_settingsmanager PRIVATE src src/qml)
    target_link_libraries(test_settingsmanager PRIVATE Qt6::Test Qt6::Core)
    add_test(NAME settingsmanager COMMAND test_settingsmanager)

    qt_add_executable(test_i18n tests/test_i18n.cpp)
    target_link_libraries(test_i18n PRIVATE Qt6::Test zapret2applet_i18n)
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

    qt_add_executable(test_blockcheckcontroller tests/test_blockcheckcontroller.cpp
        src/qml/blockcheckcontroller.cpp src/qml/blockcheckcontroller.h
        src/backend/blockcheckrunner.cpp src/backend/blockcheckrunner.h
        src/backend/systemdbackend.cpp src/backend/systemdbackend.h
        src/backend/zapretbackend.h src/backend/helperpath.h)
    target_include_directories(test_blockcheckcontroller PRIVATE src src/qml)
    target_link_libraries(test_blockcheckcontroller PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
    target_compile_definitions(test_blockcheckcontroller PRIVATE QZAPRET_HELPER_PATH="/nonexistent")
    add_test(NAME blockcheck_controller COMMAND test_blockcheckcontroller)

    qt_add_executable(test_aboutpage tests/test_aboutpage.cpp
        src/aboutpage.cpp src/aboutpage.h)
    target_include_directories(test_aboutpage PRIVATE src)
    target_link_libraries(test_aboutpage PRIVATE Qt6::Test Qt6::Widgets)
    target_compile_definitions(test_aboutpage PRIVATE QZAPRET_VERSION="${PROJECT_VERSION}")
    add_test(NAME aboutpage COMMAND test_aboutpage)
    set_tests_properties(aboutpage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

    add_test(NAME helper_validation
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

    add_test(NAME blockcheck_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME blockcheck_state
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blockcheck_state.py)

    add_test(NAME config_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_config_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

    add_test(NAME lists_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_lists_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

    add_test(NAME logs_helper
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/test_logs_helper.sh
                     ${CMAKE_SOURCE_DIR}/packaging/helper/zapret2applet-helper)

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

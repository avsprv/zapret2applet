#include <cstdlib>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QVariant>
#include <QTranslator>
#include <QStyleHints>
#include <QWindow>

#include "applicationcontroller.h"
#include "configcontroller.h"
#include "blockcheckjobcontroller.h"
#include "i18n.h"
#include "profilecontroller.h"
#include "logscontroller.h"
#include "listscontroller.h"
#include "updatecontroller.h"
#include "settingsmanager.h"
#include "settingsmigration.h"
#include "autostartcontroller.h"
#include "traycontroller.h"

namespace {
QLocale localeForLanguage(const QString &language)
{
    if (language == QLatin1String("ru"))
        return QLocale(QLocale::Russian, QLocale::Russia);
    if (language == QLatin1String("en"))
        return QLocale(QLocale::English, QLocale::UnitedStates);
    return QLocale();
}
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("zapret2control"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QZAPRET_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("zapret2control"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption minimizedOption(QStringLiteral("minimized"));
    parser.addOption(minimizedOption);
    parser.process(app);
    const bool startMinimized = parser.isSet(minimizedOption);
    SettingsMigration::migrateLegacyProfile();

    ApplicationController controller;
    SettingsManager settingsManager;
    AutostartController autostartController;
    TrayController trayController;
    const auto applyColorScheme = [&settingsManager] {
        Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
        if (settingsManager.theme() == QLatin1String("light"))
            scheme = Qt::ColorScheme::Light;
        else if (settingsManager.theme() == QLatin1String("dark"))
            scheme = Qt::ColorScheme::Dark;
        QGuiApplication::styleHints()->setColorScheme(scheme);
    };
    applyColorScheme();
    QObject::connect(&settingsManager, &SettingsManager::themeChanged, &app, applyColorScheme);

    ConfigController configController;
    ProfileController profileController;
    LogsController logsController;
    ListsController listsController;
    BlockcheckJobController blockcheckController;
    UpdateController updateController(&settingsManager);

    updateController.setBlockcheckBusy(blockcheckController.busy() || blockcheckController.recoveryRequired());
    blockcheckController.setUpdateBusy(updateController.busy());
    controller.setOperationsBusy(blockcheckController.busy() || blockcheckController.recoveryRequired() || updateController.busy());
    blockcheckController.setServiceBusy(controller.operationsBusy());
    QObject::connect(&controller, &ApplicationController::operationsBusyChanged, &app,
                     [&] { blockcheckController.setServiceBusy(controller.operationsBusy()); });
    QObject::connect(&blockcheckController, &BlockcheckJobController::busyChanged, &app,
                     [&] {
        updateController.setBlockcheckBusy(blockcheckController.busy() || blockcheckController.recoveryRequired());
        controller.setOperationsBusy(blockcheckController.busy() || blockcheckController.recoveryRequired() || updateController.busy());
    });
    QObject::connect(&blockcheckController, &BlockcheckJobController::recoveryRequiredChanged, &app,
                     [&] {
        updateController.setBlockcheckBusy(blockcheckController.busy() || blockcheckController.recoveryRequired());
        controller.setOperationsBusy(blockcheckController.busy() || blockcheckController.recoveryRequired() || updateController.busy());
    });
    QObject::connect(&updateController, &UpdateController::stateChanged, &app,
                     [&] {
        blockcheckController.setUpdateBusy(updateController.busy());
        controller.setOperationsBusy(blockcheckController.busy() || blockcheckController.recoveryRequired() || updateController.busy());
    });
    QObject::connect(&updateController, &UpdateController::operationFinished,
                     &controller, &ApplicationController::refresh);
    QObject::connect(&blockcheckController, &BlockcheckJobController::scanFinished,
                     &controller, [&] { controller.refresh(); });

    const auto updateTrayState = [&] {
        trayController.setServiceState(controller.serviceState(),
                                       controller.serviceStateCode(),
                                       controller.serviceActive(),
                                       controller.operationsBusy());
    };
    updateTrayState();
    QObject::connect(&controller, &ApplicationController::serviceStateChanged, &app, updateTrayState);
    QObject::connect(&controller, &ApplicationController::operationsBusyChanged, &app, updateTrayState);
    QObject::connect(&trayController, &TrayController::startRequested,
                     &controller, &ApplicationController::startService);
    QObject::connect(&trayController, &TrayController::stopRequested,
                     &controller, &ApplicationController::stopService);

    const QLocale initialLocale = localeForLanguage(settingsManager.language());
    QLocale::setDefault(initialLocale);
    QList<QTranslator *> translators = I18n::install(app, initialLocale);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("startMinimized"), startMinimized},
        {QStringLiteral("appController"), QVariant::fromValue(static_cast<QObject *>(&controller))},
        {QStringLiteral("settingsManager"), QVariant::fromValue(static_cast<QObject *>(&settingsManager))},
        {QStringLiteral("autostartController"), QVariant::fromValue(static_cast<QObject *>(&autostartController))},
        {QStringLiteral("trayController"), QVariant::fromValue(static_cast<QObject *>(&trayController))},
        {QStringLiteral("configController"), QVariant::fromValue(static_cast<QObject *>(&configController))},
        {QStringLiteral("profileController"), QVariant::fromValue(static_cast<QObject *>(&profileController))},
        {QStringLiteral("logsController"), QVariant::fromValue(static_cast<QObject *>(&logsController))},
        {QStringLiteral("listsController"), QVariant::fromValue(static_cast<QObject *>(&listsController))},
        {QStringLiteral("blockcheckController"), QVariant::fromValue(static_cast<QObject *>(&blockcheckController))},
        {QStringLiteral("updateController"), QVariant::fromValue(static_cast<QObject *>(&updateController))}
    });
    engine.loadFromModule(QStringLiteral("org.avsprv.zapret2control"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    if (qEnvironmentVariableIsSet("ZAPRET2CONTROL_QML_SMOKE_TEST"))
        return EXIT_SUCCESS;

    QObject *rootWindow = engine.rootObjects().constFirst();
    if (auto *window = qobject_cast<QWindow *>(rootWindow)) {
        QObject::connect(&blockcheckController, &BlockcheckJobController::scanFinished,
                         &app, [window, &trayController](int found, bool canceled) {
            if (!canceled && !window->isVisible()) {
                const QString title = QCoreApplication::translate("Main", "Zapret2 Control");
                const QString message = QCoreApplication::translate(
                    "Main", "Подбор завершён, найдено стратегий: %1").arg(found);
                trayController.showNotification(title, message);
            }
        });
        QObject::connect(&updateController, &UpdateController::updateAvailable,
                         &app, [window, &trayController](const QString &tag) {
            if (!window->isVisible()) {
                const QString title = QCoreApplication::translate("Main", "Zapret2 Control");
                const QString message = QCoreApplication::translate(
                    "Main", "Доступна новая версия zapret2: %1").arg(tag);
                trayController.showNotification(title, message);
            }
        });
    }
    QObject::connect(&trayController, &TrayController::activateRequested, &app, [rootWindow] {
        QMetaObject::invokeMethod(rootWindow, "toggleWindow");
    });
    QObject::connect(&trayController, &TrayController::settingsRequested, &app, [rootWindow] {
        QMetaObject::invokeMethod(rootWindow, "openSettings");
    });
    QObject::connect(&trayController, &TrayController::quitRequested, &app, [rootWindow] {
        QMetaObject::invokeMethod(rootWindow, "requestQuit");
    });
    QObject::connect(&trayController, &TrayController::watcherAvailableChanged, &app, [rootWindow, &trayController] {
        if (!trayController.watcherAvailable())
            QMetaObject::invokeMethod(rootWindow, "showWindow");
    });
    QObject::connect(&trayController, &TrayController::registrationFailedChanged, &app,
                     [rootWindow, &trayController] {
        if (trayController.registrationFailed())
            QMetaObject::invokeMethod(rootWindow, "showWindow");
    });
    if (startMinimized && (!trayController.watcherAvailable() || trayController.registrationFailed()))
        QMetaObject::invokeMethod(rootWindow, "showWindow");

    QObject::connect(&settingsManager, &SettingsManager::languageChanged, &app, [&]() {
        for (QTranslator *translator : translators)
            delete translator;
        const QLocale locale = localeForLanguage(settingsManager.language());
        QLocale::setDefault(locale);
        translators = I18n::install(app, locale);
        engine.retranslate();
    });

    return app.exec();
}

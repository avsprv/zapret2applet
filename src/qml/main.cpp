#include <cstdlib>

#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTranslator>
#include <QStyleHints>

#include "applicationcontroller.h"
#include "configcontroller.h"
#include "blockcheckjobcontroller.h"
#include "i18n.h"
#include "profilecontroller.h"
#include "logscontroller.h"
#include "listscontroller.h"
#include "updatecontroller.h"
#include "settingsmanager.h"
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
    QCoreApplication::setApplicationName(QStringLiteral("zapret2applet"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QZAPRET_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("zapret2applet"));

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
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsManager"), &settingsManager);
    engine.rootContext()->setContextProperty(QStringLiteral("autostartController"), &autostartController);
    engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &trayController);
    engine.rootContext()->setContextProperty(QStringLiteral("configController"), &configController);
    engine.rootContext()->setContextProperty(QStringLiteral("profileController"), &profileController);
    engine.rootContext()->setContextProperty(QStringLiteral("logsController"), &logsController);
    engine.rootContext()->setContextProperty(QStringLiteral("listsController"), &listsController);
    engine.rootContext()->setContextProperty(QStringLiteral("blockcheckController"), &blockcheckController);
    engine.rootContext()->setContextProperty(QStringLiteral("updateController"), &updateController);
    engine.loadFromModule(QStringLiteral("org.avsprv.zapret2applet"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    QObject *rootWindow = engine.rootObjects().constFirst();
    QObject::connect(&trayController, &TrayController::activateRequested, &app, [rootWindow] {
        QMetaObject::invokeMethod(rootWindow, "showWindow");
    });
    QObject::connect(&trayController, &TrayController::settingsRequested, &app, [rootWindow] {
        QMetaObject::invokeMethod(rootWindow, "openSettings");
    });
    QObject::connect(&trayController, &TrayController::quitRequested, &app, &QCoreApplication::quit);

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

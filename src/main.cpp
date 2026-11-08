#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

#include "appsettings.h"
#include "settingsmigration.h"
#include "i18n.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zapret2control"));
    QApplication::setApplicationVersion(QStringLiteral(QZAPRET_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("zapret2control"));
    SettingsMigration::migrateLegacyProfile();
    // Окно прячется в трей, поэтому закрытие последнего окна не должно завершать приложение.
    QApplication::setQuitOnLastWindowClosed(false);
    QLocale locale;
    switch (AppSettings::language()) {
    case AppSettings::Language::Russian: locale = QLocale(QLocale::Russian); break;
    case AppSettings::Language::English: locale = QLocale(QLocale::English); break;
    case AppSettings::Language::System: locale = QLocale(); break;
    }
    I18n::install(app, locale);
    AppSettings::applyTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Неофициальная Qt6-оболочка для zapret2"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized(
        QStringLiteral("minimized"),
        QCoreApplication::translate("main", "Запустить скрытым в системном трее"));
    parser.addOption(minimized);
    parser.process(app);

    MainWindow window;
    // Без трея скрытый запуск бессмысленен — окно всё равно покажем.
    if (!(parser.isSet(minimized) && window.trayAvailable()))
        window.show();
    // Если трея нет, закрытие окна должно завершать приложение.
    if (!window.trayAvailable())
        QApplication::setQuitOnLastWindowClosed(true);
    return app.exec();
}

#include "i18n.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QTranslator>

QList<QTranslator *> I18n::install(QCoreApplication &app, const QLocale &locale)
{
    QList<QTranslator *> installed;

    // Кнопки стандартных диалогов, QFileDialog и т.п. (пакет qt6-qttranslations)
    auto *qtbase = new QTranslator(&app);
    if (qtbase->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                     QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(qtbase);
        installed << qtbase;
    } else {
        delete qtbase;
    }

    if (locale.language() != QLocale::Russian) {
        auto *own = new QTranslator(&app);
        if (own->load(locale, QStringLiteral("zapret2control"), QStringLiteral("_"), QStringLiteral(":/i18n"))
            || own->load(QStringLiteral(":/i18n/zapret2control_en.qm"))) {
            app.installTranslator(own);
            installed << own;
        } else {
            delete own;
        }

        auto *about = new QTranslator(&app);
        if (about->load(locale, QStringLiteral("zapret2control_about"), QStringLiteral("_"), QStringLiteral(":/i18n"))
            || about->load(QStringLiteral(":/i18n/zapret2control_about_en.qm"))) {
            app.installTranslator(about);
            installed << about;
        } else {
            delete about;
        }
    }
    return installed;
}

#include "aboutpage.h"

#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtGlobal>

#ifndef QZAPRET_VERSION
#define QZAPRET_VERSION "unknown"
#endif

AboutPage::AboutPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *name = new QLabel(QStringLiteral("<h2>Zapret2 Control</h2>"), this);
    name->setTextFormat(Qt::RichText);
    layout->addWidget(name);

    auto *description = new QLabel(
        tr("Неофициальный графический интерфейс Qt 6 для управления отдельно установленным zapret2 в Linux."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *details = new QTextBrowser(this);
    details->setObjectName(QStringLiteral("aboutDetails"));
    details->setOpenExternalLinks(true);
    details->setReadOnly(true);
    details->setMinimumHeight(300);
    details->setHtml(tr(
        "<h3>Версия и компоненты</h3>"
        "<p><b>Версия приложения:</b> %1</p>"
        "<ul>"
        "<li>Qt %2 — графический интерфейс</li>"
        "<li>zapret2 — upstream-компонент, устанавливается отдельно</li>"
        "<li>blockcheck2 — инструмент тестирования из upstream zapret2</li>"
        "<li>systemd и Polkit — системная интеграция и авторизация</li>"
        "</ul>"
        "<h3>Лицензии</h3>"
        "<p>Код Zapret2 Control распространяется по лицензии MIT. Лицензии "
        "сторонних компонентов сохраняют собственную область действия. "
        "Для zapret2 и дополнительных уведомлений см. upstream и NOTICE.</p>"
        "<h3>Проект и документация</h3>"
        "<ul>"
        "<li><a href=\"https://github.com/avsprv/zapret2control\">Репозиторий Zapret2 Control</a></li>"
        "<li><a href=\"https://github.com/avsprv/zapret2control/blob/main/README.md\">Документация приложения</a></li>"
        "<li><a href=\"https://github.com/avsprv/zapret2control/releases\">Релизы приложения</a></li>"
        "<li><a href=\"https://github.com/avsprv/zapret2control/issues\">Сообщить об ошибке</a></li>"
        "<li><a href=\"https://github.com/avsprv/zapret2control/blob/main/NOTICE.md\">Уведомления о сторонних компонентах</a></li>"
        "<li><a href=\"https://github.com/bol-van/zapret2\">Upstream-проект zapret2</a></li>"
        "</ul>"
        "<p><i>Zapret2 Control — независимый проект, не связанный с автором "
        "или сопровождающими upstream zapret2.</i></p>")
        .arg(QString::fromLatin1(QZAPRET_VERSION).toHtmlEscaped(),
             QString::fromLatin1(qVersion()).toHtmlEscaped()));
    layout->addWidget(details, 1);
}

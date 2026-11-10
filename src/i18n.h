#pragma once

#include <QList>
#include <QLocale>

class QCoreApplication;
class QTranslator;

namespace I18n {

// Исходные строки в коде — русские, поэтому для русской локали переводчик не нужен.
// Для любой другой локали ставится перевод на её язык, а если его нет — английский
// (:/i18n/zapret2control_en.qm). Дополнительно подключаются стандартные переводы Qt (qtbase).
// Возвращает установленные переводчики (владелец — app).
QList<QTranslator *> install(QCoreApplication &app, const QLocale &locale = QLocale());

} // namespace I18n

#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

static QString desktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart/zapret2control.desktop");
}

bool Autostart::isEnabled()
{
    return QFile::exists(desktopPath());
}

bool Autostart::setEnabled(bool enabled)
{
    const QString path = desktopPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QString exe = QCoreApplication::applicationFilePath();
    exe.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=zapret2control\n"
                           "Comment=Unofficial Qt6 GUI for zapret2\n"
                           "Exec=\"%1\" --minimized\n"
                           "Icon=zapret2control\n"
                           "Terminal=false\n"
                           "X-GNOME-Autostart-enabled=true\n")
                .arg(exe)
                .toUtf8());
    return f.commit();
}

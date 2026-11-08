#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString escapeExecPath(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return value;
}

QString legacyDesktopEntry(const QString &legacyExecutable)
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=zapret2applet\n"
                          "Comment=Unofficial Qt6 GUI for zapret2\n"
                          "Exec=\"%1\" --minimized\n"
                          "Icon=zapret2applet\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(escapeExecPath(legacyExecutable));
}

QString currentDesktopEntry(const QString &applicationPath)
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=Zapret2 Control\n"
                          "Comment=Network Control for Linux\n"
                          "Exec=\"%1\" --minimized\n"
                          "Icon=zapret2control\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(escapeExecPath(applicationPath));
}
} // namespace

static QString desktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart/zapret2control.desktop");
}

bool Autostart::migrateLegacyEntry(const QString &autostartDirectory, const QString &applicationPath)
{
    const QString legacyPath = QDir(autostartDirectory).filePath(QStringLiteral("zapret2applet.desktop"));
    const QString currentPath = QDir(autostartDirectory).filePath(QStringLiteral("zapret2control.desktop"));
    const QFileInfo currentInfo(currentPath);
    if (currentInfo.exists() || currentInfo.isSymLink())
        return true;

    const QFileInfo legacyInfo(legacyPath);
    if (!legacyInfo.exists() || legacyInfo.isSymLink() || !legacyInfo.isFile())
        return true;

    const QString legacyExecutable = QDir(QFileInfo(applicationPath).absolutePath())
        .filePath(QStringLiteral("zapret2applet"));
    QFile legacy(legacyPath);
    if (!legacy.open(QIODevice::ReadOnly))
        return false;
    if (legacy.readAll() != legacyDesktopEntry(legacyExecutable).toUtf8())
        return true;

    if (!QDir().mkpath(autostartDirectory))
        return false;
    QSaveFile migrated(currentPath);
    if (!migrated.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray data = currentDesktopEntry(applicationPath).toUtf8();
    if (migrated.write(data) != data.size() || !migrated.commit())
        return false;
    if (!QFile::remove(legacyPath)) {
        QFile::remove(currentPath);
        return false;
    }
    return true;
}

bool Autostart::isEnabled()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
    migrateLegacyEntry(directory, QCoreApplication::applicationFilePath());
    return QFileInfo(desktopPath()).isFile();
}

bool Autostart::setEnabled(bool enabled)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
    if (!migrateLegacyEntry(directory, QCoreApplication::applicationFilePath()))
        return false;

    const QString path = desktopPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray data = currentDesktopEntry(QCoreApplication::applicationFilePath()).toUtf8();
    return f.write(data) == data.size() && f.commit();
}

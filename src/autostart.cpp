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

bool isManagedCurrentEntry(const QString &path, const QString &applicationPath)
{
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink() || !info.isFile())
        return false;

    QFile entry(path);
    if (!entry.open(QIODevice::ReadOnly))
        return false;

    const QByteArray contents = entry.readAll();
    return entry.error() == QFile::NoError
        && contents == currentDesktopEntry(applicationPath).toUtf8();
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
    const QString applicationPath = QCoreApplication::applicationFilePath();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
    migrateLegacyEntry(directory, applicationPath);
    return isManagedCurrentEntry(desktopPath(), applicationPath);
}

bool Autostart::setEnabled(bool enabled)
{
    const QString applicationPath = QCoreApplication::applicationFilePath();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
    if (!migrateLegacyEntry(directory, applicationPath))
        return false;

    const QString path = desktopPath();
    const QFileInfo info(path);
    const bool pathExists = info.exists() || info.isSymLink();

    if (!enabled) {
        if (!pathExists)
            return true;
        if (!isManagedCurrentEntry(path, applicationPath))
            return false;
        return QFile::remove(path);
    }

    if (pathExists)
        return isManagedCurrentEntry(path, applicationPath);

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray data = currentDesktopEntry(applicationPath).toUtf8();
    return file.write(data) == data.size() && file.commit();
}

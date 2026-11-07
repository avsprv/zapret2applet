#include "autostartcontroller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString escapeDesktopExecArgument(QString value)
{
    value.replace(QLatin1Char('%'), QStringLiteral("%%"));
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char(0x60), QString(QLatin1Char('\\')) + QLatin1Char(0x60));
    value.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool readDesktopEntryValue(const QString &path, const QString &key, QString *value)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString contents = QString::fromUtf8(file.readAll());
    if (file.error() != QFile::NoError)
        return false;

    bool inDesktopEntry = false;
    bool found = false;
    for (QString line : contents.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            if (inDesktopEntry)
                break;
            inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry)
            continue;
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator < 0 || line.first(separator) != key)
            continue;
        if (found)
            return false;
        found = true;
        *value = line.mid(separator + 1);
    }
    return found;
}
} // namespace

AutostartController::AutostartController(QObject *parent, const QString &autostartDirectory,
                                         const QString &executablePath)
    : QObject(parent)
    , m_autostartDirectory(autostartDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart")
          : autostartDirectory)
    , m_executablePath(executablePath.isEmpty() ? QCoreApplication::applicationFilePath() : executablePath)
{
}

bool AutostartController::enabled() const { return isManagedEntry(); }
QString AutostartController::errorMessage() const { return m_errorMessage; }
QString AutostartController::entryPath() const
{
    return QDir(m_autostartDirectory).filePath(QStringLiteral("zapret2applet.desktop"));
}
QString AutostartController::expectedExec() const { return escapeDesktopExecArgument(m_executablePath); }

bool AutostartController::isManagedEntry() const
{
    const QFileInfo info(entryPath());
    if (!info.exists() || info.isSymLink() || !info.isFile())
        return false;
    QString type, marker, exec;
    return readDesktopEntryValue(entryPath(), QStringLiteral("Type"), &type)
        && readDesktopEntryValue(entryPath(), QStringLiteral("X-Zapret2Applet-Managed"), &marker)
        && readDesktopEntryValue(entryPath(), QStringLiteral("Exec"), &exec)
        && type == QLatin1String("Application")
        && marker == QLatin1String("true")
        && exec == expectedExec();
}

void AutostartController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

void AutostartController::setEnabled(bool enabled)
{
    const QFileInfo info(entryPath());
    if (enabled) {
        if (isManagedEntry()) {
            setError({});
            return;
        }
        if (info.exists() || info.isSymLink()) {
            setError(tr("Файл автозапуска уже существует и не принадлежит приложению."));
            return;
        }
        if (m_executablePath.isEmpty() || !QFileInfo(m_executablePath).isAbsolute()) {
            setError(tr("Не удалось определить абсолютный путь к приложению."));
            return;
        }
        if (!QDir().mkpath(m_autostartDirectory)) {
            setError(tr("Не удалось создать каталог автозапуска."));
            return;
        }
        QSaveFile file(entryPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            setError(tr("Не удалось записать файл автозапуска: %1").arg(file.errorString()));
            return;
        }
        const QByteArray data = QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=zapret2applet\n"
            "Comment=Unofficial Qt6 GUI for zapret2\n"
            "Exec=%1\n"
            "Icon=zapret2applet\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n"
            "X-Zapret2Applet-Managed=true\n").arg(expectedExec()).toUtf8();
        if (file.write(data) != data.size() || !file.commit()) {
            setError(tr("Не удалось сохранить файл автозапуска: %1").arg(file.errorString()));
            return;
        }
        setError({});
        emit enabledChanged();
        return;
    }
    if (!info.exists() && !info.isSymLink()) {
        setError({});
        return;
    }
    if (info.isSymLink() || !isManagedEntry()) {
        setError(tr("Файл автозапуска не принадлежит приложению и не будет удалён."));
        return;
    }
    if (!QFile::remove(entryPath())) {
        setError(tr("Не удалось удалить файл автозапуска."));
        return;
    }
    setError({});
    emit enabledChanged();
}

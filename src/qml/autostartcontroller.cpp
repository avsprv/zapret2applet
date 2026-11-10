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

QString legacyDesktopEntry(const QString &legacyExecutable)
{
    return QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=zapret2control\n"
        "Comment=Unofficial Qt6 GUI for zapret2\n"
        "Exec=%1 --minimized\n"
        "Icon=zapret2control\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-Zapret2Applet-Managed=true\n").arg(escapeDesktopExecArgument(legacyExecutable));
}

QString currentDesktopEntry(const QString &executablePath)
{
    return QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Zapret2 Control\n"
        "Comment=Network Control for Linux\n"
        "Exec=%1 --minimized\n"
        "Icon=zapret2control\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n").arg(escapeDesktopExecArgument(executablePath));
}

bool fileHasExactContents(const QString &path, const QByteArray &expected)
{
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink() || !info.isFile())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray contents = file.readAll();
    return file.error() == QFile::NoError && contents == expected;
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
    const QString legacyPath = QDir(m_autostartDirectory).filePath(QStringLiteral("zapret2applet.desktop"));
    const QFileInfo legacyInfo(legacyPath);
    const QFileInfo newInfo(entryPath());
    if (!legacyInfo.exists() || legacyInfo.isSymLink() || !legacyInfo.isFile()
        || newInfo.exists() || newInfo.isSymLink() || m_executablePath.isEmpty()
        || !QFileInfo(m_executablePath).isAbsolute())
        return;

    const QString legacyExecutable = QDir(QFileInfo(m_executablePath).absolutePath())
        .filePath(QStringLiteral("zapret2applet"));
    if (!fileHasExactContents(legacyPath, legacyDesktopEntry(legacyExecutable).toUtf8()))
        return;

    QSaveFile migrated(entryPath());
    if (!migrated.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    const QByteArray data = currentDesktopEntry(m_executablePath).toUtf8();
    if (migrated.write(data) != data.size() || !migrated.commit())
        return;
    if (!QFile::remove(legacyPath))
        QFile::remove(entryPath());
}

bool AutostartController::enabled() const { return isManagedEntry(); }
QString AutostartController::errorMessage() const { return m_errorMessage; }
QString AutostartController::entryPath() const
{
    return QDir(m_autostartDirectory).filePath(QStringLiteral("zapret2control.desktop"));
}
bool AutostartController::isManagedEntry() const
{
    if (m_executablePath.isEmpty() || !QFileInfo(m_executablePath).isAbsolute())
        return false;
    return fileHasExactContents(entryPath(), currentDesktopEntry(m_executablePath).toUtf8());
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
        const QByteArray data = currentDesktopEntry(m_executablePath).toUtf8();
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

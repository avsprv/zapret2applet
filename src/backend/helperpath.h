#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Helper {

// Порядок: QZAPRET_HELPER (отладка) -> установленный путь -> путь из дерева исходников.
inline QString path()
{
    const QString env = qEnvironmentVariable("QZAPRET_HELPER");
    if (!env.isEmpty())
        return env;
    const QString installed = QStringLiteral(QZAPRET_HELPER_PATH);
    if (QFile::exists(installed))
        return installed;
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../packaging/helper/zapret2control-helper"));
}

// Запускает хелпер через pkexec. QZAPRET_NO_PKEXEC=1 — только для тестов:
// хелпер запускается напрямую, то есть с МЕНЬШИМИ правами, чем через pkexec.
inline void start(QProcess *proc, const QStringList &args)
{
    if (qEnvironmentVariableIsSet("QZAPRET_NO_PKEXEC"))
        proc->start(path(), args);
    else
        proc->start(QStringLiteral("pkexec"), QStringList{path()} + args);
}

// pkexec: 126 — пользователь закрыл диалог, 127 — не авторизован.
inline bool isAuthDismissed(int exitCode)
{
    return exitCode == 126;
}

} // namespace Helper

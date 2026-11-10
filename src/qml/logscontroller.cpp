#include "logscontroller.h"

#include "backend/helperpath.h"

LogsController::LogsController(QObject *parent)
    : QObject(parent)
{
}

QString LogsController::logText() const { return m_logText; }
QString LogsController::status() const { return m_status; }
bool LogsController::busy() const { return m_busy; }
int LogsController::lineCount() const { return m_lineCount; }

void LogsController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void LogsController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void LogsController::setLogText(const QString &text)
{
    if (m_logText == text)
        return;
    m_logText = text;
    emit logTextChanged();
}

void LogsController::readLogs(int lineCount)
{
    if (m_busy)
        return;
    if (lineCount != 50 && lineCount != 100 && lineCount != 200 && lineCount != 500) {
        setStatus(tr("Недопустимое количество строк журнала."));
        return;
    }

    m_lineCount = lineCount;
    emit lineCountChanged();
    m_output.clear();
    m_error.clear();
    setBusy(true);
    setStatus(tr("Чтение журнала zapret2.service…"));

    auto *process = new QProcess(this);
    m_process = process;

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process != process)
            return;
        m_output.append(process->readAllStandardOutput());
        if (m_output.size() > 1024 * 1024) {
            setStatus(tr("Полученный журнал превысил допустимый размер; чтение остановлено."));
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_process != process)
            return;
        m_error.append(process->readAllStandardError());
        if (m_error.size() > 64 * 1024)
            m_error = m_error.right(64 * 1024);
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process != process)
            return;
        m_output.append(process->readAllStandardOutput());
        m_error.append(process->readAllStandardError());
        const QString error = QString::fromUtf8(m_error).trimmed();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);

        if (m_output.size() > 1024 * 1024) {
            setStatus(tr("Журнал превышает ограничение 1 MiB; данные не отображены."));
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            setStatus(error.isEmpty()
                ? tr("Не удалось прочитать журнал (код %1).").arg(exitCode)
                : tr("Не удалось прочитать журнал: %1").arg(error));
            return;
        }

        const QString decoded = QString::fromUtf8(m_output);
        if (decoded.toUtf8() != m_output) {
            setStatus(tr("Журнал содержит некорректный UTF-8; данные не отображены."));
            return;
        }
        setLogText(decoded);
        setStatus(tr("Журнал обновлён: получено %1 байт.").arg(m_output.size()));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process)
            return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        setStatus(tr("Не удалось запустить helper: %1").arg(reason));
    });

    Helper::start(process, {QStringLiteral("logs-read"), QString::number(lineCount)});
}

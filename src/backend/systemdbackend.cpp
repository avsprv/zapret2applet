#include "systemdbackend.h"

#include <QProcess>
#include <QTimer>
#include <memory>

#include "helperpath.h"

int SystemdBackend::processTimeoutMs(bool privileged) const
{
    return privileged ? 120000 : 15000;
}

void SystemdBackend::startProcess(QProcess *process, const QString &program,
                                   const QStringList &arguments, bool privileged)
{
    if (privileged)
        Helper::start(process, arguments);
    else
        process->start(program, arguments);
}

SystemdBackend::SystemdBackend(QObject *parent)
    : ZapretBackend(parent)
    , m_unit(QStringLiteral("zapret2.service"))
{
}

void SystemdBackend::query(const QString &verb, const std::function<void(const QString &)> &onResult)
{
    auto *proc = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    auto timedOut = std::make_shared<bool>(false);
    auto *timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this, proc, completed, timedOut] {
        if (*completed)
            return;
        *timedOut = true;
        emit errorOccurred(tr("Превышено время ожидания ответа systemctl."));
        proc->kill();
    });
    connect(proc, &QProcess::finished, this, [proc, onResult, completed, timedOut, timeout] {
        if (*completed)
            return;
        *completed = true;
        timeout->stop();
        const QString output = *timedOut
            ? QString() : QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        onResult(output);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, onResult, completed, timeout](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *completed)
            return;
        *completed = true;
        timeout->stop();
        emit errorOccurred(tr("Ошибка systemctl: %1").arg(proc->errorString()));
        onResult(QString());
        proc->deleteLater();
    });
    startProcess(proc, QStringLiteral("systemctl"), {verb, m_unit}, false);
    timeout->start(processTimeoutMs(false));
}

namespace {
ZapretBackend::State parseUnitState(const QString &out)
{
    if (out == QLatin1String("active"))
        return ZapretBackend::State::Active;
    if (out == QLatin1String("inactive"))
        return ZapretBackend::State::Inactive;
    if (out == QLatin1String("failed"))
        return ZapretBackend::State::Failed;
    return ZapretBackend::State::Unknown;
}
} // namespace

void SystemdBackend::refresh()
{
    const quint64 generation = ++m_stateQueryGeneration;
    query(QStringLiteral("is-active"), [this, generation](const QString &out) {
        if (generation == m_stateQueryGeneration)
            emit stateChanged(parseUnitState(out));
    });
    query(QStringLiteral("is-enabled"), [this, generation](const QString &out) {
        if (generation == m_stateQueryGeneration)
            emit bootEnabledChanged(out == QLatin1String("enabled"));
    });
}

void SystemdBackend::verifyState()
{
    const quint64 generation = ++m_stateQueryGeneration;
    query(QStringLiteral("is-active"), [this, generation](const QString &out) {
        const State state = parseUnitState(out);
        if (generation == m_stateQueryGeneration)
            emit stateChanged(state);
        emit stateVerified(state);
    });
}

void SystemdBackend::verifyState(quint64 requestId)
{
    // Invalidate older refresh results while keeping this lifecycle result private.
    ++m_stateQueryGeneration;
    // Lifecycle verification is private to its requesting operation. Publishing
    // it as stateChanged would let a late response overwrite the application UI.
    query(QStringLiteral("is-active"), [this, requestId](const QString &out) {
        emit stateVerifiedForRequest(requestId, parseUnitState(out));
    });
}

void SystemdBackend::start() { runUnitAction(QStringLiteral("start")); }
void SystemdBackend::start(quint64 requestId) { runUnitAction(QStringLiteral("start"), requestId); }
void SystemdBackend::stop() { runUnitAction(QStringLiteral("stop")); }
void SystemdBackend::stop(quint64 requestId) { runUnitAction(QStringLiteral("stop"), requestId); }

void SystemdBackend::setBootEnabled(bool enabled)
{
    runUnitAction(enabled ? QStringLiteral("enable") : QStringLiteral("disable"));
}

void SystemdBackend::runUnitAction(const QString &action, quint64 requestId)
{
    m_pendingActions.enqueue({action, requestId});
    startNextUnitAction();
}

void SystemdBackend::startNextUnitAction()
{
    if (m_actionInProgress || m_pendingActions.isEmpty())
        return;

    m_actionInProgress = true;
    ++m_stateQueryGeneration; // invalidate refreshes started before this action
    const PendingUnitAction pending = m_pendingActions.dequeue();
    const QString action = pending.action;
    const quint64 requestId = pending.requestId;
    auto *proc = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    auto timedOut = std::make_shared<bool>(false);
    auto *timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    const auto reportResult = [this, action, requestId](bool success) {
        const quint64 generation = ++m_stateQueryGeneration;
        query(QStringLiteral("is-active"), [this, action, success, requestId, generation](const QString &out) {
            const State state = parseUnitState(out);
            if (generation == m_stateQueryGeneration)
                emit stateChanged(state);
            if (requestId == 0)
                emit unitActionFinished(action, state, success);
            else
                emit unitActionFinishedForRequest(requestId, action, state, success);

            m_actionInProgress = false;
            startNextUnitAction();
        });
        query(QStringLiteral("is-enabled"), [this, generation](const QString &out) {
            if (generation == m_stateQueryGeneration)
                emit bootEnabledChanged(out == QLatin1String("enabled"));
        });
    };

    connect(timeout, &QTimer::timeout, this,
            [this, proc, action, completed, timedOut] {
        if (*completed)
            return;
        *timedOut = true;
        emit errorOccurred(tr("Операция systemd выполняется дольше ожидаемого: %1. Ожидается фактическое завершение команды.").arg(action));
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, action, reportResult, completed, timedOut, timeout](int code, QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        timeout->stop();
        const bool success = !*timedOut && code == 0 && status == QProcess::NormalExit;
        if (!*timedOut && !success && !Helper::isAuthDismissed(code)) {
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            emit errorOccurred(tr("Операция не выполнена (код %1): %2").arg(code).arg(err));
        }
        proc->deleteLater();
        reportResult(success);
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, reportResult, completed, timeout](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !*completed) {
            timeout->stop();
            emit errorOccurred(tr("Не удалось запустить операцию systemd: %1").arg(proc->errorString()));
            *completed = true;
            proc->deleteLater(); // finished() после FailedToStart не приходит
            reportResult(false);
        }
    });
    startProcess(proc, QString(), {QStringLiteral("unit"), action}, true);
    timeout->start(processTimeoutMs(true));
}

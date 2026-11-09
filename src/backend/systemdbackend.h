#pragma once

#include <QStringList>
#include <QProcess>
#include <QQueue>
#include <functional>

#include "zapretbackend.h"

// Управляет апстримным юнитом zapret2.service.
// Чтение статуса — без привилегий; start/stop/enable/disable — через zapret2control-helper (polkit).
class SystemdBackend : public ZapretBackend
{
    Q_OBJECT
public:
    explicit SystemdBackend(QObject *parent = nullptr);

    void refresh() override;
    void verifyState() override;
    void verifyState(quint64 requestId) override;
    void start() override;
    void start(quint64 requestId) override;
    void stop() override;
    void stop(quint64 requestId) override;
    void setBootEnabled(bool enabled) override;

protected:
    virtual int processTimeoutMs(bool privileged) const;
    virtual void startProcess(QProcess *process, const QString &program,
                              const QStringList &arguments, bool privileged);

private:
    struct PendingUnitAction {
        QString action;
        quint64 requestId = 0;
    };
    void startNextUnitAction();
    void runUnitAction(const QString &action, quint64 requestId = 0);
    void query(const QString &verb, const std::function<void(const QString &)> &onResult);

    QString m_unit;
    QQueue<PendingUnitAction> m_pendingActions;
    bool m_actionInProgress = false;
    quint64 m_stateQueryGeneration = 0;
};

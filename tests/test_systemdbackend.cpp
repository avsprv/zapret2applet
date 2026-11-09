#include <QtTest>
#include <QProcess>
#include <QSignalSpy>
#include <QPointer>

#include "backend/systemdbackend.h"

class TestSystemdBackend final : public SystemdBackend
{
    Q_OBJECT
public:
    using SystemdBackend::SystemdBackend;

    QStringList actions;
    int activePrivilegedProcesses = 0;
    int maxConcurrentPrivilegedProcesses = 0;
    bool failNextActionToStart = false;
    bool failNextActionToExecute = false;
    bool failNextStateQueryToStart = false;
    int activeQueryCount = 0;
    bool holdPrivilegedProcessOpen = false;
    bool delayFirstStateQuery = false;
    bool delayFirstBootQuery = false;
    bool shortTimeouts = false;
    bool delayNextAction = false;
    bool delayNextStateQuery = false;
    int bootQueryCount = 0;
    QPointer<QProcess> lastPrivilegedProcess;

protected:
    int processTimeoutMs(bool privileged) const override
    {
        Q_UNUSED(privileged);
        return shortTimeouts ? 100 : SystemdBackend::processTimeoutMs(privileged);
    }

    void startProcess(QProcess *process, const QString &program,
                      const QStringList &arguments, bool privileged) override
    {
        Q_UNUSED(program);
        if (privileged) {
            const QString action = arguments.value(1);
            actions.append(action);
            ++activePrivilegedProcesses;
            maxConcurrentPrivilegedProcesses = qMax(maxConcurrentPrivilegedProcesses,
                                                       activePrivilegedProcesses);
            lastPrivilegedProcess = process;
            connect(process, &QProcess::finished, this, [this] { --activePrivilegedProcesses; });
            if (delayNextAction) {
                delayNextAction = false;
                process->start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QStringLiteral("exec sleep 0.4")});
                return;
            }
            connect(process, &QProcess::errorOccurred, this,
                    [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    --activePrivilegedProcesses;
            });
            if (holdPrivilegedProcessOpen) {
                process->start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QStringLiteral("sleep 10")});
                return;
            }
            if (failNextActionToStart) {
                failNextActionToStart = false;
                process->start(QStringLiteral("/path/that/does/not/exist"));
                return;
            }
            if (failNextActionToExecute) {
                failNextActionToExecute = false;
                process->start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QStringLiteral("exit 1")});
                return;
            }
            process->start(QStringLiteral("/bin/sh"),
                           {QStringLiteral("-c"), QStringLiteral("exit 0")});
            return;
        }

        if (arguments.value(0) == QLatin1String("is-active") && delayNextStateQuery) {
            delayNextStateQuery = false;
            process->start(QStringLiteral("/bin/sh"),
                           {QStringLiteral("-c"), QStringLiteral("exec sleep 10")});
            return;
        }
        if (arguments.value(0) == QLatin1String("is-active") && failNextStateQueryToStart) {
            failNextStateQueryToStart = false;
            process->start(QStringLiteral("/path/that/does/not/exist"));
            return;
        }

        if (arguments.value(0) == QLatin1String("is-active")) {
            ++activeQueryCount;
            if (activeQueryCount == 1 && delayFirstStateQuery) {
                process->start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QStringLiteral("sleep 0.15; printf inactive")});
                return;
            }
        }
        if (arguments.value(0) == QLatin1String("is-enabled")) {
            ++bootQueryCount;
            if (bootQueryCount == 1 && delayFirstBootQuery) {
                process->start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QStringLiteral("sleep 0.15; printf enabled")});
                return;
            }
        }
        const QString output = arguments.value(0) == QLatin1String("is-enabled")
                                   ? QStringLiteral("disabled")
                                   : QStringLiteral("active");
        process->start(QStringLiteral("/bin/sh"),
                       {QStringLiteral("-c"), QStringLiteral("printf '%1'").arg(output)});
    }
};

class SystemdBackendTest final : public QObject
{
    Q_OBJECT

private slots:
    void serializesUnitActionsInFifoOrder();
    void continuesAfterFailedToStart();
    void continuesAfterNonzeroExit();
    void releasesQueueWhenStateQueryCannotStart();
    void correlatedVerificationDoesNotPublishGlobalState();
    void ordinaryVerificationPublishesGlobalState();
    void newerRefreshSupersedesOlderStateQuery();
    void newerRefreshSupersedesOlderBootQuery();
    void stateQueryTimeoutProducesUnknown();
    void continuesAfterTimedOutAction();
    void correlatedVerificationInvalidatesOlderRefresh();
    void destroyingBackendDestroysActiveProcesses();
};

void SystemdBackendTest::serializesUnitActionsInFifoOrder()
{
    TestSystemdBackend backend;
    QSignalSpy finished(&backend, &ZapretBackend::unitActionFinished);

    backend.stop();
    backend.start();
    backend.setBootEnabled(false);
    backend.setBootEnabled(true);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, 3000);
    QCOMPARE(backend.actions,
             QStringList({QStringLiteral("stop"), QStringLiteral("start"),
                          QStringLiteral("disable"), QStringLiteral("enable")}));
    QCOMPARE(backend.maxConcurrentPrivilegedProcesses, 1);
}

void SystemdBackendTest::continuesAfterFailedToStart()
{
    TestSystemdBackend backend;
    backend.failNextActionToStart = true;
    QSignalSpy finished(&backend, &ZapretBackend::unitActionFinished);

    backend.stop();
    backend.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
    QCOMPARE(backend.actions,
             QStringList({QStringLiteral("stop"), QStringLiteral("start")}));
    QVERIFY(!finished.at(0).at(2).toBool());
    QVERIFY(finished.at(1).at(2).toBool());
}

void SystemdBackendTest::continuesAfterNonzeroExit()
{
    TestSystemdBackend backend;
    backend.failNextActionToExecute = true;
    QSignalSpy finished(&backend, &ZapretBackend::unitActionFinished);

    backend.stop();
    backend.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
    QVERIFY(!finished.at(0).at(2).toBool());
    QVERIFY(finished.at(1).at(2).toBool());
    QCOMPARE(backend.actions.size(), 2);
}

void SystemdBackendTest::correlatedVerificationDoesNotPublishGlobalState()
{
    TestSystemdBackend backend;
    QSignalSpy globalState(&backend, &ZapretBackend::stateChanged);
    QSignalSpy verified(&backend, &ZapretBackend::stateVerifiedForRequest);

    backend.verifyState(quint64(42));

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 3000);
    QCOMPARE(verified.at(0).at(0).toULongLong(), quint64(42));
    QCOMPARE(globalState.count(), 0);
}

void SystemdBackendTest::ordinaryVerificationPublishesGlobalState()
{
    TestSystemdBackend backend;
    QSignalSpy globalState(&backend, &ZapretBackend::stateChanged);
    QSignalSpy verified(&backend, &ZapretBackend::stateVerified);

    backend.verifyState();

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 3000);
    QCOMPARE(globalState.count(), 1);
    QCOMPARE(globalState.at(0).at(0).value<ZapretBackend::State>(), ZapretBackend::State::Active);
}

void SystemdBackendTest::newerRefreshSupersedesOlderStateQuery()
{
    TestSystemdBackend backend;
    backend.delayFirstStateQuery = true;
    QSignalSpy globalState(&backend, &ZapretBackend::stateChanged);

    backend.refresh();
    backend.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(globalState.count(), 1, 3000);
    QCOMPARE(globalState.at(0).at(0).value<ZapretBackend::State>(), ZapretBackend::State::Active);
    QTest::qWait(200);
    QCOMPARE(globalState.count(), 1);
}

void SystemdBackendTest::newerRefreshSupersedesOlderBootQuery()
{
    TestSystemdBackend backend;
    backend.delayFirstBootQuery = true;
    QSignalSpy bootState(&backend, &ZapretBackend::bootEnabledChanged);

    backend.refresh();
    backend.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(bootState.count(), 1, 3000);
    QCOMPARE(bootState.at(0).at(0).toBool(), false);
    QTest::qWait(200);
    QCOMPARE(bootState.count(), 1);
}

void SystemdBackendTest::stateQueryTimeoutProducesUnknown()
{
    TestSystemdBackend backend;
    backend.shortTimeouts = true;
    backend.delayNextStateQuery = true;
    QSignalSpy globalState(&backend, &ZapretBackend::stateChanged);
    QSignalSpy errors(&backend, &ZapretBackend::errorOccurred);

    backend.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(globalState.count(), 1, 3000);
    QCOMPARE(globalState.at(0).at(0).value<ZapretBackend::State>(), ZapretBackend::State::Unknown);
    QCOMPARE(errors.count(), 1);
}

void SystemdBackendTest::continuesAfterTimedOutAction()
{
    TestSystemdBackend backend;
    backend.shortTimeouts = true;
    backend.delayNextAction = true;
    QSignalSpy finished(&backend, &ZapretBackend::unitActionFinished);

    backend.stop();
    backend.start();

    QTRY_COMPARE_WITH_TIMEOUT(backend.actions.size(), 1, 1000);
    QTest::qWait(150);
    QCOMPARE(backend.actions.size(), 1);
    QCOMPARE(finished.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
    QVERIFY(!finished.at(0).at(2).toBool());
    QVERIFY(finished.at(1).at(2).toBool());
    QCOMPARE(backend.maxConcurrentPrivilegedProcesses, 1);
}

void SystemdBackendTest::correlatedVerificationInvalidatesOlderRefresh()
{
    TestSystemdBackend backend;
    backend.delayFirstStateQuery = true;
    QSignalSpy globalState(&backend, &ZapretBackend::stateChanged);
    QSignalSpy verified(&backend, &ZapretBackend::stateVerifiedForRequest);

    backend.refresh();
    backend.verifyState(quint64(73));

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 3000);
    QCOMPARE(globalState.count(), 0);
    QTest::qWait(200);
    QCOMPARE(globalState.count(), 0);
}

void SystemdBackendTest::destroyingBackendDestroysActiveProcesses()
{
    auto *backend = new TestSystemdBackend;
    backend->holdPrivilegedProcessOpen = true;
    backend->stop();
    QTRY_VERIFY_WITH_TIMEOUT(backend->lastPrivilegedProcess
                                 && backend->lastPrivilegedProcess->state() != QProcess::NotRunning,
                             3000);

    QPointer<QProcess> process = backend->lastPrivilegedProcess;
    delete backend;
    QVERIFY(process.isNull());
}

void SystemdBackendTest::releasesQueueWhenStateQueryCannotStart()
{
    TestSystemdBackend backend;
    backend.failNextStateQueryToStart = true;
    QSignalSpy finished(&backend, &ZapretBackend::unitActionFinished);

    backend.stop();
    backend.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
    QCOMPARE(backend.actions.size(), 2);
}

QTEST_GUILESS_MAIN(SystemdBackendTest)
#include "test_systemdbackend.moc"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "blockcheckjobcontroller.h"

class BlockcheckJobControllerTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void validatesOptionsBeforeConfirmation();
    void declineDoesNotSubmit();
    void submitsAndWaitsForVerifiedCompletion();
    void failedRecoveryKeepsServiceOperationsLocked();
    void pendingRecoveryCanBeRetriedAfterSubmitRejection();
    void startupProbeFindsRecoveryWithoutAnOperationId();
    void startupProbeFailureEnablesSafeRecoveryRetry();
    void startupProbeWaitsWhileAnotherJobIsActive();
    void staleStatusAdoptsQueuedOperationWithoutRecovery();
    void cancelPassesTheCurrentOperationId();

private:
    QTemporaryDir m_temp;
    QString m_helper;
};

void BlockcheckJobControllerTest::initTestCase()
{
    QVERIFY(m_temp.isValid());
    m_helper=m_temp.filePath(QStringLiteral("fake-helper"));
    QFile file(m_helper);
    QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Text));
    const QByteArray script=R"(#!/bin/sh
case "$1" in
  blockcheck-recovery-probe)
    if [ "${QZAPRET_TEST_PROBE_FAIL:-0}" = 1 ]; then
      echo 'probe failed' >&2
      exit 7
    elif [ "${QZAPRET_TEST_PROBE_BUSY:-0}" = 1 ]; then
      echo '{"version":1,"state":"busy","reason":"systemd_operation"}'
    elif [ "${QZAPRET_TEST_PROBE_RECOVERY:-0}" = 1 ]; then
      echo '{"version":1,"state":"recovery_required","reason":"durable_marker","operation_id":"123e4567-e89b-42d3-a456-426614174000"}'
    else
      echo '{"version":1,"state":"safe","reason":"no_pending_recovery"}'
    fi
    ;;
  blockcheck-submit)
    if [ "${QZAPRET_TEST_PENDING_RECOVERY:-0}" = 1 ]; then
      echo 'a previous Blockcheck recovery is still pending (operation_id=123e4567-e89b-42d3-a456-426614174000)' >&2
      exit 2
    fi
    printf '%s\n' '123e4567-e89b-42d3-a456-426614174000'
    ;;
  blockcheck-job-status)
    if [ "${QZAPRET_TEST_STALE_STAGED:-0}" = 1 ]; then
      printf '{"version":1,"operation_id":"%s","phase":"stale","recovery_marker":false,"staged_options":true,"staged_operation_id":"223e4567-e89b-42d3-a456-426614174001"}\n' "$2"
    elif [ "${QZAPRET_TEST_RECOVERY_FAILED:-0}" = 1 ]; then
      printf '{"version":1,"operation_id":"%s","phase":"recovery_failed","worker":{"ActiveState":"failed","Result":"exit-code"},"recovery":{"ActiveState":"inactive","Result":"success"},"recovery_marker":true,"staged_options":false}\n' "$2"
    else
      printf '{"version":1,"operation_id":"%s","phase":"completed","worker":{"ActiveState":"inactive","Result":"success"},"recovery":{"ActiveState":"inactive","Result":"success"},"recovery_marker":false,"staged_options":false}\n' "$2"
    fi
    ;;
  blockcheck-job-logs)
    printf '%s\n' '!!!!! curl_test_https_tls12: working strategy found for ipv4 example.org : nfqws2 --dpi-desync=fake !!!!!'
    ;;
  blockcheck-job-cancel)
    [ "$#" -eq 2 ] || exit 2
    printf '%s\n' "$2" > "$QZAPRET_TEST_CANCEL_ID_FILE"
    printf '%s\n' 'cancelled'
    ;;
  blockcheck-recover)
    exit 0
    ;;
  *)
    exit 2
    ;;
esac
)";
    QCOMPARE(file.write(script),qint64(script.size()));
    file.close();
    QVERIFY(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    qputenv("QZAPRET_HELPER",m_helper.toUtf8());
    qputenv("QZAPRET_NO_PKEXEC","1");
}

void BlockcheckJobControllerTest::cleanupTestCase()
{
    qunsetenv("QZAPRET_HELPER");
    qunsetenv("QZAPRET_NO_PKEXEC");
}

void BlockcheckJobControllerTest::validatesOptionsBeforeConfirmation()
{
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QSignalSpy confirmation(&controller,&BlockcheckJobController::confirmationRequired);
    controller.setDomains(QStringLiteral("example..org"));
    controller.start();
    QCOMPARE(confirmation.count(),0);
    QVERIFY(!controller.busy());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Error"));
}

void BlockcheckJobControllerTest::declineDoesNotSubmit()
{
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QSignalSpy confirmation(&controller,&BlockcheckJobController::confirmationRequired);
    QSignalSpy finished(&controller,&BlockcheckJobController::scanFinished);
    controller.start();
    QCOMPARE(confirmation.count(),1);
    QVERIFY(controller.busy());
    controller.confirmStop(false);
    QVERIFY(!controller.busy());
    QCOMPARE(finished.count(),1);
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Warning"));
}

void BlockcheckJobControllerTest::submitsAndWaitsForVerifiedCompletion()
{
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QSignalSpy finished(&controller,&BlockcheckJobController::scanFinished);
    QSignalSpy results(&controller,&BlockcheckJobController::resultsChanged);
    controller.start();
    controller.confirmStop(true);
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QCOMPARE(finished.count(),1);
    QCOMPARE(finished.at(0).at(0).toInt(),1);
    QCOMPARE(finished.at(0).at(1).toBool(),false);
    QCOMPARE(controller.results().size(),1);
    QVERIFY(controller.logText().contains(QStringLiteral("working strategy found")));
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Success"));
    QVERIFY(results.count()>0);
}

void BlockcheckJobControllerTest::pendingRecoveryCanBeRetriedAfterSubmitRejection()
{
    qputenv("QZAPRET_TEST_PENDING_RECOVERY","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    controller.start();
    controller.confirmStop(true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(controller.recoveryRequired());
    qunsetenv("QZAPRET_TEST_PENDING_RECOVERY");
    controller.retryRecovery();
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
}

void BlockcheckJobControllerTest::failedRecoveryKeepsServiceOperationsLocked()
{
    qputenv("QZAPRET_TEST_RECOVERY_FAILED","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    controller.start();
    controller.confirmStop(true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(controller.recoveryRequired());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Error"));
    controller.setServiceBusy(controller.recoveryRequired());
    controller.start();
    QVERIFY(controller.status().contains(QStringLiteral("другой операции")));
    qunsetenv("QZAPRET_TEST_RECOVERY_FAILED");
    controller.retryRecovery();
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Success"));
}

void BlockcheckJobControllerTest::startupProbeFindsRecoveryWithoutAnOperationId()
{
    qputenv("QZAPRET_TEST_PROBE_RECOVERY","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(controller.recoveryRequired());
    QVERIFY(controller.recoveryRetryAvailable());
    qunsetenv("QZAPRET_TEST_PROBE_RECOVERY");
    controller.retryRecovery();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
}


void BlockcheckJobControllerTest::startupProbeFailureEnablesSafeRecoveryRetry()
{
    qputenv("QZAPRET_TEST_PROBE_FAIL","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(controller.recoveryRequired());
    QVERIFY(controller.recoveryRetryAvailable());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Error"));

    qunsetenv("QZAPRET_TEST_PROBE_FAIL");
    controller.retryRecovery();
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Success"));
}

void BlockcheckJobControllerTest::startupProbeWaitsWhileAnotherJobIsActive()
{
    qputenv("QZAPRET_TEST_PROBE_BUSY","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(controller.busy(),1000);
    QVERIFY(!controller.cancelAvailable());
    QVERIFY(!controller.recoveryRetryAvailable());
    qunsetenv("QZAPRET_TEST_PROBE_BUSY");
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
}

void BlockcheckJobControllerTest::staleStatusAdoptsQueuedOperationWithoutRecovery()
{
    qputenv("QZAPRET_TEST_STALE_STAGED","1");
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    controller.start();
    controller.confirmStop(true);

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.status().contains(QStringLiteral("отслеживание продолжено")),5000);
    QVERIFY(controller.busy());
    QVERIFY(!controller.recoveryRequired());
    QVERIFY(!controller.recoveryRetryAvailable());

    qunsetenv("QZAPRET_TEST_STALE_STAGED");
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(!controller.recoveryRequired());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Success"));
}


void BlockcheckJobControllerTest::cancelPassesTheCurrentOperationId()
{
    const QString idFile=m_temp.filePath(QStringLiteral("cancel-operation-id"));
    qputenv("QZAPRET_TEST_CANCEL_ID_FILE",idFile.toUtf8());
    BlockcheckJobController controller;
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    controller.start();
    controller.confirmStop(true);
    QTRY_VERIFY_WITH_TIMEOUT(controller.status().contains(QStringLiteral("Задание принято")),5000);
    controller.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(idFile),5000);
    QFile file(idFile);
    QVERIFY(file.open(QIODevice::ReadOnly|QIODevice::Text));
    QCOMPARE(QString::fromUtf8(file.readAll()).trimmed(),
             QStringLiteral("123e4567-e89b-42d3-a456-426614174000"));
    qunsetenv("QZAPRET_TEST_CANCEL_ID_FILE");
}

QTEST_GUILESS_MAIN(BlockcheckJobControllerTest)
#include "test_blockcheckjobcontroller.moc"

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
  blockcheck-submit)
    printf '%s\n' '123e4567-e89b-42d3-a456-426614174000'
    ;;
  blockcheck-job-status)
    if [ "${QZAPRET_TEST_RECOVERY_FAILED:-0}" = 1 ]; then
      printf '%s\\n' '{"version":1,"operation_id":"123e4567-e89b-42d3-a456-426614174000","phase":"recovery_failed","worker":{"ActiveState":"failed","Result":"exit-code"},"recovery":{"ActiveState":"inactive","Result":"success"},"recovery_marker":true,"staged_options":false}'
    else
      printf '%s\\n' '{"version":1,"operation_id":"123e4567-e89b-42d3-a456-426614174000","phase":"completed","worker":{"ActiveState":"inactive","Result":"success"},"recovery":{"ActiveState":"inactive","Result":"success"},"recovery_marker":false,"staged_options":false}'
    fi
    ;;
  blockcheck-job-logs)
    printf '%s\n' '!!!!! curl_test_https_tls12: working strategy found for ipv4 example.org : nfqws2 --dpi-desync=fake !!!!!'
    ;;
  blockcheck-job-cancel)
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

void BlockcheckJobControllerTest::failedRecoveryKeepsServiceOperationsLocked()
{
    qputenv("QZAPRET_TEST_RECOVERY_FAILED","1");
    BlockcheckJobController controller;
    controller.start();
    controller.confirmStop(true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),5000);
    QVERIFY(controller.recoveryRequired());
    QCOMPARE(controller.statusSeverity(),QStringLiteral("Error"));
    controller.setServiceBusy(controller.recoveryRequired());
    controller.start();
    QVERIFY(controller.status().contains(QStringLiteral("другой операции")));
    qunsetenv("QZAPRET_TEST_RECOVERY_FAILED");
}

QTEST_GUILESS_MAIN(BlockcheckJobControllerTest)
#include "test_blockcheckjobcontroller.moc"

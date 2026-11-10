#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "logscontroller.h"

class TestLogsController final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsUnsupportedLineCount()
    {
        LogsController controller;
        controller.readLogs(25);
        QVERIFY(!controller.busy());
        QCOMPARE(controller.lineCount(), 200);
        QCOMPARE(controller.status(), QStringLiteral("Недопустимое количество строк журнала."));
    }

    void readsServiceLogsThroughHelper()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString helperPath = directory.filePath(QStringLiteral("fake-helper"));
        QFile helper(helperPath);
        QVERIFY(helper.open(QIODevice::WriteOnly | QIODevice::Text));
        helper.write("#!/bin/sh\n"
                     "[ \"$1\" = logs-read ] || exit 8\n"
                     "[ \"$2\" = 100 ] || exit 9\n"
                     "printf '2026-11-07T04:00:00+00:00 test log entry\\n'\n");
        helper.close();
        QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner));

        qputenv("QZAPRET_HELPER", helperPath.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        LogsController controller;
        controller.readLogs(100);
        QTRY_VERIFY_WITH_TIMEOUT(controller.logText().contains(QStringLiteral("test log entry")), 5000);
        QVERIFY(!controller.busy());
        QVERIFY(controller.status().contains(QStringLiteral("получено")));

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
    }
};

QTEST_GUILESS_MAIN(TestLogsController)
#include "test_logscontroller.moc"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend/zapretupdater.h"

class ZapretUpdaterTest final : public QObject
{
    Q_OBJECT

private slots:
    void boundsOversizedOutputLine();
};

void ZapretUpdaterTest::boundsOversizedOutputLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString helper = dir.filePath(QStringLiteral("fake-helper"));
    QFile script(helper);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\n"
                 "head -c 131072 /dev/zero | tr '\\000' 'x'\n"
                 "printf '\\n'\n"
                 "printf 'after oversized line\\n'\n");
    script.close();
    QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeUser));

    qputenv("QZAPRET_HELPER", helper.toLocal8Bit());
    qputenv("QZAPRET_NO_PKEXEC", "1");

    ZapretUpdater updater;
    QSignalSpy logs(&updater, &ZapretUpdater::logLine);
    QSignalSpy finished(&updater, &ZapretUpdater::finished);
    updater.start(QStringLiteral("v1.2.3"));

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QCOMPARE(finished.constFirst().at(0).toInt(), 0);
    QCOMPARE(logs.size(), 2);
    QVERIFY(logs.at(0).at(0).toString().size() < 66 * 1024);
    QCOMPARE(logs.at(1).at(0).toString(), QStringLiteral("after oversized line"));

    qunsetenv("QZAPRET_HELPER");
    qunsetenv("QZAPRET_NO_PKEXEC");
}

QTEST_GUILESS_MAIN(ZapretUpdaterTest)
#include "test_zapretupdater.moc"

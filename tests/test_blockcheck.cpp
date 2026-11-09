#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend/blockcheckrunner.h"

Q_DECLARE_METATYPE(BlockcheckRunner::Result)

class TestBlockcheck : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<BlockcheckRunner::Result>(); }

    void parseValid()
    {
        const auto r = BlockcheckRunner::parseLine(QStringLiteral(
            "!!!!! curl_test_https_tls12: working strategy found for ipv4 rutracker.org : "
            "nfqws2 --payload=tls_client_hello --lua-desync=fake:blob=0x00:ttl=3 !!!!!"));
        QVERIFY(r.has_value());
        QCOMPARE(r->test, QStringLiteral("curl_test_https_tls12"));
        QCOMPARE(r->ipv, QStringLiteral("4"));
        QCOMPARE(r->domain, QStringLiteral("rutracker.org"));
        QCOMPARE(r->daemon, QStringLiteral("nfqws2"));
        QCOMPARE(r->args, QStringLiteral("--payload=tls_client_hello --lua-desync=fake:blob=0x00:ttl=3"));
    }

    void parseIpv6AndTrailingSpace()
    {
        const auto r = BlockcheckRunner::parseLine(QStringLiteral(
            "  !!!!! curl_test_http: working strategy found for ipv6 example.org : nfqws2 --x=1 !!!!!  "));
        QVERIFY(r.has_value());
        QCOMPARE(r->ipv, QStringLiteral("6"));
        QCOMPARE(r->commandLine(), QStringLiteral("nfqws2 --x=1"));
    }

    void parseRejectsNoise_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("empty") << QString();
        QTest::newRow("progress") << QStringLiteral("- curl_test_http ipv4 rutracker.org : nfqws2 --x");
        QTest::newRow("not found")
            << QStringLiteral("curl_test_http: nfqws2 strategy for ipv4 example.org not found");
        QTest::newRow("no args")
            << QStringLiteral("!!!!! t: working strategy found for ipv4 example.org : nfqws2 !!!!!");
        QTest::newRow("bad ipv")
            << QStringLiteral("!!!!! t: working strategy found for ipv5 example.org : nfqws2 --x !!!!!");
    }
    void parseRejectsNoise()
    {
        QFETCH(QString, line);
        QVERIFY(!BlockcheckRunner::parseLine(line).has_value());
    }

    // Весь конвейер QProcess -> построчный разбор -> сигналы, с поддельным хелпером без pkexec.

    void runnerRejectsReentrantStartWhileFinishing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = dir.filePath(QStringLiteral("fake-helper"));
        const QString counter = dir.filePath(QStringLiteral("invocations"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray scriptContent = QStringLiteral(
            "#!/bin/sh\nprintf x >> '%1'\necho diagnostic\nexit 0\n")
                .arg(counter).toUtf8();
        f.write(scriptContent);
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);

        qputenv("QZAPRET_HELPER", script.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        BlockcheckRunner runner;
        QSignalSpy done(&runner, &BlockcheckRunner::finished);
        bool reentrantAttempted = false;
        connect(&runner, &BlockcheckRunner::logLine, &runner, [&] {
            if (reentrantAttempted)
                return;
            reentrantAttempted = true;
            runner.start({});
        });

        runner.start({});
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QTest::qWait(100);
        QVERIFY(reentrantAttempted);
        QFile invocations(counter);
        QVERIFY(invocations.open(QIODevice::ReadOnly));
        QCOMPARE(invocations.readAll(), QByteArray("x"));
        QVERIFY(!runner.isRunning());

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
    }

    void runnerPipeline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = dir.filePath(QStringLiteral("fake-helper"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "echo '* checking privileges'\n"
                "printf '!!!!! curl_test_https_tls12: working strategy found for ipv4 a.org : nfqws2 --one !!!!!\\r\n'\n"
                "echo '!!!!! curl_test_https_tls12: working strategy found for ipv4 a.org : nfqws2 --one !!!!!'\n"
                "echo '!!!!! curl_test_http: working strategy found for ipv4 a.org : nfqws2 --two --three !!!!!'\n"
                "printf 'tail-without-newline'\n"
                "exit 0\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);

        qputenv("QZAPRET_HELPER", script.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        BlockcheckRunner runner;
        QSignalSpy found(&runner, &BlockcheckRunner::strategyFound);
        QSignalSpy lines(&runner, &BlockcheckRunner::logLine);
        QSignalSpy done(&runner, &BlockcheckRunner::finished);

        runner.start({});
        QVERIFY(done.wait(5000));

        QCOMPARE(found.count(), 2); // повтор одной стратегии не создаёт дубликат результата
        QCOMPARE(lines.count(), 5); // исходный журнал сохраняет и повторную строку
        QCOMPARE(found.at(0).at(0).value<BlockcheckRunner::Result>().args, QStringLiteral("--one"));
        QCOMPARE(found.at(1).at(0).value<BlockcheckRunner::Result>().args, QStringLiteral("--two --three"));
        QCOMPARE(done.at(0).at(0).toInt(), 0);
        QCOMPARE(done.at(0).at(1).toBool(), false);
        QCOMPARE(runner.errorCount(), 0);
        QCOMPARE(lines.last().at(0).toString(), QStringLiteral("tail-without-newline"));
        QVERIFY(!runner.isRunning());
    }

    void runnerNoStrategiesIsNotExecutionError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = dir.filePath(QStringLiteral("fake-helper"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "echo '* checking privileges'\n"
                "echo 'diagnostic: no working strategy found'\n"
                "exit 0\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);

        qputenv("QZAPRET_HELPER", script.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        BlockcheckRunner runner;
        QSignalSpy found(&runner, &BlockcheckRunner::strategyFound);
        QSignalSpy done(&runner, &BlockcheckRunner::finished);
        runner.start({});
        QVERIFY(done.wait(5000));

        QCOMPARE(found.count(), 0);
        QCOMPARE(done.at(0).at(0).toInt(), 0);
        QCOMPARE(done.at(0).at(1).toBool(), false);
        QCOMPARE(runner.errorCount(), 0);
    }

    void runnerBoundsOversizedLines()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = dir.filePath(QStringLiteral("fake-helper"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "head -c 131072 /dev/zero | tr '\\000' 'x'\n"
                "printf '\\n'\n"
                "echo '!!!!! curl_test_http: working strategy found for ipv4 a.org : nfqws2 --valid !!!!!'\n"
                "exit 0\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);

        qputenv("QZAPRET_HELPER", script.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        BlockcheckRunner runner;
        QSignalSpy found(&runner, &BlockcheckRunner::strategyFound);
        QSignalSpy lines(&runner, &BlockcheckRunner::logLine);
        QSignalSpy done(&runner, &BlockcheckRunner::finished);
        runner.start({});
        QVERIFY(done.wait(5000));

        QVERIFY(!lines.isEmpty());
        QVERIFY(lines.at(0).at(0).toString().size() < 66 * 1024);
        QCOMPARE(found.count(), 1);
        QCOMPARE(found.at(0).at(0).value<BlockcheckRunner::Result>().args, QStringLiteral("--valid"));
    }

    void runnerFailedToStart()
    {
        qputenv("QZAPRET_HELPER", "/nonexistent/helper");
        qputenv("QZAPRET_NO_PKEXEC", "1");
        BlockcheckRunner runner;
        QSignalSpy done(&runner, &BlockcheckRunner::finished);
        runner.start({});
        QVERIFY(done.wait(5000));
        QCOMPARE(done.at(0).at(0).toInt(), -1);
        QCOMPARE(runner.errorCount(), 1);
    }

    void cancelHelperFailureIsReportedWithoutLosingScanState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = dir.filePath(QStringLiteral("fake-helper"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "if [ \"$1\" = blockcheck-cancel ]; then exit 7; fi\n"
                "if [ \"$1\" = blockcheck ]; then sleep 5; exit 0; fi\n"
                "exit 9\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);

        qputenv("QZAPRET_HELPER", script.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        BlockcheckRunner runner;
        QSignalSpy lines(&runner, &BlockcheckRunner::logLine);
        QSignalSpy done(&runner, &BlockcheckRunner::finished);
        runner.start({});
        QTRY_VERIFY_WITH_TIMEOUT(runner.isRunning(), 1000);
        runner.cancel();

        QTRY_VERIFY_WITH_TIMEOUT(!lines.isEmpty(), 1000);
        bool reported = false;
        for (const auto &line : lines) {
            if (line.at(0).toString().contains(QStringLiteral("Не удалось подтвердить отправку запроса отмены")))
                reported = true;
        }
        QVERIFY(reported);
        QVERIFY(runner.isRunning());
        QVERIFY(done.wait(7000));
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 0);
        QCOMPARE(done.at(0).at(1).toBool(), false);

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
    }
};

QTEST_MAIN(TestBlockcheck)
#include "test_blockcheck.moc"

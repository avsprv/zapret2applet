#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <memory>

#include "profilecontroller.h"

class TestProfileController final : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_configPath;
    QString m_helperPath;

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());

        m_configPath = m_dir->filePath(QStringLiteral("config"));
        QFile config(m_configPath);
        QVERIFY(config.open(QIODevice::WriteOnly));
        QVERIFY(config.write("NFQWS2_ENABLE=1\nNFQWS2_OPT=\"--filter-tcp=80 --payload=http_req\"\n") > 0);
        config.close();

        m_helperPath = m_dir->filePath(QStringLiteral("test-helper.sh"));
        QFile helper(m_helperPath);
        QVERIFY(helper.open(QIODevice::WriteOnly));
        const QByteArray script = R"SH(#!/bin/sh
set -eu
case "$1" in
    config-read)
        cat "$QZAPRET_TEST_CONFIG"
        ;;
    config-write)
        expected="$2"
        actual="$(sha256sum "$QZAPRET_TEST_CONFIG" | awk '{print $1}')"
        if [ "$expected" != "$actual" ]; then
            echo "stale config" >&2
            exit 9
        fi
        temporary="$QZAPRET_TEST_CONFIG.tmp.$$"
        cat > "$temporary"
        /bin/sh -n "$temporary"
        cp -p "$QZAPRET_TEST_CONFIG" "$QZAPRET_TEST_CONFIG.bak"
        mv "$temporary" "$QZAPRET_TEST_CONFIG"
        echo "saved"
        ;;
    *)
        echo "unsupported helper command" >&2
        exit 2
        ;;
esac
)SH";
        QCOMPARE(helper.write(script), script.size());
        helper.close();
        QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner));

        qputenv("QZAPRET_HELPER", m_helperPath.toUtf8());
        qputenv("QZAPRET_TEST_CONFIG", m_configPath.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");
    }

    void cleanup()
    {
        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_TEST_CONFIG");
        qunsetenv("QZAPRET_NO_PKEXEC");
        m_dir.reset();
    }

    void rejectsOversizedConfigOutput()
    {
        QFile config(m_configPath);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray oversized(1024 * 1024 + 1, 'x');
        QCOMPARE(config.write(oversized), oversized.size());
        config.close();

        ProfileController controller;
        controller.read();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.loaded());
        QVERIFY(controller.status().contains(QStringLiteral("превышает 1 MiB")));
    }

    void loadsAndSavesProfileMutations()
    {
        ProfileController controller;
        controller.read();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY2(controller.loaded(), qPrintable(controller.status()));
        QCOMPARE(controller.profiles().size(), 1);

        controller.add({QStringLiteral("--filter-tcp=443"),
                        QStringLiteral("--payload=tls_client_hello")});
        QCOMPARE(controller.profiles().size(), 2);
        controller.copy(1);
        QCOMPARE(controller.profiles().size(), 3);
        controller.edit(2, {QStringLiteral("--filter-tcp=8443")});
        controller.remove(1);
        QCOMPARE(controller.profiles().size(), 2);
        QVERIFY(controller.dirty());

        controller.save();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY2(!controller.dirty(), qPrintable(controller.status()));

        QFile saved(m_configPath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray contents = saved.readAll();
        QVERIFY(contents.contains("--filter-tcp=80"));
        QVERIFY(contents.contains("--filter-tcp=8443"));
        QVERIFY(!contents.contains("--filter-tcp=443"));
        QVERIFY(QFile::exists(m_configPath + QStringLiteral(".bak")));
    }

    void removingAnotherProfilePreservesSelection()
    {
        ProfileController controller;
        controller.read();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.loaded());

        controller.add({QStringLiteral("--filter-tcp=443")});
        controller.setSelectedIndex(0);
        controller.remove(1);
        QCOMPARE(controller.selectedIndex(), 0);
        QCOMPARE(controller.profiles().size(), 1);
    }

    void confirmedReloadDiscardsUnsavedChanges()
    {
        ProfileController controller;
        controller.read();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.loaded());

        controller.add({QStringLiteral("--filter-tcp=443")});
        QVERIFY(controller.dirty());
        controller.read();
        QVERIFY(controller.dirty());
        QCOMPARE(controller.profiles().size(), 2);

        controller.read(true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY2(!controller.dirty(), qPrintable(controller.status()));
        QCOMPARE(controller.profiles().size(), 1);
    }

    void staleConfigIsNeverOverwritten()
    {
        ProfileController controller;
        controller.read();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.loaded());

        QFile external(m_configPath);
        QVERIFY(external.open(QIODevice::Append));
        QVERIFY(external.write("# external change\n") > 0);
        external.close();

        controller.add({QStringLiteral("--filter-tcp=443")});
        controller.save();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.dirty());
        QVERIFY(controller.status().contains(QStringLiteral("stale config")));

        QFile current(m_configPath);
        QVERIFY(current.open(QIODevice::ReadOnly));
        QVERIFY(current.readAll().contains("# external change"));
    }
};

QTEST_GUILESS_MAIN(TestProfileController)
#include "test_profilecontroller.moc"

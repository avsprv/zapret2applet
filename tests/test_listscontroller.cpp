#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "listscontroller.h"

class TestListsController final : public QObject
{
    Q_OBJECT

private:
    QString createHelper(QTemporaryDir &directory, const QByteArray &body)
    {
        const QString path = directory.filePath(QStringLiteral("fake-helper"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return {};
        file.write("#!/bin/sh\n");
        file.write(body);
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return path;
    }

private slots:
    void rejectsUnknownListName()
    {
        ListsController controller;
        controller.readList(QStringLiteral("../../etc/passwd"));
        QVERIFY(!controller.busy());
        QVERIFY(controller.status().contains(QStringLiteral("Недопустимое")));
    }

    void exposesOnlySourceListsAsEditable()
    {
        ListsController controller;
        QVERIFY(controller.isEditableName(QStringLiteral("zapret-hosts-user.txt")));
        QVERIFY(controller.isEditableName(QStringLiteral("zapret-hosts-user-exclude.txt")));
        QVERIFY(controller.isEditableName(QStringLiteral("zapret-hosts-user-ipban.txt")));
        QVERIFY(!controller.isEditableName(QStringLiteral("zapret-ip-user.txt")));
        QVERIFY(!controller.isEditableName(QStringLiteral("zapret-ip-user6.txt")));
    }

    void readsListThroughHelper()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString helper = createHelper(directory,
            "[ \"$1\" = lists-read ] || exit 8\n"
            "[ \"$2\" = zapret-hosts-user.txt ] || exit 9\n"
            "printf 'example.org\\n'\n");
        QVERIFY(!helper.isEmpty());
        qputenv("QZAPRET_HELPER", helper.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");

        ListsController controller;
        controller.readList(QStringLiteral("zapret-hosts-user.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.loaded(), 5000);
        QCOMPARE(controller.content(), QStringLiteral("example.org\n"));
        QVERIFY(controller.editable());
        QVERIFY(!controller.dirty());

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
    }

    void validatesAndWritesOnlyChangedSourceList()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString helper = createHelper(directory,
            "if [ \"$1\" = lists-read ]; then [ \"$2\" = zapret-hosts-user.txt ] || exit 9; printf 'initial.example\\n'; exit 0; fi\n"
            "[ \"$1\" = lists-write ] || exit 8\n"
            "[ \"$2\" = zapret-hosts-user.txt ] || exit 9\n"
            "cat > \"$QZAPRET_CAPTURE\"\n"
            "printf 'OK: list updated; backup: test-backup\\n'\n");
        QVERIFY(!helper.isEmpty());
        const QString capture = directory.filePath(QStringLiteral("captured-list"));
        qputenv("QZAPRET_HELPER", helper.toUtf8());
        qputenv("QZAPRET_NO_PKEXEC", "1");
        qputenv("QZAPRET_CAPTURE", capture.toUtf8());

        ListsController controller;
        controller.readList(QStringLiteral("zapret-hosts-user.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.loaded(), 5000);
        controller.setContent(QStringLiteral("valid.example\n192.0.2.1/24\n"));
        QVERIFY(controller.dirty());
        controller.saveList();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.dirty());
        QFile saved(capture);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("valid.example\n192.0.2.1/24\n"));

        controller.setContent(QStringLiteral("invalid..domain\n"));
        QVERIFY(controller.dirty());
        controller.saveList();
        QVERIFY(!controller.busy());
        QVERIFY(controller.status().contains(QStringLiteral("Некорректный домен")));

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
        qunsetenv("QZAPRET_CAPTURE");
    }

    void resetsBusyStateWhenHelperCannotStart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString helper = createHelper(directory,
            "[ \"$1\" = lists-read ] || exit 8\n"
            "printf 'initial.example\\n'\n");
        QVERIFY(!helper.isEmpty());

        const QString missingHelper = directory.filePath(QStringLiteral("missing-helper"));
        qputenv("QZAPRET_NO_PKEXEC", "1");
        qputenv("QZAPRET_HELPER", missingHelper.toUtf8());

        ListsController controller;
        controller.readList(QStringLiteral("zapret-hosts-user.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.loaded());
        QVERIFY(controller.status().contains(QStringLiteral("Не удалось запустить helper")));

        qputenv("QZAPRET_HELPER", helper.toUtf8());
        controller.readList(QStringLiteral("zapret-hosts-user.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.loaded(), 5000);
        controller.setContent(QStringLiteral("changed.example\n"));
        QVERIFY(controller.dirty());

        qputenv("QZAPRET_HELPER", missingHelper.toUtf8());
        controller.saveList();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.dirty());
        QVERIFY(controller.status().contains(QStringLiteral("Не удалось запустить helper")));

        qunsetenv("QZAPRET_HELPER");
        qunsetenv("QZAPRET_NO_PKEXEC");
    }

};

QTEST_GUILESS_MAIN(TestListsController)
#include "test_listscontroller.moc"

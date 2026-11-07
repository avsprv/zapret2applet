#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#include "autostartcontroller.h"

class TestAutostartController final : public QObject
{
    Q_OBJECT
private slots:
    void createsAndRemovesManagedEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AutostartController controller(nullptr, directory.path(), QStringLiteral("/usr/bin/zapret2applet"));
        QVERIFY(!controller.enabled());
        controller.setEnabled(true);
        QVERIFY2(controller.errorMessage().isEmpty(), qPrintable(controller.errorMessage()));
        QVERIFY(controller.enabled());
        QFile file(directory.filePath(QStringLiteral("zapret2applet.desktop")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        QVERIFY(content.contains("X-Zapret2Applet-Managed=true"));
        QVERIFY(content.contains("Exec=\"/usr/bin/zapret2applet\""));
        controller.setEnabled(false);
        QVERIFY2(controller.errorMessage().isEmpty(), qPrintable(controller.errorMessage()));
        QVERIFY(!controller.enabled());
        QVERIFY(!file.exists());
    }

    void handlesDesktopExecSpecialCharacters()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString executable = QString::fromLatin1(R"PATH(/opt/My App/$bin/100%/tool\part"quoted")PATH");
        AutostartController controller(nullptr, directory.path(), executable);
        controller.setEnabled(true);
        QVERIFY2(controller.errorMessage().isEmpty(), qPrintable(controller.errorMessage()));
        QVERIFY(controller.enabled());
        controller.setEnabled(false);
        QVERIFY2(controller.errorMessage().isEmpty(), qPrintable(controller.errorMessage()));
        QVERIFY(!controller.enabled());
    }

    void refusesDuplicateManagedKeys()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("zapret2applet.desktop")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nType=Application\n"
                   "Exec=\"/usr/bin/zapret2applet\"\nX-Zapret2Applet-Managed=true\n");
        file.close();
        AutostartController controller(nullptr, directory.path(), QStringLiteral("/usr/bin/zapret2applet"));
        QVERIFY(!controller.enabled());
        controller.setEnabled(false);
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(file.exists());
    }

    void refusesToOverwriteUnmanagedEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("zapret2applet.desktop"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("[Desktop Entry]\nType=Application\nName=User entry\nExec=user-command\n");
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        AutostartController controller(nullptr, directory.path(), QStringLiteral("/usr/bin/zapret2applet"));
        controller.setEnabled(true);
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void refusesToRemoveUnmanagedEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("zapret2applet.desktop"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("[Desktop Entry]\nType=Application\nName=User entry\nExec=user-command\n");
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        AutostartController controller(nullptr, directory.path(), QStringLiteral("/usr/bin/zapret2applet"));
        controller.setEnabled(false);
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(file.exists());
    }

    void refusesSymlink()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath(QStringLiteral("user.desktop"));
        QFile targetFile(target);
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.write("[Desktop Entry]\nType=Application\n");
        targetFile.close();
        const QString link = directory.filePath(QStringLiteral("zapret2applet.desktop"));
        if (!QFile::link(target, link))
            QSKIP("Symlinks are unavailable on this platform");
        AutostartController controller(nullptr, directory.path(), QStringLiteral("/usr/bin/zapret2applet"));
        controller.setEnabled(false);
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(targetFile.exists());
    }
};
QTEST_GUILESS_MAIN(TestAutostartController)
#include "test_autostartcontroller.moc"

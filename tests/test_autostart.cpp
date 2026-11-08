#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardPaths>
#include <QtTest>

#include "autostart.h"

class TestAutostart final : public QObject
{
    Q_OBJECT

private slots:
    void migratesExactLegacyEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString legacyPath = directory.filePath(QStringLiteral("zapret2applet.desktop"));
        QFile legacy(legacyPath);
        QVERIFY(legacy.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray oldEntry(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=zapret2applet\n"
            "Comment=Unofficial Qt6 GUI for zapret2\n"
            "Exec=\"/usr/bin/zapret2applet\" --minimized\n"
            "Icon=zapret2applet\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n");
        QCOMPARE(legacy.write(oldEntry), qint64(oldEntry.size()));
        legacy.close();

        QVERIFY(Autostart::migrateLegacyEntry(directory.path(), QStringLiteral("/usr/bin/zapret2control")));
        const QString currentPath = directory.filePath(QStringLiteral("zapret2control.desktop"));
        QFile current(currentPath);
        QVERIFY(current.open(QIODevice::ReadOnly));
        const QByteArray migrated = current.readAll();
        QVERIFY(migrated.contains("Name=Zapret2 Control"));
        QVERIFY(migrated.contains("Exec=\"/usr/bin/zapret2control\" --minimized"));
        QVERIFY(migrated.contains("Icon=zapret2control"));
        QVERIFY(!QFile::exists(legacyPath));
    }

    void preservesModifiedLegacyEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString legacyPath = directory.filePath(QStringLiteral("zapret2applet.desktop"));
        QFile legacy(legacyPath);
        QVERIFY(legacy.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray original("[Desktop Entry]\nName=User modified entry\n");
        QCOMPARE(legacy.write(original), qint64(original.size()));
        legacy.close();

        QVERIFY(Autostart::migrateLegacyEntry(directory.path(), QStringLiteral("/usr/bin/zapret2control")));
        QVERIFY(QFile::exists(legacyPath));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("zapret2control.desktop"))));
    }

    void directoryNamedDesktopEntryIsNotEnabled()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const bool hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", directory.path().toLocal8Bit());

        const QString autostartDirectory = directory.filePath(QStringLiteral("autostart"));
        QVERIFY(QDir().mkpath(autostartDirectory));
        QVERIFY(QDir().mkdir(QDir(autostartDirectory).filePath(QStringLiteral("zapret2control.desktop"))));

        QVERIFY(!Autostart::isEnabled());

        if (hadConfigHome)
            qputenv("XDG_CONFIG_HOME", previousConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");
    }

    void doesNotOverwriteExistingNewEntry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile legacy(directory.filePath(QStringLiteral("zapret2applet.desktop")));
        QVERIFY(legacy.open(QIODevice::WriteOnly | QIODevice::Text));
        legacy.write("[Desktop Entry]\n");
        legacy.close();
        const QString currentPath = directory.filePath(QStringLiteral("zapret2control.desktop"));
        QFile current(currentPath);
        QVERIFY(current.open(QIODevice::WriteOnly));
        const QByteArray original("user-owned new entry");
        current.write(original);
        current.close();

        QVERIFY(Autostart::migrateLegacyEntry(directory.path(), QStringLiteral("/usr/bin/zapret2control")));
        QVERIFY(legacy.exists());
        QVERIFY(current.open(QIODevice::ReadOnly));
        QCOMPARE(current.readAll(), original);
    }
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "test_autostart.moc"

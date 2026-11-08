#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "settingsmigration.h"

class TestSettingsMigration final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configDir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_configDir.isValid());
        // A dedicated organization keeps the test profile inside QTemporaryDir.
        // Production leaves organizationName empty; both identities use the same org here.
        QCoreApplication::setOrganizationName(QStringLiteral("zapret2control-migration-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("zapret2control"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_configDir.path());
    }

    void migratesLegacyValuesAndKeepsSource()
    {
        QSettings current;
        current.clear();

        const QString currentApplicationName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("zapret2applet"));
        QSettings legacy;
        QCoreApplication::setApplicationName(currentApplicationName);
        legacy.clear();
        legacy.setValue(QStringLiteral("appearance/theme"), QStringLiteral("dark"));
        legacy.setValue(QStringLiteral("appearance/language"), QStringLiteral("ru"));
        legacy.setValue(QStringLiteral("update/autoCheck"), false);
        legacy.setValue(QStringLiteral("tray/closeToTray"), false);
        legacy.sync();
        QCOMPARE(legacy.status(), QSettings::NoError);

        QVERIFY(SettingsMigration::migrateLegacyProfile());
        current.sync();
        QCOMPARE(current.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
        QCOMPARE(current.value(QStringLiteral("appearance/language")).toString(), QStringLiteral("ru"));
        QVERIFY(!current.value(QStringLiteral("update/autoCheck")).toBool());
        QVERIFY(!current.value(QStringLiteral("tray/closeToTray")).toBool());
        QVERIFY(current.value(QStringLiteral("migration/zapret2control-v1")).toBool());

        legacy.sync();
        QCOMPARE(legacy.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    }

    void migrationIsIdempotent()
    {
        QSettings current;
        current.clear();
        current.setValue(QStringLiteral("appearance/theme"), QStringLiteral("light"));
        current.setValue(QStringLiteral("migration/zapret2control-v1"), true);
        current.sync();

        const QString currentApplicationName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("zapret2applet"));
        QSettings legacy;
        QCoreApplication::setApplicationName(currentApplicationName);
        legacy.setValue(QStringLiteral("appearance/theme"), QStringLiteral("dark"));
        legacy.sync();

        QVERIFY(SettingsMigration::migrateLegacyProfile());
        QCOMPARE(current.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("light"));
    }

    void existingNewProfileTakesPrecedence()
    {
        QSettings current;
        current.clear();
        current.setValue(QStringLiteral("appearance/theme"), QStringLiteral("light"));
        current.sync();

        const QString currentApplicationName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("zapret2applet"));
        QSettings legacy;
        QCoreApplication::setApplicationName(currentApplicationName);
        legacy.setValue(QStringLiteral("appearance/theme"), QStringLiteral("dark"));
        legacy.setValue(QStringLiteral("appearance/language"), QStringLiteral("ru"));
        legacy.sync();

        QVERIFY(SettingsMigration::migrateLegacyProfile());
        current.sync();
        QCOMPARE(current.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("light"));
        QCOMPARE(current.value(QStringLiteral("appearance/language")).toString(), QStringLiteral("ru"));
        QVERIFY(current.value(QStringLiteral("migration/zapret2control-v1")).toBool());
    }
};

QTEST_GUILESS_MAIN(TestSettingsMigration)
#include "test_settingsmigration.moc"

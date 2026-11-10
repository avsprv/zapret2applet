#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "settingsmanager.h"

class TestSettingsManager : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configDir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_configDir.isValid());
        QCoreApplication::setApplicationName(QStringLiteral("zapret2control"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_configDir.path());
    }

    void defaultsAndRoundTrip()
    {
        QSettings().clear();
        SettingsManager settings;
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.language(), QStringLiteral("system"));
        QVERIFY(settings.autoCheckUpdates());
        QVERIFY(settings.closeToTray());

        settings.setTheme(QStringLiteral("dark"));
        settings.setLanguage(QStringLiteral("en"));
        settings.setAutoCheckUpdates(false);
        settings.setCloseToTray(false);

        QCOMPARE(settings.theme(), QStringLiteral("dark"));
        QCOMPARE(settings.language(), QStringLiteral("en"));
        QVERIFY(!settings.autoCheckUpdates());
        QVERIFY(!settings.closeToTray());
        QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
        QCOMPARE(QSettings().value(QStringLiteral("appearance/language")).toString(), QStringLiteral("en"));
        QVERIFY(!QSettings().value(QStringLiteral("update/autoCheck")).toBool());
        QVERIFY(!QSettings().value(QStringLiteral("tray/closeToTray")).toBool());
    }

    void themeChangesEmitSignal()
    {
        QSettings().clear();
        SettingsManager settings;
        QSignalSpy changed(&settings, &SettingsManager::themeChanged);

        settings.setTheme(QStringLiteral("dark"));
        QCOMPARE(settings.theme(), QStringLiteral("dark"));
        QCOMPARE(changed.size(), 1);

        settings.setTheme(QStringLiteral("dark"));
        QCOMPARE(changed.size(), 1);

        settings.setTheme(QStringLiteral("system"));
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(changed.size(), 2);
    }

    void invalidValuesFallBackToSystem()
    {
        SettingsManager settings;
        settings.setTheme(QStringLiteral("unexpected"));
        settings.setLanguage(QStringLiteral("unexpected"));
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.language(), QStringLiteral("system"));
    }
};

QTEST_GUILESS_MAIN(TestSettingsManager)
#include "test_settingsmanager.moc"

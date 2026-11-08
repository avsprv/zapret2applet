#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "appsettings.h"

class TestAppSettings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configDir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_configDir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("zapret2control-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("settings"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_configDir.path());
    }

    void defaultsPreserveExistingBehavior()
    {
        QSettings().clear();
        QCOMPARE(AppSettings::theme(), AppSettings::Theme::System);
        QCOMPARE(AppSettings::language(), AppSettings::Language::System);
        QVERIFY(AppSettings::autoCheckUpdates());
        QVERIFY(AppSettings::closeToTray());
    }

    void appearancePreferencesRoundTrip()
    {
        AppSettings::setTheme(AppSettings::Theme::Dark);
        AppSettings::setLanguage(AppSettings::Language::English);
        QCOMPARE(AppSettings::theme(), AppSettings::Theme::Dark);
        QCOMPARE(AppSettings::language(), AppSettings::Language::English);

        AppSettings::setTheme(AppSettings::Theme::Light);
        AppSettings::setLanguage(AppSettings::Language::Russian);
        QCOMPARE(AppSettings::theme(), AppSettings::Theme::Light);
        QCOMPARE(AppSettings::language(), AppSettings::Language::Russian);

        AppSettings::setTheme(AppSettings::Theme::System);
        AppSettings::setLanguage(AppSettings::Language::System);
        QCOMPARE(AppSettings::theme(), AppSettings::Theme::System);
        QCOMPARE(AppSettings::language(), AppSettings::Language::System);
    }

    void updateCheckKeepsLegacyKey()
    {
        QSettings().setValue(QStringLiteral("update/autoCheck"), false);
        QVERIFY(!AppSettings::autoCheckUpdates());
        AppSettings::setAutoCheckUpdates(true);
        QVERIFY(QSettings().value(QStringLiteral("update/autoCheck")).toBool());
    }

    void closeToTrayCanBeChanged()
    {
        AppSettings::setCloseToTray(false);
        QVERIFY(!AppSettings::closeToTray());
        AppSettings::setCloseToTray(true);
        QVERIFY(AppSettings::closeToTray());
    }
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "test_appsettings.moc"

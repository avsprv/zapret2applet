#include <QtTest>

#include "dbusmenu.h"

class TestDBusMenu final : public QObject
{
    Q_OBJECT

private slots:
    void rootLayoutUsesStableIds()
    {
        DBusMenu menu;
        uint revision = 0;
        DBusMenuLayoutItem layout;

        menu.GetLayout(0, -1, {}, revision, layout);

        QCOMPARE(revision, 1u);
        QCOMPARE(layout.id, 0);
        QCOMPARE(layout.children.size(), 7);
        const DBusMenuLayoutItem startItem =
            layout.children.at(2).variant().value<DBusMenuLayoutItem>();
        const DBusMenuLayoutItem windowItem =
            layout.children.at(0).variant().value<DBusMenuLayoutItem>();
        QCOMPARE(windowItem.id, 1);
        QCOMPARE(windowItem.properties.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Показать / скрыть окно"));
        QCOMPARE(startItem.id, 3);
        QCOMPARE(startItem.properties.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Запустить zapret2"));
        QVERIFY(startItem.properties.value(QStringLiteral("enabled")).toBool());
    }

    void layoutFiltersProperties()
    {
        DBusMenu menu;
        uint revision = 0;
        DBusMenuLayoutItem layout;

        menu.GetLayout(0, 1, {QStringLiteral("label")}, revision, layout);

        QCOMPARE(layout.children.size(), 7);
        const DBusMenuLayoutItem statusItem =
            layout.children.at(1).variant().value<DBusMenuLayoutItem>();
        QCOMPARE(statusItem.properties.size(), 1);
        QVERIFY(statusItem.properties.contains(QStringLiteral("label")));
    }

    void groupPropertiesReturnsRequestedItems()
    {
        DBusMenu menu;
        DBusMenuItemPropertiesList properties;

        menu.GetGroupProperties({3, 4}, {}, properties);

        QCOMPARE(properties.size(), 2);
        QCOMPARE(properties.at(0).id, 3);
        QCOMPARE(properties.at(1).id, 4);
    }

    void serviceStateUpdatesRevisionAndActionAvailability()
    {
        DBusMenu menu;
        QSignalSpy layoutSpy(&menu, &DBusMenu::LayoutUpdated);
        menu.setServiceState(QStringLiteral("Работает"), true, false);

        QCOMPARE(layoutSpy.size(), 1);
        QCOMPARE(layoutSpy.constFirst().at(0).toUInt(), 2u);

        DBusMenuItemPropertiesList properties;
        menu.GetGroupProperties({3, 4}, {}, properties);
        QVERIFY(!properties.at(0).properties.value(QStringLiteral("enabled")).toBool());
        QVERIFY(properties.at(1).properties.value(QStringLiteral("enabled")).toBool());

        menu.setServiceState(QStringLiteral("Работает"), true, true);
        menu.GetGroupProperties({3, 4}, {}, properties);
        QVERIFY(!properties.at(0).properties.value(QStringLiteral("enabled")).toBool());
        QVERIFY(!properties.at(1).properties.value(QStringLiteral("enabled")).toBool());
    }

    void eventsOnlyDispatchEnabledActions()
    {
        DBusMenu menu;
        QSignalSpy activateSpy(&menu, &DBusMenu::activateRequested);
        QSignalSpy startSpy(&menu, &DBusMenu::startRequested);
        QSignalSpy stopSpy(&menu, &DBusMenu::stopRequested);

        menu.Event(1, QStringLiteral("clicked"), QDBusVariant(), 0);
        QCOMPARE(activateSpy.size(), 1);

        menu.Event(3, QStringLiteral("clicked"), QDBusVariant(), 0);
        QCOMPARE(startSpy.size(), 1);

        menu.setServiceState(QStringLiteral("Работает"), true, false);
        menu.Event(3, QStringLiteral("clicked"), QDBusVariant(), 0);
        QCOMPARE(startSpy.size(), 1);

        menu.Event(4, QStringLiteral("clicked"), QDBusVariant(), 0);
        QCOMPARE(stopSpy.size(), 1);
    }

    void getPropertyReturnsVariant()
    {
        DBusMenu menu;
        QDBusVariant value;

        menu.GetProperty(2, QStringLiteral("enabled"), value);

        QVERIFY(!value.variant().toBool());
    }
};

QTEST_GUILESS_MAIN(TestDBusMenu)
#include "test_dbusmenu.moc"

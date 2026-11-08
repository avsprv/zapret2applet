#include <QtTest>

#include "dbusmenu.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QSignalSpy>

class TestDBusMenuOverSessionBus final : public QObject
{
    Q_OBJECT

private:
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    DBusMenu *m_menu = nullptr;
    QString m_serviceName;

private slots:
    void initTestCase()
    {
        QVERIFY2(m_bus.isConnected(), "A private session bus is required (run with dbus-run-session)");

        m_serviceName = QStringLiteral("org.avsprv.zapret2applet.Test_%1")
                            .arg(QCoreApplication::applicationPid());
        QVERIFY2(m_bus.registerService(m_serviceName), qPrintable(m_bus.lastError().message()));

        m_menu = new DBusMenu(this);
        const auto flags = QDBusConnection::ExportAllSlots
                           | QDBusConnection::ExportAllSignals
                           | QDBusConnection::ExportAllProperties;
        QVERIFY2(m_bus.registerObject(QStringLiteral("/Menu"), m_menu, flags),
                 qPrintable(m_bus.lastError().message()));
    }

    void cleanupTestCase()
    {
        if (m_bus.isConnected()) {
            m_bus.unregisterObject(QStringLiteral("/Menu"));
            m_bus.unregisterService(m_serviceName);
        }
    }

    void introspectionExposesCanonicalInterface()
    {
        const QDBusMessage request = QDBusMessage::createMethodCall(
            m_serviceName, QStringLiteral("/Menu"),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
        const QDBusMessage reply = m_bus.call(request);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QVERIFY(!reply.arguments().isEmpty());

        const QString xml = reply.arguments().constFirst().toString();
        QVERIFY(xml.contains(QStringLiteral("com.canonical.dbusmenu")));
        QVERIFY(xml.contains(QStringLiteral("GetLayout")));
        QVERIFY(xml.contains(QStringLiteral("GetGroupProperties")));
        QVERIFY(xml.contains(QStringLiteral("GetProperty")));
        QVERIFY(xml.contains(QStringLiteral("AboutToShowGroup")));
        QVERIFY(xml.contains(QStringLiteral("LayoutUpdated")));
    }

    void getLayoutRoundTripsNestedVariants()
    {
        QDBusInterface interface(m_serviceName, QStringLiteral("/Menu"),
                                 QStringLiteral("com.canonical.dbusmenu"), m_bus);
        QVERIFY(interface.isValid());

        const QDBusMessage reply = interface.callWithArgumentList(
            QDBus::Block, QStringLiteral("GetLayout"),
            QVariantList{QVariant::fromValue(0), QVariant::fromValue(-1),
                         QVariant::fromValue(QStringList{})});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().size(), 2);

        const uint revision = reply.arguments().at(0).toUInt();
        QCOMPARE(revision, 1u);

        const DBusMenuLayoutItem layout =
            reply.arguments().at(1).value<DBusMenuLayoutItem>();
        QCOMPARE(layout.id, 0);
        QCOMPARE(layout.children.size(), 7);

        const DBusMenuLayoutItem startItem =
            layout.children.at(2).variant().value<DBusMenuLayoutItem>();
        QCOMPARE(startItem.id, 3);
        QCOMPARE(startItem.properties.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Запустить zapret2"));
        QVERIFY(startItem.properties.value(QStringLiteral("enabled")).toBool());
    }

    void groupPropertiesAndGetPropertyRoundTrip()
    {
        QDBusInterface interface(m_serviceName, QStringLiteral("/Menu"),
                                 QStringLiteral("com.canonical.dbusmenu"), m_bus);
        QVERIFY(interface.isValid());

        const QDBusMessage groupReply = interface.callWithArgumentList(
            QDBus::Block, QStringLiteral("GetGroupProperties"),
            QVariantList{QVariant::fromValue(QList<int>{3, 4}),
                         QVariant::fromValue(QStringList{})});
        QCOMPARE(groupReply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(groupReply.arguments().size(), 1);
        const DBusMenuItemPropertiesList properties =
            groupReply.arguments().at(0).value<DBusMenuItemPropertiesList>();
        QCOMPARE(properties.size(), 2);
        QCOMPARE(properties.at(0).id, 3);
        QCOMPARE(properties.at(1).id, 4);

        const QDBusMessage propertyReply = interface.callWithArgumentList(
            QDBus::Block, QStringLiteral("GetProperty"),
            QVariantList{QVariant::fromValue(2), QVariant::fromValue(QStringLiteral("enabled"))});
        QCOMPARE(propertyReply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(propertyReply.arguments().size(), 1);
        const QDBusVariant enabled = propertyReply.arguments().constFirst().value<QDBusVariant>();
        QVERIFY(!enabled.variant().toBool());
    }

    void eventsAreDispatchedOnlyForEnabledActions()
    {
        QSignalSpy startSpy(m_menu, &DBusMenu::startRequested);
        QVERIFY(startSpy.isValid());

        QDBusInterface interface(m_serviceName, QStringLiteral("/Menu"),
                                 QStringLiteral("com.canonical.dbusmenu"), m_bus);
        QVERIFY(interface.isValid());

        const auto sendClick = [&interface]() {
            return interface.callWithArgumentList(
                QDBus::Block, QStringLiteral("Event"),
                QVariantList{QVariant::fromValue(3), QVariant::fromValue(QStringLiteral("clicked")),
                             QVariant::fromValue(QDBusVariant(QVariant(false))),
                             QVariant::fromValue(uint{0})});
        };
        QCOMPARE(sendClick().type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(startSpy.size(), 1);

        m_menu->setServiceState(QStringLiteral("Работает"), true, false);
        QCOMPARE(sendClick().type(), QDBusMessage::ReplyMessage);
        QTest::qWait(50);
        QCOMPARE(startSpy.size(), 1);
    }

    void invalidItemReturnsDBusError()
    {
        QDBusInterface interface(m_serviceName, QStringLiteral("/Menu"),
                                 QStringLiteral("com.canonical.dbusmenu"), m_bus);
        QVERIFY(interface.isValid());

        const QDBusMessage reply = interface.callWithArgumentList(
            QDBus::Block, QStringLiteral("GetProperty"),
            QVariantList{QVariant::fromValue(999), QVariant::fromValue(QStringLiteral("enabled"))});
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    }
};

QTEST_GUILESS_MAIN(TestDBusMenuOverSessionBus)
#include "test_dbusmenu_dbus.moc"

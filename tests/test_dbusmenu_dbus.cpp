#include <QtTest>

#include "dbusmenu.h"
#include "traycontroller.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QSignalSpy>


class FakeStatusNotifierWatcher final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.StatusNotifierWatcher")

public:
    QStringList registeredItems;

public slots:
    void RegisterStatusNotifierItem(const QString &service)
    {
        registeredItems.append(service);
    }
};

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

        const QDBusArgument layoutArgument = reply.arguments().at(1).value<QDBusArgument>();
        const DBusMenuLayoutItem layout = qdbus_cast<DBusMenuLayoutItem>(layoutArgument);
        QCOMPARE(layout.id, 0);
        QCOMPARE(layout.children.size(), 7);

        const QDBusArgument startItemArgument =
            layout.children.at(2).variant().value<QDBusArgument>();
        const DBusMenuLayoutItem startItem = qdbus_cast<DBusMenuLayoutItem>(startItemArgument);
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
        const QDBusArgument propertiesArgument =
            groupReply.arguments().at(0).value<QDBusArgument>();
        const DBusMenuItemPropertiesList properties =
            qdbus_cast<DBusMenuItemPropertiesList>(propertiesArgument);
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


    void statusNotifierRegistersAndRecoversAfterWatcherRestart()
    {
        constexpr auto watcherService = "org.freedesktop.StatusNotifierWatcher";
        constexpr auto watcherPath = "/StatusNotifierWatcher";

        QVERIFY(m_bus.objectRegisteredAt(QStringLiteral("/Menu")) == m_menu);
        m_bus.unregisterObject(QStringLiteral("/Menu"));
        QVERIFY(m_bus.objectRegisteredAt(QStringLiteral("/Menu")) == nullptr);

        struct CleanupGuard
        {
            QDBusConnection bus;
            DBusMenu *menu = nullptr;

            ~CleanupGuard()
            {
                bus.unregisterService(QStringLiteral("org.freedesktop.StatusNotifierWatcher"));
                bus.unregisterObject(QStringLiteral("/StatusNotifierWatcher"));
                if (bus.objectRegisteredAt(QStringLiteral("/Menu")) == nullptr) {
                    const auto flags = QDBusConnection::ExportAllSlots
                                       | QDBusConnection::ExportAllSignals
                                       | QDBusConnection::ExportAllProperties;
                    bus.registerObject(QStringLiteral("/Menu"), menu, flags);
                }
            }
        } cleanup{m_bus, m_menu};

        QString itemService;
        {
            TrayController controller;
            QVERIFY(!controller.watcherAvailable());
            QVERIFY(!controller.registered());

            FakeStatusNotifierWatcher watcher;
            QVERIFY2(m_bus.registerObject(QString::fromLatin1(watcherPath), &watcher,
                                          QDBusConnection::ExportAllSlots),
                     qPrintable(m_bus.lastError().message()));
            QVERIFY2(m_bus.registerService(QString::fromLatin1(watcherService)),
                     qPrintable(m_bus.lastError().message()));

            QTRY_VERIFY(controller.watcherAvailable());
            QTRY_VERIFY(controller.registered());
            QCOMPARE(watcher.registeredItems.size(), 1);
            itemService = watcher.registeredItems.constFirst();
            QVERIFY(itemService.startsWith(QStringLiteral("org.freedesktop.StatusNotifierItem-")));

            const auto introspect = [this, &itemService](const QString &path) {
                const QDBusMessage request = QDBusMessage::createMethodCall(
                    itemService, path, QStringLiteral("org.freedesktop.DBus.Introspectable"),
                    QStringLiteral("Introspect"));
                return m_bus.call(request);
            };
            const QDBusMessage itemReply = introspect(QStringLiteral("/StatusNotifierItem"));
            QCOMPARE(itemReply.type(), QDBusMessage::ReplyMessage);
            const QString itemXml = itemReply.arguments().constFirst().toString();
            QVERIFY(itemXml.contains(QStringLiteral("org.freedesktop.StatusNotifierItem")));
            QVERIFY(itemXml.contains(QStringLiteral("Activate")));
            QVERIFY(itemXml.contains(QStringLiteral("SecondaryActivate")));
            QVERIFY(itemXml.contains(QStringLiteral("ContextMenu")));
            QVERIFY(itemXml.contains(QStringLiteral("Scroll")));
            QVERIFY(itemXml.contains(QStringLiteral("IconPixmap")));
            QVERIFY(itemXml.contains(QStringLiteral("ToolTip")));
            QVERIFY(itemXml.contains(QStringLiteral("NewStatus")));
            QVERIFY(itemXml.contains(QStringLiteral("type=\"a(iiay)\"")));
            QVERIFY(itemXml.contains(QStringLiteral("type=\"(sa(iiay)ss)\"")));
            QVERIFY(itemXml.contains(QStringLiteral("NewOverlayIcon")));
            QVERIFY(itemXml.contains(QStringLiteral("NewAttentionIcon")));
            QVERIFY(itemXml.contains(QStringLiteral("NewToolTip")));

            QDBusInterface item(itemService, QStringLiteral("/StatusNotifierItem"),
                                QStringLiteral("org.freedesktop.StatusNotifierItem"), m_bus);
            QVERIFY(item.isValid());
            QCOMPARE(item.property("Category").toString(), QStringLiteral("ApplicationStatus"));
            QCOMPARE(item.property("Id").toString(), QStringLiteral("zapret2applet"));
            QTRY_COMPARE(item.property("Status").toString(), QStringLiteral("Active"));
            controller.setServiceState(QStringLiteral("Не удалось запустить"), QStringLiteral("failed"),
                                       false, false);
            QTRY_COMPARE(item.property("Status").toString(), QStringLiteral("NeedsAttention"));
            controller.setServiceState(QStringLiteral("Остановлен"), QStringLiteral("inactive"),
                                       false, false);
            QTRY_COMPARE(item.property("Status").toString(), QStringLiteral("NeedsAttention"));
            controller.setServiceState(QStringLiteral("Работает"), QStringLiteral("active"),
                                       true, false);
            QTRY_COMPARE(item.property("Status").toString(), QStringLiteral("Active"));

            const QDBusMessage menuReply = introspect(QStringLiteral("/Menu"));
            QCOMPARE(menuReply.type(), QDBusMessage::ReplyMessage);
            QVERIFY(menuReply.arguments().constFirst().toString().contains(
                QStringLiteral("com.canonical.dbusmenu")));

            QVERIFY(m_bus.unregisterService(QString::fromLatin1(watcherService)));
            QTRY_VERIFY(!controller.watcherAvailable());
            QTRY_VERIFY(!controller.registered());

            QVERIFY2(m_bus.registerService(QString::fromLatin1(watcherService)),
                     qPrintable(m_bus.lastError().message()));
            QTRY_COMPARE(watcher.registeredItems.size(), 2);
            QCOMPARE(watcher.registeredItems.constLast(), itemService);
            QTRY_VERIFY(controller.registered());

            m_bus.unregisterService(QString::fromLatin1(watcherService));
            m_bus.unregisterObject(QString::fromLatin1(watcherPath));
        }

        QVERIFY(m_bus.objectRegisteredAt(QStringLiteral("/StatusNotifierItem")) == nullptr);
        QVERIFY(m_bus.objectRegisteredAt(QStringLiteral("/Menu")) == nullptr);
        const QDBusReply<bool> serviceRegistered =
            m_bus.interface()->isServiceRegistered(itemService);
        QVERIFY2(serviceRegistered.isValid(), qPrintable(serviceRegistered.error().message()));
        QVERIFY(!serviceRegistered.value());

        const auto flags = QDBusConnection::ExportAllSlots
                           | QDBusConnection::ExportAllSignals
                           | QDBusConnection::ExportAllProperties;
        QVERIFY2(m_bus.registerObject(QStringLiteral("/Menu"), m_menu, flags),
                 qPrintable(m_bus.lastError().message()));
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

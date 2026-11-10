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

class FakeNotifications final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    QString appName;
    uint replacesId = 0;
    QString appIcon;
    QString summary;
    QString body;
    QStringList actions;
    QVariantMap hints;
    int timeout = 0;
    int calls = 0;

public slots:
    uint Notify(const QString &name, uint replaces, const QString &icon,
                const QString &notificationSummary, const QString &notificationBody,
                const QStringList &notificationActions, const QVariantMap &notificationHints,
                int expireTimeout)
    {
        appName = name;
        replacesId = replaces;
        appIcon = icon;
        summary = notificationSummary;
        body = notificationBody;
        actions = notificationActions;
        hints = notificationHints;
        timeout = expireTimeout;
        ++calls;
        return 42;
    }
};

class StatusSignalReceiver final : public QObject
{
    Q_OBJECT

public:
    QStringList statuses;

public slots:
    void statusChanged(const QString &status)
    {
        statuses.append(status);
    }
};

class PropertiesChangedReceiver final : public QObject
{
    Q_OBJECT

public:
    QList<QVariantMap> changes;

public slots:
    void propertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties,
                           const QStringList &)
    {
        if (interfaceName == QLatin1String("org.freedesktop.StatusNotifierItem"))
            changes.append(changedProperties);
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

        m_serviceName = QStringLiteral("org.avsprv.zapret2control.Test_%1")
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

    void desktopNotificationsUseFreedesktopService()
    {
        constexpr auto notificationService = "org.freedesktop.Notifications";
        FakeNotifications notifications;
        QVERIFY2(m_bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                                      &notifications, QDBusConnection::ExportAllSlots),
                 qPrintable(m_bus.lastError().message()));
        QVERIFY2(m_bus.registerService(QString::fromLatin1(notificationService)),
                 qPrintable(m_bus.lastError().message()));

        {
            TrayController controller;
            controller.showNotification(QStringLiteral("Zapret2 Control"),
                                        QStringLiteral("Strategy scan completed: 3"));
            QTRY_COMPARE(notifications.calls, 1);
            QCOMPARE(notifications.appName, QCoreApplication::applicationName());
            QCOMPARE(notifications.replacesId, 0u);
            QCOMPARE(notifications.appIcon, QStringLiteral("zapret2control"));
            QCOMPARE(notifications.summary, QStringLiteral("Zapret2 Control"));
            QCOMPARE(notifications.body, QStringLiteral("Strategy scan completed: 3"));
            QVERIFY(notifications.actions.isEmpty());
            QVERIFY(notifications.hints.isEmpty());
            QCOMPARE(notifications.timeout, -1);
        }

        QVERIFY(m_bus.unregisterService(QString::fromLatin1(notificationService)));
        m_bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));

        // Missing notification daemons must be a non-fatal condition.
        TrayController controller;
        controller.showNotification(QStringLiteral("Zapret2 Control"),
                                    QStringLiteral("No notification server"));
        QTest::qWait(50);
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
            QCOMPARE(item.property("Id").toString(), QStringLiteral("zapret2control"));

            // Read the property through the D-Bus Properties interface. QDBusInterface's
            // property cache is signal-driven and is not a reliable source for a fresh
            // synchronous read of a remote property.
            const auto readStatus = [this, &itemService]() {
                QDBusMessage request = QDBusMessage::createMethodCall(
                    itemService, QStringLiteral("/StatusNotifierItem"),
                    QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
                request << QStringLiteral("org.freedesktop.StatusNotifierItem")
                        << QStringLiteral("Status");
                const QDBusReply<QDBusVariant> reply = m_bus.call(request);
                return reply.isValid() ? reply.value().variant().toString() : QString{};
            };
            const auto readAllStatus = [this, &itemService]() {
                QDBusMessage request = QDBusMessage::createMethodCall(
                    itemService, QStringLiteral("/StatusNotifierItem"),
                    QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
                request << QStringLiteral("org.freedesktop.StatusNotifierItem");
                const QDBusReply<QVariantMap> reply = m_bus.call(request);
                return reply.isValid()
                    ? reply.value().value(QStringLiteral("Status")).toString()
                    : QString{};
            };
            StatusSignalReceiver statusSignals;
            QVERIFY(m_bus.connect(itemService, QStringLiteral("/StatusNotifierItem"),
                                  QStringLiteral("org.freedesktop.StatusNotifierItem"),
                                  QStringLiteral("NewStatus"), &statusSignals,
                                  SLOT(statusChanged(QString))));
            PropertiesChangedReceiver propertyChanges;
            QVERIFY(m_bus.connect(itemService, QStringLiteral("/StatusNotifierItem"),
                                  QStringLiteral("org.freedesktop.DBus.Properties"),
                                  QStringLiteral("PropertiesChanged"), &propertyChanges,
                                  SLOT(propertiesChanged(QString,QVariantMap,QStringList))));

            QTRY_COMPARE(readStatus(), QStringLiteral("Active"));
            controller.setServiceState(QStringLiteral("Не удалось запустить"), QStringLiteral("failed"),
                                       false, false);
            // Check the protocol notification independently before reading the property.
            QTRY_COMPARE(statusSignals.statuses.size(), 1);
            QCOMPARE(statusSignals.statuses.constLast(), QStringLiteral("NeedsAttention"));
            QTRY_VERIFY(!propertyChanges.changes.isEmpty());
            QVERIFY(propertyChanges.changes.constLast().contains(QStringLiteral("Status")));
            QCOMPARE(propertyChanges.changes.constLast().value(QStringLiteral("Status")).toString(),
                     QStringLiteral("NeedsAttention"));
            QTRY_COMPARE(readStatus(), QStringLiteral("NeedsAttention"));
            QTRY_COMPARE(readAllStatus(), QStringLiteral("NeedsAttention"));

            controller.setServiceState(QStringLiteral("Остановлен"), QStringLiteral("inactive"),
                                       false, false);
            QTRY_COMPARE(readStatus(), QStringLiteral("NeedsAttention"));
            QCOMPARE(statusSignals.statuses.size(), 1);

            controller.setServiceState(QStringLiteral("Работает"), QStringLiteral("active"),
                                       true, false);
            QTRY_COMPARE(readStatus(), QStringLiteral("Active"));
            QTRY_COMPARE(readAllStatus(), QStringLiteral("Active"));
            QTRY_COMPARE(statusSignals.statuses.size(), 2);
            QCOMPARE(statusSignals.statuses.constLast(), QStringLiteral("Active"));
            QTRY_COMPARE(propertyChanges.changes.size(), 2);
            QCOMPARE(propertyChanges.changes.constLast().value(QStringLiteral("Status")).toString(),
                     QStringLiteral("Active"));
            m_bus.disconnect(itemService, QStringLiteral("/StatusNotifierItem"),
                             QStringLiteral("org.freedesktop.StatusNotifierItem"),
                             QStringLiteral("NewStatus"), &statusSignals,
                             SLOT(statusChanged(QString)));
            m_bus.disconnect(itemService, QStringLiteral("/StatusNotifierItem"),
                             QStringLiteral("org.freedesktop.DBus.Properties"),
                             QStringLiteral("PropertiesChanged"), &propertyChanges,
                             SLOT(propertiesChanged(QString,QVariantMap,QStringList)));

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

QTEST_MAIN(TestDBusMenuOverSessionBus)
#include "test_dbusmenu_dbus.moc"

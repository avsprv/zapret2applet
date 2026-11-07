#include "traycontroller.h"
#include "dbusmenu.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusReply>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QCoreApplication>
#include <QDebug>

namespace {
constexpr auto watcherService = "org.freedesktop.StatusNotifierWatcher";
constexpr auto watcherPath = "/StatusNotifierWatcher";
constexpr auto watcherInterface = "org.freedesktop.StatusNotifierWatcher";
constexpr auto itemPath = "/StatusNotifierItem";
constexpr auto menuPath = "/Menu";
}

struct TrayImage
{
    int width = 0;
    int height = 0;
    QByteArray data;
};

QDBusArgument &operator<<(QDBusArgument &argument, const TrayImage &image)
{
    argument.beginStructure();
    argument << image.width << image.height << image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayImage &image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.data;
    argument.endStructure();
    return argument;
}

using TrayImageList = QList<TrayImage>;

struct TrayToolTip
{
    QString iconName;
    TrayImageList iconPixmap;
    QString title;
    QString description;
};

QDBusArgument &operator<<(QDBusArgument &argument, const TrayToolTip &toolTip)
{
    argument.beginStructure();
    argument << toolTip.iconName << toolTip.iconPixmap
             << toolTip.title << toolTip.description;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayToolTip &toolTip)
{
    argument.beginStructure();
    argument >> toolTip.iconName >> toolTip.iconPixmap
             >> toolTip.title >> toolTip.description;
    argument.endStructure();
    return argument;
}

Q_DECLARE_METATYPE(TrayImage)
Q_DECLARE_METATYPE(TrayImageList)
Q_DECLARE_METATYPE(TrayToolTip)

class StatusNotifierItem final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(uint WindowId READ windowId)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(TrayImageList IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName)
    Q_PROPERTY(TrayImageList OverlayIconPixmap READ emptyPixmap)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(TrayImageList AttentionIconPixmap READ emptyPixmap)
    Q_PROPERTY(QString AttentionMovieName READ attentionMovieName)
    Q_PROPERTY(TrayToolTip ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

public:
    explicit StatusNotifierItem(QObject *parent = nullptr) : QObject(parent)
    {
        qRegisterMetaType<TrayImage>();
        qRegisterMetaType<TrayImageList>();
        qRegisterMetaType<TrayToolTip>();
        qDBusRegisterMetaType<TrayImage>();
        qDBusRegisterMetaType<TrayImageList>();
        qDBusRegisterMetaType<TrayToolTip>();
    }

    QString category() const { return QStringLiteral("ApplicationStatus"); }
    QString id() const { return QStringLiteral("zapret2applet"); }
    QString title() const { return QCoreApplication::applicationName(); }
    uint windowId() const { return 0; }
    QString status() const { return QStringLiteral("Active"); }
    QString iconName() const { return QStringLiteral("zapret2applet"); }
    TrayImageList iconPixmap() const { return {}; }
    TrayImageList emptyPixmap() const { return {}; }
    QString overlayIconName() const { return {}; }
    QString attentionIconName() const { return QStringLiteral("dialog-warning"); }
    QString attentionMovieName() const { return {}; }
    TrayToolTip toolTip() const
    {
        return {iconName(), {}, title(), QStringLiteral("Управление zapret2")};
    }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath(QStringLiteral("/Menu")); }

public slots:
    void ContextMenu(int, int) {}
    void Activate(int, int) { emit activateRequested(); }
    void SecondaryActivate(int, int) { emit activateRequested(); }
    void Scroll(int, const QString &) {}

signals:
    void NewTitle();
    void NewIcon();
    void NewAttentionIcon();
    void NewOverlayIcon();
    void NewToolTip();
    void NewStatus(const QString &status);
    void activateRequested();
};

TrayController::TrayController(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
    , m_watcher(new QDBusServiceWatcher(QString::fromLatin1(watcherService),
                                        m_bus,
                                        QDBusServiceWatcher::WatchForOwnerChange,
                                        this))
    , m_item(new StatusNotifierItem(this))
    , m_menu(new DBusMenu(this))
    , m_serviceName(QStringLiteral("org.freedesktop.StatusNotifierItem-%1-1")
                        .arg(QCoreApplication::applicationPid()))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, [this](const QString &, const QString &, const QString &newOwner) {
        const bool available = !newOwner.isEmpty();
        if (m_watcherAvailable != available) {
            m_watcherAvailable = available;
            emit watcherAvailableChanged();
        }
        if (available)
            registerWithWatcher();
        else
            setRegistered(false);
    });
    connect(m_item, &StatusNotifierItem::activateRequested,
            this, &TrayController::activateRequested);
    connect(m_menu, &DBusMenu::activateRequested, this, &TrayController::activateRequested);
    connect(m_menu, &DBusMenu::startRequested, this, &TrayController::startRequested);
    connect(m_menu, &DBusMenu::stopRequested, this, &TrayController::stopRequested);
    connect(m_menu, &DBusMenu::settingsRequested, this, &TrayController::settingsRequested);
    connect(m_menu, &DBusMenu::quitRequested, this, &TrayController::quitRequested);

    refreshWatcherAvailability();
}

void TrayController::setServiceState(const QString &state, bool active, bool busy)
{
    m_menu->setServiceState(state, active, busy);
}

bool TrayController::watcherAvailable() const
{
    return m_watcherAvailable;
}

bool TrayController::registered() const
{
    return m_registered;
}

void TrayController::setRegistered(bool registered)
{
    if (m_registered == registered)
        return;
    m_registered = registered;
    emit registeredChanged();
}

void TrayController::refreshWatcherAvailability()
{
    if (!m_bus.isConnected() || !m_bus.interface()) {
        if (m_watcherAvailable) {
            m_watcherAvailable = false;
            emit watcherAvailableChanged();
        }
        setRegistered(false);
        return;
    }

    const QDBusReply<QString> owner =
        m_bus.interface()->serviceOwner(QString::fromLatin1(watcherService));
    const bool available = owner.isValid() && !owner.value().isEmpty();
    if (m_watcherAvailable != available) {
        m_watcherAvailable = available;
        emit watcherAvailableChanged();
    }
    if (available)
        registerWithWatcher();
    else
        setRegistered(false);
}

void TrayController::registerWithWatcher()
{
    if (!m_watcherAvailable || !m_bus.isConnected())
        return;

    if (!m_exported) {
        if (!m_bus.registerService(m_serviceName)) {
            qWarning() << "Unable to own StatusNotifierItem service:"
                       << m_bus.lastError().message();
            return;
        }
        if (!m_bus.registerObject(QString::fromLatin1(itemPath), m_item,
                                  QDBusConnection::ExportAllSlots
                                      | QDBusConnection::ExportAllSignals
                                      | QDBusConnection::ExportAllProperties)) {
            qWarning() << "Unable to export StatusNotifierItem:"
                       << m_bus.lastError().message();
            m_bus.unregisterService(m_serviceName);
            return;
        }
        m_exported = true;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(watcherService),
        QString::fromLatin1(watcherPath),
        QString::fromLatin1(watcherInterface),
        QStringLiteral("RegisterStatusNotifierItem"));
    message << m_serviceName;

    auto *call = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            setRegistered(false);
            qWarning() << "StatusNotifierItem registration failed:"
                       << reply.error().message();
        } else {
            setRegistered(true);
        }
        watcher->deleteLater();
    });
}

#include "traycontroller.moc"

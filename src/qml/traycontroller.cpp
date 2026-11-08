#include "traycontroller.h"
#include "dbusmenu.h"

#include <QDBusConnectionInterface>
#include <QDBusAbstractAdaptor>
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
#include <QEvent>
#include <QColor>
#include <QImage>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>
#include <QPen>

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

namespace {
TrayImageList renderTrayPixmap(int size, const QColor &indicator)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = size / 32.0;
    QColor foreground;
    if (qGuiApp)
        foreground = qGuiApp->palette().color(QPalette::WindowText);
    if (!foreground.isValid())
        foreground = QColor(QStringLiteral("#232629"));
    QPen pen(foreground, 2.5 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QRectF first(2.5 * scale, 4.0 * scale, 10.0 * scale, 8.0 * scale);
    const QRectF second(11.0 * scale, 12.0 * scale, 10.0 * scale, 8.0 * scale);
    const QRectF third(19.5 * scale, 20.0 * scale, 10.0 * scale, 8.0 * scale);
    painter.drawLine(QPointF(first.right(), first.center().y()),
                     QPointF(second.left(), second.center().y()));
    painter.drawLine(QPointF(second.right(), second.center().y()),
                     QPointF(third.left(), third.center().y()));
    painter.drawRoundedRect(first, 2.0 * scale, 2.0 * scale);
    painter.drawRoundedRect(second, 2.0 * scale, 2.0 * scale);
    painter.drawRoundedRect(third, 2.0 * scale, 2.0 * scale);

    if (indicator.isValid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(indicator);
        painter.drawEllipse(QPointF(25.5 * scale, 6.5 * scale), 4.0 * scale, 4.0 * scale);
        painter.setPen(QPen(QColor(QStringLiteral("#232629")), 1.0 * scale));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(25.5 * scale, 6.5 * scale), 4.0 * scale, 4.0 * scale);
    }
    painter.end();

    QByteArray bytes;
    bytes.reserve(size * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const QRgb pixel = image.pixel(x, y);
            bytes.append(static_cast<char>(qAlpha(pixel)));
            bytes.append(static_cast<char>(qRed(pixel)));
            bytes.append(static_cast<char>(qGreen(pixel)));
            bytes.append(static_cast<char>(qBlue(pixel)));
        }
    }
    return {{size, size, bytes}};
}

TrayImageList renderTrayPixmaps(const QColor &indicator)
{
    TrayImageList result;
    for (const int size : {16, 22, 24, 32, 48})
        result.append(renderTrayPixmap(size, indicator).constFirst());
    return result;
}

TrayImageList renderIndicatorPixmaps(const QColor &indicator)
{
    TrayImageList result;
    for (const int size : {16, 22, 24, 32, 48}) {
        QImage image(size, size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal scale = size / 32.0;
        painter.setPen(QPen(QColor(QStringLiteral("#232629")), 1.0 * scale));
        painter.setBrush(indicator);
        painter.drawEllipse(QPointF(25.5 * scale, 6.5 * scale), 4.0 * scale, 4.0 * scale);
        painter.end();

        QByteArray bytes;
        bytes.reserve(size * size * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const QRgb pixel = image.pixel(x, y);
                bytes.append(static_cast<char>(qAlpha(pixel)));
                bytes.append(static_cast<char>(qRed(pixel)));
                bytes.append(static_cast<char>(qGreen(pixel)));
                bytes.append(static_cast<char>(qBlue(pixel)));
            }
        }
        result.append({size, size, bytes});
    }
    return result;
}
} // namespace

class StatusNotifierItem final : public QObject
{
    Q_OBJECT
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
    QString id() const { return QStringLiteral("zapret2control"); }
    QString title() const { return QCoreApplication::applicationName(); }
    uint windowId() const { return 0; }
    QString status() const { return m_failed ? QStringLiteral("NeedsAttention") : QStringLiteral("Active"); }
    QString iconName() const { return QStringLiteral("zapret2control-tray-symbolic"); }
    TrayImageList iconPixmap() const { return renderTrayPixmaps(QColor{}); }
    TrayImageList overlayIconPixmap() const
    {
        return m_inactive ? renderIndicatorPixmaps(QColor(QStringLiteral("#fdbc4b"))) : TrayImageList{};
    }
    TrayImageList attentionIconPixmap() const
    {
        return m_failed ? renderIndicatorPixmaps(QColor(QStringLiteral("#da4453"))) : TrayImageList{};
    }
    QString overlayIconName() const { return {}; }
    QString attentionIconName() const { return {}; }
    QString attentionMovieName() const { return {}; }
    TrayToolTip toolTip() const
    {
        return {iconName(), {}, title(), QStringLiteral("Управление zapret2")};
    }
    bool itemIsMenu() const { return false; }

    void refreshTheme()
    {
        emit NewIcon();
    }

    void setServiceState(const QString &stateCode)
    {
        const bool inactive = stateCode == QLatin1String("inactive");
        // Keep the attention state until the service successfully becomes active.
        // A failed unit may transition to inactive during recovery; clearing the
        // failure here would hide the error before the next successful start.
        const bool failed = stateCode == QLatin1String("failed")
                            || (m_failed && stateCode != QLatin1String("active"));
        const bool changed = m_inactive != inactive || m_failed != failed;
        if (!changed)
            return;
        const bool statusChanged = m_failed != failed;
        m_inactive = inactive;
        m_failed = failed;
        if (statusChanged) {
            const QString currentStatus = status();
            emit NewStatus(currentStatus);

            QDBusMessage propertiesChanged = QDBusMessage::createSignal(
                QString::fromLatin1(itemPath),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"));
            propertiesChanged << QStringLiteral("org.freedesktop.StatusNotifierItem")
                              << QVariantMap{{QStringLiteral("Status"), currentStatus}}
                              << QStringList{};
            QDBusConnection::sessionBus().send(propertiesChanged);
        }
        emit NewOverlayIcon();
        emit NewAttentionIcon();
    }
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

private:
    bool m_inactive = false;
    bool m_failed = false;
};

class StatusNotifierItemAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(uint WindowId READ windowId)
    Q_PROPERTY(QString Status READ status NOTIFY NewStatus)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(TrayImageList IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName)
    Q_PROPERTY(TrayImageList OverlayIconPixmap READ overlayIconPixmap)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(TrayImageList AttentionIconPixmap READ attentionIconPixmap)
    Q_PROPERTY(QString AttentionMovieName READ attentionMovieName)
    Q_PROPERTY(TrayToolTip ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

public:
    explicit StatusNotifierItemAdaptor(StatusNotifierItem *item)
        : QDBusAbstractAdaptor(item)
        , m_item(item)
    {
        setAutoRelaySignals(true);
    }

    QString category() const { return m_item->category(); }
    QString id() const { return m_item->id(); }
    QString title() const { return m_item->title(); }
    uint windowId() const { return m_item->windowId(); }
    QString status() const { return m_item->status(); }
    QString iconName() const { return m_item->iconName(); }
    TrayImageList iconPixmap() const { return m_item->iconPixmap(); }
    QString overlayIconName() const { return m_item->overlayIconName(); }
    TrayImageList overlayIconPixmap() const { return m_item->overlayIconPixmap(); }
    QString attentionIconName() const { return m_item->attentionIconName(); }
    TrayImageList attentionIconPixmap() const { return m_item->attentionIconPixmap(); }
    QString attentionMovieName() const { return m_item->attentionMovieName(); }
    TrayToolTip toolTip() const { return m_item->toolTip(); }
    bool itemIsMenu() const { return m_item->itemIsMenu(); }
    QDBusObjectPath menu() const { return m_item->menu(); }

public slots:
    void ContextMenu(int x, int y) { m_item->ContextMenu(x, y); }
    void Activate(int x, int y) { m_item->Activate(x, y); }
    void SecondaryActivate(int x, int y) { m_item->SecondaryActivate(x, y); }
    void Scroll(int delta, const QString &orientation) { m_item->Scroll(delta, orientation); }

signals:
    void NewTitle();
    void NewIcon();
    void NewAttentionIcon();
    void NewOverlayIcon();
    void NewToolTip();
    void NewStatus(const QString &status);

private:
    StatusNotifierItem *m_item = nullptr;
};

TrayController::~TrayController()
{
    if (!m_exported)
        return;

    m_bus.unregisterObject(QString::fromLatin1(itemPath));
    m_bus.unregisterObject(QString::fromLatin1(menuPath));
    m_bus.unregisterService(m_serviceName);
}

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
            this, [this](const QString &, const QString &oldOwner, const QString &newOwner) {
        m_watcherOwner = newOwner;
        if (!oldOwner.isEmpty() && oldOwner != newOwner)
            setRegistered(false);
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
    if (qGuiApp)
        qGuiApp->installEventFilter(this);
    connect(m_menu, &DBusMenu::activateRequested, this, &TrayController::activateRequested);
    connect(m_menu, &DBusMenu::startRequested, this, &TrayController::startRequested);
    connect(m_menu, &DBusMenu::stopRequested, this, &TrayController::stopRequested);
    connect(m_menu, &DBusMenu::settingsRequested, this, &TrayController::settingsRequested);
    connect(m_menu, &DBusMenu::quitRequested, this, &TrayController::quitRequested);

    refreshWatcherAvailability();
}

bool TrayController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange)
        m_item->refreshTheme();

    return QObject::eventFilter(watched, event);
}

void TrayController::setServiceState(const QString &state, const QString &stateCode,
                                     bool active, bool busy)
{
    m_menu->setServiceState(state, active, busy);
    m_item->setServiceState(stateCode);
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
    m_watcherOwner = owner.isValid() ? owner.value() : QString{};
    const bool available = !m_watcherOwner.isEmpty();
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
    if (!m_watcherAvailable || m_watcherOwner.isEmpty() || !m_bus.isConnected()
        || m_registered || m_registrationPending)
        return;

    if (!m_exported) {
        if (!m_bus.registerService(m_serviceName)) {
            qWarning() << "Unable to own StatusNotifierItem service:"
                       << m_bus.lastError().message();
            return;
        }
        const auto exportFlags = QDBusConnection::ExportAllSlots
                                 | QDBusConnection::ExportAllSignals
                                 | QDBusConnection::ExportAllProperties;
        if (!m_bus.registerObject(QString::fromLatin1(menuPath), m_menu, exportFlags)) {
            qWarning() << "Unable to export DBusMenu:" << m_bus.lastError().message();
            m_bus.unregisterService(m_serviceName);
            return;
        }
        if (!m_item->findChild<StatusNotifierItemAdaptor *>())
            new StatusNotifierItemAdaptor(m_item);
        if (!m_bus.registerObject(QString::fromLatin1(itemPath), m_item,
                                  QDBusConnection::ExportAdaptors)) {
            qWarning() << "Unable to export StatusNotifierItem:"
                       << m_bus.lastError().message();
            m_bus.unregisterObject(QString::fromLatin1(menuPath));
            m_bus.unregisterService(m_serviceName);
            return;
        }
        m_exported = true;
    }

    const QString expectedOwner = m_watcherOwner;
    m_registrationPending = true;

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(watcherService),
        QString::fromLatin1(watcherPath),
        QString::fromLatin1(watcherInterface),
        QStringLiteral("RegisterStatusNotifierItem"));
    message << m_serviceName;

    auto *call = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this,
            [this, expectedOwner](QDBusPendingCallWatcher *watcher) {
        m_registrationPending = false;
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            setRegistered(false);
            qWarning() << "StatusNotifierItem registration failed:"
                       << reply.error().message();
        } else {
            const QDBusReply<QString> currentOwner = m_bus.isConnected() && m_bus.interface()
                ? m_bus.interface()->serviceOwner(QString::fromLatin1(watcherService))
                : QDBusReply<QString>{};
            if (m_watcherAvailable && currentOwner.isValid()
                && currentOwner.value() == expectedOwner) {
                setRegistered(true);
            } else {
                setRegistered(false);
                refreshWatcherAvailability();
            }
        }
        watcher->deleteLater();
    });
}

#include "traycontroller.moc"

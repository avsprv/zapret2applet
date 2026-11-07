#include "traycontroller.h"

#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDBusReply>

namespace {
constexpr auto watcherService = "org.freedesktop.StatusNotifierWatcher";
}

TrayController::TrayController(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
    , m_watcher(new QDBusServiceWatcher(QString::fromLatin1(watcherService),
                                        m_bus,
                                        QDBusServiceWatcher::WatchForOwnerChange,
                                        this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, [this](const QString &, const QString &, const QString &newOwner) {
        const bool available = !newOwner.isEmpty();
        if (m_watcherAvailable == available)
            return;
        m_watcherAvailable = available;
        emit watcherAvailableChanged();
    });

    refreshWatcherAvailability();
}

bool TrayController::watcherAvailable() const
{
    return m_watcherAvailable;
}

void TrayController::refreshWatcherAvailability()
{
    if (!m_bus.isConnected() || !m_bus.interface()) {
        if (m_watcherAvailable) {
            m_watcherAvailable = false;
            emit watcherAvailableChanged();
        }
        return;
    }

    const QDBusReply<QString> owner =
        m_bus.interface()->serviceOwner(QString::fromLatin1(watcherService));
    const bool available = owner.isValid() && !owner.value().isEmpty();
    if (m_watcherAvailable == available)
        return;

    m_watcherAvailable = available;
    emit watcherAvailableChanged();
}

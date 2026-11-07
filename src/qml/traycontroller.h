#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QString>

class QDBusServiceWatcher;
class StatusNotifierItem;
class DBusMenu;

class TrayController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool watcherAvailable READ watcherAvailable NOTIFY watcherAvailableChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)

public:
    explicit TrayController(QObject *parent = nullptr);

    void setServiceState(const QString &state, bool active, bool busy);

    bool watcherAvailable() const;
    bool registered() const;

signals:
    void watcherAvailableChanged();
    void registeredChanged();
    void activateRequested();
    void settingsRequested();
    void quitRequested();
    void startRequested();
    void stopRequested();

private:
    void refreshWatcherAvailability();
    void registerWithWatcher();
    void setRegistered(bool registered);

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher = nullptr;
    StatusNotifierItem *m_item = nullptr;
    DBusMenu *m_menu = nullptr;
    QString m_serviceName;
    bool m_watcherAvailable = false;
    bool m_registered = false;
    bool m_exported = false;
};

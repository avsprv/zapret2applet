#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QString>

class QDBusServiceWatcher;
class QEvent;
class StatusNotifierItem;
class DBusMenu;

class TrayController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool watcherAvailable READ watcherAvailable NOTIFY watcherAvailableChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    void setServiceState(const QString &state, const QString &stateCode, bool active, bool busy);

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
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    bool m_registrationPending = false;
    QString m_watcherOwner;
};

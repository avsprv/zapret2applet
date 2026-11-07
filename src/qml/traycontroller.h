#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QString>

class QDBusServiceWatcher;

class TrayController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool watcherAvailable READ watcherAvailable NOTIFY watcherAvailableChanged)

public:
    explicit TrayController(QObject *parent = nullptr);

    bool watcherAvailable() const;

signals:
    void watcherAvailableChanged();

private:
    void refreshWatcherAvailability();

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher = nullptr;
    bool m_watcherAvailable = false;
};

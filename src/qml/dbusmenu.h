#pragma once

#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusVariant>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

struct DBusMenuLayoutItem
{
    int id = 0;
    QVariantMap properties;
    QList<QDBusVariant> children;
};

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuLayoutItem &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuLayoutItem &item);

struct DBusMenuItemProperties
{
    int id = 0;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemProperties &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemProperties &item);

struct DBusMenuItemPropertyNames
{
    int id = 0;
    QStringList properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemPropertyNames &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemPropertyNames &item);

using DBusMenuItemPropertiesList = QList<DBusMenuItemProperties>;
using DBusMenuItemPropertyNamesList = QList<DBusMenuItemPropertyNames>;

Q_DECLARE_METATYPE(DBusMenuLayoutItem)
Q_DECLARE_METATYPE(DBusMenuItemProperties)
Q_DECLARE_METATYPE(DBusMenuItemPropertiesList)
Q_DECLARE_METATYPE(DBusMenuItemPropertyNames)
Q_DECLARE_METATYPE(DBusMenuItemPropertyNamesList)

class DBusMenu final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    explicit DBusMenu(QObject *parent = nullptr);

    uint version() const;
    QString textDirection() const;
    QString status() const;
    QStringList iconThemePath() const;

    void setServiceState(const QString &state, bool active, bool busy);

public slots:
    void GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                   uint &revision, DBusMenuLayoutItem &layout);
    void GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames,
                            DBusMenuItemPropertiesList &properties);
    void GetProperty(int id, const QString &name, QDBusVariant &value);
    void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    void AboutToShow(int id, bool &needUpdate);
    void AboutToShowGroup(const QList<int> &ids, QList<int> &updatesNeeded,
                          QList<int> &idErrors);

signals:
    void LayoutUpdated(uint revision, int parent);
    void ItemsPropertiesUpdated(const DBusMenuItemPropertiesList &updatedProperties,
                                const DBusMenuItemPropertyNamesList &removedProperties);
    void activateRequested();
    void startRequested();
    void stopRequested();
    void settingsRequested();
    void quitRequested();

private:
    QVariantMap propertiesForItem(int id) const;
    QVariantMap filteredProperties(const QVariantMap &properties,
                                   const QStringList &propertyNames) const;
    DBusMenuLayoutItem layoutForItem(int id, int recursionDepth,
                                     const QStringList &propertyNames) const;
    void updateRevision();

    QString m_serviceState = QStringLiteral("Неизвестно");
    bool m_serviceActive = false;
    bool m_busy = false;
    uint m_revision = 1;
};

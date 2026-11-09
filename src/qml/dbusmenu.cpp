#include "dbusmenu.h"

#include <QDBusContext>
#include <QDBusError>
#include <QDBusMetaType>

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuLayoutItem &item)
{
    argument.beginStructure();
    argument << item.id << item.properties << item.children;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuLayoutItem &item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties >> item.children;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemProperties &item)
{
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemProperties &item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemPropertyNames &item)
{
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemPropertyNames &item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

namespace {
const QList<int> itemIds{1, 2, 3, 4, 5, 6, 7};
} // namespace

DBusMenu::DBusMenu(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DBusMenuLayoutItem>();
    qRegisterMetaType<DBusMenuItemProperties>();
    qRegisterMetaType<DBusMenuItemPropertiesList>();
    qRegisterMetaType<DBusMenuItemPropertyNames>();
    qRegisterMetaType<DBusMenuItemPropertyNamesList>();
    qDBusRegisterMetaType<DBusMenuLayoutItem>();
    qDBusRegisterMetaType<DBusMenuItemProperties>();
    qDBusRegisterMetaType<DBusMenuItemPropertiesList>();
    qDBusRegisterMetaType<DBusMenuItemPropertyNames>();
    qDBusRegisterMetaType<DBusMenuItemPropertyNamesList>();
}

uint DBusMenu::version() const
{
    return 3;
}

QString DBusMenu::textDirection() const
{
    return QStringLiteral("ltr");
}

QString DBusMenu::status() const
{
    return QStringLiteral("normal");
}

QStringList DBusMenu::iconThemePath() const
{
    return {};
}

void DBusMenu::setServiceState(const QString &state, bool active, bool busy)
{
    if (m_serviceState == state && m_serviceActive == active && m_busy == busy)
        return;
    m_serviceState = state;
    m_serviceActive = active;
    m_busy = busy;
    updateRevision();
}

QVariantMap DBusMenu::propertiesForItem(int id) const
{
    switch (id) {
    case 1:
        return {{QStringLiteral("label"), QStringLiteral("Показать / скрыть окно")},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("visible"), true}};
    case 2:
        return {{QStringLiteral("label"), QStringLiteral("Состояние: %1").arg(m_serviceState)},
                {QStringLiteral("enabled"), false},
                {QStringLiteral("visible"), true}};
    case 3:
        return {{QStringLiteral("label"), QStringLiteral("Запустить zapret2")},
                {QStringLiteral("enabled"), !m_serviceActive && !m_busy},
                {QStringLiteral("visible"), true}};
    case 4:
        return {{QStringLiteral("label"), QStringLiteral("Остановить zapret2")},
                {QStringLiteral("enabled"), m_serviceActive && !m_busy},
                {QStringLiteral("visible"), true}};
    case 5:
        return {{QStringLiteral("type"), QStringLiteral("separator")},
                {QStringLiteral("visible"), true}};
    case 6:
        return {{QStringLiteral("label"), QStringLiteral("Настройки")},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("visible"), true}};
    case 7:
        return {{QStringLiteral("label"), QStringLiteral("Выход")},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("visible"), true}};
    default:
        return {};
    }
}

QVariantMap DBusMenu::filteredProperties(const QVariantMap &properties,
                                         const QStringList &propertyNames) const
{
    if (propertyNames.isEmpty())
        return properties;

    QVariantMap filtered;
    for (const QString &name : propertyNames) {
        const auto it = properties.constFind(name);
        if (it != properties.cend())
            filtered.insert(name, it.value());
    }
    return filtered;
}

DBusMenuLayoutItem DBusMenu::layoutForItem(int id, int recursionDepth,
                                           const QStringList &propertyNames) const
{
    DBusMenuLayoutItem item;
    item.id = id;
    item.properties = filteredProperties(id == 0 ? QVariantMap{} : propertiesForItem(id),
                                         propertyNames);

    if (id == 0 && recursionDepth != 0) {
        for (const int childId : itemIds) {
            DBusMenuLayoutItem child = layoutForItem(childId, 0, propertyNames);
            item.children.append(QDBusVariant(QVariant::fromValue(child)));
        }
    }
    return item;
}

void DBusMenu::GetLayout(int parentId, int recursionDepth,
                         const QStringList &propertyNames, uint &revision,
                         DBusMenuLayoutItem &layout)
{
    if (parentId != 0 && !itemIds.contains(parentId)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown menu item"));
        return;
    }

    revision = m_revision;
    layout = layoutForItem(parentId, recursionDepth, propertyNames);
}

void DBusMenu::GetGroupProperties(const QList<int> &ids,
                                  const QStringList &propertyNames,
                                  DBusMenuItemPropertiesList &properties)
{
    properties.clear();
    const QList<int> requestedIds = ids.isEmpty() ? itemIds : ids;
    for (const int id : requestedIds) {
        if (!itemIds.contains(id))
            continue;
        properties.append({id, filteredProperties(propertiesForItem(id), propertyNames)});
    }
}

void DBusMenu::GetProperty(int id, const QString &name, QDBusVariant &value)
{
    if (!itemIds.contains(id)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown menu item"));
        return;
    }

    const QVariantMap properties = propertiesForItem(id);
    const auto it = properties.constFind(name);
    if (it == properties.cend()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown menu property"));
        return;
    }
    value = QDBusVariant(it.value());
}

void DBusMenu::Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp)
{
    Q_UNUSED(data)
    Q_UNUSED(timestamp)

    if (!itemIds.contains(id)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown menu item"));
        return;
    }
    if (eventId != QLatin1String("clicked"))
        return;

    const QVariantMap properties = propertiesForItem(id);
    if (!properties.value(QStringLiteral("enabled"), true).toBool())
        return;

    switch (id) {
    case 1: emit activateRequested(); break;
    case 3: emit startRequested(); break;
    case 4: emit stopRequested(); break;
    case 6: emit settingsRequested(); break;
    case 7: emit quitRequested(); break;
    default: break;
    }
}

void DBusMenu::AboutToShow(int id, bool &needUpdate)
{
    if (id != 0 && !itemIds.contains(id)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown menu item"));
        return;
    }
    needUpdate = false;
}

void DBusMenu::AboutToShowGroup(const QList<int> &ids, QList<int> &updatesNeeded,
                                QList<int> &idErrors)
{
    updatesNeeded.clear();
    idErrors.clear();
    for (const int id : ids) {
        if (id != 0 && !itemIds.contains(id))
            idErrors.append(id);
    }
}

void DBusMenu::updateRevision()
{
    ++m_revision;
    if (m_revision == 0)
        m_revision = 1;
    emit LayoutUpdated(m_revision, 0);
}

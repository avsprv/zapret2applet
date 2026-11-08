#include "settingsmigration.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>

namespace {
constexpr auto kLegacyApplicationName = "zapret2applet";
constexpr auto kMigrationMarker = "migration/zapret2control-v1";
}

bool SettingsMigration::migrateLegacyProfile()
{
    QSettings current;
    current.sync();
    if (current.status() != QSettings::NoError)
        return false;

    if (current.value(QLatin1String(kMigrationMarker)).toBool())
        return true;

    const QStringList currentKeys = current.allKeys();
    if (!currentKeys.isEmpty()) {
        current.setValue(QLatin1String(kMigrationMarker), true);
        current.sync();
        return current.status() == QSettings::NoError;
    }

    const QString currentApplicationName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QLatin1String(kLegacyApplicationName));
    QSettings legacy;
    QCoreApplication::setApplicationName(currentApplicationName);
    legacy.sync();
    if (legacy.status() != QSettings::NoError)
        return false;

    const QStringList legacyKeys = legacy.allKeys();
    if (legacyKeys.isEmpty()) {
        current.setValue(QLatin1String(kMigrationMarker), true);
        current.sync();
        return current.status() == QSettings::NoError;
    }

    for (const QString &key : legacyKeys)
        current.setValue(key, legacy.value(key));

    current.sync();
    if (current.status() != QSettings::NoError) {
        current.clear();
        current.sync();
        return false;
    }

    current.setValue(QLatin1String(kMigrationMarker), true);
    current.sync();
    return current.status() == QSettings::NoError;
}

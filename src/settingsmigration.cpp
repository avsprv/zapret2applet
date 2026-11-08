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

    // Merge missing keys so an interrupted/partial first run can be retried.
    // Values already present in the new profile always take precedence.
    for (const QString &key : legacyKeys) {
        if (!current.contains(key))
            current.setValue(key, legacy.value(key));
    }

    current.sync();
    if (current.status() != QSettings::NoError)
        return false;

    current.setValue(QLatin1String(kMigrationMarker), true);
    current.sync();
    return current.status() == QSettings::NoError;
}

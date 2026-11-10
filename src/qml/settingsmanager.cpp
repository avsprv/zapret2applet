#include "settingsmanager.h"

#include <QSettings>

namespace {
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kLanguageKey = "appearance/language";
constexpr auto kAutoCheckKey = "update/autoCheck";
constexpr auto kCloseToTrayKey = "tray/closeToTray";
}

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
}

QString SettingsManager::theme() const
{
    const QString value = QSettings().value(QLatin1String(kThemeKey), QStringLiteral("system")).toString();
    return value == QLatin1String("light") || value == QLatin1String("dark")
        ? value : QStringLiteral("system");
}

void SettingsManager::setTheme(const QString &value)
{
    const QString normalized = value == QLatin1String("light") || value == QLatin1String("dark")
        ? value : QStringLiteral("system");
    if (theme() == normalized)
        return;
    QSettings().setValue(QLatin1String(kThemeKey), normalized);
    emit themeChanged();
}

QString SettingsManager::language() const
{
    const QString value = QSettings().value(QLatin1String(kLanguageKey), QStringLiteral("system")).toString();
    return value == QLatin1String("ru") || value == QLatin1String("en")
        ? value : QStringLiteral("system");
}

void SettingsManager::setLanguage(const QString &value)
{
    const QString normalized = value == QLatin1String("ru") || value == QLatin1String("en")
        ? value : QStringLiteral("system");
    if (language() == normalized)
        return;
    QSettings().setValue(QLatin1String(kLanguageKey), normalized);
    emit languageChanged();
}

bool SettingsManager::autoCheckUpdates() const
{
    return QSettings().value(QLatin1String(kAutoCheckKey), true).toBool();
}

void SettingsManager::setAutoCheckUpdates(bool enabled)
{
    if (autoCheckUpdates() == enabled)
        return;
    QSettings().setValue(QLatin1String(kAutoCheckKey), enabled);
    emit autoCheckUpdatesChanged();
}

bool SettingsManager::closeToTray() const
{
    return QSettings().value(QLatin1String(kCloseToTrayKey), true).toBool();
}

void SettingsManager::setCloseToTray(bool enabled)
{
    if (closeToTray() == enabled)
        return;
    QSettings().setValue(QLatin1String(kCloseToTrayKey), enabled);
    emit closeToTrayChanged();
}

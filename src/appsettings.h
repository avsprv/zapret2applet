#pragma once

#include <QString>

class QApplication;

namespace AppSettings {

// Настройки QSettings мигрируются из прежнего applicationName через SettingsMigration.
enum class Theme {
    System,
    Light,
    Dark
};

enum class Language {
    System,
    Russian,
    English
};

Theme theme();
void setTheme(Theme value);
void applyTheme(QApplication &app);

Language language();
void setLanguage(Language value);

bool autoCheckUpdates();
void setAutoCheckUpdates(bool enabled);

bool closeToTray();
void setCloseToTray(bool enabled);

} // namespace AppSettings

#pragma once

namespace SettingsMigration {

// Переносит пользовательские настройки из профиля zapret2applet в zapret2control.
// Старый профиль не удаляется. Возвращает false при ошибке чтения или записи.
bool migrateLegacyProfile();

} // namespace SettingsMigration

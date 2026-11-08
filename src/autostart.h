#pragma once

#include <QString>

// Автозапуск самого GUI при входе в сеанс (XDG autostart, ~/.config/autostart).
// Не путать с автозапуском сервиса zapret2 при загрузке системы (ZapretBackend::setBootEnabled).
namespace Autostart {
bool migrateLegacyEntry(const QString &autostartDirectory, const QString &applicationPath);
bool isEnabled();
bool setEnabled(bool enabled);
}

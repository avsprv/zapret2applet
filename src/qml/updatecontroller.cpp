#include "updatecontroller.h"

#include <QDateTime>
#include <QSettings>
#include <QTimer>

#include "backend/updatechecker.h"
#include "backend/zapretupdater.h"
#include "backend/zapretpaths.h"
#include "settingsmanager.h"

namespace {
constexpr qint64 kAutoCheckIntervalSecs = 24 * 60 * 60;
constexpr int kPeriodicTimerIntervalMs = 6 * 60 * 60 * 1000;
constexpr int kStartupDelayMs = 8000;
constexpr qsizetype kMaxLogChars = 1024 * 1024;
constexpr qsizetype kMaxLogLineChars = 64 * 1024;
}

UpdateController::UpdateController(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_checker(new UpdateChecker(this))
    , m_updater(new ZapretUpdater(this))
    , m_periodicTimer(new QTimer(this))
{
    m_periodicTimer->setInterval(kPeriodicTimerIntervalMs);
    connect(m_periodicTimer, &QTimer::timeout, this, &UpdateController::maybeAutoCheck);
    connect(m_settings, &SettingsManager::autoCheckUpdatesChanged, this, [this] {
        if (m_settings->autoCheckUpdates()) {
            m_periodicTimer->start();
            maybeAutoCheck();
        } else {
            m_periodicTimer->stop();
        }
    });
    connect(m_checker, &UpdateChecker::finished, this, [this](const QString &tag) {
        m_checking = false;
        m_latestTag = tag;
        QSettings().setValue(QStringLiteral("update/lastCheck"), QDateTime::currentSecsSinceEpoch());

        const bool newer = !m_installed.isEmpty() && ZapretVersion::compare(m_installed, tag) < 0;
        if (m_automaticCheck && m_settings->autoCheckUpdates() && newer) {
            QSettings settings;
            if (settings.value(QStringLiteral("update/notifiedTag")).toString() != tag) {
                settings.setValue(QStringLiteral("update/notifiedTag"), tag);
                emit updateAvailable(tag);
            }
        }

        if (m_installed.isEmpty())
            setStatus(tr("Доступна чистая установка %1.").arg(tag));
        else if (newer)
            setStatus(tr("Доступна новая версия: %1.").arg(tag));
        else if (ZapretVersion::compare(m_installed, tag) > 0)
            setStatus(tr("Установленная версия новее последнего релиза GitHub."));
        else
            setStatus(tr("Установлена актуальная версия."));
        emit stateChanged();
    });
    connect(m_checker, &UpdateChecker::failed, this, [this](const QString &message) {
        m_checking = false;
        if (!m_automaticCheck)
            setStatus(tr("Не удалось проверить обновления: %1").arg(message));
        emit stateChanged();
    });
    connect(m_updater, &ZapretUpdater::logLine, this, &UpdateController::appendLog);
    connect(m_updater, &ZapretUpdater::failedToStart, this, [this](const QString &message) {
        m_busy = false;
        m_installing = false;
        appendLog(message);
        setStatus(message);
        emit stateChanged();
        emit operationFinished();
    });
    connect(m_updater, &ZapretUpdater::finished, this, &UpdateController::handleUpdateFinished);

    refreshInstalled();
    if (m_settings->autoCheckUpdates()) {
        m_periodicTimer->start();
        QTimer::singleShot(kStartupDelayMs, this, &UpdateController::maybeAutoCheck);
    }
    if (m_installed.isEmpty())
        setStatus(tr("zapret2 не установлен. Проверьте наличие релиза для установки."));
    else
        setStatus(tr("Версия zapret2 определена."));
}

QString UpdateController::installedVersion() const
{
    return m_installed.isEmpty() ? tr("не установлена") : QStringLiteral("v") + m_installed;
}

QString UpdateController::latestVersion() const
{
    return m_latestTag.isEmpty() ? QStringLiteral("—") : m_latestTag;
}

QString UpdateController::status() const { return m_status; }
QString UpdateController::logText() const { return m_logText; }
QString UpdateController::releaseNotesUrl() const
{
    return m_latestTag.isEmpty() ? QString() : ZapretVersion::releaseUrl(m_latestTag);
}
bool UpdateController::installed() const { return !m_installed.isEmpty(); }
bool UpdateController::checking() const { return m_checking; }
bool UpdateController::busy() const { return m_busy; }
bool UpdateController::blockcheckBusy() const { return m_blockcheckBusy; }

bool UpdateController::canInstall() const
{
    return m_installed.isEmpty() && !m_latestTag.isEmpty() && !busy() && !m_checking && !m_blockcheckBusy;
}

bool UpdateController::canUpdate() const
{
    return !m_installed.isEmpty() && !m_latestTag.isEmpty()
        && ZapretVersion::compare(m_installed, m_latestTag) < 0
        && !busy() && !m_checking && !m_blockcheckBusy;
}

void UpdateController::setBlockcheckBusy(bool busy)
{
    if (m_blockcheckBusy == busy)
        return;
    m_blockcheckBusy = busy;
    emit stateChanged();
}

void UpdateController::maybeAutoCheck()
{
    if (!m_settings->autoCheckUpdates() || busy() || m_checking)
        return;
    const qint64 last = QSettings().value(QStringLiteral("update/lastCheck"), 0).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - last < kAutoCheckIntervalSecs)
        return;
    startCheck(true);
}

void UpdateController::checkNow()
{
    startCheck(false);
}

void UpdateController::startCheck(bool automatic)
{
    if (m_checking || busy())
        return;
    m_automaticCheck = automatic;
    refreshInstalled();
    m_checking = true;
    setStatus(tr("Проверяю обновления…"));
    emit stateChanged();
    m_checker->check();
}

void UpdateController::refreshInstalled()
{
    m_installed = ZapretVersion::installed(ZapretPaths::baseDir());
    emit stateChanged();
}

void UpdateController::appendLog(const QString &line)
{
    QString bounded = line;
    if (bounded.size() > kMaxLogLineChars)
        bounded = bounded.left(kMaxLogLineChars) + tr("… [строка журнала сокращена]");
    m_logText += (m_logText.isEmpty() ? QString() : QStringLiteral("\n")) + bounded;
    if (m_logText.size() > kMaxLogChars)
        m_logText.remove(0, m_logText.size() - kMaxLogChars);
    emit logTextChanged();
}

void UpdateController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit stateChanged();
}

void UpdateController::clearLog()
{
    if (busy())
        return;
    m_logText.clear();
    emit logTextChanged();
}

void UpdateController::install()
{
    if (!canInstall())
        return;
    startOperation(true);
}

void UpdateController::update()
{
    if (!canUpdate())
        return;
    startOperation(false);
}

void UpdateController::startOperation(bool installing)
{
    m_installing = installing;
    m_busy = true;
    m_logText.clear();
    emit logTextChanged();
    setStatus(installing ? tr("Устанавливаю zapret2 %1…").arg(m_latestTag)
                         : tr("Обновляю zapret2 до %1…").arg(m_latestTag));
    emit stateChanged();
    if (installing)
        m_updater->startInstall(m_latestTag);
    else
        m_updater->start(m_latestTag);
}

void UpdateController::handleUpdateFinished(int exitCode)
{
    refreshInstalled();
    if (exitCode == 0) {
        setStatus(m_installing ? tr("zapret2 установлен: %1.").arg(m_latestTag)
                               : tr("zapret2 обновлён до %1.").arg(m_latestTag));
    } else if (exitCode == 126) {
        setStatus(m_installing ? tr("Установка отменена: диалог авторизации закрыт.")
                               : tr("Обновление отменено: диалог авторизации закрыт."));
    } else {
        setStatus(m_installing ? tr("Установка не выполнена (код %1). Подробности — в журнале.").arg(exitCode)
                               : tr("Обновление не выполнено (код %1). Подробности — в журнале.").arg(exitCode));
    }
    m_installing = false;
    m_busy = false;
    emit stateChanged();
    emit operationFinished();
}

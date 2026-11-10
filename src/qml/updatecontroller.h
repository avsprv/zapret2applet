#pragma once

#include <QObject>
#include <QString>

class QTimer;
class SettingsManager;
class UpdateChecker;
class ZapretUpdater;

class UpdateController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY stateChanged)
    Q_PROPERTY(bool installed READ installed NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString releaseNotesUrl READ releaseNotesUrl NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)
    Q_PROPERTY(bool canUpdate READ canUpdate NOTIFY stateChanged)
    Q_PROPERTY(bool blockcheckBusy READ blockcheckBusy WRITE setBlockcheckBusy NOTIFY stateChanged)

public:
    explicit UpdateController(SettingsManager *settings, QObject *parent = nullptr);

    QString installedVersion() const;
    bool installed() const;
    QString latestVersion() const;
    QString status() const;
    QString logText() const;
    QString releaseNotesUrl() const;
    bool checking() const;
    bool busy() const;
    bool canInstall() const;
    bool canUpdate() const;
    bool blockcheckBusy() const;
    void setBlockcheckBusy(bool busy);

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void install();
    Q_INVOKABLE void update();
    Q_INVOKABLE void clearLog();

signals:
    void stateChanged();
    void logTextChanged();
    void updateAvailable(const QString &tag);
    void operationFinished();

private:
    void maybeAutoCheck();
    void startCheck(bool automatic);
    void refreshInstalled();
    void appendLog(const QString &line);
    void setStatus(const QString &status);
    void startOperation(bool installing);
    void handleUpdateFinished(int exitCode);

    SettingsManager *m_settings = nullptr;
    UpdateChecker *m_checker = nullptr;
    ZapretUpdater *m_updater = nullptr;
    QTimer *m_periodicTimer = nullptr;
    QString m_installed;
    QString m_latestTag;
    QString m_status;
    QString m_logText;
    bool m_checking = false;
    bool m_busy = false;
    bool m_automaticCheck = false;
    bool m_blockcheckBusy = false;
    bool m_installing = false;
};

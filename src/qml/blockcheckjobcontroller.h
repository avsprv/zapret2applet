#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QUrl>

#include "backend/blockcheckrunner.h"

class BlockcheckJobController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString domains READ domains WRITE setDomains NOTIFY optionsChanged)
    Q_PROPERTY(QString ipVersion READ ipVersion WRITE setIpVersion NOTIFY optionsChanged)
    Q_PROPERTY(bool http READ http WRITE setHttp NOTIFY optionsChanged)
    Q_PROPERTY(bool tls12 READ tls12 WRITE setTls12 NOTIFY optionsChanged)
    Q_PROPERTY(bool tls13 READ tls13 WRITE setTls13 NOTIFY optionsChanged)
    Q_PROPERTY(bool http3 READ http3 WRITE setHttp3 NOTIFY optionsChanged)
    Q_PROPERTY(int repeats READ repeats WRITE setRepeats NOTIFY optionsChanged)
    Q_PROPERTY(QString scanLevel READ scanLevel WRITE setScanLevel NOTIFY optionsChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusSeverity READ statusSeverity NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool cancelAvailable READ cancelAvailable NOTIFY busyChanged)
    Q_PROPERTY(bool recoveryRequired READ recoveryRequired NOTIFY recoveryRequiredChanged)
    Q_PROPERTY(bool recoveryRetryAvailable READ recoveryRetryAvailable NOTIFY recoveryRetryAvailableChanged)
    Q_PROPERTY(bool updateBusy READ updateBusy WRITE setUpdateBusy NOTIFY updateBusyChanged)

public:
    enum class StatusType { Information, Success, Warning, Error };
    Q_ENUM(StatusType)

    explicit BlockcheckJobController(QObject *parent = nullptr);
    QString domains() const; void setDomains(const QString &value);
    QString ipVersion() const; void setIpVersion(const QString &value);
    bool http() const; void setHttp(bool value);
    bool tls12() const; void setTls12(bool value);
    bool tls13() const; void setTls13(bool value);
    bool http3() const; void setHttp3(bool value);
    int repeats() const; void setRepeats(int value);
    QString scanLevel() const; void setScanLevel(const QString &value);
    QString logText() const;
    QVariantList results() const;
    QString status() const;
    QString statusSeverity() const;
    bool busy() const;
    bool cancelAvailable() const;
    bool recoveryRequired() const;
    bool recoveryRetryAvailable() const;
    bool updateBusy() const;
    void setUpdateBusy(bool value);
    void setServiceBusy(bool value);

    Q_INVOKABLE void start();
    Q_INVOKABLE void confirmStop(bool accepted);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retryRecovery();
    Q_INVOKABLE void copyStrategy(int index);
    Q_INVOKABLE bool saveLog(const QUrl &url);

signals:
    void optionsChanged();
    void logTextChanged();
    void resultsChanged();
    void statusChanged();
    void busyChanged();
    void recoveryRequiredChanged();
    void recoveryRetryAvailableChanged();
    void updateBusyChanged();
    void confirmationRequired(const QString &title, const QString &message);
    void scanFinished(int found, bool canceled);

private:
    enum class ProcessTask { None, Probe, Submit, Status, Logs, Cancel, Recovery };
    void setStatus(const QString &value, StatusType type = StatusType::Information);
    void setBusy(bool value);
    void setRecoveryRequired(bool value);
    void appendLog(const QString &line);
    void launch(ProcessTask task, const QStringList &arguments);
    void handleProcessFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void poll();
    void processProbe(const QByteArray &output);
    void processStatus(const QByteArray &output);
    void processLogs(const QByteArray &output);
    void finish(bool success, bool canceled = false);
    bool validateOptions(QStringList *domainsOut) const;

    QString m_domains = QStringLiteral("rutracker.org");
    QString m_ipVersion = QStringLiteral("4");
    bool m_http = true, m_tls12 = true, m_tls13 = false, m_http3 = false;
    int m_repeats = 1;
    QString m_scanLevel = QStringLiteral("standard");
    QString m_logText, m_status, m_operationId, m_terminalPhase;
    QVariantList m_results;
    StatusType m_statusType = StatusType::Information;
    bool m_busy = false, m_updateBusy = false, m_serviceBusy = false, m_recoveryRequired = false;
    bool m_cancelRequested = false, m_finalLogsPending = false, m_probePending = false;
    bool m_recoveryRetryInProgress = false;
    int m_found = 0, m_pollTicks = 0;
    qint64 m_startedAtSecs = 0;
    ProcessTask m_task = ProcessTask::None;
    QProcess *m_process = nullptr;
    QByteArray m_stdout, m_stderr;
    QTimer *m_pollTimer = nullptr;
};

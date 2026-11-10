#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QProcess>

class LogsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY lineCountChanged)

public:
    explicit LogsController(QObject *parent = nullptr);

    QString logText() const;
    QString status() const;
    bool busy() const;
    int lineCount() const;

    Q_INVOKABLE void readLogs(int lineCount = 200);

signals:
    void logTextChanged();
    void statusChanged();
    void busyChanged();
    void lineCountChanged();

private:
    void setStatus(const QString &status);
    void setBusy(bool busy);
    void setLogText(const QString &text);

    QString m_logText;
    QString m_status;
    int m_lineCount = 200;
    bool m_busy = false;
    QByteArray m_output;
    QByteArray m_error;
    QProcess *m_process = nullptr;
};

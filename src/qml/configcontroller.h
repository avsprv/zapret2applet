#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QProcess>

class ConfigController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString configText READ configText WRITE setConfigText NOTIFY configTextChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)

public:
    explicit ConfigController(QObject *parent = nullptr);
    QString configText() const;
    void setConfigText(const QString &text);
    QString status() const;
    bool busy() const;
    bool loaded() const;

    Q_INVOKABLE void read();
    Q_INVOKABLE void validate();
    Q_INVOKABLE void save();
    Q_INVOKABLE void rollback();
    Q_INVOKABLE bool exportConfig(const QUrl &url);

signals:
    void configTextChanged();
    void statusChanged();
    void busyChanged();
    void loadedChanged();

private:
    void setStatus(const QString &status);
    void setBusy(bool busy);
    void finishProcess(QProcess *process, const QString &operation,
                       const QByteArray &input = {});
    QString m_configText;
    QString m_status;
    bool m_busy = false;
    bool m_loaded = false;
    QByteArray m_originalBytes;
    QProcess *m_process = nullptr;
};

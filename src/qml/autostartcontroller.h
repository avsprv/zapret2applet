#pragma once

#include <QObject>
#include <QString>

class AutostartController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit AutostartController(QObject *parent = nullptr,
                                  const QString &autostartDirectory = {},
                                  const QString &executablePath = {});

    bool enabled() const;
    QString errorMessage() const;

    Q_INVOKABLE void setEnabled(bool enabled);

signals:
    void enabledChanged();
    void errorMessageChanged();

private:
    QString entryPath() const;
    QString expectedExec() const;
    bool isManagedEntry() const;
    void setError(const QString &message);

    QString m_autostartDirectory;
    QString m_executablePath;
    QString m_errorMessage;
};

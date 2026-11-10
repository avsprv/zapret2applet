#pragma once

#include <QObject>
#include <QByteArray>
#include <QProcess>
#include <QVariant>
#include <QVector>
#include "profileanalyzer.h"

class ProfileController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
public:
    explicit ProfileController(QObject *parent = nullptr);
    QVariantList profiles() const;
    int selectedIndex() const;
    void setSelectedIndex(int index);
    QString status() const;
    bool busy() const;
    bool loaded() const;
    bool dirty() const;
    Q_INVOKABLE void read(bool discardChanges = false);
    Q_INVOKABLE void add(const QStringList &arguments);
    Q_INVOKABLE void edit(int index, const QStringList &arguments);
    Q_INVOKABLE void copy(int index);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void save();
signals:
    void profilesChanged();
    void selectedIndexChanged();
    void statusChanged();
    void busyChanged();
    void loadedChanged();
    void dirtyChanged();
private:
    void setStatus(const QString &value);
    void setBusy(bool value);
    void setDirty(bool value);
    void setLoaded(bool value);
    bool validArguments(const QStringList &arguments, QString *error) const;
    void startWrite(const QString &content);
    QByteArray m_configBytes;
    QByteArray m_output;
    QByteArray m_error;
    QString m_config;
    QVector<ProfileAnalyzer::Profile> m_profiles;
    QString m_status;
    int m_selectedIndex = -1;
    bool m_busy = false;
    bool m_loaded = false;
    bool m_dirty = false;
    QProcess *m_process = nullptr;
};

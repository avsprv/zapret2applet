#pragma once

#include <QObject>
#include <QString>

class SettingsManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    QString theme() const;
    void setTheme(const QString &value);

    QString language() const;
    void setLanguage(const QString &value);

    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool enabled);

    bool closeToTray() const;
    void setCloseToTray(bool enabled);

signals:
    void themeChanged();
    void languageChanged();
    void autoCheckUpdatesChanged();
    void closeToTrayChanged();
};

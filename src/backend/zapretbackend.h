#pragma once

#include <QObject>
#include <QString>

// Единственная точка контакта с апстримом zapret2.
// При смене CLI/юнитов апстрима правится только реализация этого интерфейса.
class ZapretBackend : public QObject
{
    Q_OBJECT
public:
    enum class State { Unknown, Active, Inactive, Failed };
    Q_ENUM(State)

    using QObject::QObject;
    ~ZapretBackend() override = default;

    virtual void refresh() = 0; // асинхронно, результат придёт в сигналах
    virtual void verifyState() = 0; // отдельная проверка для критических переходов состояния
    virtual void verifyState(quint64 requestId) { Q_UNUSED(requestId); verifyState(); } // коррелированный запрос для lifecycle-операций
    virtual void start() = 0;
    virtual void start(quint64 requestId) { Q_UNUSED(requestId); start(); }
    virtual void stop() = 0;
    virtual void stop(quint64 requestId) { Q_UNUSED(requestId); stop(); }
    virtual void setBootEnabled(bool enabled) = 0; // автозапуск сервиса при загрузке системы
    // TODO: applyProfile(const Profile &) — запись конфига через привилегированный хелпер

signals:
    void stateChanged(ZapretBackend::State state);
    void stateVerified(ZapretBackend::State state);
    void stateVerifiedForRequest(quint64 requestId, ZapretBackend::State state);
    void unitActionFinished(const QString &action, ZapretBackend::State state, bool success);
    void unitActionFinishedForRequest(quint64 requestId, const QString &action,
                                      ZapretBackend::State state, bool success);
    void bootEnabledChanged(bool enabled);
    void errorOccurred(const QString &message);
};

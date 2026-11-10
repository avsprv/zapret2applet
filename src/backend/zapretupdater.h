#pragma once

#include <QObject>

class QProcess;

// Установка и обновление zapret2 через привилегированный helper; здесь только запуск и разбор вывода.
class ZapretUpdater : public QObject
{
    Q_OBJECT
public:
    explicit ZapretUpdater(QObject *parent = nullptr);

    bool isRunning() const { return m_proc != nullptr; }
    void start(const QString &tag);
    void startInstall(const QString &tag);

signals:
    void logLine(const QString &line);
    void finished(int exitCode);               // 0 — успех, 126 — диалог polkit закрыт, -1 — не запустился
    void failedToStart(const QString &message);

private:
    void startOperation(const QString &operation, const QString &tag);
    void drain();

    QProcess *m_proc = nullptr;
    QByteArray m_buf;
    bool m_discardingOversizedLine = false;
};

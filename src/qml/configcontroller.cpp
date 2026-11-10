#include "configcontroller.h"

#include <QCryptographicHash>
#include <QProcess>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QSaveFile>

#include "backend/helperpath.h"

ConfigController::ConfigController(QObject *parent)
    : QObject(parent)
{
}

QString ConfigController::configText() const { return m_configText; }

void ConfigController::setConfigText(const QString &text)
{
    if (m_configText == text)
        return;
    m_configText = text;
    emit configTextChanged();
}

QString ConfigController::status() const { return m_status; }
bool ConfigController::busy() const { return m_busy; }
bool ConfigController::loaded() const { return m_loaded; }

void ConfigController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void ConfigController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ConfigController::finishProcess(QProcess *process, const QString &operation,
                                     const QByteArray &input)
{
    m_process = process;
    connect(process, &QProcess::started, this, [process, input, operation] {
        if (operation == QLatin1String("save")) {
            process->write(input);
            process->closeWriteChannel();
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, operation, input](int code, QProcess::ExitStatus exitStatus) {
        if (m_process != process)
            return;
        const QByteArray out = process->readAllStandardOutput();
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        const bool ok = exitStatus == QProcess::NormalExit && code == 0;
        if (!ok) {
            setStatus(error.isEmpty()
                ? tr("Операция не выполнена (код %1).").arg(code) : error);
            return;
        }
        if (operation == QLatin1String("read")) {
            const QString decoded = QString::fromUtf8(out);
            if (decoded.toUtf8() != out) {
                setStatus(tr("Файл конфигурации содержит некорректную последовательность UTF-8; чтение отменено во избежание потери данных."));
                return;
            }
            m_originalBytes = out;
            setConfigText(decoded);
            if (!m_loaded) {
                m_loaded = true;
                emit loadedChanged();
            }
            setStatus(tr("Конфигурация загружена. Перед сохранением будет проверен синтаксис и создана резервная копия."));
        } else if (operation == QLatin1String("save")) {
            m_originalBytes = input;
            setStatus(QString::fromUtf8(out).trimmed());
        } else if (operation == QLatin1String("rollback")) {
            setStatus(QString::fromUtf8(out).trimmed());
            read();
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process)
            return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        setStatus(tr("Не удалось запустить привилегированный helper: %1").arg(reason));
    });
}

void ConfigController::read()
{
    if (m_busy)
        return;
    setBusy(true);
    setStatus(tr("Чтение /opt/zapret2/config…"));
    auto *process = new QProcess(this);
    finishProcess(process, QStringLiteral("read"));
    Helper::start(process, {QStringLiteral("config-read")});
}

void ConfigController::validate()
{
    if (!m_loaded || m_busy)
        return;
    const QByteArray bytes = m_configText.toUtf8();
    if (bytes.size() > 1024 * 1024) {
        setStatus(tr("Конфигурация превышает ограничение 1 MiB."));
        return;
    }
    QTemporaryFile staged;
    if (!staged.open() || staged.write(bytes) != bytes.size() || !staged.flush()) {
        setStatus(tr("Не удалось подготовить файл для проверки: %1").arg(staged.errorString()));
        return;
    }
    QProcess validator;
    validator.start(QStringLiteral("/bin/sh"), {QStringLiteral("-n"), staged.fileName()});
    if (!validator.waitForStarted(3000) || !validator.waitForFinished(10000)) {
        validator.kill();
        validator.waitForFinished();
        setStatus(tr("Проверка shell-синтаксиса не завершилась."));
    } else if (validator.exitStatus() == QProcess::NormalExit && validator.exitCode() == 0) {
        setStatus(tr("Синтаксис корректен. Файл не исполнялся; системная конфигурация не изменена."));
    } else {
        setStatus(tr("Ошибка shell-синтаксиса: %1").arg(QString::fromUtf8(validator.readAllStandardError()).trimmed()));
    }
}

void ConfigController::save()
{
    if (!m_loaded || m_busy)
        return;
    const QByteArray bytes = m_configText.toUtf8();
    if (bytes.size() > 1024 * 1024) {
        setStatus(tr("Конфигурация превышает ограничение 1 MiB."));
        return;
    }
    QTemporaryFile staged;
    if (!staged.open() || staged.write(bytes) != bytes.size() || !staged.flush()) {
        setStatus(tr("Не удалось подготовить файл для проверки: %1").arg(staged.errorString()));
        return;
    }
    QProcess validator;
    validator.start(QStringLiteral("/bin/sh"), {QStringLiteral("-n"), staged.fileName()});
    if (!validator.waitForStarted(3000) || !validator.waitForFinished(10000)
        || validator.exitStatus() != QProcess::NormalExit || validator.exitCode() != 0) {
        setStatus(tr("Сохранение отменено: shell-синтаксис не прошёл проверку."));
        return;
    }
    const QString expectedHash = QString::fromLatin1(
        QCryptographicHash::hash(m_originalBytes, QCryptographicHash::Sha256).toHex());
    setBusy(true);
    setStatus(tr("Helper повторно проверит синтаксис и контрольную сумму, создаст резервную копию и атомарно сохранит файл…"));
    auto *process = new QProcess(this);
    finishProcess(process, QStringLiteral("save"), bytes);
    Helper::start(process, {QStringLiteral("config-write"), expectedHash});
}


bool ConfigController::exportConfig(const QUrl &url)
{
    if (!m_loaded) {
        setStatus(tr("Экспорт доступен после чтения конфигурации."));
        return false;
    }
    if (m_busy)
        return false;
    if (!url.isLocalFile()) {
        setStatus(tr("Не удалось сохранить копию: недопустимый путь к локальному файлу."));
        return false;
    }

    const QString path = url.toLocalFile();
    const QByteArray bytes = m_configText.toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size()
        || !file.commit()) {
        setStatus(tr("Не удалось сохранить копию: %1").arg(file.errorString()));
        return false;
    }

    setStatus(tr("Копия конфигурации сохранена: %1").arg(QFileInfo(path).fileName()));
    return true;
}

void ConfigController::rollback()
{
    if (!m_loaded || m_busy)
        return;
    setBusy(true);
    setStatus(tr("Восстановление последней резервной копии…"));
    auto *process = new QProcess(this);
    finishProcess(process, QStringLiteral("rollback"));
    Helper::start(process, {QStringLiteral("config-rollback")});
}

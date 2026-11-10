#include "zapretupdater.h"

#include <QProcess>
#include <QRegularExpression>

#include "helperpath.h"

ZapretUpdater::ZapretUpdater(QObject *parent)
    : QObject(parent)
{
}

void ZapretUpdater::start(const QString &tag)
{
    startOperation(QStringLiteral("update"), tag);
}

void ZapretUpdater::startInstall(const QString &tag)
{
    startOperation(QStringLiteral("install"), tag);
}

void ZapretUpdater::startOperation(const QString &operation, const QString &tag)
{
    if (m_proc)
        return;
    static const QRegularExpression valid(QStringLiteral(R"(^v\d+(\.\d+)*$)"));
    if (!valid.match(tag).hasMatch()) {
        emit failedToStart(tr("Некорректная версия: %1").arg(tag));
        emit finished(-1);
        return;
    }

    m_buf.clear();
    m_discardingOversizedLine = false;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &ZapretUpdater::drain);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        drain();
        if (!m_discardingOversizedLine && !m_buf.isEmpty())
            emit logLine(QString::fromUtf8(m_buf.left(64 * 1024)));
        m_buf.clear();
        m_discardingOversizedLine = false;
        QProcess *p = m_proc;
        m_proc = nullptr;
        p->deleteLater();
        emit finished(st == QProcess::NormalExit ? code : -1);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        const QString msg = tr("Не удалось запустить pkexec/хелпер: %1").arg(m_proc->errorString());
        QProcess *p = m_proc;
        m_proc = nullptr;
        p->deleteLater();
        emit failedToStart(msg);
        emit finished(-1);
    });
    Helper::start(m_proc, {operation, tag});
}

void ZapretUpdater::drain()
{
    if (!m_proc)
        return;

    constexpr qsizetype maxLineBytes = 64 * 1024;
    const QByteArray truncationMarker = tr("… [строка журнала сокращена]").toUtf8();
    m_buf += m_proc->readAllStandardOutput();

    while (true) {
        const qsizetype newline = m_buf.indexOf('\n');
        if (m_discardingOversizedLine) {
            if (newline < 0) {
                m_buf.clear();
                return;
            }
            m_buf.remove(0, newline + 1);
            m_discardingOversizedLine = false;
            continue;
        }

        const qsizetype lineLength = newline >= 0 ? newline : m_buf.size();
        if (lineLength > maxLineBytes) {
            QByteArray bounded = m_buf.left(maxLineBytes);
            bounded += truncationMarker;
            emit logLine(QString::fromUtf8(bounded));
            if (newline >= 0) {
                m_buf.remove(0, newline + 1);
                continue;
            }
            m_buf.clear();
            m_discardingOversizedLine = true;
            return;
        }

        if (newline < 0)
            return;

        QString line = QString::fromUtf8(m_buf.left(newline));
        m_buf.remove(0, newline + 1);
        if (line.endsWith(u'\r'))
            line.chop(1);
        emit logLine(line);
    }
}

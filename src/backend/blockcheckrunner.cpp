#include "blockcheckrunner.h"

#include <QRegularExpression>

#include "helperpath.h"

BlockcheckRunner::BlockcheckRunner(QObject *parent)
    : QObject(parent)
{
}

std::optional<BlockcheckRunner::Result> BlockcheckRunner::parseLine(const QString &line)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^!!!!! (\S+): working strategy found for ipv([46]) (\S+) : (\S+) (.+?) !!!!!$)"));
    const auto m = re.match(line.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    return Result{m.captured(1), m.captured(2), m.captured(3), m.captured(4), m.captured(5).trimmed()};
}

bool BlockcheckRunner::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void BlockcheckRunner::start(const Options &o)
{
    if (isRunning())
        return;

    m_buf.clear();
    m_discardingOversizedLine = false;
    m_seenResults.clear();
    m_errorCount = 0;
    m_cancelRequested = false;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::readyRead, this, &BlockcheckRunner::onReadyRead);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        onReadyRead();
        if (!m_discardingOversizedLine && !m_buf.isEmpty())
            handleLine(QString::fromUtf8(m_buf.left(64 * 1024)));
        m_buf.clear();
        m_discardingOversizedLine = false;
        const bool canceled = m_cancelRequested || Helper::isAuthDismissed(code);
        if (!canceled && (st == QProcess::CrashExit || code != 0) && m_errorCount == 0)
            ++m_errorCount;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit finished(st == QProcess::CrashExit ? -1 : code, canceled);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        ++m_errorCount;
        emit logLine(tr("Не удалось запустить хелпер: %1").arg(m_proc->errorString()));
        m_proc->deleteLater();
        m_proc = nullptr;
        emit finished(-1, false);
    });

    const auto flag = [](bool b) { return b ? QStringLiteral("1") : QStringLiteral("0"); };
    Helper::start(m_proc, {QStringLiteral("blockcheck"),
                           QStringLiteral("--domains"), o.domains.join(QLatin1Char(' ')),
                           QStringLiteral("--ipvs"), o.ipvs,
                           QStringLiteral("--http"), flag(o.http),
                           QStringLiteral("--tls12"), flag(o.tls12),
                           QStringLiteral("--tls13"), flag(o.tls13),
                           QStringLiteral("--http3"), flag(o.http3),
                           QStringLiteral("--repeats"), QString::number(o.repeats),
                           QStringLiteral("--scanlevel"), o.scanLevel});
}

void BlockcheckRunner::cancel()
{
    if (!isRunning())
        return;
    m_cancelRequested = true;
    // Процесс принадлежит root, сигнал ему можно послать только через хелпер.
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
    Helper::start(proc, {QStringLiteral("blockcheck-cancel")});
}

void BlockcheckRunner::onReadyRead()
{
    if (!m_proc)
        return;
    constexpr qsizetype maxPendingLineBytes = 64 * 1024;
    static const QByteArray truncationMarker = QByteArrayLiteral("… [строка журнала сокращена]");
    m_buf += m_proc->readAll();

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
        if (lineLength > maxPendingLineBytes) {
            QByteArray bounded = m_buf.left(maxPendingLineBytes);
            bounded += truncationMarker;
            handleLine(QString::fromUtf8(bounded));

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

        handleLine(QString::fromUtf8(m_buf.left(newline)));
        m_buf.remove(0, newline + 1);
    }
}

void BlockcheckRunner::handleLine(const QString &rawLine)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    QString line = rawLine;
    line.remove(ansi);
    line.remove(QLatin1Char('\r'));
    emit logLine(line);
    if (line.startsWith(QStringLiteral("zapret2control-helper:")))
        ++m_errorCount;
    if (const auto r = parseLine(line)) {
        const QString key = QStringList{r->test, r->ipv, r->domain, r->daemon, r->args}
                                .join(QChar(0));
        if (!m_seenResults.contains(key)) {
            m_seenResults.insert(key);
            emit strategyFound(*r);
        }
    }
}

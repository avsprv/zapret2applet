#include "listscontroller.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QRegularExpression>

#include "backend/helperpath.h"

namespace {
const QStringList allowedNames {
    QStringLiteral("zapret-hosts-user.txt"),
    QStringLiteral("zapret-hosts-user-exclude.txt"),
    QStringLiteral("zapret-hosts-user-ipban.txt"),
    QStringLiteral("zapret-ip-user.txt"),
    QStringLiteral("zapret-ip-user6.txt"),
    QStringLiteral("zapret-ip-exclude.txt"),
    QStringLiteral("zapret-ip-exclude6.txt"),
    QStringLiteral("zapret-ip-user-ipban.txt"),
    QStringLiteral("zapret-ip-user-ipban6.txt")
};
const QStringList editableNames {
    QStringLiteral("zapret-hosts-user.txt"),
    QStringLiteral("zapret-hosts-user-exclude.txt"),
    QStringLiteral("zapret-hosts-user-ipban.txt")
};
}

ListsController::ListsController(QObject *parent) : QObject(parent) {}
QString ListsController::content() const { return m_content; }
QString ListsController::status() const { return m_status; }
QString ListsController::loadedName() const { return m_loadedName; }
bool ListsController::busy() const { return m_busy; }
bool ListsController::loaded() const { return m_loaded; }
bool ListsController::dirty() const { return m_dirty; }
bool ListsController::editable() const { return m_editable; }

bool ListsController::isAllowedName(const QString &name) const { return allowedNames.contains(name); }
bool ListsController::isEditableName(const QString &name) const { return editableNames.contains(name); }

void ListsController::setStatus(const QString &value)
{
    if (m_status == value) return;
    m_status = value;
    emit statusChanged();
}
void ListsController::setBusy(bool value)
{
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}
void ListsController::setLoaded(bool value)
{
    if (m_loaded == value) return;
    m_loaded = value;
    emit loadedChanged();
}
void ListsController::setDirty(bool value)
{
    if (m_dirty == value) return;
    m_dirty = value;
    emit dirtyChanged();
}
void ListsController::setContentInternal(const QString &value)
{
    if (m_content == value) return;
    m_content = value;
    emit contentChanged();
}
void ListsController::setLoadedName(const QString &value)
{
    if (m_loadedName == value) return;
    m_loadedName = value;
    emit loadedNameChanged();
}
void ListsController::updateEditable()
{
    const bool value = m_loaded && isEditableName(m_loadedName);
    if (m_editable == value) return;
    m_editable = value;
    emit editableChanged();
}

QString ListsController::validateHostlist(const QString &value) const
{
    const QByteArray bytes = value.toUtf8();
    if (bytes.size() > 1024 * 1024)
        return tr("Файл превышает ограничение 1 MiB.");
    QStringList lines = value.split(QLatin1Char('\n'));
    if (value.endsWith(QLatin1Char('\n')) && !lines.isEmpty())
        lines.removeLast();
    if (lines.size() > 50000)
        return tr("Слишком много строк (максимум 50000).");
    static const QRegularExpression labelPattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression numericPattern(QStringLiteral("^[0-9.]+$"));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString raw = lines.at(i);
        if (raw.toUtf8().size() > 4095)
            return tr("Строка %1 длиннее 4095 байт.").arg(i + 1);
        for (const QChar ch : raw) {
            if (ch.category() == QChar::Other_Control && ch != QLatin1Char('\r'))
                return tr("Строка %1 содержит управляющий символ.").arg(i + 1);
        }
        QString line = raw;
        while (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')))
            line.remove(0, 1);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
            line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('/')))
            continue;
        while (line.startsWith(QLatin1Char('^')) || line.startsWith(QLatin1Char('!')))
            line.remove(0, 1);
        const qsizetype tokenEnd = line.indexOf(QRegularExpression(QStringLiteral("[\\s]")));
        const QString token = tokenEnd < 0 ? line : line.left(tokenEnd);
        if (token.isEmpty())
            return tr("Строка %1 содержит флаги без записи.").arg(i + 1);
        if (QString::fromLatin1(token.toLatin1()) != token)
            return tr("Строка %1: используйте ASCII-домен или punycode.").arg(i + 1);
        QHostAddress address;
        if (address.setAddress(token))
            continue;
        const auto subnet = QHostAddress::parseSubnet(token);
        if (!subnet.first.isNull() && subnet.second >= 0 &&
            subnet.second <= (subnet.first.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128))
            continue;
        if (token.contains(QLatin1Char('/')) || token.contains(QLatin1Char(':')) ||
            numericPattern.match(token).hasMatch())
            return tr("Некорректный IP/CIDR в строке %1: %2").arg(i + 1).arg(token);
        QString domain = token;
        if (domain.endsWith(QLatin1Char('.'))) domain.chop(1);
        if (domain.isEmpty() || domain.size() > 253)
            return tr("Домен в строке %1 имеет недопустимую длину.").arg(i + 1);
        for (const QString &label : domain.split(QLatin1Char('.'))) {
            if (label.isEmpty() || label.size() > 63 || !labelPattern.match(label).hasMatch() ||
                label.startsWith(QLatin1Char('-')) || label.endsWith(QLatin1Char('-')))
                return tr("Некорректный домен или IP/CIDR в строке %1: %2").arg(i + 1).arg(token);
        }
    }
    return {};
}

void ListsController::readList(const QString &name)
{
    if (m_busy) return;
    if (!isAllowedName(name)) {
        setStatus(tr("Недопустимое имя списка."));
        return;
    }
    m_output.clear();
    m_error.clear();
    setContentInternal(QString());
    setLoaded(false);
    setDirty(false);
    setLoadedName(QString());
    updateEditable();
    setBusy(true);
    setStatus(tr("Чтение списка %1…").arg(name));

    auto *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process != process) return;
        m_output.append(process->readAllStandardOutput());
        if (m_output.size() > 1024 * 1024) {
            setStatus(tr("Список превышает ограничение 1 MiB; чтение остановлено."));
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_process != process) return;
        m_error.append(process->readAllStandardError());
        if (m_error.size() > 64 * 1024) m_error = m_error.right(64 * 1024);
    });
    connect(process, &QProcess::finished, this,
            [this, process, name](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process != process) return;
        m_output.append(process->readAllStandardOutput());
        m_error.append(process->readAllStandardError());
        const QString error = QString::fromUtf8(m_error).trimmed();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        if (m_output.size() > 1024 * 1024) {
            setStatus(tr("Список превышает ограничение 1 MiB; данные не отображены."));
            return;
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 3 &&
            error.contains(QStringLiteral("list file does not exist"))) {
            setLoadedName(name);
            setContentInternal(QString());
            setLoaded(true);
            setDirty(false);
            updateEditable();
            setStatus(tr("Файл %1 ещё не создан. Можно добавить записи и сохранить его.").arg(name));
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            setStatus(error.isEmpty() ? tr("Не удалось прочитать список (код %1).").arg(exitCode)
                                      : tr("Не удалось прочитать список: %1").arg(error));
            return;
        }
        const QString decoded = QString::fromUtf8(m_output);
        if (decoded.toUtf8() != m_output) {
            setStatus(tr("Список содержит некорректный UTF-8; данные не отображены."));
            return;
        }
        setLoadedName(name);
        setContentInternal(decoded);
        setLoaded(true);
        setDirty(false);
        updateEditable();
        setStatus(tr("Список %1 прочитан.").arg(name));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process) return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        setStatus(tr("Не удалось запустить helper: %1").arg(reason));
    });
    Helper::start(process, {QStringLiteral("lists-read"), name});
}

void ListsController::setContent(const QString &value)
{
    if (m_busy || !m_loaded || !m_editable || m_content == value) return;
    setContentInternal(value);
    setDirty(true);
}

void ListsController::saveList()
{
    if (m_busy || !m_loaded || !m_dirty || !m_editable) return;
    const QString validation = validateHostlist(m_content);
    if (!validation.isEmpty()) {
        setStatus(validation);
        return;
    }
    m_output.clear();
    m_error.clear();
    setBusy(true);
    setStatus(tr("Проверка и сохранение списка %1…").arg(m_loadedName));

    auto *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::started, this, [process, data = m_content.toUtf8()] {
        process->write(data);
        process->closeWriteChannel();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process == process) {
            m_output.append(process->readAllStandardOutput());
            if (m_output.size() > 64 * 1024)
                m_output = m_output.right(64 * 1024);
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_process == process) {
            m_error.append(process->readAllStandardError());
            if (m_error.size() > 64 * 1024) m_error = m_error.right(64 * 1024);
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process != process) return;
        m_output.append(process->readAllStandardOutput());
        m_error.append(process->readAllStandardError());
        if (m_output.size() > 64 * 1024)
            m_output = m_output.right(64 * 1024);
        if (m_error.size() > 64 * 1024)
            m_error = m_error.right(64 * 1024);
        const QString output = QString::fromUtf8(m_output).trimmed();
        const QString error = QString::fromUtf8(m_error).trimmed();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            setDirty(false);
            setStatus(output.isEmpty() ? tr("Список сохранён; резервная копия создана.") : output);
        } else {
            setStatus(error.isEmpty() ? tr("Не удалось сохранить список (код %1).").arg(exitCode)
                                      : tr("Не удалось сохранить список: %1").arg(error));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process) return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        setStatus(tr("Не удалось запустить helper: %1").arg(reason));
    });
    Helper::start(process, {QStringLiteral("lists-write"), m_loadedName});
}

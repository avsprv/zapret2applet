#include "profilecontroller.h"

#include <QCryptographicHash>
#include <QProcess>
#include "backend/helperpath.h"

ProfileController::ProfileController(QObject *parent) : QObject(parent) {}
QVariantList ProfileController::profiles() const
{
    QVariantList result;
    for (int i = 0; i < m_profiles.size(); ++i) {
        const auto &p = m_profiles.at(i);
        result.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), tr("Профиль %1").arg(i + 1)},
            {QStringLiteral("arguments"), p.allArguments},
            {QStringLiteral("filters"), p.filters.size()},
            {QStringLiteral("payloads"), p.payloads.size()},
            {QStringLiteral("luaDesync"), p.luaDesync.size()},
            {QStringLiteral("other"), p.other.size()}
        });
    }
    return result;
}
int ProfileController::selectedIndex() const { return m_selectedIndex; }
void ProfileController::setSelectedIndex(int index)
{
    if (index < -1 || index >= m_profiles.size() || index == m_selectedIndex) return;
    m_selectedIndex = index;
    emit selectedIndexChanged();
}
QString ProfileController::status() const { return m_status; }
bool ProfileController::busy() const { return m_busy; }
bool ProfileController::loaded() const { return m_loaded; }
bool ProfileController::dirty() const { return m_dirty; }
void ProfileController::setStatus(const QString &v) { if (m_status == v) return; m_status = v; emit statusChanged(); }
void ProfileController::setBusy(bool v) { if (m_busy == v) return; m_busy = v; emit busyChanged(); }
void ProfileController::setDirty(bool v) { if (m_dirty == v) return; m_dirty = v; emit dirtyChanged(); }
void ProfileController::setLoaded(bool v) { if (m_loaded == v) return; m_loaded = v; emit loadedChanged(); }
bool ProfileController::validArguments(const QStringList &args, QString *error) const
{
    if (args.isEmpty()) { *error = tr("Профиль должен содержать хотя бы один аргумент."); return false; }
    if (args.size() > 4096) { *error = tr("Слишком много аргументов."); return false; }
    for (const QString &arg : args) {
        if (arg.isEmpty() || arg == QStringLiteral("--new")) { *error = tr("Пустой аргумент или зарезервированный разделитель --new."); return false; }
        for (const QChar ch : arg) {
            if (ch.isSpace() || ch.category() == QChar::Other_Control || ch == QLatin1Char('"')
                || ch == QLatin1Char('$') || ch == QChar(96) || ch == QLatin1Char('\\')) {
                *error = tr("Аргумент содержит пробелы или небезопасные shell-символы."); return false;
            }
        }
    }
    return true;
}
void ProfileController::read(bool discardChanges)
{
    if (m_busy) return;
    if (m_dirty && !discardChanges) { setStatus(tr("Есть несохранённые изменения. Сохраните их или подтвердите отмену.")); return; }
    m_output.clear(); m_error.clear();
    setBusy(true); setStatus(tr("Чтение конфигурации…"));
    auto *p = new QProcess(this); m_process = p;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p] {
        if (m_process != p) return;
        m_output.append(p->readAllStandardOutput());
        if (m_output.size() > 1024 * 1024)
            p->kill();
    });
    connect(p, &QProcess::readyReadStandardError, this, [this, p] {
        if (m_process != p) return;
        m_error.append(p->readAllStandardError());
        if (m_error.size() > 64 * 1024)
            m_error = m_error.right(64 * 1024);
    });
    connect(p, &QProcess::finished, this, [this,p](int code, QProcess::ExitStatus state) {
        if (m_process != p) return;
        m_output.append(p->readAllStandardOutput());
        m_error.append(p->readAllStandardError());
        if (m_error.size() > 64 * 1024)
            m_error = m_error.right(64 * 1024);
        const QByteArray bytes = m_output;
        const QString err = QString::fromUtf8(m_error).trimmed();
        m_process = nullptr; p->deleteLater(); setBusy(false);
        if (bytes.size() > 1024 * 1024) { setStatus(tr("Конфигурация превышает 1 MiB.")); return; }
        if (state != QProcess::NormalExit || code != 0) { setStatus(err.isEmpty() ? tr("Не удалось прочитать конфигурацию (код %1).").arg(code) : err); return; }
        const QString decoded = QString::fromUtf8(bytes);
        if (decoded.toUtf8() != bytes) { setStatus(tr("Конфигурация содержит некорректный UTF-8; анализ отменён.")); return; }
        const auto parsed = ProfileAnalyzer::parse(decoded);
        if (!parsed.ok()) { setStatus(parsed.error); return; }
        m_configBytes = bytes; m_config = decoded; m_profiles = parsed.profiles;
        setSelectedIndex(-1); setLoaded(true); setDirty(false);
        emit profilesChanged(); setStatus(tr("Загружено профилей: %1. Конфигурация не исполнялась.").arg(m_profiles.size()));
    });
    connect(p, &QProcess::errorOccurred, this, [this,p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || m_process != p) return;
        const QString reason=p->errorString(); m_process=nullptr; p->deleteLater(); setBusy(false);
        setStatus(tr("Не удалось запустить helper: %1").arg(reason));
    });
    Helper::start(p, {QStringLiteral("config-read")});
}
void ProfileController::add(const QStringList &args)
{
    if (!m_loaded || m_busy || m_profiles.size() >= 128) return;
    QString error; if (!validArguments(args,&error)) { setStatus(error); return; }
    auto candidate=m_profiles; ProfileAnalyzer::Profile p; p.allArguments=args;
    for (const auto &a:args) {
        if (a.startsWith("--filter-")) p.filters.append(a);
        else if (a.startsWith("--payload=")) p.payloads.append(a);
        else if (a.startsWith("--lua-desync=")) p.luaDesync.append(a);
        else p.other.append(a);
    }
    candidate.append(p);
    if (!ProfileAnalyzer::replaceProfiles(m_config,candidate).ok()) { setStatus(tr("Изменения не прошли проверку анализатора.")); return; }
    m_profiles=candidate; setSelectedIndex(m_profiles.size()-1); setDirty(true); emit profilesChanged();
    setStatus(tr("Профиль добавлен. Изменения ещё не сохранены."));
}
void ProfileController::edit(int index, const QStringList &args)
{
    if (!m_loaded || m_busy || index<0 || index>=m_profiles.size()) return;
    QString error; if (!validArguments(args,&error)) { setStatus(error); return; }
    auto candidate=m_profiles; auto &p=candidate[index]; p=ProfileAnalyzer::Profile{}; p.allArguments=args;
    for (const auto &a:args) {
        if (a.startsWith("--filter-")) p.filters.append(a);
        else if (a.startsWith("--payload=")) p.payloads.append(a);
        else if (a.startsWith("--lua-desync=")) p.luaDesync.append(a);
        else p.other.append(a);
    }
    if (!ProfileAnalyzer::replaceProfiles(m_config,candidate).ok()) { setStatus(tr("Изменения не прошли проверку анализатора.")); return; }
    m_profiles=candidate; setSelectedIndex(index); setDirty(true); emit profilesChanged();
    setStatus(tr("Профиль изменён. Изменения ещё не сохранены."));
}
void ProfileController::copy(int index)
{
    if (!m_loaded || m_busy || index<0 || index>=m_profiles.size() || m_profiles.size()>=128) return;
    m_profiles.insert(index+1,m_profiles.at(index)); setSelectedIndex(index+1); setDirty(true); emit profilesChanged();
    setStatus(tr("Профиль скопирован. Изменения ещё не сохранены."));
}
void ProfileController::remove(int index)
{
    if (!m_loaded || m_busy || index<0 || index>=m_profiles.size() || m_profiles.size()<=1) return;
    const int previousSelection = m_selectedIndex;
    m_profiles.removeAt(index);

    int nextSelection = previousSelection;
    if (previousSelection == index)
        nextSelection = qMin(index, m_profiles.size() - 1);
    else if (previousSelection > index)
        nextSelection = previousSelection - 1;

    setSelectedIndex(nextSelection);
    setDirty(true);
    emit profilesChanged();
    setStatus(tr("Профиль удалён. Изменения ещё не сохранены."));
}
void ProfileController::save()
{
    if (!m_loaded || m_busy || !m_dirty) return;
    const auto edited=ProfileAnalyzer::replaceProfiles(m_config,m_profiles);
    if (!edited.ok()) { setStatus(edited.error); return; }
    if (edited.config.toUtf8().size()>1024*1024) { setStatus(tr("Конфигурация превышает 1 MiB.")); return; }
    startWrite(edited.config);
}
void ProfileController::startWrite(const QString &content)
{
    const QByteArray bytes=content.toUtf8();
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(m_configBytes,QCryptographicHash::Sha256).toHex());
    m_output.clear(); m_error.clear();
    setBusy(true); setStatus(tr("Проверка и безопасная запись профилей…"));
    auto *p=new QProcess(this); m_process=p;
    connect(p,&QProcess::started,this,[p,bytes]{ p->write(bytes); p->closeWriteChannel(); });
    connect(p,&QProcess::readyReadStandardOutput,this,[this,p]{
        if (m_process!=p) return;
        m_output.append(p->readAllStandardOutput());
        if (m_output.size()>64*1024) m_output=m_output.right(64*1024);
    });
    connect(p,&QProcess::readyReadStandardError,this,[this,p]{
        if (m_process!=p) return;
        m_error.append(p->readAllStandardError());
        if (m_error.size()>64*1024) m_error=m_error.right(64*1024);
    });
    connect(p,&QProcess::finished,this,[this,p,content,bytes](int code,QProcess::ExitStatus state){
        if (m_process!=p) return;
        m_output.append(p->readAllStandardOutput());
        m_error.append(p->readAllStandardError());
        if (m_output.size()>64*1024) m_output=m_output.right(64*1024);
        if (m_error.size()>64*1024) m_error=m_error.right(64*1024);
        const QString out=QString::fromUtf8(m_output).trimmed();
        const QString err=QString::fromUtf8(m_error).trimmed();
        m_process=nullptr; p->deleteLater(); setBusy(false);
        if (state!=QProcess::NormalExit || code!=0) { setStatus(err.isEmpty()?tr("Запись не выполнена (код %1).").arg(code):err); return; }
        m_config=content; m_configBytes=bytes; setDirty(false); emit profilesChanged();
        setStatus(out.isEmpty()?tr("Профили сохранены. Резервная копия создана; служба не перезапускалась."):out);
    });
    connect(p,&QProcess::errorOccurred,this,[this,p](QProcess::ProcessError e){
        if(e!=QProcess::FailedToStart || m_process!=p) return;
        const QString reason=p->errorString(); m_process=nullptr; p->deleteLater(); setBusy(false);
        setStatus(tr("Не удалось запустить helper: %1").arg(reason));
    });
    Helper::start(p,{QStringLiteral("config-write"),hash});
}

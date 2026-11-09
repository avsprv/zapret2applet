#include "blockcheckjobcontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "backend/helperpath.h"

namespace {
QString flag(bool value) { return value ? QStringLiteral("1") : QStringLiteral("0"); }
constexpr qsizetype MaxOutput = 1024 * 1024;
}

BlockcheckJobController::BlockcheckJobController(QObject *parent)
    : QObject(parent), m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &BlockcheckJobController::poll);
    m_probePending = true;
    setBusy(true);
    setStatus(tr("Проверка состояния восстановления Blockcheck…"));
    launch(ProcessTask::Probe, {QStringLiteral("blockcheck-recovery-probe")});
}

QString BlockcheckJobController::domains() const { return m_domains; }
void BlockcheckJobController::setDomains(const QString &v) { if (m_domains != v) { m_domains=v; emit optionsChanged(); } }
QString BlockcheckJobController::ipVersion() const { return m_ipVersion; }
void BlockcheckJobController::setIpVersion(const QString &v) { if (m_ipVersion != v) { m_ipVersion=v; emit optionsChanged(); } }
bool BlockcheckJobController::http() const { return m_http; }
void BlockcheckJobController::setHttp(bool v) { if (m_http != v) { m_http=v; emit optionsChanged(); } }
bool BlockcheckJobController::tls12() const { return m_tls12; }
void BlockcheckJobController::setTls12(bool v) { if (m_tls12 != v) { m_tls12=v; emit optionsChanged(); } }
bool BlockcheckJobController::tls13() const { return m_tls13; }
void BlockcheckJobController::setTls13(bool v) { if (m_tls13 != v) { m_tls13=v; emit optionsChanged(); } }
bool BlockcheckJobController::http3() const { return m_http3; }
void BlockcheckJobController::setHttp3(bool v) { if (m_http3 != v) { m_http3=v; emit optionsChanged(); } }
int BlockcheckJobController::repeats() const { return m_repeats; }
void BlockcheckJobController::setRepeats(int v) { v=qBound(1,v,10); if (m_repeats != v) { m_repeats=v; emit optionsChanged(); } }
QString BlockcheckJobController::scanLevel() const { return m_scanLevel; }
void BlockcheckJobController::setScanLevel(const QString &v) { if (m_scanLevel != v) { m_scanLevel=v; emit optionsChanged(); } }
QString BlockcheckJobController::logText() const { return m_logText; }
QVariantList BlockcheckJobController::results() const { return m_results; }
QString BlockcheckJobController::status() const { return m_status; }
QString BlockcheckJobController::statusSeverity() const {
    switch (m_statusType) {
    case StatusType::Success: return QStringLiteral("Success");
    case StatusType::Warning: return QStringLiteral("Warning");
    case StatusType::Error: return QStringLiteral("Error");
    case StatusType::Information: return QStringLiteral("Information");
    }
    return QStringLiteral("Information");
}
bool BlockcheckJobController::busy() const { return m_busy; }
bool BlockcheckJobController::cancelAvailable() const { return m_busy && !m_probePending; }
bool BlockcheckJobController::recoveryRequired() const { return m_recoveryRequired; }
bool BlockcheckJobController::recoveryRetryAvailable() const { return m_recoveryRequired && !m_busy; }
bool BlockcheckJobController::updateBusy() const { return m_updateBusy; }
void BlockcheckJobController::setUpdateBusy(bool v) { if (m_updateBusy != v) { m_updateBusy=v; emit updateBusyChanged(); } }
void BlockcheckJobController::setServiceBusy(bool v) { m_serviceBusy=v; }

void BlockcheckJobController::setStatus(const QString &v, StatusType type)
{
    if (m_status == v && m_statusType == type) return;
    m_status=v; m_statusType=type; emit statusChanged();
}
void BlockcheckJobController::setBusy(bool v)
{
    if (m_busy == v) return;
    m_busy=v; emit busyChanged(); emit recoveryRetryAvailableChanged();
}
void BlockcheckJobController::setRecoveryRequired(bool v)
{
    if (m_recoveryRequired == v) return;
    m_recoveryRequired=v; emit recoveryRequiredChanged(); emit recoveryRetryAvailableChanged();
}
void BlockcheckJobController::appendLog(const QString &line)
{
    QString bounded=line;
    if (bounded.size()>64*1024) bounded=bounded.left(64*1024)+QStringLiteral("… [строка журнала сокращена]");
    m_logText += (m_logText.isEmpty()?QString():QStringLiteral("\n"))+bounded;
    if (m_logText.size()>MaxOutput) m_logText.remove(0,m_logText.size()-MaxOutput);
    emit logTextChanged();
}

bool BlockcheckJobController::validateOptions(QStringList *domainsOut) const
{
    const QStringList domains=m_domains.simplified().split(QLatin1Char(' '),Qt::SkipEmptyParts);
    static const QRegularExpression labelRe(QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    if(domains.isEmpty()||domains.size()>10) return false;
    for(QString domain:domains){
        if(domain.endsWith(QLatin1Char('.'))) domain.chop(1);
        if(domain.isEmpty()||domain.size()>253) return false;
        for(const QString &label:domain.split(QLatin1Char('.')))
            if(label.size()>63||!labelRe.match(label).hasMatch()) return false;
    }
    if(m_ipVersion!=QLatin1String("4")&&m_ipVersion!=QLatin1String("6")&&m_ipVersion!=QLatin1String("46")) return false;
    if(!m_http&&!m_tls12&&!m_tls13&&!m_http3) return false;
    if(m_scanLevel!=QLatin1String("veryquick")&&m_scanLevel!=QLatin1String("quick")&&m_scanLevel!=QLatin1String("standard")&&m_scanLevel!=QLatin1String("force")) return false;
    if(domainsOut) *domainsOut=domains;
    return true;
}

void BlockcheckJobController::start()
{
    if(m_busy||m_updateBusy||m_serviceBusy){setStatus(tr("Подбор недоступен во время другой операции."),StatusType::Warning);return;}
    QStringList domains;
    if(!validateOptions(&domains)){setStatus(tr("Проверьте домены и параметры диагностики."),StatusType::Error);return;}
    m_results.clear();emit resultsChanged();m_logText.clear();emit logTextChanged();
    m_operationId.clear();m_terminalPhase.clear();m_cancelRequested=false;m_finalLogsPending=false;m_found=0;m_pollTicks=0;
    setBusy(true);
    setStatus(tr("Диагностика может временно остановить службу zapret2. После завершения systemd попытается восстановить её работу."),StatusType::Information);
    emit confirmationRequired(tr("Запуск диагностики Blockcheck"),tr("Для проверки может потребоваться временная остановка zapret2. Продолжить?"));
}

void BlockcheckJobController::confirmStop(bool accepted)
{
    if(!m_busy||!m_operationId.isEmpty()||m_task!=ProcessTask::None) return;
    if(!accepted){finish(false,true);return;}
    QStringList domains;
    if(!validateOptions(&domains)){finish(false);return;}
    m_startedAtSecs=QDateTime::currentSecsSinceEpoch();
    launch(ProcessTask::Submit,{
        QStringLiteral("blockcheck-submit"),QStringLiteral("--domains"),domains.join(QLatin1Char(' ')),
        QStringLiteral("--ipvs"),m_ipVersion,QStringLiteral("--http"),flag(m_http),
        QStringLiteral("--tls12"),flag(m_tls12),QStringLiteral("--tls13"),flag(m_tls13),
        QStringLiteral("--http3"),flag(m_http3),QStringLiteral("--repeats"),QString::number(m_repeats),
        QStringLiteral("--scanlevel"),m_scanLevel
    });
    setStatus(tr("Отправка задания в systemd…"));
}

void BlockcheckJobController::launch(ProcessTask task,const QStringList &arguments)
{
    if(m_process) return;
    m_task=task;m_stdout.clear();m_stderr.clear();
    auto *process=new QProcess(this);m_process=process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process]{
        if(m_process!=process)return;
        m_stdout+=process->readAllStandardOutput();
        if(m_stdout.size()>MaxOutput){process->kill();setStatus(tr("Вывод helper превысил лимит 1 MiB."),StatusType::Error);}
    });
    connect(process,&QProcess::readyReadStandardError,this,[this,process]{
        if(m_process!=process)return;
        m_stderr+=process->readAllStandardError();
        if(m_stderr.size()>64*1024)m_stderr=m_stderr.right(64*1024);
    });
    connect(process,&QProcess::finished,this,[this,process,task](int code,QProcess::ExitStatus state){
        if(m_process!=process)return;
        handleProcessFinished(process,code,state);
        process->deleteLater();
        if (m_cancelRequested && !m_operationId.isEmpty()
            && m_terminalPhase.isEmpty() && m_task == ProcessTask::None)
            launch(ProcessTask::Cancel,{QStringLiteral("blockcheck-job-cancel")});
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart||m_process!=process)return;
        const QString reason=process->errorString();
        const ProcessTask failedTask=m_task;
        m_process=nullptr;m_task=ProcessTask::None;
        process->deleteLater();appendLog(tr("Не удалось запустить helper: %1").arg(reason));
        if(failedTask==ProcessTask::Probe){
            m_probePending=false;
            setRecoveryRequired(true);
            setBusy(false);
            setStatus(tr("Не удалось проверить состояние восстановления Blockcheck. Операции заблокированы."),StatusType::Error);
        }
        else if(failedTask==ProcessTask::Recovery){
            setBusy(false);
            setStatus(tr("Не удалось повторить восстановление службы. Маркер сохранён."),StatusType::Error);
        }
        else if(m_operationId.isEmpty())finish(false);
        else if(m_terminalPhase.isEmpty())setStatus(tr("Связь с заданием потеряна; состояние будет проверено повторно."),StatusType::Warning);
        else finish(m_terminalPhase==QLatin1String("completed"),m_cancelRequested&&m_terminalPhase!=QLatin1String("completed"));
    });
    Helper::start(process,arguments);
}

void BlockcheckJobController::handleProcessFinished(QProcess *process,int exitCode,QProcess::ExitStatus exitStatus)
{
    if(m_process!=process)return;
    m_stdout+=process->readAllStandardOutput();m_stderr+=process->readAllStandardError();
    const ProcessTask task=m_task;m_process=nullptr;m_task=ProcessTask::None;
    const QString error=QString::fromUtf8(m_stderr).trimmed();
    if(task==ProcessTask::Probe){ processProbe(m_stdout); return; }
    if(exitStatus!=QProcess::NormalExit||exitCode!=0){
        appendLog(error.isEmpty()?tr("Helper завершился с кодом %1.").arg(exitCode):error);
        if(task==ProcessTask::Probe){
            m_pollTimer->stop();
            m_probePending=false;
            setRecoveryRequired(true);
            setBusy(false);
            setStatus(tr("Не удалось проверить состояние восстановления Blockcheck. Операции заблокированы."),StatusType::Error);
        }
        else if(task==ProcessTask::Recovery){
            setBusy(false);
            setStatus(tr("Не удалось повторить восстановление службы. Маркер сохранён."),StatusType::Error);
        }
        else if(task==ProcessTask::Submit){
            if(error.contains(QStringLiteral("recovery is still pending"),Qt::CaseInsensitive)
                || error.contains(QStringLiteral("recovery marker"),Qt::CaseInsensitive)){
                static const QRegularExpression pendingIdRe(
                    QStringLiteral("operation_id=([0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})"));
                const auto match=pendingIdRe.match(error);
                if(match.hasMatch())m_operationId=match.captured(1);
                setRecoveryRequired(true);
                emit recoveryRetryAvailableChanged();
            }
            finish(false);
        }
        else if(task==ProcessTask::Cancel)setStatus(tr("Не удалось отправить запрос отмены; проверка состояния продолжается."),StatusType::Warning);
        else if(task==ProcessTask::Status)setStatus(tr("Не удалось получить состояние задания; повторная проверка будет выполнена."),StatusType::Warning);
        else if(task==ProcessTask::Logs&&m_terminalPhase.isEmpty())setStatus(tr("Не удалось обновить журнал Blockcheck."),StatusType::Warning);
        if(task==ProcessTask::Logs&&!m_terminalPhase.isEmpty())finish(m_terminalPhase==QLatin1String("completed"),m_cancelRequested&&m_terminalPhase!=QLatin1String("completed"));
        return;
    }
    if(task==ProcessTask::Recovery){
        m_terminalPhase.clear();
        m_operationId.clear();
        m_probePending=false;
        setStatus(tr("Команда восстановления выполнена; проверяется состояние Blockcheck…"),StatusType::Information);
        launch(ProcessTask::Probe, {QStringLiteral("blockcheck-recovery-probe")});
        return;
    }
    if(task==ProcessTask::Submit){
        const QString id=QString::fromUtf8(m_stdout).trimmed();
        static const QRegularExpression uuidRe(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
        if(!uuidRe.match(id).hasMatch()){appendLog(tr("Helper вернул некорректный идентификатор задания."));finish(false);return;}
        m_operationId=id;m_pollTicks=0;m_pollTimer->start();setStatus(tr("Задание принято systemd; ожидание запуска worker…"));
        return;
    }
    if(task==ProcessTask::Status)processStatus(m_stdout);
    else if(task==ProcessTask::Logs){processLogs(m_stdout);if(!m_terminalPhase.isEmpty())finish(m_terminalPhase==QLatin1String("completed"),m_cancelRequested&&m_terminalPhase!=QLatin1String("completed"));}
    else if(task==ProcessTask::Cancel)setStatus(tr("Запрос отмены отправлен. Ожидается завершение worker и восстановление службы."),StatusType::Warning);
}

void BlockcheckJobController::poll()
{
    if(m_process)return;
    if(m_probePending){launch(ProcessTask::Probe,{QStringLiteral("blockcheck-recovery-probe")});return;}
    if(m_operationId.isEmpty())return;
    ++m_pollTicks;
    if(!m_terminalPhase.isEmpty()){
        m_pollTimer->stop();launch(ProcessTask::Logs,{QStringLiteral("blockcheck-job-logs"),QStringLiteral("500"),QString::number(m_startedAtSecs)});return;
    }
    if(m_pollTicks%3==0)launch(ProcessTask::Logs,{QStringLiteral("blockcheck-job-logs"),QStringLiteral("200"),QString::number(m_startedAtSecs)});
    else launch(ProcessTask::Status,{QStringLiteral("blockcheck-job-status"),m_operationId});
}

void BlockcheckJobController::processProbe(const QByteArray &output)
{
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(output,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()
        ||document.object().value(QStringLiteral("version")).toInt()!=1){
        m_probePending=false; setRecoveryRequired(true); setBusy(false);
        setStatus(tr("Не удалось достоверно определить состояние восстановления Blockcheck. Операции заблокированы."),StatusType::Error);
        return;
    }
    const QJsonObject object=document.object();
    const QString state=object.value(QStringLiteral("state")).toString();
    if(state==QLatin1String("safe")){
        m_probePending=false; m_operationId.clear(); setRecoveryRequired(false); setBusy(false);
        if (m_recoveryRetryInProgress) {
            m_recoveryRetryInProgress=false;
            setStatus(tr("Восстановление службы zapret2 подтверждено."),StatusType::Success);
        } else {
            setStatus(QString());
        }
        return;
    }
    if(state==QLatin1String("busy")){
        m_probePending=true; setRecoveryRequired(false); setBusy(true);
        setStatus(tr("Обнаружена выполняющаяся операция Blockcheck. Ожидается безопасное завершение…"),StatusType::Warning);
        m_pollTimer->start();
        return;
    }
    m_probePending=false; m_operationId.clear();
    if(state==QLatin1String("recovery_required")){
        const QString id=object.value(QStringLiteral("operation_id")).toString();
        static const QRegularExpression uuidRe(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
        if(uuidRe.match(id).hasMatch()) m_operationId=id;
        setRecoveryRequired(true); setBusy(false);
        setStatus(tr("Обнаружено незавершённое восстановление zapret2. Управление службой и обновление заблокированы."),StatusType::Error);
        return;
    }
    setRecoveryRequired(true); setBusy(false);
    const QString reason=object.value(QStringLiteral("reason")).toString();
    setStatus(reason.isEmpty() ? tr("Состояние восстановления Blockcheck неизвестно. Операции заблокированы.")
        : tr("Не удалось проверить восстановление Blockcheck: %1. Операции заблокированы.").arg(reason), StatusType::Error);
}

void BlockcheckJobController::processStatus(const QByteArray &output)
{
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(output,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()){
        setStatus(tr("Helper вернул некорректное состояние задания."),StatusType::Error);return;
    }
    const QJsonObject object=document.object();
    if(object.value(QStringLiteral("operation_id")).toString()!=m_operationId){
        setStatus(tr("Получено состояние другого задания; операция не считается завершённой."),StatusType::Error);return;
    }
    const QString phase=object.value(QStringLiteral("phase")).toString();
    const bool recoveryMarker=object.value(QStringLiteral("recovery_marker")).toBool();
    if (phase==QLatin1String("recovery_failed") || phase==QLatin1String("stale"))
        setRecoveryRequired(true);
    else if ((phase==QLatin1String("completed")||phase==QLatin1String("failed")) && !recoveryMarker)
        setRecoveryRequired(false);
    if(phase==QLatin1String("busy")){setStatus(tr("Ожидание блокировки состояния Blockcheck…"));return;}
    if(phase==QLatin1String("queued")){setStatus(tr("Задание ожидает запуска systemd…"));return;}
    if(phase==QLatin1String("running")){setStatus(tr("Выполняется диагностика Blockcheck…"));return;}
    if(phase==QLatin1String("recovering")){setStatus(tr("Восстановление службы zapret2…"),StatusType::Warning);return;}
    if(phase==QLatin1String("recovery_pending")){setStatus(tr("Восстановление службы ещё не подтверждено; задание остаётся активным."),StatusType::Warning);return;}
    if(phase==QLatin1String("recovery_failed")){m_terminalPhase=phase;setStatus(tr("Ошибка восстановления службы. Маркер восстановления сохранён."),StatusType::Error);m_pollTimer->stop();launch(ProcessTask::Logs,{QStringLiteral("blockcheck-job-logs"),QStringLiteral("500"),QString::number(m_startedAtSecs)});return;}
    if(phase==QLatin1String("stale")){
        const QString recoveryOperationId=object.value(QStringLiteral("recovery_operation_id")).toString();
        static const QRegularExpression uuidRe(
            QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
        if(uuidRe.match(recoveryOperationId).hasMatch()){
            m_operationId=recoveryOperationId;
            m_startedAtSecs=QDateTime::currentSecsSinceEpoch();
            emit recoveryRequiredChanged();
            emit recoveryRetryAvailableChanged();
        }
        m_terminalPhase=phase;
        m_pollTimer->stop();
        setStatus(tr("Идентификатор задания устарел; журнал другой операции не будет прочитан."),StatusType::Error);
        finish(false);
        return;
    }
    if(phase==QLatin1String("completed")||phase==QLatin1String("failed")){
        m_terminalPhase=phase;m_pollTimer->stop();setStatus(phase==QLatin1String("completed")?tr("Диагностика завершена; восстановление службы подтверждено."):tr("Диагностика завершилась с ошибкой или состояние задания устарело."),phase==QLatin1String("completed")?StatusType::Success:StatusType::Error);
        launch(ProcessTask::Logs,{QStringLiteral("blockcheck-job-logs"),QStringLiteral("500"),QString::number(m_startedAtSecs)});return;
    }
    setStatus(tr("Получено неизвестное состояние задания; ожидание продолжается."),StatusType::Warning);
}

void BlockcheckJobController::processLogs(const QByteArray &output)
{
    if(output.size()>MaxOutput){setStatus(tr("Журнал Blockcheck превышает лимит 1 MiB."),StatusType::Error);return;}
    const QString decoded=QString::fromUtf8(output);
    if(decoded.toUtf8()!=output){setStatus(tr("Журнал Blockcheck содержит некорректный UTF-8."),StatusType::Error);return;}
    m_logText.clear();m_results.clear();m_found=0;
    const QStringList lines=decoded.split(QLatin1Char('\n'));
    for(const QString &line:lines){
        if(line.isEmpty())continue;
        appendLog(line);
        if(const auto result=BlockcheckRunner::parseLine(line)){
            const QString key=QStringList{result->test,result->ipv,result->domain,result->daemon,result->args}.join(QChar(0));
            bool duplicate=false;
            for(const QVariant &entry:m_results)if(entry.toMap().value(QStringLiteral("key")).toString()==key){duplicate=true;break;}
            if(!duplicate){++m_found;m_results.append(QVariantMap{{"label",QStringLiteral("[ipv%1 %2 %3] %4").arg(result->ipv,result->domain,result->test,result->commandLine())},{"command",result->commandLine()},{"key",key}});}
        }
    }
    emit logTextChanged();emit resultsChanged();
}

void BlockcheckJobController::finish(bool success,bool canceled)
{
    m_pollTimer->stop();m_terminalPhase.clear();if(!m_recoveryRequired)m_operationId.clear();m_task=ProcessTask::None;
    setBusy(false);
    if(canceled)setStatus(tr("Подбор отменён."),StatusType::Warning);
    else if(success)setStatus(tr("Диагностика завершена; восстановление службы подтверждено."),StatusType::Success);
    else setStatus(tr("Не удалось выполнить диагностику или подтвердить восстановление службы."),StatusType::Error);
    emit scanFinished(m_found,canceled);
}

void BlockcheckJobController::cancel()
{
    if(!m_busy||m_probePending)return;
    if(m_operationId.isEmpty()){
        if(m_task==ProcessTask::None)finish(false,true);
        else if(m_task==ProcessTask::Submit){
            m_cancelRequested=true;
            setStatus(tr("Отмена будет отправлена после получения идентификатора задания."),StatusType::Warning);
        }
        return;
    }
    if(m_terminalPhase.isEmpty()){
        m_cancelRequested=true;
        if(m_task==ProcessTask::None)
            launch(ProcessTask::Cancel,{QStringLiteral("blockcheck-job-cancel")});
    }
}

void BlockcheckJobController::retryRecovery()
{
    if(!recoveryRetryAvailable())return;
    m_recoveryRetryInProgress=true;
    setBusy(true);
    setStatus(tr("Повторная попытка восстановления службы zapret2…"),StatusType::Warning);
    launch(ProcessTask::Recovery,{QStringLiteral("blockcheck-recover")});
}

void BlockcheckJobController::copyStrategy(int index)
{
    if(index<0||index>=m_results.size())return;
    const QString command=m_results.at(index).toMap().value(QStringLiteral("command")).toString();
    if(!command.isEmpty()&&QGuiApplication::clipboard())QGuiApplication::clipboard()->setText(command);
}

bool BlockcheckJobController::saveLog(const QUrl &url)
{
    if(!url.isLocalFile()||url.toLocalFile().isEmpty()){setStatus(tr("Выберите локальный файл для сохранения журнала."),StatusType::Error);return false;}
    const QString path=url.toLocalFile();QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){setStatus(tr("Не удалось открыть файл журнала: %1").arg(file.errorString()),StatusType::Error);return false;}
    const QByteArray data=m_logText.toUtf8();
    if(file.write(data)!=data.size()||!file.commit()){setStatus(tr("Не удалось сохранить журнал: %1").arg(file.errorString()),StatusType::Error);return false;}
    setStatus(tr("Журнал сохранён: %1").arg(QFileInfo(path).fileName()),StatusType::Success);return true;
}

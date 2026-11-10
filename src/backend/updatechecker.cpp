#include "updatechecker.h"

#include <QCoreApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace ZapretVersion {

QString normalize(const QString &versionOrTag)
{
    QString v = versionOrTag.trimmed();
    if (v.startsWith(u'v') || v.startsWith(u'V'))
        v.remove(0, 1);
    return v;
}

int compare(const QString &a, const QString &b)
{
    const QStringList pa = normalize(a).split(u'.');
    const QStringList pb = normalize(b).split(u'.');
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QString installed(const QString &baseDir)
{
    QFile f(baseDir + QStringLiteral("/docs/changes.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression heading(QStringLiteral(R"(^\d+(\.\d+)+\s*$)"));
    QString last;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine());
        if (heading.match(line).hasMatch())
            last = line.trimmed();
    }
    return last;
}

QString tagFromLocation(const QString &location)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^https://github\.com/bol-van/zapret2/releases/tag/(v\d+(?:\.\d+)*)$)"));
    const auto m = re.match(location);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString releaseUrl(const QString &tag)
{
    return QStringLiteral("https://github.com/bol-van/zapret2/releases/tag/") + tag;
}

} // namespace ZapretVersion

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void UpdateChecker::check()
{
    if (m_running)
        return;
    m_running = true;

    QNetworkRequest req(QUrl(QStringLiteral("https://github.com/bol-van/zapret2/releases/latest")));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("zapret2control/%1").arg(QCoreApplication::applicationVersion()));

    QNetworkReply *reply = m_nam->head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_running = false;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status < 300) {
            emit failed(reply->errorString());
            return;
        }
        if (status < 300 || status >= 400) {
            emit failed(tr("Неожиданный ответ GitHub (HTTP %1).").arg(status));
            return;
        }
        const QString tag = ZapretVersion::tagFromLocation(
            reply->header(QNetworkRequest::LocationHeader).toString());
        if (tag.isEmpty())
            emit failed(tr("Не удалось определить последнюю версию из ответа GitHub."));
        else
            emit finished(tag);
    });
}

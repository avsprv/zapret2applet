#include <QtTest>

#include <QTextBrowser>

#include "aboutpage.h"

class AboutPageTest : public QObject
{
    Q_OBJECT

private slots:
    void showsApplicationVersionAndProjectLinks()
    {
        AboutPage page;
        const auto *details = page.findChild<QTextBrowser *>(QStringLiteral("aboutDetails"));
        QVERIFY(details);

        const QString html = details->toHtml();
        QVERIFY(html.contains(QString::fromLatin1(QZAPRET_VERSION)));
        QVERIFY(html.contains(QStringLiteral("https://github.com/avsprv/zapret2control")));
        QVERIFY(html.contains(QStringLiteral("https://github.com/bol-van/zapret2")));
        QVERIFY(html.contains(QStringLiteral("NOTICE.md")));
    }
};

QTEST_MAIN(AboutPageTest)
#include "test_aboutpage.moc"

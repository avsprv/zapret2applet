#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QtTest>

#include "i18n.h"

class TestI18n : public QObject
{
    Q_OBJECT
public:
    // Исходные строки русские; контекст — имя класса (см. lupdate)
    static QString translate(const char *context, const char *source)
    {
        return QCoreApplication::translate(context, source);
    }

private:
    QList<QTranslator *> m_installed;

private slots:
    void cleanup()
    {
        qDeleteAll(m_installed); // деструктор QTranslator сам снимает его с приложения
        m_installed.clear();
    }

    // Файл .ts: нет незавершённых/пустых переводов, не осталось русских букв,
    // плейсхолдеры %1, %2 … совпадают с исходной строкой.
    void tsFileIsComplete()
    {
        QFile f(QStringLiteral(QZAPRET_I18N_DIR "/zapret2control_en.ts"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "cannot open zapret2control_en.ts");

        const QRegularExpression placeholder(QStringLiteral("%\\d+"));
        const QRegularExpression cyrillic(QStringLiteral("[А-Яа-яЁё]"));
        const auto placeholders = [&](const QString &s) {
            QStringList out;
            for (auto it = placeholder.globalMatch(s); it.hasNext();)
                out << it.next().captured();
            out.sort();
            return out;
        };

        QXmlStreamReader xml(&f);
        int messages = 0;
        int locations = 0;
        QString source, translation, type;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"location") {
                const QString filename = xml.attributes().value(u"filename").toString();
                const QString path = QDir(QStringLiteral(QZAPRET_I18N_DIR)).filePath(filename);
                QVERIFY2(!filename.isEmpty() && QFileInfo::exists(path),
                         qPrintable("missing translation source location: " + filename));
                ++locations;
            } else if (xml.isStartElement() && xml.name() == u"source") {
                source = xml.readElementText();
            } else if (xml.isStartElement() && xml.name() == u"translation") {
                type = xml.attributes().value(u"type").toString();
                translation = xml.readElementText();
            } else if (xml.isEndElement() && xml.name() == u"message") {
                ++messages;
                QVERIFY2(type.isEmpty(), qPrintable("unfinished/obsolete: " + source));
                QVERIFY2(!translation.isEmpty(), qPrintable("empty translation: " + source));
                QVERIFY2(!translation.contains(cyrillic), qPrintable("untranslated: " + source));
                QVERIFY2(placeholders(source) == placeholders(translation),
                         qPrintable("placeholders differ: " + source));
                source.clear();
                translation.clear();
                type.clear();
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(messages > 0);
        QVERIFY(locations > 0);
    }

    void englishLocale()
    {
        m_installed = I18n::install(*QCoreApplication::instance(), QLocale(QLocale::English, QLocale::UnitedStates));
        QCOMPARE(translate("BlockcheckPage", "Начать подбор"), QStringLiteral("Start scan"));
        QCOMPARE(translate("MainWindow", "Выход"), QStringLiteral("Quit"));
        QCOMPARE(translate("main", "Запустить скрытым в системном трее"),
                 QStringLiteral("Start hidden in the system tray"));
        QCOMPARE(translate("Main", "Статус"), QStringLiteral("Status"));
        QCOMPARE(translate("AboutPage", "О программе"), QStringLiteral("About"));
        QCOMPARE(translate("SettingsPage", "Внешний вид"), QStringLiteral("Appearance"));
        QCOMPARE(translate("ConfigPage", "Прочитать"), QStringLiteral("Read"));
        QCOMPARE(translate("ProfilesPage", "Новый профиль"), QStringLiteral("New profile"));
    }

    // Языка нет в переводах — показываем английский, а не русские исходники
    void otherLocaleFallsBackToEnglish()
    {
        m_installed = I18n::install(*QCoreApplication::instance(), QLocale(QLocale::German, QLocale::Germany));
        QCOMPARE(translate("BlockcheckPage", "Начать подбор"), QStringLiteral("Start scan"));
    }

    void russianKeepsSource()
    {
        m_installed = I18n::install(*QCoreApplication::instance(), QLocale(QLocale::Russian, QLocale::Russia));
        QCOMPARE(translate("BlockcheckPage", "Начать подбор"), QStringLiteral("Начать подбор"));
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "test_i18n.moc"

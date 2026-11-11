#include <QCoreApplication>
#include <QObject>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("avsprv"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("avsprv.github.io"));
        QCoreApplication::setApplicationName(QStringLiteral("zapret2control-qml-tests"));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(zapret2control_qml, Setup)

#include "main.moc"

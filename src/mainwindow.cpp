#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QColor>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QCheckBox>
#include <QCloseEvent>
#include <QLabel>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "appsettings.h"
#include "aboutpage.h"
#include "backend/systemdbackend.h"
#include "blockcheckpage.h"
#include "configpage.h"
#include "listsmanagerpage.h"
#include "logmonitorpage.h"
#include "profilepage.h"
#include "settingsdialog.h"
#include "updatepage.h"


namespace {
QIcon makeTrayIcon(ZapretBackend::State state)
{
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    const bool dark = scheme == Qt::ColorScheme::Dark
        || (scheme == Qt::ColorScheme::Unknown
            && QGuiApplication::palette().color(QPalette::Window).lightness() < 128);
    const QColor foreground = dark ? QColor(239, 240, 241) : QColor(35, 38, 41);
    const QColor accent = state == ZapretBackend::State::Inactive ? QColor(246, 190, 48)
        : state == ZapretBackend::State::Failed ? QColor(218, 68, 83) : foreground;

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(foreground, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(3.5, 5.0, 13.0, 9.0), 2.0, 2.0);
    painter.drawRoundedRect(QRectF(15.5, 11.5, 13.0, 9.0), 2.0, 2.0);
    painter.drawRoundedRect(QRectF(3.5, 21.0, 13.0, 8.0), 2.0, 2.0);
    painter.drawLine(QPointF(16.5, 9.5), QPointF(20.0, 9.5));
    painter.drawLine(QPointF(20.0, 9.5), QPointF(22.0, 11.5));
    painter.drawLine(QPointF(16.5, 24.5), QPointF(20.0, 24.5));
    painter.drawLine(QPointF(20.0, 24.5), QPointF(22.0, 22.5));
    if (state == ZapretBackend::State::Inactive || state == ZapretBackend::State::Failed) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(QRectF(22.0, 21.0, 8.0, 8.0));
    }
    painter.end();
    return QIcon(pixmap);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_backend(new SystemdBackend(this))
    , m_poll(new QTimer(this))
{
    setWindowTitle(QStringLiteral("Zapret2 Control"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("zapret2control")));
    resize(760, 640);

    auto *appMenu = menuBar()->addMenu(tr("Приложение"));
    auto *settingsAction = appMenu->addAction(tr("Настройки…"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    appMenu->addSeparator();
    auto *exitAction = appMenu->addAction(tr("Выход"));
    connect(exitAction, &QAction::triggered, this, &MainWindow::quit);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildStatusPage(), tr("Статус"));
    tabs->addTab(new ConfigPage(this), tr("Конфигурация"));
    tabs->addTab(new ProfilePage(this), tr("Профили"));
    tabs->addTab(new ListsManagerPage(this), tr("Списки"));
    tabs->addTab(new LogMonitorPage(this), tr("Мониторинг"));
    m_blockcheck = new BlockcheckPage(m_backend, this);
    tabs->addTab(m_blockcheck, tr("Подбор стратегий"));
    m_update = new UpdatePage(m_backend, this);
    tabs->addTab(m_update, tr("Обновления"));
    tabs->addTab(new AboutPage(this), tr("О программе"));
    connect(m_blockcheck, &BlockcheckPage::busyChanged, this, [this](bool busy) {
        m_update->setServiceBusy(busy);
        m_backend->refresh();
    });
    connect(m_update, &UpdatePage::busyChanged, this, [this](bool busy) {
        m_blockcheck->setUpdateBusy(busy);
        m_backend->refresh();
    });
    setCentralWidget(tabs);

    connect(m_backend, &ZapretBackend::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_backend, &ZapretBackend::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, tr("Ошибка"), msg);
    });
    connect(m_backend, &ZapretBackend::bootEnabledChanged, this, [this](bool on) {
        const QSignalBlocker block(m_bootCheck);
        m_bootCheck->setChecked(on);
    });

    buildTray();
    connect(m_blockcheck, &BlockcheckPage::scanFinished, this, [this](int found, bool canceled) {
        if (m_tray && !isVisible() && !canceled)
            m_tray->showMessage(QStringLiteral("Zapret2 Control"),
                                tr("Подбор завершён, найдено стратегий: %1").arg(found));
    });

    connect(m_update, &UpdatePage::updateAvailable, this, [this](const QString &tag) {
        if (m_tray && !isVisible())
            m_tray->showMessage(QStringLiteral("Zapret2 Control"), tr("Доступна новая версия zapret2: %1").arg(tag));
    });

    m_poll->setInterval(3000);
    connect(m_poll, &QTimer::timeout, m_backend, &ZapretBackend::refresh);
    m_poll->start();
    m_backend->refresh();
}

QWidget *MainWindow::buildStatusPage()
{
    m_status = new QLabel(tr("Статус: …"));
    m_startBtn = new QPushButton(tr("Запустить"));
    m_stopBtn = new QPushButton(tr("Остановить"));
    m_bootCheck = new QCheckBox(tr("Запускать zapret2 при загрузке системы"));

    connect(m_startBtn, &QPushButton::clicked, this, [this] {
        if (m_blockcheck->isRunning() || m_update->isBusy()) {
            QMessageBox::information(this, tr("Идёт подбор"),
                                     tr("Дождитесь завершения подбора стратегий перед управлением сервисом."));
            return;
        }
        m_backend->start();
    });
    connect(m_stopBtn, &QPushButton::clicked, this, [this] {
        if (m_blockcheck->isRunning() || m_update->isBusy()) {
            QMessageBox::information(this, tr("Идёт подбор"),
                                     tr("Дождитесь завершения подбора стратегий перед управлением сервисом."));
            return;
        }
        m_backend->stop();
    });
    // clicked, а не toggled: состояние чекбокса потом приводится к реальному через refresh()
    connect(m_bootCheck, &QCheckBox::clicked, m_backend, &ZapretBackend::setBootEnabled);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_status);
    auto *row = new QHBoxLayout;
    row->addWidget(m_startBtn);
    row->addWidget(m_stopBtn);
    row->addStretch();
    layout->addLayout(row);
    layout->addSpacing(12);
    layout->addWidget(m_bootCheck);
    layout->addStretch();
    return page;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(this);
    updateTrayIcon(ZapretBackend::State::Unknown);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { updateTrayIcon(m_trayState); });
    m_tray->setToolTip(QStringLiteral("Zapret2 Control"));

    auto *menu = new QMenu(this);
    auto *show = menu->addAction(tr("Показать / скрыть окно"));
    auto *settings = menu->addAction(tr("Настройки…"));
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    menu->addSeparator();
    m_trayStart = menu->addAction(tr("Запустить zapret2"));
    m_trayStop = menu->addAction(tr("Остановить zapret2"));
    menu->addSeparator();
    auto *quitAct = menu->addAction(tr("Выход"));

    connect(show, &QAction::triggered, this, &MainWindow::toggleWindow);
    connect(m_trayStart, &QAction::triggered, this, [this] {
        if (!m_blockcheck->isRunning() && !m_update->isBusy())
            m_backend->start();
    });
    connect(m_trayStop, &QAction::triggered, this, [this] {
        if (!m_blockcheck->isRunning() && !m_update->isBusy())
            m_backend->stop();
    });
    connect(quitAct, &QAction::triggered, this, &MainWindow::quit);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            toggleWindow();
    });

    m_tray->setContextMenu(menu);
    m_tray->show();
}

void MainWindow::onStateChanged(ZapretBackend::State state)
{
    m_trayState = state;
    QString text;
    switch (state) {
    case ZapretBackend::State::Active:   text = tr("работает"); break;
    case ZapretBackend::State::Inactive: text = tr("остановлен"); break;
    case ZapretBackend::State::Failed:   text = tr("ошибка юнита"); break;
    default:                             text = tr("неизвестно (zapret2 не установлен?)"); break;
    }
    m_status->setText(tr("Статус: %1").arg(text));
    const bool active = state == ZapretBackend::State::Active;
    const bool locked = m_blockcheck->isRunning() || m_update->isBusy();
    m_startBtn->setEnabled(!active && !locked);
    m_stopBtn->setEnabled(active && !locked);

    if (m_tray) {
        updateTrayIcon(state);
        m_tray->setToolTip(tr("Zapret2 Control: %1").arg(text));
        m_trayStart->setEnabled(!active && !locked);
        m_trayStop->setEnabled(active && !locked);
    }
}


void MainWindow::updateTrayIcon(ZapretBackend::State state)
{
    if (m_tray)
        m_tray->setIcon(makeTrayIcon(state));
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_tray && m_tray->isVisible(), this);
    connect(&dialog, &SettingsDialog::languageRestartRequired, this, [this] {
        QMessageBox::information(this, tr("Язык интерфейса"),
                                 tr("Изменение языка будет применено после следующего запуска приложения."));
    });
    dialog.exec();
}

void MainWindow::toggleWindow()
{
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::quit()
{
    if (m_update->isBusy()) {
        showNormal();
        QMessageBox::information(this, tr("Идёт обновление"),
                                 tr("Дождитесь завершения обновления zapret2 перед выходом."));
        return;
    }
    if (m_blockcheck->isRunning()) {
        showNormal();
        QMessageBox::information(this, tr("Идёт подбор"),
                                 tr("Остановите подбор стратегий перед выходом."));
        return;
    }
    m_quitting = true;
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Поведение закрытия управляется настройкой; по умолчанию сохраняется прежнее сворачивание в трей.
    if (m_tray && m_tray->isVisible() && !m_quitting && AppSettings::closeToTray()) {
        hide();
        event->ignore();
        return;
    }
    if (m_update->isBusy()) {
        QMessageBox::information(this, tr("Идёт обновление"),
                                 tr("Дождитесь завершения обновления zapret2 перед выходом."));
        event->ignore();
        return;
    }
    if (m_blockcheck->isRunning()) {
        QMessageBox::information(this, tr("Идёт подбор"),
                                 tr("Остановите подбор стратегий перед выходом."));
        event->ignore();
        return;
    }
    if (m_tray && !m_quitting)
        QApplication::quit();
    event->accept();
}
